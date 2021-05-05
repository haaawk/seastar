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

// Current state of tracing, doesn't work with multiple calls to app_template::run().
enum trace_state {
    START_TRACE,
    BEFORE_INITIALIZATION,
    START_IO,
    STARTING_TRACER,
    RUNNING,
    GLOBAL_SYNC,
    STOPPED_TRACE,
    FLUSHING,
    STOPPED_IO,
    FINISHED,
};
static thread_local trace_state local_trace_state = trace_state::BEFORE_INITIALIZATION;
// Used in state GLOBAL_SYNC, to avoid races in disabling tracing.
static std::atomic<bool> global_can_append_trace = true;

/// Collects traces in shard and asynchronously writes them to file.
class tracer {
public:
    enum class state {
        DISABLED,
        RUNNING,
        FLUSHING,
    };
private:
    // We assume that chunk_size % disk_write_dma_alignment() == 0.
    static constexpr size_t chunk_size = 0x1000;
    // Minimal size of a batch to write.
    // If value is too low, then tracing loop will trace itself in infinite loop.
    static constexpr size_t minimal_chunk_count = 64;

    struct alignas(chunk_size) page {
        char _data[chunk_size];
    };

    // Buffer for trace data.
    // Supports move operations.
    struct buffer {
        size_t _length = 0;
        std::vector<page> _data{};

        void reserve_for_size(size_t append_size) {
            if (_length + append_size > capacity()) {
                size_t new_capacity = capacity() * 2 + append_size;
                size_t new_size = new_capacity / sizeof(page) + 1;
                _data.reserve(new_size);
                _data.resize(new_size);
            }
        }

        size_t capacity() const {
            return _data.size() * sizeof(page);
        }

        char* start_ptr() {
            return reinterpret_cast<char*>(_data.data()->_data);
        }

        char* end_ptr() {
            return start_ptr() + _length;
        }

        // Writes deadlock_trace in following format:
        // - uint16_t in little-endian representing size of following trace,
        // - trace data itself as definied by protobuf.
        void write(const seastar::deadlock_detection::deadlock_trace& data) {
            assert(data.IsInitialized());
            size_t data_size = data.ByteSizeLong();
            assert(data_size <= UINT16_MAX);
            uint16_t size = data_size;

            reserve_for_size(size + sizeof(size));

            uint16_t written_size = htole16(size);
            memcpy(end_ptr(), &written_size, sizeof(size));
            _length += sizeof(size);

            bool result = data.SerializeToArray(end_ptr(), size);
            (void)result;
            assert(result);
            _length += size;
        }

