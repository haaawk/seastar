/*
 * This file is open source software, licensed to you under the terms
 * of the Apache License, Version 2.0 (the "License").  See the NOTICE file
 * distributed with this work for additional information regarding copyright
 * ownership.  You may not use this file except in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
/*
 * Copyright (C) 2020 ScyllaDB
 */

#ifdef SEASTAR_DEADLOCK_DETECTION
#include <sys/types.h>
#include <seastar/core/internal/deadlock_utils.hh>
#include <seastar/core/task.hh>
#include <seastar/core/reactor.hh>
#include <map>
#include <fstream>
#include <seastar/core/sstring.hh>

struct json_object;

using bigger_sstring = seastar::basic_sstring<char, uint32_t, 127>;
using dumped_value = std::vector<std::pair<const char*, json_object>>;
using dumped_type = std::variant<const char*, bigger_sstring, bool, uintmax_t, std::nullptr_t, dumped_value>;

struct json_object {
    dumped_type _value;
    json_object(const char* value) {
        if (value) {
            _value = value;
        } else {
            _value = nullptr;
        }
    }
    json_object(bigger_sstring value) : _value(std::move(value)) {}
    json_object(dumped_value value) : _value(std::move(value)) {}
    template <typename T, std::enable_if_t<!std::is_same_v<T, bool> && std::is_integral_v<T>, bool> = true>
    json_object(T value) : _value(uintmax_t(value)) {}
    template <typename T, std::enable_if_t<std::is_same_v<T, bool>, bool> = true>
    json_object(T value) : _value(bool(value)) {}

    friend std::ostream& operator<<(std::ostream& s, const json_object& obj);
};

template <typename> inline constexpr bool always_false_v = false;

std::ostream& operator<<(std::ostream& s, const json_object& obj) {
    std::visit([&s](const auto& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, const char*> or std::is_same_v<T, bigger_sstring>) {
            s << "\"" << arg << "\"";
        } else if constexpr (std::is_same_v<T, uintmax_t>) {
            s << arg;
        } else if constexpr (std::is_same_v<T, dumped_value>) {
            s << "{";
            bool started = false;
            for (const auto& elem : arg) {
                if (started) {
                    s << ", ";
                }
                s << "\"" << elem.first << "\": " << elem.second;
                started = true;
            }
            s << "}";
        } else if constexpr (std::is_same_v<T, bool>) {
            s << (arg ? "true" : "false");
        } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
            s << "null";
        } else {
            static_assert(always_false_v<T>, "non-exhaustive visitor!");
        }
    }, obj._value);
    return s;
}


