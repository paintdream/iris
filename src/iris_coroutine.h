/*
The Iris Concurrency Framework

This software is a C++ 20 Header-Only reimplementation of core part from project PaintsNow.

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

#include "iris_dispatcher.h"

// C++20 coroutine support
#include <coroutine>

namespace iris {
	// standard coroutine interface settings
	namespace impl {
		template <typename return_t>
		struct promise_type_base {
			constexpr std::suspend_always initial_suspend() noexcept { return std::suspend_always(); }
			constexpr std::suspend_never final_suspend() noexcept { return std::suspend_never(); }

			template <typename value_t>
			void return_value(value_t&& value) noexcept {
				if (completion) {
					completion(std::forward<value_t>(value));
				}
			}

			void unhandled_exception() noexcept { return std::terminate(); }

			std::function<void(return_t&&)> completion;
		};

		template <>
		struct promise_type_base<void> {
			constexpr std::suspend_always initial_suspend() noexcept { return std::suspend_always(); }
			constexpr std::suspend_never final_suspend() noexcept { return std::suspend_never(); }

			void return_void() noexcept {
				if (completion) {
					completion();
				}
			}

			void unhandled_exception() noexcept { return std::terminate(); }
			std::function<void()> completion;
		};

		struct promise_abstract {};
	}

	template <typename return_t = void>
	struct iris_coroutine_t {
		using return_type_t = return_t;

		struct promise_type : impl::promise_type_base<return_t> {
			iris_coroutine_t get_return_object() noexcept {
				return iris_coroutine_t(std::coroutine_handle<promise_type>::from_promise(*this));
			}
		};

		explicit iris_coroutine_t(std::coroutine_handle<promise_type>&& h) : handle(std::move(h)) {}
		iris_coroutine_t(const iris_coroutine_t& rhs) = delete;
		iris_coroutine_t(iris_coroutine_t&& rhs) noexcept : handle(std::move(rhs.handle)) {
			rhs.handle = std::coroutine_handle<promise_type>();
		}

		iris_coroutine_t& operator = (const iris_coroutine_t& rhs) = delete;
		iris_coroutine_t& operator = (iris_coroutine_t&& rhs) noexcept {
			std::swap(handle, rhs.handle);
			rhs.handle = std::coroutine_handle<promise_type>();
			return *this;
		}

		~iris_coroutine_t() noexcept {
			assert(!handle); // must call run() or join() before destruction
		}

		template <typename func_t>
		iris_coroutine_t& complete(func_t&& func) noexcept {
			assert(handle);
			handle.promise().completion = std::forward<func_t>(func);

			return *this;
		}

		void run() noexcept(noexcept(std::declval<std::coroutine_handle<promise_type>>().resume())) {
			assert(handle);
			std::coroutine_handle<promise_type> execute_handle(std::move(handle));
			handle = std::coroutine_handle<promise_type>();
			execute_handle.resume();
		}

		return_t join() {
			std::atomic<size_t> variable = 0;
			auto prev = std::move(handle.promise().completion);

			if constexpr (!std::is_void_v<return_t>) {
				return_t ret;
				if (prev) {
					complete([&variable, &ret, prev = std::move(prev)](return_t&& value) {
						ret = std::move(value);
						prev(std::move(ret));
						variable.store(1, std::memory_order_relaxed);
						variable.notify_one();
					});
				} else {
					complete([&variable, &ret](return_t&& value) {
						ret = std::move(value);
						variable.store(1, std::memory_order_relaxed);
						variable.notify_one();
					});
				}
				
				run();
				variable.wait(0, std::memory_order_acquire);
				return ret;
			} else {
				if (prev) {
					complete([&variable, prev = std::move(prev)]() {
						prev();
						variable.store(1, std::memory_order_relaxed);
						variable.notify_one();
					});
				} else {
					complete([&variable]() {
						variable.store(1, std::memory_order_relaxed);
						variable.notify_one();
					});
				}

				run();
				variable.wait(0, std::memory_order_acquire);
			}
		}

		// iris_coroutine_t is also awaitable
		constexpr bool await_ready() const noexcept {
			return false;
		}

		void await_suspend(std::coroutine_handle<> parent_handle) {
			if constexpr (!std::is_void_v<return_t>) {
				complete([this, parent_handle = std::move(parent_handle)](return_t&& value) mutable noexcept(noexcept(std::declval<std::coroutine_handle<>>().resume())) {
					await_result = &value;
					parent_handle.resume();
				});
			} else {
				complete([this, parent_handle = std::move(parent_handle)]() mutable noexcept(noexcept(std::declval<std::coroutine_handle<>>().resume())) {
					parent_handle.resume();
				});
			}

			run();
		}

		return_t await_resume() noexcept {
			if constexpr (!std::is_void_v<return_t>) {
				assert(await_result != nullptr);
				return_t* p = await_result;
				return std::move(*p);
			}
		}

		std::coroutine_handle<promise_type>& get_handle() noexcept {
			return handle;
		}

		const std::coroutine_handle<promise_type>& get_handle() const noexcept {
			return handle;
		}

	protected:
		std::coroutine_handle<promise_type> handle;
		return_t* await_result = nullptr;
	};

	template <typename return_t = impl::promise_abstract>
	using iris_coroutine_handle_t = std::coroutine_handle<std::conditional_t<std::is_same_v<return_t, impl::promise_abstract>, void, typename iris_coroutine_t<return_t>::promise_type>>;

	// awaitable object, can be used by:
	// co_await iris_awaitable_t(...);
	template <typename warp_type_t, typename func_type_t>
	struct iris_awaitable_t {
		using warp_t = warp_type_t;
		using func_t = func_type_t;
		using return_t = std::invoke_result_t<func_t>;

		// constructed from a given target warp and routine function
		// notice that we do not initialize `caller` here, let `await_suspend` do
		// parallel_priority: ~(size_t)0 means no parallization, other value indicates the dispatch priority of parallel routines
		template <typename callable_t>
		iris_awaitable_t(warp_t* target_warp, callable_t&& f, size_t p) noexcept : target(target_warp), parallel_priority(p), func(std::forward<callable_t>(f)) {
			assert(target_warp != nullptr || parallel_priority == ~size_t(0));
		}

		// always suspended
		constexpr bool await_ready() const noexcept {
			return false;
		}

		void resume_one(iris_coroutine_handle_t<> handle) {
			// return to caller's warp
			if (caller != nullptr) {
				// notice that the condition `caller != target` holds
				// so we can use `post` to skip self-queueing check
				caller->queue_routine_post([this, handle = std::move(handle)]() mutable noexcept(noexcept(handle.resume())) {
					handle.resume();
				});
			} else {
				// otherwise dispatch to thread pool
				// notice that we mustn't call handle.resume() directly
				// since it may blocks execution of current warp
				target->get_async_worker().queue([handle = std::move(handle)]() mutable noexcept(noexcept(handle.resume())) {
					handle.resume();
				});
			}
		}

		void await_suspend(iris_coroutine_handle_t<> handle) {
			caller = warp_t::get_current_warp();

			// the same warp, execute at once!
			// even they are both null
			if (target == caller) {
				if constexpr (std::is_void_v<return_t>) {
					func();
				} else {
					ret = func(); // auto moved here
				}

				handle.resume(); // resume coroutine directly.
			} else if (target == nullptr) {
				// targeting to thread pool with no warp context
				caller->get_async_worker().queue([this, handle = std::move(handle)]() mutable {
					if constexpr (std::is_void_v<return_t>) {
						func();
					} else {
						ret = func();
					}

					// return to caller's warp
					// notice that we are running in one thread of our thread pool, so just use queue_routine
					caller->queue_routine_post([this, handle = std::move(handle)]() mutable noexcept(noexcept(handle.resume())) {
						handle.resume();
					});
				});
			} else {
				if (parallel_priority == ~size_t(0)) {
					// targeting to a valid warp
					// prepare callback first
					auto callback = [this, handle = std::move(handle)]() mutable noexcept(noexcept(func()) && noexcept(std::declval<iris_awaitable_t>().resume_one(handle))) {
						if constexpr (std::is_void_v<return_t>) {
							func();
						} else {
							ret = func();
						}

						resume_one(handle);
					};

					// if we are in an external thread, post to thread pool first
					// otherwise post it directly
					if (target->get_async_worker().get_current_thread_index() != ~size_t(0)) {
						target->queue_routine_post(std::move(callback));
					} else {
						target->queue_routine_external(std::move(callback));
					}
				} else {
					// suspend the target warp
					target->suspend();

					// let async_worker running them, so multiple routines around the same target could be executed in parallel
					typename warp_t::suspend_guard_t guard(target);
					target->get_async_worker().queue([this, handle = std::move(handle)]() mutable noexcept(noexcept(func()) && noexcept(std::declval<iris_awaitable_t>().resume_one(handle)) && noexcept(target->resume())) {
						typename warp_t::suspend_guard_t guard(target);
						if constexpr (std::is_void_v<return_t>) {
							func();
						} else {
							ret = func();
						}

						guard.cleanup();
						target->resume();

						resume_one(handle);
					}, parallel_priority);

					guard.cleanup();
				}
			}
		}

		return_t await_resume() noexcept {
			if constexpr (!std::is_void_v<return_t>) {
				return std::move(ret);
			}
		}

		struct void_t {};
		warp_t* caller;
		warp_t* target;
		size_t parallel_priority;
		func_t func;
		std::conditional_t<std::is_void_v<return_t>, void_t, return_t> ret;
	};

	// simple wrapper for constructing an awaitable object
	template <typename warp_t, typename iris_func_t>
	auto iris_awaitable(warp_t* target_warp, iris_func_t&& func) noexcept {
		return iris_awaitable_t<warp_t, std::decay_t<iris_func_t>>(target_warp, std::forward<iris_func_t>(func), ~size_t(0));
	}

	// simple wrapper for constructing an awaitable object in parallel
	template <typename warp_t, typename iris_func_t>
	auto iris_awaitable_parallel(warp_t* target_warp, iris_func_t&& func, size_t priority = 0) noexcept {
		assert(priority != ~size_t(0));
		return iris_awaitable_t<warp_t, std::decay_t<iris_func_t>>(target_warp, std::forward<iris_func_t>(func), priority);
	}

	// an awaitable proxy for combining multiple awaitable objects
	template <typename warp_t, typename iris_func_t>
	struct iris_awaitable_multiple_t {
		struct void_t {};
		using return_t = std::invoke_result_t<iris_func_t>;
		using return_multiple_t = std::conditional_t<std::is_void_v<return_t>, void_t, std::vector<return_t>>;
		using return_multiple_declare_t = std::conditional_t<std::is_void_v<return_t>, void, std::vector<return_t>>;
		using async_worker_t = typename warp_t::async_worker_t;

		struct awaitable_t {
			awaitable_t(warp_t* t, iris_func_t&& f, size_t p) : target(t), parallel_priority(p), func(std::move(f)) {}

			warp_t* target;
			size_t parallel_priority;
			iris_func_t func;
		};

		explicit iris_awaitable_multiple_t(async_worker_t& worker) noexcept : caller(warp_t::get_current_warp()), async_worker(worker) {}

		constexpr void initialize_args() noexcept {}

		template <typename element_t, typename... args_t>
		void initialize_args(element_t&& first, args_t&&... args) {
			*this += std::forward<element_t>(first);
			initialize_args(std::forward<args_t>(args)...);
		}

		// can be initialized with series of awaitables
		// `pending_count` is not necessarily initialized here
		template <typename... args_t>
		iris_awaitable_multiple_t(async_worker_t& worker, args_t&&... args) : caller(warp_t::get_current_warp()), async_worker(worker) {
			awaitables.reserve(sizeof...(args));
			initialize_args(std::forward<args_t>(args)...);
		}

		// just make visual studio linter happy
		// atomic variables are not movable.
		iris_awaitable_multiple_t(iris_awaitable_multiple_t&& rhs) noexcept : caller(rhs.caller), async_worker(rhs.async_worker), awaitables(std::move(rhs.awaitables)), returns(rhs.returns) {}

		iris_awaitable_multiple_t& operator = (iris_awaitable_multiple_t&& rhs) noexcept {
			iris_awaitable_multiple_t t(std::move(rhs));
			std::swap(*this, t);
			return *this;
		}

		iris_awaitable_multiple_t& operator += (iris_awaitable_t<warp_t, iris_func_t>&& arg) {
			awaitables.emplace_back(awaitable_t(arg.target, std::move(arg.func), arg.parallel_priority));
			return *this;
		}

		constexpr bool await_ready() const noexcept {
			return false;
		}

		void resume_one(iris_coroutine_handle_t<> handle) {
			// if all sub-awaitables finished, then resume coroutine
			if (pending_count.fetch_sub(1, std::memory_order_acquire) == 1) {
				warp_t* warp = warp_t::get_current_warp();
				if (warp == caller) {
					// last finished one is the one who invoke coroutine
					// resume at once!
					handle.resume();
				} else {
					if (caller != nullptr) {
						// caller is a valid warp, post to it
						caller->queue_routine_post([this, handle = std::move(handle)]() mutable noexcept(noexcept(handle.resume())) {
							handle.resume();
						});
					} else {
						// caller is not a valid warp, post to thread pool
						async_worker.queue([this, handle = std::move(handle)]() mutable noexcept(noexcept(handle.resume())) {
							handle.resume();
						});
					}
				}
			}
		}

		void await_suspend(iris_coroutine_handle_t<> handle) {
			if constexpr (!std::is_void_v<return_t>) {
				returns.resize(awaitables.size());
			}

			assert(!current_handle); // can only be called once!
			current_handle = handle;
			// prepare pending counter here!
			pending_count.store(awaitables.size(), std::memory_order_release);

			for (size_t i = 0; i < awaitables.size(); i++) {
				awaitable_t& awaitable = awaitables[i];
				warp_t* target = awaitable.target;
				if (target == nullptr) {
					// target is thread pool
					async_worker.queue([this, i]() mutable {
						if constexpr (std::is_void_v<return_t>) {
							awaitables[i].func();
						} else {
							returns[i] = awaitables[i].func();
						}

						resume_one(current_handle);
					});
				} else {
					if (awaitable.parallel_priority == ~size_t(0)) {
						// target is a valid warp
						// prepare callback
						auto callback = [this, i]() mutable {
							if constexpr (std::is_void_v<return_t>) {
								awaitables[i].func();
							} else {
								returns[i] = awaitables[i].func();
							}

							resume_one(current_handle);
						};

						// if we are in an external thread, post to thread pool first
						// otherwise post it directly
						if (async_worker.get_current_thread_index() != ~size_t(0)) {
							target->queue_routine_post(std::move(callback));
						} else {
							target->queue_routine_external(std::move(callback));
						}
					} else {
						// suspend the target warp
						target->suspend();

						// let async_worker running them, so multiple routines around the same target could be executed in parallel
						typename warp_t::suspend_guard_t guard(target);
						async_worker.queue([this, i]() mutable {
							warp_t* target = awaitables[i].target;
							typename warp_t::suspend_guard_t guard(target);
							if constexpr (std::is_void_v<return_t>) {
								awaitables[i].func();
							} else {
								returns[i] = awaitables[i].func();
							}

							guard.cleanup();
							target->resume();
							resume_one(current_handle); // cleanup must happened before resume_one()!
						}, awaitable.parallel_priority);

						guard.cleanup();
					}
				}
			}
		}

		// return all values by moving semantic
		return_multiple_declare_t await_resume() noexcept {
			if constexpr (!std::is_void_v<return_t>) {
				return std::move(returns);
			}
		}

	protected:
		warp_t* caller;
		async_worker_t& async_worker;
		std::atomic<size_t> pending_count;
		iris_coroutine_handle_t<> current_handle;
		std::vector<awaitable_t> awaitables;
		return_multiple_t returns;
	};

	// wrapper for joining multiple awaitables together
	template <typename async_worker_t, typename awaitable_t, typename... args_t>
	auto iris_awaitable_union(async_worker_t& worker, awaitable_t&& first, args_t&&... args) {
		return iris_awaitable_multiple_t<typename awaitable_t::warp_t, typename awaitable_t::func_t>(worker, std::forward<awaitable_t>(first), std::forward<args_t>(args)...);
	}

	// switch to specified warp, and return the original current warp
	template <typename warp_t>
	struct iris_switch_t {
		using async_worker_t = typename warp_t::async_worker_t;

		explicit iris_switch_t(warp_t* warp) noexcept : source(warp_t::get_current_warp()), target(warp) {}

		bool await_ready() const noexcept {
			return source == target;
		}

		void await_suspend(iris_coroutine_handle_t<> handle) {
			if (target == nullptr) {
				assert(source != nullptr);
				source->get_async_worker().queue([handle = std::move(handle)]() mutable noexcept(noexcept(handle.resume())) {
					handle.resume();
				});
			} else {
				if (target->get_async_worker().get_current_thread_index() != ~size_t(0)) {
					target->queue_routine_post([handle = std::move(handle)]() mutable noexcept(noexcept(handle.resume())) {
						handle.resume();
					});
				} else {
					target->queue_routine_external([handle = std::move(handle)]() mutable noexcept(noexcept(handle.resume())) {
						handle.resume();
					});
				}
			}
		}

		warp_t* await_resume() const noexcept {
			return source;
		}

	protected:
		warp_t* source;
		warp_t* target;
	};

	template <typename warp_t>
	auto iris_switch(warp_t* target) noexcept {
		return iris_switch_t<warp_t>(target);
	}

	// switch to any warp from specified range [from, to] 
	template <typename iterator_t>
	struct iris_select_t {
		using warp_t = std::decay_t<decltype(*std::declval<iterator_t>())>;
		using async_worker_t = typename warp_t::async_worker_t;

		iris_select_t(iterator_t from, iterator_t to) noexcept : begin(from), end(to), selected(nullptr) {
			assert(from != to);
			assert(warp_t::get_current_warp() == nullptr);
		}

		constexpr bool await_ready() const noexcept {
			return false;
		}

		void await_suspend(iris_coroutine_handle_t<> handle) {
			// try fast preempt
			for (iterator_t p = begin; p != end; ++p) {
				warp_t* target = &(*p);
				typename warp_t::template preempt_guard_t<false> guard(*target);
				if (guard) {
					selected = target;
					handle.resume();
					return;
				}
			}

			// all warps are busy, so we need to post tasks to them
			auto shared_handle = std::make_shared<std::pair<std::atomic<iris_coroutine_handle_t<>>, iris_select_t*>>(std::move(handle), this);
			for (iterator_t p = begin; p != end; ++p) {
				warp_t* target = &(*p);

				if (target->get_async_worker().get_current_thread_index() != ~size_t(0)) {
					target->queue_routine_post([shared_handle, target]() mutable noexcept(noexcept(handle.resume())) {
						auto handle = shared_handle->first.exchange(iris_coroutine_handle_t<>(), std::memory_order_release);
						if (handle) {
							shared_handle->second->selected = target;
							handle.resume();
						}
					});
				} else {
					target->queue_routine_external([shared_handle, target]() mutable noexcept(noexcept(handle.resume())) {
						auto handle = shared_handle->first.exchange(iris_coroutine_handle_t<>(), std::memory_order_release);
						if (handle) {
							shared_handle->second->selected = target;
							handle.resume();
						}
					});
				}

				// already dispatched to one warp sucessfully, just give up the remaining
				if (shared_handle->first.load(std::memory_order_acquire) == iris_coroutine_handle_t<>())
					break;
			}
		}

		warp_t* await_resume() const noexcept {
			return selected;
		}

	protected:
		warp_t* selected;
		iterator_t begin;
		iterator_t end;
	};

	template <typename iterator_t>
	auto iris_select(iterator_t begin, iterator_t end) noexcept {
		return iris_select_t<iterator_t>(begin, end);
	}

	// basic asynchornized base class
	template <typename warp_t, typename async_worker_t = typename warp_t::async_worker_t>
	struct iris_sync_t {
		async_worker_t& get_async_worker() noexcept {
			return async_worker;
		}

	protected:
		explicit iris_sync_t(async_worker_t& worker) : async_worker(worker) {}
		iris_sync_t(const iris_sync_t& rhs) = delete;
		iris_sync_t& operator = (const iris_sync_t& rhs) = delete;

		struct info_base_warp_t {
			iris_coroutine_handle_t<> handle;
			warp_t* warp;
		};

		struct info_base_t {
			iris_coroutine_handle_t<> handle;
		};

		using info_t = std::conditional_t<std::is_same_v<warp_t, void>, info_base_t, info_base_warp_t>;

		void dispatch(info_t&& info) {
			if constexpr (std::is_same_v<warp_t, void>) {
				async_worker.queue([handle = std::move(info.handle)]() mutable noexcept(noexcept(info.handle.resume())) {
					handle.resume();
				});
			} else {
				warp_t* target = info.warp;
				if (target == nullptr) {
					async_worker.queue([handle = std::move(info.handle)]() mutable noexcept(noexcept(info.handle.resume())) {
						handle.resume();
					});
				} else if (target->get_async_worker().get_current_thread_index() != ~size_t(0)) {
					target->queue_routine_post([handle = std::move(info.handle)]() mutable noexcept(noexcept(info.handle.resume())) {
						handle.resume();
					});
				} else {
					target->queue_routine_external([handle = std::move(info.handle)]() mutable noexcept(noexcept(info.handle.resume())) {
						handle.resume();
					});
				}
			}
		}

		async_worker_t& async_worker;
	};

	// event-like multiple coroutine sychronization
	template <typename warp_t, typename async_worker_t = typename warp_t::async_worker_t>
	struct iris_event_t : iris_sync_t<warp_t, async_worker_t> {
		iris_event_t(async_worker_t& worker) : iris_sync_t<warp_t, async_worker_t>(worker) {
			signaled.store(0, std::memory_order_release);
		}

		bool await_ready() const noexcept {
			return signaled.load(std::memory_order_acquire) != 0;
		}

		void await_suspend(iris_coroutine_handle_t<> handle) {
			info_t info;
			info.handle = std::move(handle);

			if constexpr (!std::is_same_v<warp_t, void>) {
				info.warp = warp_t::get_current_warp();
			}

			if (signaled.load(std::memory_order_acquire) == 0) {
				std::lock_guard<std::mutex> guard(lock);
				// double check
				if (signaled.load(std::memory_order_acquire) == 0) {
					handles.emplace_back(std::move(info));
					return;
				}
			}

			iris_sync_t<warp_t, async_worker_t>::dispatch(std::move(info));
		}

		constexpr void await_resume() const noexcept {}

		void reset() {
			signaled.store(0, std::memory_order_release);
		}

		void notify() {
			std::vector<info_t> set_handles;
			do {
				std::lock_guard<std::mutex> guard(lock);
				signaled.store(1, std::memory_order_relaxed);

				std::swap(set_handles, handles);
			} while (false);

			for (auto&& info : set_handles) {
				iris_sync_t<warp_t, async_worker_t>::dispatch(std::move(info));
			}
		}

	protected:
		using info_t = typename iris_sync_t<warp_t, async_worker_t>::info_t;
		std::atomic<size_t> signaled;
		std::mutex lock;
		std::vector<info_t> handles;
	};

	// pipe-like multiple coroutine sychronization
	template <typename element_t, typename warp_t, typename async_worker_t = typename warp_t::async_worker_t>
	struct iris_pipe_t : iris_sync_t<warp_t, async_worker_t> {
		iris_pipe_t(async_worker_t& worker) : iris_sync_t<warp_t, async_worker_t>(worker) {
			prepared_count.store(0, std::memory_order_relaxed);
			waiting_count.store(0, std::memory_order_release);
		}

		constexpr bool await_ready() const noexcept {
			return false;
		}

		void await_suspend(iris_coroutine_handle_t<> handle) {
			info_t info;
			info.handle = std::move(handle);

			if constexpr (!std::is_same_v<warp_t, void>) {
				info.warp = warp_t::get_current_warp();
			}

			// fast path
			if (flush_prepared()) {
				iris_sync_t<warp_t, async_worker_t>::dispatch(std::move(info));
				return;
			}
			
			std::unique_lock<std::mutex> guard(handle_lock);
			// retry
			if (flush_prepared()) {
				guard.unlock();
				iris_sync_t<warp_t, async_worker_t>::dispatch(std::move(info));
				return;
			}

			// failed, push to deferred list
			handles.push(std::move(info));
			waiting_count.fetch_add(1, std::memory_order_release);
		}

		element_t await_resume() noexcept {
			std::lock_guard<std::mutex> guard(data_pop_lock);
			element_t element = std::move(elements.top());
			elements.pop();

			return element;
		}

		template <typename element_data_t>
		void emplace(element_data_t&& element) {
			do {
				std::lock_guard<std::mutex> guard(data_push_lock);
				elements.push(std::forward<element_data_t>(element));
			} while (false);

			// fast path
			if (flush_waiting()) {
				info_t info;
				do {
					std::lock_guard<std::mutex> guard(handle_lock);
					info = std::move(handles.top());
					handles.pop();
				} while (false);

				iris_sync_t<warp_t, async_worker_t>::dispatch(std::move(info));
				return;
			}

			std::unique_lock<std::mutex> guard(handle_lock);
			if (flush_waiting()) {
				info_t info = std::move(handles.top());
				handles.pop();
				guard.unlock();

				iris_sync_t<warp_t, async_worker_t>::dispatch(std::move(info));
				return;
			}

			prepared_count.fetch_add(1, std::memory_order_release);
		}

	protected:
		bool flush_prepared() noexcept {
			size_t prepared = prepared_count.load(std::memory_order_acquire);
			while (prepared != 0) {
				if (prepared_count.compare_exchange_strong(prepared, prepared - 1, std::memory_order_release)) {
					return true;
				}
			}

			return false;
		}

		bool flush_waiting() noexcept {
			size_t waiting = waiting_count.load(std::memory_order_acquire);
			while (waiting != 0) {
				if (waiting_count.compare_exchange_strong(waiting, waiting - 1, std::memory_order_release)) {
					return true;
				}
			}

			return false;
		}

	protected:
		using info_t = typename iris_sync_t<warp_t, async_worker_t>::info_t;
		std::atomic<size_t> prepared_count;
		std::atomic<size_t> waiting_count;
		std::mutex handle_lock;
		std::mutex data_push_lock;
		std::mutex data_pop_lock;

		iris_queue_list_t<info_t> handles;
		iris_queue_list_t<element_t> elements;
	};

	// barrier-like multiple coroutine sychronization
	template <typename warp_t, typename async_worker_t = typename warp_t::async_worker_t>
	struct iris_barrier_t : iris_sync_t<warp_t, async_worker_t> {
		iris_barrier_t(async_worker_t& worker, size_t yield_max_count) : iris_sync_t<warp_t, async_worker_t>(worker), yield_max(yield_max_count) {
			handles.resize(yield_max);
			resume_count.store(0, std::memory_order_relaxed);
			yield_count.store(0, std::memory_order_release);
		}

		constexpr bool await_ready() const noexcept {
			return false;
		}

		void await_suspend(iris_coroutine_handle_t<> handle) {
			size_t index = yield_count.fetch_add(1, std::memory_order_acquire);
			assert(index < yield_max);
#ifdef _DEBUG
			for (size_t k = 0; k < index; k++) {
				assert(handles[index].handle != handle); // duplicated barrier of the same coroutine!
			}
#endif
			auto& info = handles[index];
			info.handle = std::move(handle);

			if constexpr (!std::is_same_v<warp_t, void>) {
				info.warp = warp_t::get_current_warp();
			}

			// all finished?
			if (index + 1 == yield_max) {
				yield_count.store(0, std::memory_order_relaxed);
				resume_count.store(0, std::memory_order_release);

				// notify all coroutines
				for (size_t i = 0; i < handles.size(); i++) {
					auto info = std::move(handles[i]);
#ifdef _DEBUG
					handles[i].handle = iris_coroutine_handle_t<>();
					if constexpr (!std::is_same_v<warp_t, void>) {
						info.warp = nullptr;
					}
#endif
					iris_sync_t<warp_t, async_worker_t>::dispatch(std::move(info));
				}
			}
		}

		size_t await_resume() noexcept {
			return resume_count.fetch_add(1, std::memory_order_acquire); // first resume!
		}

	protected:
		using info_t = typename iris_sync_t<warp_t, async_worker_t>::info_t;
		size_t yield_max;
		std::atomic<size_t> resume_count;
		std::atomic<size_t> yield_count;
		std::vector<info_t> handles;
	};

	// specify warp_t = void for warp-ignored dispatch
	template <typename warp_t, typename async_worker_t>
	auto iris_barrier(async_worker_t& worker, size_t yield_max_count) {
		return iris_barrier_t<warp_t, async_worker_t>(worker, yield_max_count);
	}

	// frame-like multiple coroutine sychronization
	template <typename warp_t, typename async_worker_t = typename warp_t::async_worker_t>
	struct iris_frame_t : iris_sync_t<warp_t, async_worker_t>, protected enable_read_write_fence_t<size_t> {
		explicit iris_frame_t(async_worker_t& worker) : iris_sync_t<warp_t, async_worker_t>(worker) {
			frame_pending_count.store(0, std::memory_order_relaxed);
			frame_complete.store(0, std::memory_order_relaxed);
			frame_terminated.store(0, std::memory_order_release);
		}

		bool await_ready() const noexcept {
			return is_terminated();
		}

		void await_suspend(iris_coroutine_handle_t<> handle) noexcept(noexcept(queue(std::move(handle)))) {
			queue(std::move(handle));
		}

		bool dispatch(bool running = true) {
			auto guard = write_fence();
			queue(iris_coroutine_handle_t<>()); // mark for frame edge

			if (frame_pending_count.load(std::memory_order_acquire) != 0) {
				frame_complete.wait(0, std::memory_order_acquire);
				frame_complete.store(0, std::memory_order_release);
			}

			frame_terminated.store(running ? 0 : 1, std::memory_order_release);

			// frame loop
			while (true) {
				info_t info = std::move(frame_coroutine_handles.top());
				frame_coroutine_handles.pop();

				if (info.handle) {
					if constexpr (!std::is_same_v<warp_t, void>) {
						// dispatch() must not shared the same warp with any coroutines
						assert(info.warp == nullptr || info.warp != warp_t::get_current_warp());
					}

					frame_pending_count.fetch_add(1, std::memory_order_acquire);
					iris_sync_t<warp_t, async_worker_t>::dispatch(std::move(info));
				} else {
					break;
				}
			}

			return running;
		}

		struct resume_t {
			explicit resume_t(iris_frame_t& host_frame) noexcept : frame(host_frame) {}
			~resume_t() noexcept {
				if (!frame.is_terminated()) {
					frame.next();
				}
			}

			operator bool() const noexcept {
				return !frame.is_terminated();
			}

			iris_frame_t& frame;
		};

		resume_t await_resume() noexcept {
			return resume_t(*this);
		}

		bool is_terminated() const noexcept {
			return frame_terminated.load(std::memory_order_acquire);
		}

	protected:
		// go next frame
		void next() noexcept {
			if (frame_pending_count.fetch_sub(1, std::memory_order_relaxed) == 1) {
				frame_complete.store(1, std::memory_order_release);
				frame_complete.notify_one();
			}
		}

		using info_t = typename iris_sync_t<warp_t, async_worker_t>::info_t;
		void queue(iris_coroutine_handle_t<>&& handle) {
			info_t info;
			info.handle = std::move(handle);
			if constexpr (!std::is_same_v<warp_t, void>) {
				info.warp = warp_t::get_current_warp();
			}

			std::lock_guard<std::mutex> guard(frame_mutex);
			frame_coroutine_handles.push(std::move(info));
		}

	protected:
		std::mutex frame_mutex;
		iris_queue_list_t<info_t> frame_coroutine_handles;
		std::atomic<size_t> frame_pending_count;
		std::atomic<size_t> frame_complete;
		std::atomic<size_t> frame_terminated;
	};

	// listen specified task completes
	template <typename async_dispatcher_t>
	struct iris_listen_dispatch_t : iris_sync_t<typename async_dispatcher_t::warp_t, typename async_dispatcher_t::async_worker_t> {
		using warp_t = typename async_dispatcher_t::warp_t;
		using async_worker_t = typename async_dispatcher_t::async_worker_t;
		using routine_t = typename async_dispatcher_t::routine_t;

		explicit iris_listen_dispatch_t(async_dispatcher_t& disp) : iris_sync_t<warp_t, async_worker_t>(disp.get_async_worker()), dispatcher(disp) {
			warp_t* warp = nullptr;
			if constexpr (!std::is_same_v<warp_t, void>) {
				warp = warp_t::get_current_warp();
			}

			info.warp = warp;
			routine = dispatcher.allocate(warp, [this]() {
				iris_sync_t<warp_t, async_worker_t>::dispatch(std::move(info));
			});
		}

		template <typename... args_t>
		iris_listen_dispatch_t(async_dispatcher_t& disp, routine_t* first, args_t&&... args) : iris_listen_dispatch_t(disp, std::forward<args_t>(args)...) {
			dispatcher.order(first, routine);
			dispatcher.dispatch(first);
		}

		constexpr bool await_ready() const noexcept {
			return false;
		}

		void await_suspend(iris_coroutine_handle_t<> handle) {
			assert(info.handle == iris_coroutine_handle_t<>());
			info.handle = std::move(handle);

			if constexpr (!std::is_same_v<warp_t, void>) {
				assert(info.warp == warp_t::get_current_warp());
			} else {
				assert(info.warp == nullptr);
			}

			dispatcher.dispatch(routine);
		}

		constexpr void await_resume() const noexcept {}

	protected:
		using info_t = typename iris_sync_t<warp_t, async_worker_t>::info_t;
		async_dispatcher_t& dispatcher;
		routine_t* routine;
		info_t info;
	};

	// specify warp_t = void for warp-ignored dispatch
	template <typename async_dispatcher_t, typename... args_t>
	auto iris_listen_dispatch(async_dispatcher_t& dispatcher, args_t&&... args) {
		return iris_listen_dispatch_t<async_dispatcher_t>(dispatcher, std::forward<args_t>(args)...);
	}

	// get quota in coroutine
	template <typename quota_t, typename warp_t, typename async_worker_t = typename warp_t::async_worker_t>
	struct iris_quota_queue_t : iris_sync_t<warp_t, async_worker_t> {
		iris_quota_queue_t(async_worker_t& worker, quota_t& q) : iris_sync_t<warp_t, async_worker_t>(worker), quota(q) {}

		using amount_t = typename quota_t::amount_t;
		using info_t = typename iris_sync_t<warp_t, async_worker_t>::info_t;

		struct finalizer_t {
			finalizer_t(iris_quota_queue_t& q, const amount_t& m) noexcept : host(q), amount(m) {}
			finalizer_t(const finalizer_t&) = delete;
			finalizer_t(finalizer_t&& rhs) noexcept : host(rhs.host), amount(rhs.amount) { rhs.quota = nullptr; }
			~finalizer_t() noexcept {
				host.release(amount);
			}

			// release part of them
			void release(const amount_t& delta) {
				for (size_t i = 0; i < amount.size(); i++) {
					assert(amount[i] >= delta);
					amount[i] -= delta[i];
				}

				host.release(delta);
			}
			
		protected:
			iris_quota_queue_t& host;
			amount_t amount;
		};

		struct awaitable_t {
			awaitable_t(iris_quota_queue_t& q, const amount_t& m, bool r) noexcept : host(q), amount(m), ready(r) {}

			bool await_ready() const noexcept {
				return ready;
			}

			void await_suspend(iris_coroutine_handle_t<> handle) {
				info_t info;
				info.handle = std::move(handle);

				if constexpr (!std::is_same_v<warp_t, void>) {
					info.warp = warp_t::get_current_warp();
				}

				host.acquire_queued(std::move(info), amount);
			}

			finalizer_t await_resume() noexcept {
				return finalizer_t(host, amount);
			}

		protected:
			iris_quota_queue_t& host;
			amount_t amount;
			bool ready;
		};

		awaitable_t guard(const amount_t& amount) {
			return awaitable_t(*this, amount, quota.acquire(amount));
		}

		bool acquire(const amount_t& amount) {
			return quota.acquire(amount);
		}

		void release(const amount_t& amount) {
			quota.release(std::move(amount));

			while (!handles.empty()) {
				std::unique_lock<std::mutex> guard(out_lock);

				auto& top = handles.top();
				if (quota.acquire(top.second)) {
					info_t handle = std::move(top.first);
					handles.pop();
					guard.unlock();

					iris_sync_t<warp_t, async_worker_t>::dispatch(std::move(handle));
				} else {
					break;
				}
			}
		}

	protected:
		void acquire_queued(info_t&& info, const amount_t& amount) {
			std::lock_guard<std::mutex> guard(in_lock);
			handles.push(std::make_pair(std::move(info), amount));
		}

	protected:
		quota_t& quota;
		std::mutex in_lock;
		std::mutex out_lock;
		iris_queue_list_t<std::pair<info_t, amount_t>> handles;
	};
}

