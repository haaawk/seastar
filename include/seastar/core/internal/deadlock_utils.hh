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

#pragma once

#include <list>
#include <map>
#include <variant>
#include <assert.h>
#include <stdint.h>
#include <string>

namespace seastar {

#ifdef SEASTAR_DEADLOCK_DETECTION

class task;
template <typename, typename>
class basic_semaphore;
template <typename>
class future;
template <typename>
class promise;

namespace internal {
class future_base;
class promise_base;
template <typename>
class promise_base_with_type;
}

namespace deadlock_detection {

using info_tuple = std::tuple<const void*, const std::type_info*, const std::type_info*>;

// Those functions get necessary info about certain object in runtime graph.
inline info_tuple get_info(const internal::promise_base* ptr);
inline info_tuple get_info(const internal::future_base* ptr);
template <typename T>
info_tuple get_info(const internal::promise_base_with_type<T>* ptr);
template <typename T>
info_tuple get_info(const future<T>* ptr);
template <typename T>
info_tuple get_info(const promise<T>* ptr);
inline info_tuple get_info(const task* ptr);


/// Represents runtime vertex (e.g. task, promise, future),
/// in a way that doesn't use template and allows to remove cyclical dependencies.
struct runtime_vertex {
    uintptr_t _ptr;
    const std::type_info* _base_type;
    const std::type_info* _type;

    runtime_vertex(std::nullptr_t) : _ptr(0), _base_type(&typeid(nullptr)), _type(&typeid(nullptr)) {}

    template <typename T>
    runtime_vertex(const T* ptr) : runtime_vertex(nullptr) {
        if (ptr) {
            const void* tmp_ptr;
            std::tie(tmp_ptr, _base_type, _type) = seastar::deadlock_detection::get_info(ptr);
            _ptr = (uintptr_t)tmp_ptr;
        }
    }

    uintptr_t get_ptr() const noexcept;

    friend bool operator==(const runtime_vertex& lhs, const runtime_vertex& rhs);
};

/// Get current vertex that is being executed.
runtime_vertex get_current_traced_ptr();

/// Updated current traced vertex using RAII.
class current_traced_vertex_updater {
    runtime_vertex _previous_ptr;
    runtime_vertex _new_ptr;

public:
    current_traced_vertex_updater(runtime_vertex new_ptr);

    ~current_traced_vertex_updater();
};

/// Traces causal edge between two vertices.
/// e.g from task to promise that it completes.
void trace_edge(runtime_vertex pre, runtime_vertex post, bool speculative = false);
/*inline void trace_edge(runtime_vertex pre, runtime_vertex post) {
    trace_edge(pre, post, false);
}
*/

/// Traces creation of runtime vertex (or reinitialization).
void trace_vertex_constructor(runtime_vertex v);
/// Traced destruction of runtime vertex (or deinitialization).
void trace_vertex_destructor(runtime_vertex v);

/// Traces construction of semaphore.
void trace_semaphore_constructor(const void* sem, size_t count);
template <typename T1, typename T2>
void inline trace_semaphore_constructor(const basic_semaphore<T1, T2>* sem) {
    trace_semaphore_constructor(static_cast<const void*>(sem), sem->available_units());
}

/// Traces destruction of semaphore.
void trace_semaphore_destructor(const void* sem, size_t count);
template <typename T1, typename T2>
void inline trace_semaphore_destructor(const basic_semaphore<T1, T2>* sem) {
    trace_semaphore_destructor(static_cast<const void*>(sem), sem->available_units());
}

/// Traces calling of signal on semaphore by certain runtime vertex.
void trace_semaphore_signal(const void* sem, size_t count, runtime_vertex caller);
template <typename T1, typename T2>
void inline trace_semaphore_signal_caller(const basic_semaphore<T1, T2>* sem, size_t count, runtime_vertex caller) {
    trace_semaphore_signal(static_cast<const void*>(sem), count, caller);
}

/// Traces scheduling of runtime vertex after signal was called on semaphore.
void trace_semaphore_wait_completed(const void* sem, runtime_vertex post);
template <typename T1, typename T2>
void inline trace_semaphore_signal_schedule(const basic_semaphore<T1, T2>* sem, runtime_vertex post) {
    trace_semaphore_wait_completed(static_cast<const void*>(sem), post);
}

/// Traces successful wait on a semaphore with the vertex that is result of wait.
void trace_semaphore_wait(const void* sem, size_t count, runtime_vertex pre, runtime_vertex post);
template <typename T1, typename T2>
void inline trace_semaphore_wait(const basic_semaphore<T1, T2>* sem, size_t count, runtime_vertex pre, runtime_vertex post) {
    trace_semaphore_wait(static_cast<const void*>(sem), count, pre, post);
}

void attach_func_type(runtime_vertex pt, const std::type_info& func_typ, const char* file = __builtin_FILE(), uint32_t line = __builtin_LINE());
template<typename FuncType>
void inline attach_func_type(runtime_vertex ptr, const char* file = __builtin_FILE(), uint32_t line = __builtin_LINE()) {
    attach_func_type(ptr, typeid(FuncType), file, line);
}

void trace_move_vertex(runtime_vertex from, runtime_vertex to);
void trace_move_semaphore(const void* from, const void* to);

future<void> start_tracing();

future<void> stop_tracing();

}

#else

namespace deadlock_detection {

// Mutliple empty functions to be optimized out when deadlock detection is disabled.
constexpr void trace_edge(void*, void*, bool = false) {}
constexpr void trace_vertex_constructor(void*) {}
constexpr void trace_vertex_destructor(void*) {}
constexpr void trace_move_vertex(const void*, const void*) {}
constexpr void trace_move_semaphore(const void*, const void*) {}
template <typename>
constexpr void attach_func_type(const void*, const char* = nullptr, uint32_t  = 0) {}
constexpr void trace_semaphore_constructor(const void*) {}
constexpr void trace_semaphore_destructor(const void*) {}
constexpr void trace_semaphore_signal(const void*, size_t, const void*) {}
constexpr void trace_semaphore_wait_completed(const void*, const void*) {}
constexpr void trace_semaphore_wait(const void*, size_t, const void*, const void*) {}
constexpr void attach_func_type(const void*, const char* = nullptr, const char* = nullptr, uint32_t = 0) {}
future<> start_tracing();
future<> stop_tracing();

constexpr std::nullptr_t get_current_traced_ptr() {
    return nullptr;
}

struct current_traced_vertex_updater {
    constexpr current_traced_vertex_updater(void*) noexcept {}
};

struct local_trace_stopper {};

}

#endif
}
