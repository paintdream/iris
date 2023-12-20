/*
The Iris Concurrency Framework

This software is a C++ 11 Header-Only reimplementation of core part from project PaintsNow.

The MIT License (MIT)

Copyright (c) 2014-2023 PaintDream

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#pragma once

#include "iris_common.h"
#include <functional>
#include <vector>
#include <mutex>
#include <chrono>
#include <thread>
#include <condition_variable>

namespace iris {
	namespace impl {
		// storage for queued tasks
		template <typename queue_buffer_t, bool>
		struct storage_t {
			storage_t() noexcept {}
			storage_t(storage_t&& rhs) noexcept {
				queue_buffer = std::move(rhs.queue_buffer);
			}

			storage_t& operator = (storage_t&& rhs) noexcept {
				queue_buffer = std::move(rhs.queue_buffer);
				return *this;
			}

			bool empty() const noexcept {
				return queue_buffer.empty();
			}

			queue_buffer_t queue_buffer;
			std::mutex mutex;
		};

		template <typename queue_buffer_t>
		struct storage_t<queue_buffer_t, false> {
			storage_t() noexcept : current_version(0), next_version(0) {
				barrier_version.store(0, std::memory_order_relaxed);
			}

			storage_t(storage_t&& rhs) noexcept {
				barrier_version.store(rhs.barrier_version.load(std::memory_order_acquire), std::memory_order_release);
				queue_buffers = std::move(rhs.queue_buffers);
				queue_versions = std::move(rhs.queue_versions);
				current_version = rhs.current_version;
				next_version = rhs.next_version;
			}

			bool empty() const noexcept {
				for (size_t i = 0; i < queue_buffers.size(); i++) {
					if (!queue_buffers[i].empty())
						return false;
				}

				return true;
			}

			std::atomic<size_t> barrier_version;
			std::vector<queue_buffer_t> queue_buffers;
			std::vector<size_t> queue_versions;
			size_t current_version;
			size_t next_version;
		};
		
		// for exception safe, roll back atomic operations as needed
		enum guard_operation {
			add, sub, invalidate
		};

		template <guard_operation operation>
		struct atomic_guard_t {
			atomic_guard_t(std::atomic<size_t>& var) : variable(&var) {}
			~atomic_guard_t() noexcept {
				if (variable != nullptr) {
					if /* constexpr */ (operation == add) {
						variable->fetch_add(1, std::memory_order_release);
					} else if /* constexpr */ (operation == sub) {
						variable->fetch_sub(1, std::memory_order_release);
					} else {
						variable->store(~size_t(0), std::memory_order_release);
					}
				}
			}

			void cleanup() {
				variable = nullptr;
			}

		private:
			std::atomic<size_t>* variable;
		};

	}

	// dispatch routines:
	//     1. from warp to warp. (queue_routine/queue_routine_post).
	//     2. from external thread to warp (queue_routine_external).
	//     3. from warp to external in parallel (queue_routine_parallel).
	// you can select implemention from warp/strand via 'strand' template parameter.
	template <typename worker_t, bool strand = false, typename func_t = std::function<void()>, template <typename...> class allocator_t = iris_default_block_allocator_t>
	struct iris_warp_t {
		// for exception safe!
		struct suspend_guard_t {
			suspend_guard_t(iris_warp_t* w) noexcept : warp(w) {}
			void cleanup() noexcept { warp = nullptr; }

			~suspend_guard_t() noexcept {
				// if compiler detects warp is nullptr
				// it can remove the ~suspend_guard_t() calling
				if (warp != nullptr) {
					warp->resume();
				}
			}

		private:
			iris_warp_t* warp;
		};

		template <bool allow_suspended>
		struct preempt_guard_t {
			explicit preempt_guard_t(iris_warp_t& warp_instance) noexcept : warp(warp_instance) {
#if IRIS_DEBUG
				current = iris_warp_t::get_current_warp();
#endif
				state = allow_suspended ? true : !warp.is_suspended();
				if (state) {
					if (iris_warp_t::get_current_warp() == &warp) {
						preempted = false;
					} else {
						preempted = warp.preempt();
						state = preempted && (allow_suspended || !warp.is_suspended());
					}
				} else {
					preempted = false;
				}
			}

			void cleanup() noexcept {
				state = preempted = false;
			}

			~preempt_guard_t() noexcept {
#if IRIS_DEBUG
				iris_warp_t* m = iris_warp_t::get_current_warp();
				if (state) {
					IRIS_ASSERT(m == &warp);
				}
#endif

				if (preempted) {
					warp.yield();
#if IRIS_DEBUG
					iris_warp_t* n = iris_warp_t::get_current_warp();
					IRIS_ASSERT(current == n);
#endif
				}
			}

			operator bool() const noexcept {
				return state;
			}

		protected:
			iris_warp_t& warp;
#if IRIS_DEBUG
			iris_warp_t* current;
#endif
			bool preempted;
			bool state;
		};

		using function_t = func_t;
		using queue_buffer_t = iris_queue_list_t<function_t, allocator_t>;
		using async_worker_t = worker_t;
		static constexpr size_t block_size = iris_extract_block_size<function_t, allocator_t>::value;
		static constexpr size_t queue_state_idle = 0u;
		static constexpr size_t queue_state_pending = 1u;
		static constexpr size_t queue_state_executing = 2u;

		// moving capture is not supported until C++ 14
		// so we wrap some functors here

		struct execute_t {
			execute_t(iris_warp_t& w) noexcept : warp(w) {}
			void operator () () {
				warp.template execute<strand, false>();
			}

			iris_warp_t& warp;
		};

		template <typename callable_t>
		struct external_t {
			template <typename proc_t>
			external_t(iris_warp_t& w, proc_t&& c) noexcept : warp(w), callable(std::forward<proc_t>(c)) {}
			void operator () () {
				warp.queue_routine_post(std::move(callable));
			}

			iris_warp_t& warp;
			callable_t callable;
		};

		template <typename callable_t>
		struct suspend_t {
			template <typename proc_t>
			suspend_t(iris_warp_t& w, proc_t&& c) noexcept : warp(w), callable(std::forward<proc_t>(c)) {}
			void operator () () {
				suspend_guard_t guard(&warp);
				callable();
				guard.cleanup();

				warp.resume();
			}

			iris_warp_t& warp;
			callable_t callable;
		};

		// do not copy this structure, only to move
		iris_warp_t(const iris_warp_t& rhs) = delete;
		iris_warp_t& operator = (const iris_warp_t& rhs) = delete;
		iris_warp_t& operator = (iris_warp_t&& rhs) = delete;

		// for strands, we prepare just one queue protected by a mutex
		// for warps, we prepare one queue for each thread to remove mutex requirements

		template <bool s>
		typename std::enable_if<s>::type init_buffers(size_t thread_count) noexcept {}

		template <bool s>
		typename std::enable_if<!s>::type init_buffers(size_t thread_count) noexcept(noexcept(std::declval<iris_warp_t>().storage.queue_buffers.resize(thread_count))) {
			storage.queue_buffers.resize(thread_count);
			storage.queue_versions.resize(thread_count);
		}

		// initialize with specified priority, all tasks that runs on this warp will be scheduled with this priority
		explicit iris_warp_t(async_worker_t& worker, size_t prior = 0) : async_worker(worker), priority(prior), stack_next_warp(nullptr) {
			init_buffers<strand>(worker.get_thread_count());

			thread_warp.store(nullptr, std::memory_order_relaxed);
			suspend_count.store(0, std::memory_order_relaxed);
			interrupting.store(0, std::memory_order_relaxed);
#if IRIS_DEBUG
			self_polling_count.store(0, std::memory_order_relaxed);
#endif
			queueing.store(queue_state_idle, std::memory_order_release);
		}

		iris_warp_t(iris_warp_t&& rhs) noexcept : async_worker(rhs.async_worker), storage(std::move(rhs.storage)), priority(rhs.priority), stack_next_warp(rhs.stack_next_warp) {
			thread_warp.store(rhs.thread_warp.load(std::memory_order_relaxed), std::memory_order_relaxed);
			suspend_count.store(rhs.suspend_count.load(std::memory_order_relaxed), std::memory_order_relaxed);
			interrupting.store(rhs.interrupting.load(std::memory_order_relaxed), std::memory_order_relaxed);
#if IRIS_DEBUG
			self_polling_count.store(rhs.self_polling_count.load(std::memory_order_release), std::memory_order_relaxed);
#endif
			queueing.store(rhs.queueing.load(std::memory_order_relaxed), std::memory_order_relaxed);

			rhs.stack_next_warp = nullptr;
			rhs.thread_warp.store(nullptr, std::memory_order_relaxed);
			rhs.suspend_count.store(0, std::memory_order_relaxed);
			rhs.interrupting.store(0, std::memory_order_relaxed);
#if IRIS_DEBUG
			rhs.self_polling_count.store(0, std::memory_order_relaxed);
#endif
			rhs.queueing.store(queue_state_idle, std::memory_order_release);
		}

		~iris_warp_t() noexcept {
			// a warp cannot be destructed in its context
			IRIS_ASSERT(get_current_warp_internal() != this);

			// execute remaining tasks on destruction
			while (!join<true, true>()) {}
		}

		// take execution atomically, returns true on success.
		bool preempt() noexcept {
			iris_warp_t** expected = nullptr;
			iris_warp_t* current = get_current_warp_internal();
			if (thread_warp.compare_exchange_strong(expected, &get_current_warp_internal(), std::memory_order_acquire)) {
				get_current_warp_internal() = this;
				stack_next_warp = current;

				return true;
			} else {
				IRIS_ASSERT(get_current_warp_internal() != this);
				return false;
			}
		}

		// get stack warp pointer
		iris_warp_t* get_stack_next() const noexcept {
			return stack_next_warp;
		}

		// interrupt warp on running
		bool interrupt() noexcept {
			return interrupting.exchange(1, std::memory_order_relaxed) == 0;
		}

		// check if running, the result is meaningless for most calls in multithreaded context
		bool running() const noexcept {
			return thread_warp.load(std::memory_order_acquire) != nullptr;
		}

		// assert that current warp is just this
		void validate() const noexcept {
			IRIS_ASSERT(this == get_current_warp_internal());
		}

		bool is_suspended() const noexcept {
			return suspend_count.load(std::memory_order_acquire) != 0;
		}

		bool empty() const noexcept {
			// must called while not running!
			return storage.empty();
		}

		// yield execution atomically, returns true on success.
		bool yield() noexcept(noexcept(std::declval<iris_warp_t>().flush())) {
			iris_warp_t** exp = &get_current_warp_internal();
			if (thread_warp.compare_exchange_strong(exp, reinterpret_cast<iris_warp_t**>(~(size_t)0), std::memory_order_release)) {
				get_current_warp_internal() = stack_next_warp;
				stack_next_warp = nullptr;
				thread_warp.store(nullptr, std::memory_order_release);

				if (queueing.exchange(queue_state_idle, std::memory_order_relaxed) == queue_state_pending) {
					flush();
				}

				return true;
			} else {
				IRIS_ASSERT(get_current_warp_internal() == nullptr || exp == nullptr || *exp == nullptr);
				return false;
			}
		}

		// blocks all tasks preemptions, stacked with internally counting.
		bool suspend() noexcept {
			return suspend_count.fetch_add(1, std::memory_order_acquire) == 0;
		}

		// allows all tasks preemptions, stacked with internally counting.
		// returns true on final resume.
		bool resume() noexcept(noexcept(std::declval<iris_warp_t>().flush())) {
			bool ret = suspend_count.fetch_sub(1, std::memory_order_release) == 1;

			if (ret) {
				// all suspend requests removed, try to flush me
				queueing.store(queue_state_idle, std::memory_order_relaxed);
				flush();
			}

			return ret;
		}

		// send task to this warp. call it directly if we are on warp.
		template <typename callable_t>
		void queue_routine(callable_t&& func) noexcept(noexcept(func()) &&
			noexcept(std::declval<iris_warp_t>().template push<strand>(std::forward<callable_t>(func)))) {
			IRIS_ASSERT(async_worker.get_current_thread_index() != ~size_t(0));

			// can be executed immediately?
			// try to acquire execution, if it fails, just go posting
			preempt_guard_t<false> preempt_guard(*this);
			if (preempt_guard) {
#if IRIS_DEBUG
				self_polling_count.fetch_add(1, std::memory_order_acquire);
				impl::atomic_guard_t<impl::sub> self_polling_guard(self_polling_count);
#endif
				func();
			} else {
				// send to current thread slot of current warp.
				queue_routine_post<callable_t>(std::forward<callable_t>(func));
			}
		}

		// send task to warp indicated by warp. always post it to queue.
		template <typename callable_t>
		void queue_routine_post(callable_t&& func) noexcept(noexcept(std::declval<iris_warp_t>().template push<strand>(std::forward<callable_t>(func)))) {
			// always send to current thread slot of current warp.
			push<strand>(std::forward<callable_t>(func));
		}

		// queue external routine from non-warp/yielded warp
		template <typename callable_t>
		void queue_routine_external(callable_t&& func) {
			IRIS_ASSERT(async_worker.get_current_thread_index() == ~size_t(0));
			async_worker.queue(external_t<typename std::remove_reference<callable_t>::type>(*this, std::forward<callable_t>(func)), priority);
		}

		// queue a barrier here, any routines queued after this barrier must be scheduled before any routines before this barrier
		void queue_barrier() {
			queue_barrier_internal<strand>();
		}

		// queue task parallelly to async_worker, blocking the execution of current warp at the same time
		// it is useful to implement read-lock affairs
		template <typename callable_t>
		void queue_routine_parallel(callable_t&& func, size_t priority = 0) {
			IRIS_ASSERT(get_current_warp_internal() == this);
#if IRIS_DEBUG
			IRIS_ASSERT(self_polling_count.load(std::memory_order_acquire) == 0);
#endif
			suspend();

			suspend_guard_t guard(this);
			async_worker.queue(suspend_t<typename std::remove_reference<callable_t>::type>(*this, std::forward<callable_t>(func)), priority);
			guard.cleanup();
		}

		// cleanup the dispatcher, pass true to 'execute_remaining' to make sure all tasks are executed finally.
		template <bool execute_remaining = true, bool finalize = false, typename iterator_t = iris_warp_t*>
		static bool join(iterator_t begin, iterator_t end) {
			IRIS_PROFILE_SCOPE(__FUNCTION__);

			if /* constexpr */ (!finalize) {
				// suspend all warps so we can take over tasks
				for (iterator_t p = begin; p != end; ++p) {
					(*p).suspend();
				}
			}

			// do cleanup
			bool empty = true;
			for (iterator_t p = begin; p != end; ++p) {
				empty = empty && (*p).empty();

				while (true) {
					preempt_guard_t<true> preempt_guard(*p);
					if (!preempt_guard) {
						std::this_thread::sleep_for(std::chrono::milliseconds(50));
					} else {
						// execute remaining
						if (execute_remaining) {
							(*p).template execute_internal<strand, true>();
						}

						break;
					}
				} 
			}

			// resume warps if not finalizing
			if /* constexpr */ (!finalize) {
				for (iterator_t p = begin; p != end; ++p) {
					(*p).resume();
				}
			}

			return empty;
		}

		template <bool execute_remaining = true, bool finalize = false>
		bool join() {
			return join<execute_remaining, finalize>(this, this + 1);
		}

		// get current thread's warp binding instance
		static iris_warp_t* get_current_warp() noexcept {
			return get_current_warp_internal();
		}

		async_worker_t& get_async_worker() noexcept {
			return async_worker;
		}

		const async_worker_t& get_async_worker() const noexcept {
			return async_worker;
		}

	protected:
		// get current warp index (saved in thread_local storage)
		static iris_warp_t*& get_current_warp_internal() noexcept {
			return iris_static_instance_t<iris_warp_t*>::get_thread_local();
		}

		template <bool s>
		typename std::enable_if<s>::type queue_barrier_internal() {}

		template <bool s>
		typename std::enable_if<!s>::type queue_barrier_internal() {
			size_t counter = storage.barrier_version.fetch_add(1, std::memory_order_acquire) + 1;
			queue_routine_post([this, counter]() noexcept {
				storage.next_version = counter;
			});
		}

		// execute all tasks scheduled at once.
		template <bool s, bool force>
		typename std::enable_if<s>::type execute_internal() noexcept(
			noexcept(std::declval<iris_warp_t>().flush()) && noexcept(std::declval<function_t>()())) {
			IRIS_PROFILE_SCOPE(__FUNCTION__);

			// mark for queueing, avoiding flush me more than once.
			queueing.store(queue_state_executing, std::memory_order_release);
			iris_warp_t** warp_ptr = &get_current_warp_internal();
			IRIS_ASSERT(*warp_ptr == this);
			queue_buffer_t& buffer = storage.queue_buffer;

			// execute tasks in queue_buffer until suspended or interruption occurred
			size_t execute_counter;

			do {
				execute_counter = 0;
				while (!buffer.empty()) {
					typename queue_buffer_t::element_t func = std::move(buffer.top());
					buffer.pop();

					func(); // we have already thread_fence acquired above
					execute_counter++;

					if ((!force && suspend_count.load(std::memory_order_relaxed) != 0) || *warp_ptr != this)
						break;

					if (interrupting.load(std::memory_order_relaxed) != 0) {
						interrupting.store(0, std::memory_order_release);
						break;
					}
				}
			} while (execute_counter != 0);
		}

		template <bool s, bool force>
		typename std::enable_if<!s>::type execute_internal() noexcept(
			noexcept(std::declval<iris_warp_t>().flush()) && noexcept(std::declval<function_t>()())) {
			IRIS_PROFILE_SCOPE(__FUNCTION__);

			// mark for queueing, avoiding flush me more than once.
			queueing.store(queue_state_executing, std::memory_order_release);
			iris_warp_t** warp_ptr = &get_current_warp_internal();
			IRIS_ASSERT(*warp_ptr == this);

			// execute tasks in queue_buffer until suspended or interruption occurred
			std::vector<queue_buffer_t>& queue_buffers = storage.queue_buffers;
			std::vector<size_t>& queue_versions = storage.queue_versions;
			size_t& current_version = storage.current_version;
			size_t& next_version = storage.next_version;
			size_t execute_counter;

			do {
				execute_counter = 0;
				size_t step_version = current_version;
				for (size_t i = 0; i < queue_buffers.size(); i++) {
					queue_buffer_t& buffer = queue_buffers[i];
					size_t& counter = queue_versions[i];

					next_version = counter;
					while (static_cast<ptrdiff_t>(current_version - counter) >= 0 && !buffer.empty()) {
						typename queue_buffer_t::element_t func = std::move(buffer.top());
						buffer.pop(); // pop up before calling

						func(); // may throws exceptions
						execute_counter++;
						counter = next_version;

						if ((!force && suspend_count.load(std::memory_order_relaxed) != 0) || *warp_ptr != this)
							return;

						if (interrupting.load(std::memory_order_relaxed) != 0) {
							interrupting.store(0, std::memory_order_release);
							return;
						}
					}

					if (current_version + 1 == counter) {
						step_version = counter;
					} else if (static_cast<ptrdiff_t>(current_version - counter) > 0) { // in case of counter overflow
						counter = current_version;
					}
				}

				current_version = step_version;
			} while (execute_counter != 0);
		}

		template <bool s, bool force>
		void execute() noexcept(noexcept(std::declval<iris_warp_t>().template execute_internal<s, force>())) {
			IRIS_PROFILE_SCOPE(__FUNCTION__);

			if (suspend_count.load(std::memory_order_acquire) == 0) {
				// try to acquire execution, if it fails, there must be another thread doing the same thing
				// and it's ok to return immediately.
				preempt_guard_t<true> preempt_guard(*this);
				if (preempt_guard) {
					if (suspend_count.load(std::memory_order_acquire) == 0) { // double check for suspend_count
						execute_internal<s, force>();
						
						preempt_guard.cleanup();
						if (!yield()) {
							// already yielded? try to repost me to process remaining tasks.
							flush();
						}
					} else {
						queueing.store(queue_state_pending, std::memory_order_relaxed);
					}
				}
			}
		}

		// commit execute request to specified worker.
		void flush() noexcept(noexcept(std::declval<iris_warp_t>().async_worker.queue(std::declval<function_t>(), 0))) {
			// if current state is executing, the executing routine will reinvoke flush() if it detected pending state while exiting
			// so we just need to queue a flush routine as soon as current state is idle
			if (queueing.exchange(queue_state_pending, std::memory_order_acq_rel) == queue_state_idle) {
				async_worker.queue(execute_t(*this), priority);
			}
		}

		// queue task from specified thread.
		template <bool s, typename callable_t>
		typename std::enable_if<s>::type push(callable_t&& func) {
			do {
				std::lock_guard<std::mutex> guard(storage.mutex);
				storage.queue_buffer.push(std::forward<callable_t>(func));
			} while (false);

			flush();
		}

		template <bool s, typename callable_t>
		typename std::enable_if<!s>::type push(callable_t&& func) noexcept(
			noexcept(std::declval<queue_buffer_t>().push(std::forward<callable_t>(func))) &&
			noexcept(std::declval<iris_warp_t>().flush())) {
			size_t thread_index = async_worker.get_current_thread_index();
			if (thread_index != ~size_t(0)) {
				std::vector<queue_buffer_t>& queue_buffers = storage.queue_buffers;
				IRIS_ASSERT(thread_index < queue_buffers.size());
				queue_buffer_t& buffer = queue_buffers[thread_index];
				buffer.push(std::forward<callable_t>(func));

				// flush the task immediately
				flush();
			} else {
				IRIS_ASSERT(async_worker.is_terminated());
				IRIS_ASSERT(!storage.queue_buffers.empty());
				storage.queue_buffers[0].push(std::forward<callable_t>(func));
			}
		}

	protected:
		async_worker_t& async_worker; // host async worker
		std::atomic<iris_warp_t**> thread_warp; // save the running thread warp address.
		std::atomic<size_t> suspend_count; // current suspend count
		std::atomic<size_t> interrupting; // is interrupting by external request?
#if IRIS_DEBUG
		std::atomic<size_t> self_polling_count; // counting self polling, it is not compatible with queue_routine_parallel if it has non-zero value.
#endif
		std::atomic<size_t> queueing; // is flush request sent to async_worker? 0 : not yet, 1 : yes, 2 : is to flush right away.
		impl::storage_t<queue_buffer_t, strand> storage; // task storage
		size_t priority;
		iris_warp_t* stack_next_warp;
	};

	// dispatcher based-on directed-acyclic graph
	template <typename base_warp_t>
	struct iris_dispatcher_t {
		using warp_t = base_warp_t;
		// wraps task data
		using function_t = typename warp_t::function_t;
		struct routine_t {
		protected:
			template <typename func_t>
			routine_t(warp_t* w, func_t&& func, size_t prior) noexcept : routine(std::forward<func_t>(func)), priority(prior), warp(w), next(nullptr) {
				lock_count.store(1, std::memory_order_relaxed);
				memset(next_tasks, 0, sizeof(next_tasks));
			}

			friend struct iris_dispatcher_t<warp_t>;
			function_t routine;
			std::atomic<size_t> lock_count;
			size_t priority;
			warp_t* warp;
			routine_t* next;
			routine_t* next_tasks[4];
		};

		// on execution of tasks
		struct execute_t {
			execute_t(iris_dispatcher_t& d, routine_t* r) noexcept : dispatcher(d), routine(r) {}

			void operator () () {
				dispatcher.execute(routine);
			}

			iris_dispatcher_t& dispatcher;
			routine_t* routine;
		};
		using async_worker_t = typename warp_t::async_worker_t;
		using allocator_t = typename async_worker_t::template general_allocator_t<routine_t>;

		// all_complete will be called each time all tasks complete
		template <typename func_t>
		iris_dispatcher_t(async_worker_t& worker, func_t&& all_complete) noexcept
			: async_worker(worker), completion(std::forward<func_t>(all_complete)) {
			pending_count.store(0, std::memory_order_relaxed);
			resurrect_routines.store(nullptr, std::memory_order_release);
		}

		iris_dispatcher_t(async_worker_t& worker) noexcept : async_worker(worker) {
			pending_count.store(0, std::memory_order_relaxed);
			resurrect_routines.store(nullptr, std::memory_order_release);
		}

		async_worker_t& get_async_worker() noexcept {
			return async_worker;
		}

		// queue a routine, notice that priority takes effect if and only if warp == nullptr
		template <typename func_t>
		routine_t* allocate(warp_t* warp, func_t&& func, size_t priority = 0) {
			routine_t* routine = routine_allocator.allocate(1);
			new (routine) routine_t(warp, std::forward<func_t>(func), priority);
			pending_count.fetch_add(1, std::memory_order_acquire);
			return routine;
		}

		routine_t* allocate(warp_t* warp) {
			return allocate(warp, function_t());
		}

		// set routine dependency [from] -> [to]
		void order(routine_t* from, routine_t* to) {
			IRIS_ASSERT(from != nullptr);
			IRIS_ASSERT(to != nullptr);

#if IRIS_DEBUG
			validate(from, to);
#endif

			for (size_t i = 0; i < sizeof(from->next_tasks) / sizeof(from->next_tasks[0]); i++) {
				routine_t*& p = from->next_tasks[i];
				if (p == nullptr) {
					to->lock_count.fetch_add(1, std::memory_order_relaxed);
					p = to;
					return;
				}
			}

			if (!from->next_tasks[0]->routine) { // is junction node?
				order(from->next_tasks[0], to);
			} else {
				to->lock_count.fetch_add(1, std::memory_order_relaxed);
				routine_t* p = allocate(from->warp);
				p->next_tasks[0] = from->next_tasks[0];
				p->next_tasks[1] = to;
				from->next_tasks[0] = p;
			}
		}

		// delay a task temporarily, must called before it actually runs
		routine_t* defer(routine_t* routine) noexcept {
			IRIS_ASSERT(pending_count.load(std::memory_order_acquire) != 0);
			IRIS_ASSERT(routine->lock_count.load(std::memory_order_relaxed) != 0);
			routine->lock_count.fetch_add(1, std::memory_order_relaxed);
			return routine;
		}

		// dispatch a task
		void dispatch(routine_t* routine) {
			impl::atomic_guard_t<impl::guard_operation::add> guard(routine->lock_count);
			if (routine->lock_count.fetch_sub(1, std::memory_order_relaxed) == 1) {
				if (routine->routine) {
					// if not a warped routine, queue it to worker directly.
					if (routine->warp == nullptr) {
						async_worker.queue(execute_t(*this, routine), routine->priority);
					} else {
						routine->warp->queue_routine(execute_t(*this, routine));
					}
				} else {
					execute(routine);
				}
			}

			guard.cleanup();
		}

		bool has_exception() const noexcept {
			return resurrect_routines.load(std::memory_order_acquire) != nullptr;
		}

		bool cleanup() noexcept {
			IRIS_PROFILE_SCOPE(__FUNCTION__);

			routine_t* p = resurrect_routines.exchange(nullptr, std::memory_order_acquire);
			if (p != nullptr) {
				while (p != nullptr) {
					routine_t* q = p->next;
					p->~routine_t();
					routine_allocator.deallocate(p, 1);
					complete(false);
					p = q;
				}

				return true;
			} else {
				return false;
			}
		}

		bool resurrect() {
			IRIS_PROFILE_SCOPE(__FUNCTION__);

			routine_t* p = resurrect_routines.exchange(nullptr, std::memory_order_acquire);
			if (p != nullptr) {
				while (p != nullptr) {
					routine_t* q = p->next;
					dispatch(p);
					p = q;
				}

				return true;
			} else {
				return false;
			}
		}

		size_t get_pending_count() const {
			return pending_count.load(std::memory_order_acquire);
		}

	protected:
		// after finshing a routine, unlock the next_routines
		struct routine_guard_t {
			routine_guard_t(iris_dispatcher_t& d, routine_t* r, std::atomic<routine_t*>* resurrect) noexcept : dispatcher(d), routine(r), resurrect_routines(resurrect) {}
			~routine_guard_t() noexcept {
				if (resurrect_routines != nullptr) {
					// some exception throws, relink to surrect linked-list
					routine->lock_count.fetch_add(1, std::memory_order_relaxed);
					// avoid legacy compiler bugs
					// see https://en.cppreference.com/w/cpp/atomic/atomic/compare_exchange
					routine_t* node = resurrect_routines->load(std::memory_order_relaxed);
					do {
						routine->next = node;
					} while (!resurrect_routines->compare_exchange_weak(node, routine, std::memory_order_release, std::memory_order_relaxed));
				} else {
					routine->~routine_t();
					dispatcher.routine_allocator.deallocate(routine, 1);
				}
			}

			void cleanup() noexcept {
				resurrect_routines = nullptr;
			}

			iris_dispatcher_t& dispatcher;
			std::atomic<routine_t*>* resurrect_routines;
			routine_t* routine;
		};

		void complete(bool success) {
			IRIS_PROFILE_SCOPE(__FUNCTION__);

			// all pending routines finished?
			if (pending_count.fetch_sub(1, std::memory_order_release) == 1) {
				// if completion throws exception, we still do not care about pending_count anyway
				if (completion) {
					completion(*this, success);
				}
			}
		}

		void execute(routine_t* routine) {
			IRIS_PROFILE_SCOPE(__FUNCTION__);

			IRIS_ASSERT(routine->lock_count.load(std::memory_order_relaxed) == 0);
			do {
				routine_guard_t guard(*this, routine, &resurrect_routines);
				if (routine->routine) {
					routine->routine();
					routine->routine = {};
				}

				for (size_t i = 0; i < sizeof(routine->next_tasks) / sizeof(routine->next_tasks[0]); i++) {
					routine_t*& next = routine->next_tasks[i];
					if (next != nullptr) {
						IRIS_ASSERT(next != routine);
						dispatch(next);
						next = nullptr;
					}
				}

				guard.cleanup();
			} while (false);

			complete(true);
		}

		void validate(routine_t* from, routine_t* to) {
			IRIS_ASSERT(from != to);

			for (size_t i = 0; i < sizeof(to->next_tasks) / sizeof(to->next_tasks[0]); i++) {
				routine_t* p = to->next_tasks[i];
				if (p != nullptr) {
					validate(from, p);
				}
			}
		}

	protected:
		async_worker_t& async_worker;
		allocator_t routine_allocator;
		std::atomic<size_t> pending_count;
		std::atomic<routine_t*> resurrect_routines;
		std::function<void(iris_dispatcher_t&, bool)> completion;
	};

	// here we code a trivial worker demo
	// could be replaced by your implementation
	template <typename thread_t = std::thread, typename callback_t = std::function<void()>, template <typename...> class allocator_t = iris_default_object_allocator_t>
	struct iris_async_worker_t {
		// task wrapper
		struct alignas(64) task_t {
			template <typename func_t>
			task_t(func_t&& func, task_t* n) noexcept(noexcept(callback_t(std::forward<func_t>(func))))
				: task(std::forward<func_t>(func)), next(n) {}

			task_t(task_t&& rhs) noexcept : task(std::move(rhs.task)), next(rhs.next) {
				rhs.next = nullptr;
			}

			task_t& operator = (task_t&& rhs) noexcept {
				task = std::move(rhs.task);
				next = rhs.next;
				rhs.next = nullptr;
				return *this;
			}

			callback_t task;
			task_t* next;
		};

		static constexpr size_t task_head_duplicate_count = 4;
		template <typename element_t>
		using general_allocator_t = allocator_t<element_t>;
		using task_allocator_t = allocator_t<task_t>;

		iris_async_worker_t() : waiting_thread_count(0), limit_count(0), internal_thread_count(0) {
			running_count.store(0, std::memory_order_relaxed);
			terminated.store(1, std::memory_order_release);
		}

		explicit iris_async_worker_t(size_t thread_count) : iris_async_worker_t() {
			resize(thread_count);
		}

		explicit iris_async_worker_t(const task_allocator_t& alloc) : task_allocator(alloc), waiting_thread_count(0), limit_count(0), internal_thread_count(0) {
			running_count.store(0, std::memory_order_relaxed);
			terminated.store(1, std::memory_order_release);
		}

		iris_async_worker_t(size_t thread_count, const task_allocator_t& alloc) : iris_async_worker_t(alloc) {
			resize(thread_count);
		}

		void resize(size_t thread_count) {
			IRIS_ASSERT(task_heads.empty()); // must not started

			threads.resize(thread_count);
			internal_thread_count = thread_count;
		}

		// initialize and start thread poll
		void start() {
			IRIS_ASSERT(task_heads.empty()); // must not started

			std::vector<std::atomic<task_t*>> heads(threads.size() * task_head_duplicate_count);
			for (size_t i = 0; i < heads.size(); i++) {
				heads[i].store(nullptr, std::memory_order_relaxed);
			}

			task_heads = std::move(heads);
			terminated.store(0, std::memory_order_release);

			for (size_t i = 0; i < internal_thread_count; i++) {
				threads[i] = thread_t([this, i]() {
					IRIS_PROFILE_THREAD("iris_async_worker", i);
					thread_loop(i);
				});
			}
		}

		void make_current(size_t i) noexcept {
			get_current_thread_index_internal() = i;
		}

		void thread_loop(size_t i) {
			make_current(i);

			while (!is_terminated()) {
				if (!poll()) {
					delay();
				}
			}
		}

		// guard for exception on wait_for
		struct waiting_guard_t {
			waiting_guard_t(iris_async_worker_t* w) noexcept : worker(w) {
				++worker->waiting_thread_count;
			}

			~waiting_guard_t() noexcept {
				--worker->waiting_thread_count;
			}

		private:
			iris_async_worker_t* worker;
		};

		friend struct waiting_guard_t;

		// append new customized thread to worker
		// must be called before start()
		// notice that we allow a dummy thread placeholder here (if you just want to do polling outside the internal threads)
		template <typename... args_t>
		size_t append(args_t&&... args) {
			IRIS_ASSERT(is_terminated());
			size_t id = threads.size();
			threads.emplace_back(std::forward<args_t>(args)...);
			return id;
		}

		// get thread instance of given id
		thread_t& get(size_t i) noexcept {
			return threads[i];
		}

		// guard for exceptions on polling
		struct poll_guard_t {
			poll_guard_t(task_allocator_t& alloc, task_t* t) noexcept : allocator(alloc), task(t) {}
			~poll_guard_t() noexcept {
				// do cleanup work
				task->~task_t();
				allocator.deallocate(task, 1);
			}

			task_allocator_t& allocator;
			task_t* task;
		};

		// poll any task from thread poll manually
		bool poll() {
			size_t inv_priority = running_count.fetch_add(1, std::memory_order_acquire);
			running_guard_t guard(running_count);
			return poll_internal(threads.size() + 1 - std::min(inv_priority + 1, threads.size()));
		}

		// poll any task from thread poll manually with given priority
		bool poll(size_t priority) {
			running_count.fetch_add(1, std::memory_order_acquire);
			running_guard_t guard(running_count);
			return poll_internal(std::min(priority + 1, threads.size()));
		}

		// poll any task from thread poll manually with given priority in specified duration
		// usually used in your customized thread procedures
		bool poll_delay(size_t priority, size_t millseconds) {
			if (!poll(priority)) {
				std::unique_lock<std::mutex> lock(mutex);
				condition.wait_for(lock, std::chrono::milliseconds(millseconds));
				lock.unlock();

				if (!poll(priority)) {
					// priority restriction not satisfied, wake up anther thread to solve it
					wakeup_one_with_priority(0);

					return false;
				}
			}

			return true;
		}

		// guard for exception on running
		struct running_guard_t {
			std::atomic<size_t>& count;
			running_guard_t(std::atomic<size_t>& var) noexcept : count(var) {}
			~running_guard_t() noexcept { count.fetch_sub(1, std::memory_order_release); }
		};

		~iris_async_worker_t() noexcept {
			terminate();
			join();
		}

		// get current thread index
		static size_t get_current_thread_index() noexcept { return get_current_thread_index_internal(); }

		// get the count of threads in worker, including customized threads
		size_t get_thread_count() const noexcept {
			return threads.size();
		}

		// get the count of waiting task
		size_t get_task_count() const noexcept {
			return task_count.load(std::memory_order_acquire);
		}

		// limit the count of running thread. e.g. 0 is not limited, 1 is to pause one thread from running, etc.
		void limit(size_t count) noexcept {
			limit_count = count;
		}

		// queue a task to worker with given priority [0, thread_count - 1], which 0 is the highest priority
		template <typename callable_t>
		void queue(callable_t&& func, size_t priority = 0) {
			if (!is_terminated()) {
				IRIS_ASSERT(!threads.empty());
				priority = std::min(priority, std::max(internal_thread_count, (size_t)1) - 1u);
				task_t* task = task_allocator.allocate(1);
				new (task) task_t(std::forward<callable_t>(func), nullptr);
				task_count.fetch_add(1, std::memory_order_relaxed);

				// try empty slots first
				size_t index = 0;
				ptrdiff_t max_diff = std::numeric_limits<ptrdiff_t>::min();
				size_t thread_count = threads.size();
				size_t current_thread_index = get_current_thread_index();
				current_thread_index = current_thread_index == ~(size_t)0 ? 0 : current_thread_index;

				for (size_t n = 0; n < task_head_duplicate_count; n++) {
					size_t k = (n + current_thread_index) % task_head_duplicate_count;
					std::atomic<task_t*>& task_head = task_heads[priority + k * thread_count];
					task_t* expected = nullptr;
					if (task_head.compare_exchange_strong(expected, task, std::memory_order_release)) {
						// dispatch immediately
						wakeup_one_with_priority(priority);
						return;
					} else {
						ptrdiff_t diff = task - expected;
						if (diff >= max_diff) {
							max_diff = diff;
							index = k;
						}
					}
				}

				// full, chain to farest one
				std::atomic<task_t*>& task_head = task_heads[priority + index * thread_count];

				// avoid legacy compiler bugs
				// see https://en.cppreference.com/w/cpp/atomic/atomic/compare_exchange
				task_t* node = task_head.load(std::memory_order_relaxed);
				do {
					task->next = node;
				} while (!task_head.compare_exchange_weak(node, task, std::memory_order_acq_rel, std::memory_order_relaxed));

				// dispatch immediately
				wakeup_one_with_priority(priority);
			} else {
				// terminate requested, chain to default task_head at 0
				if (!task_heads.empty()) {
					task_t* task = task_allocator.allocate(1);
					new (task) task_t(std::forward<callable_t>(func), nullptr);
					task_count.fetch_add(1, std::memory_order_relaxed);

					std::atomic<task_t*>& task_head = task_heads[0];
					task_t* node = task_head.load(std::memory_order_relaxed);
					do {
						task->next = node;
					} while (!task_head.compare_exchange_weak(node, task, std::memory_order_acq_rel, std::memory_order_relaxed));
				} else {
					// terminate finished, just run at here
					IRIS_ASSERT(get_current_thread_index_internal() == ~size_t(0));
					func();
				}
			}
		}

		// mark as terminated
		void terminate() {
			terminated.store(1, std::memory_order_release);
			wakeup_all();
		}

		// is about to terminated
		bool is_terminated() const noexcept {
			return terminated.load(std::memory_order_acquire) != 0;
		}

		// wait for all threads in worker to be finished.
		void join() {
			IRIS_PROFILE_SCOPE(__FUNCTION__);

			if (!task_heads.empty()) {
				for (size_t i = 0; i < threads.size(); i++) {
					if (threads[i].joinable()) {
						threads[i].join();
					}
				}

				IRIS_ASSERT(running_count.load(std::memory_order_acquire) == 0);
				IRIS_ASSERT(waiting_thread_count == 0);
				while (!cleanup()) {}

				threads.clear();
				task_heads.clear();
				threads.resize(internal_thread_count);
			}
		}

		// notify threads in thread pool, usually used for customized threads
		void wakeup_one() {
			std::lock_guard<std::mutex> lock(mutex);
			condition.notify_one();
		}

		void wakeup_all() {
			std::lock_guard<std::mutex> lock(mutex);
			condition.notify_all();
		}

		// blocked delay for any task
		void delay() {
			if (!is_terminated()) {
				std::unique_lock<std::mutex> lock(mutex);
				waiting_guard_t guard(this);

				if (fetch(threads.size()).first == ~size_t(0)) {
					if (!is_terminated()) {
						condition.wait(lock);
					}
				}
			}
		}

		struct thread_index_t {
			thread_index_t() noexcept : value(~size_t(0)) {}
			size_t value;
		};

	protected:
		static size_t& get_current_thread_index_internal() noexcept {
			return iris_static_instance_t<thread_index_t>::get_thread_local().value;
		}

		void wakeup_one_with_priority(size_t priority) {
			if (waiting_thread_count > priority + limit_count) {
				wakeup_one();
			}
		}

		// cleanup all pending tasks
		bool cleanup() {
			IRIS_PROFILE_SCOPE(__FUNCTION__);

			bool empty = true;
			for (size_t i = 0; i < task_heads.size(); i++) {
				std::atomic<task_t*>& task_head = task_heads[i];
				task_t* task = task_head.exchange(nullptr, std::memory_order_acquire);
				empty = empty && (task == nullptr);
				while (task != nullptr) {
					task_t* p = task;
					task = task->next;
					task_head.store(task, std::memory_order_relaxed); // in case of exception

					poll_guard_t guard(task_allocator, p);
					p->task();
				}
			}

			return empty;
		}

		// try fetching a task with given priority
		std::pair<size_t, size_t> fetch(size_t priority_size) const noexcept {
			size_t thread_count = threads.size();
			size_t current_thread_index = get_current_thread_index();
			current_thread_index = current_thread_index == ~(size_t)0 ? 0 : current_thread_index;

			for (size_t k = 0; k < task_head_duplicate_count; k++) {
				for (size_t n = 0; n < priority_size; n++) {
					size_t i = ((k + current_thread_index) % task_head_duplicate_count) * thread_count + n;
					if (task_heads[i].load(std::memory_order_acquire) != nullptr) {
						return std::make_pair(i, n);
					}
				}
			}

			return std::make_pair(~size_t(0), ~size_t(0));
		}

		// poll with given priority
		bool poll_internal(size_t priority_size) {
			IRIS_PROFILE_SCOPE(__FUNCTION__);

			std::pair<size_t, size_t> slot = fetch(priority_size);
			size_t index = slot.first;

			if (index != ~size_t(0)) {
				size_t priority = slot.second;
				std::atomic<task_t*>& task_head = task_heads[index];
				if (task_head.load(std::memory_order_acquire) != nullptr) {
					// fetch a task atomically
					task_t* task = task_head.exchange(nullptr, std::memory_order_acquire);
					if (task != nullptr) {
						task_t* org = task_head.exchange(task->next, std::memory_order_release);

						// return the remaining
						if (org != nullptr) {
							do {
								task_t* next = org->next;

								// avoid legacy compiler bugs
								// see https://en.cppreference.com/w/cpp/atomic/atomic/compare_exchange
								task_t* node = task_head.load(std::memory_order_relaxed);
								do {
									org->next = node;
								} while (!task_head.compare_exchange_weak(node, org, std::memory_order_relaxed, std::memory_order_relaxed));

								org = next;
							} while (org != nullptr);

							std::atomic_thread_fence(std::memory_order_acq_rel);
							wakeup_one_with_priority(priority);
						}

						task_count.fetch_sub(1, std::memory_order_release);
						// in case task->task() throws exceptions
						poll_guard_t guard(task_allocator, task);
						task->task();
					}
				}

				return true;
			} else {
				return false;
			}
		}

	protected:
		task_allocator_t task_allocator; // default task allocator
		std::vector<thread_t> threads; // worker
		std::atomic<size_t> running_count; // running_count
		std::vector<std::atomic<task_t*>> task_heads; // task pointer list
		std::mutex mutex; // mutex to protect condition
		std::condition_variable condition; // condition variable for idle wait
		std::atomic<size_t> terminated; // is to terminate
		size_t waiting_thread_count; // thread count of waiting on condition variable
		size_t limit_count; // limit the count of concurrently running thread
		size_t internal_thread_count; // the count of internal thread
		std::atomic<size_t> task_count; // the count of total waiting tasks 
	};

	template <typename async_worker_t>
	struct iris_async_balancer_t {
		iris_async_balancer_t(async_worker_t& worker, size_t size = 4u) : async_worker(worker), current_limit(0), window_size(static_cast<ptrdiff_t>(size)) {
			async_worker.limit(current_limit);
			balance.store(0, std::memory_order_release);
		}

		void down() noexcept {
			if (current_limit + 1 < async_worker.get_thread_count() && async_worker.get_task_count() == 0) {
				ptrdiff_t size = balance.load(std::memory_order_acquire);
				if (size + window_size < 0) {
					async_worker.limit(++current_limit);
					balance.fetch_add(window_size, std::memory_order_relaxed);
				} else {
					balance.fetch_sub(1, std::memory_order_relaxed);
				}
			}
		}

		void up() noexcept {
			if (current_limit != 0 && async_worker.get_task_count() > 0) {
				ptrdiff_t size = balance.load(std::memory_order_acquire);
				if (size > window_size) {
					async_worker.limit(--current_limit);
					balance.fetch_sub(window_size, std::memory_order_relaxed);
				} else {
					balance.fetch_add(1, std::memory_order_relaxed);
				}
			}
		}

	private:
		async_worker_t& async_worker;
		size_t current_limit;
		ptrdiff_t window_size;
		std::atomic<ptrdiff_t> balance;
	};
}