namespace seastar::deadlock_detection {

bool operator==(const runtime_vertex& lhs, const runtime_vertex& rhs) {
    return lhs._ptr == rhs._ptr && lhs._base_type->hash_code() == rhs._base_type->hash_code();
}

uintptr_t runtime_vertex::get_ptr() const noexcept {
    return (uintptr_t)_ptr;
}

/// \brief Get output unique to each threads for dumping graph.
///
/// For each thread creates unique file for dumping graph.
/// Is thread and not shard-based because there are multiple threads in shard 0.
static std::ostream& get_output_stream() {
    static thread_local std::ofstream stream(fmt::format("deadlock_detection_graphdump.{}.json", gettid()));
    return stream;
}

/// Global variable for storing currently executed runtime graph vertex.
static runtime_vertex& current_traced_ptr() {
    static thread_local runtime_vertex ptr(nullptr);
    return ptr;
}

/// Serializes and writes data to appropriate file.
static void write_data(dumped_value data) {
    auto now = std::chrono::steady_clock::now();
    auto nanoseconds = std::chrono::nanoseconds(now.time_since_epoch()).count();
    data.emplace_back("timestamp", nanoseconds);
    get_output_stream() << json_object(std::move(data)) << std::endl;
}

/// Converts runtime vertex to serializable data.
static dumped_value serialize_vertex(runtime_vertex v) {
    return {{"address", v.get_ptr()},
            {"base_type", v._base_type->name()},
            {"type", v._type->name()}};
}

/// Converts runtime vertex to serializable data without debug info.
static dumped_value serialize_vertex_short(runtime_vertex v) {
    return {{"address", v.get_ptr()}};
}

/// Converts semaphore to serializable data.
static dumped_value serialize_semaphore(const void* sem, size_t count) {
    return {{"address", reinterpret_cast<uintptr_t>(sem)}, {"available_units", count}};
}

/// Converts semaphore to serializable data without debug info.
static dumped_value serialize_semaphore_short(const void* sem) {
    return {{"address", reinterpret_cast<uintptr_t>(sem)}};
}

runtime_vertex get_current_traced_ptr() {
    return current_traced_ptr();
}

current_traced_vertex_updater::current_traced_vertex_updater(runtime_vertex new_ptr) : _previous_ptr(current_traced_ptr()), _new_ptr(new_ptr) {
    current_traced_ptr() = new_ptr;
}

current_traced_vertex_updater::~current_traced_vertex_updater() {
    assert(current_traced_ptr() == _new_ptr);
    current_traced_ptr() = _previous_ptr;
}

void trace_edge(runtime_vertex pre, runtime_vertex post, bool speculative) {
    dumped_value data{
            {"type", "edge"},
            {"pre", serialize_vertex(pre)},
            {"post", serialize_vertex(post)},
            {"speculative", speculative}
    };
    write_data(data);
}

void trace_vertex_constructor(runtime_vertex v) {
    dumped_value data{
            {"type", "vertex_ctor"},
            {"vertex", serialize_vertex(v)}
    };
    write_data(data);
}

void trace_vertex_destructor(runtime_vertex v) {
    dumped_value data{
            {"type", "vertex_dtor"},
            {"vertex", serialize_vertex(v)}
    };
    write_data(data);
}

void trace_semaphore_constructor(const void* sem, size_t count) {
    dumped_value data{
            {"type", "sem_ctor"},
            {"sem", serialize_semaphore(sem, count)}
    };
    write_data(data);
}

void trace_semaphore_destructor(const void* sem, size_t count) {
    dumped_value data{
            {"type", "sem_dtor"},
            {"sem", serialize_semaphore(sem, count)}
    };
    write_data(data);
}

void attach_func_type(runtime_vertex ptr, const std::type_info& func_type, const char* file, uint32_t line) {
    dumped_value data{
            {"type", "attach_func_type"},
            {"vertex", serialize_vertex(ptr)},
            {"func_type", func_type.name()},
            {"file", file},
            {"line", line}
    };
    write_data(data);
}

void trace_move_vertex(runtime_vertex from, runtime_vertex to) {
    dumped_value data{
        {"type", "vertex_move"},
        {"from", serialize_vertex_short(from)},
        {"to", serialize_vertex_short(to)}
    };
    write_data(data);
}

void trace_move_semaphore(const void* from, const void* to) {
    dumped_value data{
        {"type", "sem_move"},
        {"from", serialize_semaphore_short(from)},
        {"to", serialize_semaphore_short(to)}
    };
    write_data(data);
}

void trace_semaphore_signal(const void* sem, size_t count, runtime_vertex caller) {
    dumped_value data{
            {"type", "sem_signal"},
            {"sem", serialize_semaphore_short(sem)},
            {"count", count},
            {"vertex", caller.get_ptr()}
    };
    write_data(data);
}

void trace_semaphore_wait_completed(const void* sem, runtime_vertex post) {
    dumped_value data{
            {"type", "sem_wait_completed"},
            {"sem", serialize_semaphore_short(sem)},
            {"post", serialize_vertex_short(post)}
    };
    write_data(data);
}

void trace_semaphore_wait(const void* sem, size_t count, runtime_vertex pre, runtime_vertex post) {
    dumped_value data{
            {"type", "sem_wait"},
            {"sem", serialize_semaphore_short(sem)},
            {"pre", serialize_vertex_short(pre)},
            {"post", serialize_vertex_short(post)},
            {"count", count}
    };
    write_data(data);
}

}
#endif
