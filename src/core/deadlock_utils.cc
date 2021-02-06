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
#include <seastar/core/file.hh>
#include <seastar/core/sstring.hh>
#include <proto/deadlock_trace.pb.h>

static thread_local bool local_can_append_trace = true;
static thread_local bool local_trace_can_io = false;

class tracer {
public:
    enum class state {
        DISABLED,
        RUNNING,
        FLUSHING,
    };
private:
    static constexpr size_t chunk_size = 0x1000;
    static constexpr size_t minimal_chunk_count = 64;

    struct alignas(chunk_size) page {
        char _data[chunk_size];
    };

    struct buffer {
        size_t _length = 0;
        std::vector<page> _data{};

        size_t capacity() const {
            return _data.size() * sizeof(page);
        }

        char* start_ptr() {
            return reinterpret_cast<char*>(_data.data()->_data);
        }
        char* end_ptr() {
            return start_ptr() + _length;
        }

        void write(const seastar::deadlock_detection::deadlock_trace& data) {
            assert(data.IsInitialized());
            size_t data_size = data.ByteSizeLong();
            assert(data_size <= UINT16_MAX);
            uint16_t size = data_size;
            size_t total_size = size + sizeof(size);
            if (_length + total_size > capacity()) {
                size_t new_capacity = capacity() * 2 + total_size;
                size_t new_size = new_capacity / sizeof(page) + 1;
                _data.reserve(new_size);
                _data.resize(new_size);
            }


            uint16_t written_size = htole16(size);
            memcpy(end_ptr(), &written_size, sizeof(size));
            _length += sizeof(size);

            bool result = data.SerializeToArray(end_ptr(), size);
            (void)result;
            assert(result);
            _length += size;
        }

        void write(const std::string_view& data) {
            size_t size = data.size();
            if (_length + size > capacity()) {
                size_t new_capacity = capacity() * 2 + size;
                size_t new_size = new_capacity / sizeof(page) + 1;
                _data.reserve(new_size);
                _data.resize(new_size);
            }

            memcpy(end_ptr(), data.data(), size);
            _length += size;
        }

        void reset() {
            _length = 0;
        }

        size_t size() const {
            return _length;
        }
    };

    std::unique_ptr<seastar::future<>> _operation{};
    std::unique_ptr<seastar::condition_variable> _new_data{};
    buffer _trace_buffer{};
    buffer _write_buffer{};
    state _state = state::DISABLED;
    const size_t _id;
    size_t _file_size = 0;
    bool _disable_condition_signal = false;


    struct disable_wake {
        bool _prev_val;
        tracer* _tracer;
        disable_wake(tracer& tracer) {
            _prev_val = tracer._disable_condition_signal;
            tracer._disable_condition_signal = true;
            _tracer = &tracer;
        }

        ~disable_wake() {
            assert(_tracer->_disable_condition_signal == true);
            _tracer->_disable_condition_signal = _prev_val;
        }
    };

    seastar::future<> loop(seastar::file&& file) {
        _file_size = 0;
        return seastar::do_with(std::move(file), [this](auto& file) {
            return loop_impl(file);
        });
    }

    seastar::future<> loop_impl(seastar::file& file) {
        auto disable = disable_wake(*this);
        assert(_state != state::DISABLED);
        assert(local_trace_can_io);
        if (_state == state::FLUSHING) {
            return flush(file);
        }
        if (_trace_buffer.size() < chunk_size * minimal_chunk_count) {
            return _new_data->wait().then([this, &file] {
                return loop_impl(file);
            });
        } else {
            std::swap(_trace_buffer, _write_buffer);
            size_t chunk_count = _write_buffer.size() / chunk_size;
            assert(chunk_count >= minimal_chunk_count);
            size_t length = chunk_count * chunk_size;
            _trace_buffer.write(std::string_view(_write_buffer.start_ptr() + length, _write_buffer.size() - length));
            _write_buffer._length = length;
            return file.dma_write(_file_size, _write_buffer.start_ptr(), length).then([this, &file](size_t written) {
                if (written != _write_buffer.size()) {
                    throw std::exception();
                }
                _write_buffer.reset();
                _file_size += written;
                return loop_impl(file);
            });
        }
    }

    seastar::future<> flush(seastar::file& file) {
        assert(_state == state::FLUSHING);
        assert(!local_can_append_trace);
        assert(local_trace_can_io);
        std::swap(_trace_buffer, _write_buffer);
        size_t chunk_count = (_write_buffer.size() + chunk_size - 1) / chunk_size;
        size_t length = chunk_count * chunk_size;
        size_t overflow = length - _write_buffer.size();
        _write_buffer._length = length;
        return file.dma_write(_file_size, _write_buffer.start_ptr(), length).then([this](size_t written) {
            if (written != _write_buffer.size()) {
                throw std::exception();
            }
            _write_buffer.reset();
            _file_size += written;
            return seastar::make_ready_future<>();
        }).then([this, &file, overflow] {
            return file.truncate(_file_size - overflow);
        }).then([&file] {
            return file.flush();
        }).then([&file] {
            return file.close();
        });
    }


public:

    tracer(size_t id) : _id(id) {}

    ~tracer() {
        assert(!_operation);
    }

    state state() const noexcept {
        return _state;
    }

    void start() {
        _new_data = std::make_unique<seastar::condition_variable>();
        assert(_state == state::DISABLED);
        _state = state::RUNNING;
        auto init_future = seastar::open_file_dma(
                fmt::format("deadlock_detection_graphdump.{}.proto", _id),
                seastar::open_flags::wo | seastar::open_flags::create | seastar::open_flags::truncate
        ).then([this](seastar::file file) {
            return loop(std::move(file));
        });
        _operation = std::make_unique<seastar::future<>>(std::move(init_future));
    }

    seastar::future<> stop() {
        assert(_state == state::RUNNING);
        _state = state::FLUSHING;
        seastar::future<> operation = std::move(*_operation);
        _operation.reset();
        _new_data->signal();
        return operation.then([this] {
            assert(_state == state::FLUSHING);
            _state = state::DISABLED;
        });
    }

    void trace(const seastar::deadlock_detection::deadlock_trace& data) {
        // Trace should be disabled while flushing.
        assert(_state != state::FLUSHING);
        _trace_buffer.write(data);

        if (_state != state::DISABLED && !_disable_condition_signal) {
            if (_trace_buffer.size() >= chunk_size * minimal_chunk_count) {
                _disable_condition_signal = true;
                _new_data->signal();
                _disable_condition_signal = false;
            }
        }
    }
};