        // Append raw data, used when moving data from one buffer to another.
        void write(const std::string_view& data) {
            size_t size = data.size();

            reserve_for_size(size);

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

    // Future that represents tracer has stopped.
    std::unique_ptr<seastar::future<>> _operation{};
    // Condition variable for waking up worker.
    std::unique_ptr<seastar::condition_variable> _new_data{};
    // Buffer to which traces are appended.
    buffer _trace_buffer{};
    // Buffer from which I/O is outgoind.
    buffer _write_buffer{};

    state _state = state::DISABLED;
    // Id of tracer (should be unique across all threads).
    size_t _id;
    // Current trace file size.
    size_t _file_size = 0;
    // Disables use of wake on _new_data.
    bool _disable_condition_signal = false;


    struct disable_wake {
        tracer* _tracer;
        bool _prev_val;
        disable_wake(tracer& tracer) : _tracer(&tracer), _prev_val(tracer._disable_condition_signal) {
            tracer._disable_condition_signal = true;
        }

        ~disable_wake() {
            assert(_tracer->_disable_condition_signal == true);
            _tracer->_disable_condition_signal = _prev_val;
        }
    } __attribute__((aligned(16)));

    // Main worker loop.
    seastar::future<> loop(seastar::file&& file) {
        _file_size = 0;
        return seastar::do_with(std::move(file), [this](auto& file) {
            return seastar::repeat([&file, this] {
                return loop_impl(file);
            });
        });
    }

    // One iterator of worker loop.
    seastar::future<seastar::stop_iteration> loop_impl(seastar::file& file) {
        // We don't want to try waking up ourselves.
        auto disable = disable_wake(*this);

        // Check if we can actually trace.
        assert(_state != state::DISABLED);
        assert(trace_state::START_IO <= local_trace_state && local_trace_state < trace_state::STOPPED_IO);

        if (_state == state::FLUSHING) {
            // If we are flushing then flush and end loop.
            return flush(file).then([] {
                return seastar::stop_iteration::yes;
            });
        }
        if (_trace_buffer.size() < chunk_size * minimal_chunk_count) {
            // Not enough data to write.
            return _new_data->wait().then([] {
                return seastar::stop_iteration::no;
            });
        } else {
            // Swap buffer, so we don't need to copy.
            std::swap(_trace_buffer, _write_buffer);
            size_t chunk_count = _write_buffer.size() / chunk_size;
            assert(chunk_count >= minimal_chunk_count);

            size_t length = chunk_count * chunk_size;
            // Put last partial chunk back into trace buffer.
            _trace_buffer.write(std::string_view(_write_buffer.start_ptr() + length, _write_buffer.size() - length));
            // Truncate partial chunk.
            _write_buffer._length = length;
            return file.dma_write(_file_size, _write_buffer.start_ptr(), length).then([this](size_t written) {
                if (written != _write_buffer.size()) {
                    throw std::exception();
                }
                _write_buffer.reset();
                _file_size += written;
                return seastar::stop_iteration::no;
            });
        }
    }

    seastar::future<> flush(seastar::file& file) {
        assert(_state == state::FLUSHING);
        assert(local_trace_state == trace_state::FLUSHING);
        std::swap(_trace_buffer, _write_buffer);

        size_t chunk_count = (_write_buffer.size() + chunk_size - 1) / chunk_size;
        size_t length = chunk_count * chunk_size;
        size_t overflow = length - _write_buffer.size();

        // Write extra bytes if last chunk is partial.
        _write_buffer._length = length;
        return file.dma_write(_file_size, _write_buffer.start_ptr(), length).then([this](size_t written) {
            if (written != _write_buffer.size()) {
                throw std::exception();
            }
            _write_buffer.reset();
            _file_size += written;
            return seastar::make_ready_future<>();
        }).then([this, &file, overflow] {
            // Truncate extra data.
            return file.truncate(_file_size - overflow);
        }).then([&file] {
            return file.flush();
        }).then([&file] {
            return file.close();
        });
    }


public:

    tracer() {}

    ~tracer() {
        // If operation then traces was started but not stopped.
        assert(!_operation);
    }

    state state() const noexcept {
        return _state;
    }

    /// Starts tracer loop and initializes additional needed data.
    void start() {
        _id = seastar::this_shard_id();
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

    /// Returns future representing tracer stop.
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

    /// Appends trace to current trace buffer.
    void trace(const seastar::deadlock_detection::deadlock_trace& data, bool can_wake) {
        // Trace should be disabled while flushing.
        assert(_state != state::FLUSHING);

        _trace_buffer.write(data);

        if (_state != state::DISABLED && !_disable_condition_signal && _new_data && can_wake) {
            if (_trace_buffer.size() >= chunk_size * minimal_chunk_count) {
                auto disable = disable_wake(*this);
                _new_data->signal();
            }
        }
    }
};

namespace seastar::deadlock_detection {

/// \brief Get tracer unique to each thread for dumping graph.
///
/// For each thread creates unique file for dumping graph.
/// Is thread and not shard-based because there are multiple threads in shard 0.
static tracer& get_tracer() {
    static thread_local tracer t;
    return t;
}

future<> start_tracing() {
    return seastar::smp::invoke_on_all([] {
        assert(local_trace_state == trace_state::BEFORE_INITIALIZATION);
        local_trace_state = trace_state::STARTING_TRACER;
        get_tracer().start();
        assert(local_trace_state == trace_state::STARTING_TRACER);
        local_trace_state = trace_state::RUNNING;
    }).discard_result();
}

future<> stop_tracing() {
    return seastar::smp::invoke_on_all([] {
        assert(local_trace_state == trace_state::RUNNING);
        local_trace_state = trace_state::GLOBAL_SYNC;
    }).finally([] {
        // We have to stop all traces all at once, otherwise we have following problem:
        // shard 0 disables tracing
        // shard 0 creates smp task (without traced vertex)
        // shard 1 run smp task creating edge from the task
        // Vertex in said edge has missing ctor, breaking parser.
        global_can_append_trace.store(false);
    }).finally([] {
        return seastar::smp::invoke_on_all([] {
            assert(local_trace_state == trace_state::GLOBAL_SYNC);
            local_trace_state = trace_state::FLUSHING;
            return get_tracer().stop().finally([] {
                assert(local_trace_state == trace_state::FLUSHING);
                local_trace_state = trace_state::FINISHED;
            });
        });
    });
}

}
#else
#include <seastar/core/future.hh>
namespace seastar::deadlock_detection {
future<void> start_tracing() {
    return seastar::make_ready_future<void>();
}
future<void> stop_tracing() {
    return seastar::make_ready_future<void>();
}
}
#endif