namespace seastar::deadlock_detection {

static std::unique_ptr<tracer>& get_tracer_ptr() {
    static thread_local std::unique_ptr<tracer> t = std::make_unique<tracer>(gettid());
    return t;
}

/// \brief Get output unique to each threads for dumping graph.
///
/// For each thread creates unique file for dumping graph.
/// Is thread and not shard-based because there are multiple threads in shard 0.
static tracer& get_tracer() {
    auto& tracer = get_tracer_ptr();
    assert(tracer);
    return *tracer;
}

/// Global variable for storing currently executed runtime graph vertex.
static runtime_vertex& current_traced_ptr() {
    static thread_local runtime_vertex ptr(nullptr);
    return ptr;
}

/// Serializes and writes data to appropriate file.
static void write_data(deadlock_trace& data) {
    if (!local_can_append_trace) {
        return;
    }
    // Here we check implication (can_append_trace & trace_can_io) => state == RUNNING.
    // It should be true for any thread with initialized tracer.
    // That should be reactor threads and not syscall threads.
    assert(!(local_can_append_trace && local_trace_can_io) || (get_tracer().state() == tracer::state::RUNNING));
    // Here we check implication start == FLUSHING => (!can_append_trace & trace_can_io)
    assert(!(get_tracer().state() == tracer::state::FLUSHING) || (!local_can_append_trace && local_trace_can_io));
    auto now = std::chrono::steady_clock::now();
    auto nanoseconds = std::chrono::nanoseconds(now.time_since_epoch()).count();
    data.set_timestamp(nanoseconds);
    get_tracer().trace(data);
}

static void trace_string_id(const char* ptr, size_t id) {
    static thread_local deadlock_trace data;
    data.set_type(deadlock_trace::STRING_ID);
    data.set_value(id);
    data.set_extra(ptr);
    write_data(data);
}

static size_t get_string_id(const char* ptr) {
    static thread_local std::unordered_map<const char*, size_t> ids;
    static thread_local size_t next_id = 0;
    auto it = ids.find(ptr);
    if (it == ids.end()) {
        size_t new_id = next_id++;
        ids.emplace(ptr, new_id);

        trace_string_id(ptr, new_id);

        return new_id;
    }

    return it->second;
}

/// Converts runtime vertex to serializable data.
static void serialize_vertex(const runtime_vertex& v, deadlock_trace::typed_address* data) {
    data->set_address(v.get_ptr());
    data->set_type_id(get_string_id(v._type->name()));
}

/// Converts runtime vertex to serializable data without debug info.
static void serialize_vertex_short(const runtime_vertex& v, deadlock_trace::typed_address* data) {
    data->set_address(v.get_ptr());
    // data->clear_type_id();
}

/// Converts semaphore to serializable data.
static void serialize_semaphore(const void* sem, size_t count, deadlock_trace& data) {
    data.set_sem(reinterpret_cast<uintptr_t>(sem));
    data.set_value(count);
}

/// Converts semaphore to serializable data without debug info.
static uintptr_t serialize_semaphore_short(const void* sem) {
    return reinterpret_cast<uintptr_t>(sem);
}

bool operator==(const runtime_vertex& lhs, const runtime_vertex& rhs) {
    return lhs._ptr == rhs._ptr && lhs._base_type->hash_code() == rhs._base_type->hash_code();
}

uintptr_t runtime_vertex::get_ptr() const noexcept {
    return (uintptr_t)_ptr;
}

future<> start_tracing() {
    return seastar::smp::invoke_on_all([] {
        get_tracer().start();
        assert(local_trace_can_io == false);
        local_trace_can_io = true;
    }).discard_result();
}

future<> stop_tracing() {
    return seastar::smp::invoke_on_all([] {
        assert(local_can_append_trace == true);
        local_can_append_trace = false;
        return get_tracer().stop().then([] {
            assert(local_trace_can_io == true);
            local_trace_can_io = false;
            // Delete tracer, because it can cause segfault in static destruction.
            get_tracer_ptr().reset();
        });
    });
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
    static thread_local deadlock_trace data;
    data.set_type(deadlock_trace::EDGE);
    serialize_vertex(pre, data.mutable_pre());
    serialize_vertex(post, data.mutable_vertex());
    data.set_value(speculative);
    write_data(data);
}

void trace_vertex_constructor(runtime_vertex v) {
    static thread_local deadlock_trace data;
    data.set_type(deadlock_trace::VERTEX_CTOR);
    serialize_vertex(v, data.mutable_vertex());
    write_data(data);
}

void trace_vertex_destructor(runtime_vertex v) {
    static thread_local deadlock_trace data;
    data.set_type(deadlock_trace::VERTEX_DTOR);
    serialize_vertex(v, data.mutable_vertex());
    write_data(data);
}

void trace_semaphore_constructor(const void* sem, size_t count) {
    static thread_local deadlock_trace data;
    data.set_type(deadlock_trace::SEM_CTOR);
    serialize_semaphore(sem, count, data);
    write_data(data);
}

void trace_semaphore_destructor(const void* sem, size_t count) {
    static thread_local deadlock_trace data;
    data.set_type(deadlock_trace::SEM_DTOR);
    serialize_semaphore(sem, count, data);
    write_data(data);
}

void attach_func_type(runtime_vertex ptr, const std::type_info& func_type, const char* file, uint32_t line) {
    static thread_local deadlock_trace data;
    data.set_type(deadlock_trace::FUNC_TYPE);
    serialize_vertex(ptr, data.mutable_vertex());
    data.set_value(get_string_id(func_type.name()));
    data.set_extra(fmt::format("{}:{}", file, line));
    write_data(data);
}

void trace_move_vertex(runtime_vertex from, runtime_vertex to) {
    static thread_local deadlock_trace data;
    data.set_type(deadlock_trace::VERTEX_MOVE);
    serialize_vertex_short(to, data.mutable_vertex());
    serialize_vertex_short(from, data.mutable_pre());
    write_data(data);
}

void trace_move_semaphore(const void* from, const void* to) {
    static thread_local deadlock_trace data;
    data.set_type(deadlock_trace::SEM_MOVE);
    data.set_sem(serialize_semaphore_short(to));
    data.mutable_pre()->set_address(serialize_semaphore_short(from));
    write_data(data);
}

void trace_semaphore_signal(const void* sem, size_t count, runtime_vertex caller) {
    static thread_local deadlock_trace data;
    data.set_type(deadlock_trace::SEM_SIGNAL);
    data.set_sem(serialize_semaphore_short(sem));
    data.set_value(count);
    serialize_vertex_short(caller, data.mutable_vertex());
    write_data(data);
}

void trace_semaphore_wait_completed(const void* sem, runtime_vertex post) {
    static thread_local deadlock_trace data;
    data.set_type(deadlock_trace::SEM_WAIT_CMPL);
    data.set_sem(serialize_semaphore_short(sem));
    serialize_vertex_short(post, data.mutable_vertex());
    write_data(data);
}

void trace_semaphore_wait(const void* sem, size_t count, runtime_vertex pre, runtime_vertex post) {
    static thread_local deadlock_trace data;
    data.set_type(deadlock_trace::SEM_WAIT);
    data.set_sem(serialize_semaphore_short(sem));
    data.set_value(count);
    serialize_vertex_short(post, data.mutable_vertex());
    serialize_vertex_short(pre, data.mutable_pre());
    write_data(data);
}

}
#else
#include <seastar/core/future.hh>
namespace seastar::deadlock_detection {
future<> start_tracing() {
    return seastar::make_ready_future<>();
}
future<> stop_tracing() {
    return seastar::make_ready_future<>();
}
}
#endif
