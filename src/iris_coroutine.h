/*
The Iris Concurrency Framework

This software is a C++ 20 Header-Only reimplementation of core part from project PaintsNow.

The MIT License (MIT)

Copyright (c) 2014-2024 PaintDream

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

			// call completion handle when returning a value
			// notice that value must be a rvalue and this completion is happened before destruction of living local variables in coroutine body
			template <typename value_t>
			void return_value(value_t&& value) noexcept {
				if (completion) {
					completion(std::forward<value_t>(value));
				}
			}

			// currently we do not handle unexcepted exceptions
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
	}

	// uniform coroutine class with a return type specified
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
			IRIS_ASSERT(!handle); // must call run() or join() before destruction
		}

		// set a unique, optional completion handler
		template <typename func_t>
		iris_coroutine_t& complete(func_t&& func) noexcept {
			IRIS_ASSERT(handle);
			handle.promise().completion = std::forward<func_t>(func);

			return *this;
		}

		// run coroutine intermediately
		void run() noexcept(noexcept(std::declval<std::coroutine_handle<promise_type>>().resume())) {
			IRIS_ASSERT(handle);
			std::coroutine_handle<promise_type> execute_handle(std::move(handle));
			handle = std::coroutine_handle<promise_type>();
			execute_handle.resume();
		}

		// iris_coroutine_t is also awaitable, so here we implement await_* series methods
		constexpr bool await_ready() const noexcept {
			return false;
		}

		// chain execution
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

		// carry out return value
		return_t await_resume() noexcept {
			if constexpr (!std::is_void_v<return_t>) {
				IRIS_ASSERT(await_result != nullptr);
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

	// awaitable object, can be used by:
	// co_await iris_awaitable_t(...);
	template <typename warp_type_t, typename func_type_t>
	struct iris_awaitable_t {
		using warp_t = warp_type_t;
		using func_t = func_type_t;
		using return_t = std::invoke_result_t<func_t>;

		// constructed from a given target warp and routine function
		// notice that we do not initialize `caller` here, let `await_suspend` do
		// parallel_priority: ~size_t(0) means no parallization, other value indicates the dispatch priority of parallel routines
		template <typename callable_t>
		iris_awaitable_t(warp_t* target_warp, callable_t&& f, size_t p) noexcept : target(target_warp), parallel_priority(p), func(std::forward<callable_t>(f)) {
			IRIS_ASSERT(target_warp != nullptr || parallel_priority == ~size_t(0));
		}

		// always suspended
		constexpr bool await_ready() const noexcept {
			return false;
		}

		void resume_one(std::coroutine_handle<> handle) {
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

		void await_suspend(std::coroutine_handle<> handle) {
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
		IRIS_ASSERT(priority != ~size_t(0));
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

		void resume_one(std::coroutine_handle<> handle) {
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

		void await_suspend(std::coroutine_handle<> handle) {
			if constexpr (!std::is_void_v<return_t>) {
				returns.resize(awaitables.size());
			}

			IRIS_ASSERT(!current_handle); // can only be called once!
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
		std::coroutine_handle<> current_handle;
		std::vector<awaitable_t> awaitables;
		return_multiple_t returns;
	};

	// wrapper for joining multiple awaitables together
	template <typename async_worker_t, typename awaitable_t, typename... args_t>
	auto iris_awaitable_union(async_worker_t& worker, awaitable_t&& first, args_t&&... args) {
		return iris_awaitable_multiple_t<typename awaitable_t::warp_t, typename awaitable_t::func_t>(worker, std::forward<awaitable_t>(first), std::forward<args_t>(args)...);
	}

	// switch to specified warp or warp pair, and return the original current warp
	template <typename warp_t>
	struct iris_switch_t {
		using async_worker_t = typename warp_t::async_worker_t;
		iris_switch_t(warp_t* target_warp, warp_t* other_warp, bool parallel_target_warp, bool parallel_other_warp) noexcept : source(warp_t::get_current_warp()), target(target_warp), other(other_warp), parallel_target(parallel_target_warp), parallel_other(parallel_other_warp) {}

		bool await_ready() const noexcept {
			if (parallel_target || parallel_other)
				return false;

			if (source == target) {
				return other == nullptr || source == other;
			} else {
				return target == nullptr && source == other;
			}
		}

		void handler(std::coroutine_handle<>&& handle) {
			// 1-1 mapping, just resume directly
			if (other == nullptr) {
				handle.resume();
				return;
			} else if (parallel_other) {
				other->suspend();
				typename warp_t::suspend_guard_t guard(other);
				if (!other->running()) {
					handle.resume();
					return;
				}
			} else {
				// try to preempt the other one
				typename warp_t::preempt_guard_t guard(*other, 0);
				if (guard) {
					// success, go resume directly
					handle.resume();
					return;
				}
			}

			// failed, swap and continue dispatching until success!
			std::swap(other, target);
			std::swap(parallel_other, parallel_target);

			if (parallel_target) {
				target->queue_routine_parallel_post([this, handle = std::move(handle)]() mutable noexcept(noexcept(handle.resume())) {
					handler(std::move(handle));
				});
			} else {
				target->queue_routine_post([this, handle = std::move(handle)]() mutable noexcept(noexcept(handle.resume())) {
					handler(std::move(handle));
				});
			}
		}

		void await_suspend(std::coroutine_handle<> handle) {
			if (target == nullptr) {
				std::swap(other, target);
			}

			if (target == nullptr) {
				// full parallel dispatching
				IRIS_ASSERT(source != nullptr);
				source->get_async_worker().queue([this, handle = std::move(handle)]() mutable {
					handler(std::move(handle));
				});
			} else {
				// dispatching under warp context
				if (target->get_async_worker().get_current_thread_index() != ~size_t(0)) {
					if (parallel_target) {
						target->queue_routine_parallel([this, handle = std::move(handle)]() mutable {
							handler(std::move(handle));
						});
					} else {
						target->queue_routine_post([this, handle = std::move(handle)]() mutable {
							handler(std::move(handle));
						});
					}
				} else {
					target->queue_routine_external([this, handle = std::move(handle)]() mutable {
						handler(std::move(handle));
					});
				}
			}
		}

		// return the caller warp
		warp_t* await_resume() const noexcept {
			return source;
		}

	protected:
		warp_t* source;
		warp_t* target;
		warp_t* other;
		bool parallel_target;
		bool parallel_other;
	};

	template <typename warp_t>
	auto iris_switch(warp_t* target, warp_t* other = nullptr, bool parallel_target = false, bool parallel_other = false) noexcept {
		return iris_switch_t<warp_t>(target, other, parallel_target, parallel_other);
	}

	// switch to any warp from specified range [from, to] 
	template <typename iterator_t>
	struct iris_select_t {
		using warp_t = std::decay_t<decltype(*std::declval<iterator_t>())>;
		using async_worker_t = typename warp_t::async_worker_t;

		iris_select_t(iterator_t from, iterator_t to) noexcept : begin(from), end(to), selected(nullptr) {
			IRIS_ASSERT(from != to);
			IRIS_ASSERT(warp_t::get_current_warp() == nullptr);
		}

		constexpr bool await_ready() const noexcept {
			return false;
		}

		void await_suspend(std::coroutine_handle<> handle) {
			// try fast preempt
			for (iterator_t p = begin; p != end; ++p) {
				warp_t* target = &(*p);
				typename warp_t::template preempt_guard_t guard(*target, 0);
				if (guard) {
					selected = target;
					handle.resume();
					return;
				}
			}

			// all warps are busy, so we need to post tasks to them
			auto shared_handle = std::make_shared<std::pair<std::atomic<std::coroutine_handle<>>, iris_select_t*>>(std::move(handle), this);
			for (iterator_t p = begin; p != end; ++p) {
				warp_t* target = &(*p);

				if (target->get_async_worker().get_current_thread_index() != ~size_t(0)) {
					target->queue_routine_post([shared_handle, target]() mutable noexcept(noexcept(handle.resume())) {
						// taken and go execute
						auto handle = shared_handle->first.exchange(std::coroutine_handle<>(), std::memory_order_release);
						if (handle) {
							shared_handle->second->selected = target;
							handle.resume();
						}
					});
				} else {
					target->queue_routine_external([shared_handle, target]() mutable noexcept(noexcept(handle.resume())) {
						auto handle = shared_handle->first.exchange(std::coroutine_handle<>(), std::memory_order_release);
						if (handle) {
							shared_handle->second->selected = target;
							handle.resume();
						}
					});
				}

				// already dispatched to one warp sucessfully, just give up the remaining
				if (shared_handle->first.load(std::memory_order_acquire) == std::coroutine_handle<>())
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
			std::coroutine_handle<> handle;
			warp_t* warp;
		};

		struct info_base_t {
			std::coroutine_handle<> handle;
		};

		using info_t = std::conditional_t<std::is_same_v<warp_t, void>, info_base_t, info_base_warp_t>;

		// dispatch coroutine based on warp status
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

		void await_suspend(std::coroutine_handle<> handle) {
			info_t info;
			info.handle = std::move(handle);

			if constexpr (!std::is_same_v<warp_t, void>) {
				info.warp = warp_t::get_current_warp();
			}

			// already signaled?
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

		// notify all waiting coroutines
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

	// pipe-like multiple coroutine synchronization (spsc)
	template <typename element_t, typename warp_t, typename async_worker_t = typename warp_t::async_worker_t>
	struct iris_pipe_t : iris_sync_t<warp_t, async_worker_t>, protected enable_in_out_fence_t<size_t> {
		iris_pipe_t(async_worker_t& worker) : iris_sync_t<warp_t, async_worker_t>(worker), in_index(0), out_index(0) {}

		constexpr bool await_ready() const noexcept {
			auto guard = out_fence();
			return !elements.empty();
		}

		void await_suspend(std::coroutine_handle<> handle) {
			auto guard = out_fence();

			info_t info;
			info.handle = std::move(handle);

			if constexpr (!std::is_same_v<warp_t, void>) {
				info.warp = warp_t::get_current_warp();
			}

			std::atomic_thread_fence(std::memory_order_acquire);
			IRIS_ASSERT(out_index == in_index);
		
			handles[out_index] = std::move(info);
			std::atomic_thread_fence(std::memory_order_release);
			out_index = out_index ^ 1;
		}

		element_t await_resume() noexcept {
			auto fence = out_fence();
			element_t element = std::move(elements.top());
			elements.pop();

			return element;
		}

		template <typename element_data_t>
		void emplace(element_data_t&& element) {
			auto fence = in_fence();
			elements.push(std::forward<element_data_t>(element));

			std::atomic_thread_fence(std::memory_order_acquire);
			if (in_index != out_index) {
				auto handle = std::move(handles[in_index]);
				handles[in_index] = info_t { nullptr, nullptr };
				iris_sync_t<warp_t, async_worker_t>::dispatch(std::move(handle));
				std::atomic_thread_fence(std::memory_order_release);
				in_index = out_index;
			}
		}
		
	protected:
		using info_t = typename iris_sync_t<warp_t, async_worker_t>::info_t;
		uint32_t in_index;
		uint32_t out_index;
		info_t handles[2];
		iris_queue_list_t<element_t> elements;
	};

	// stock-like multiple coroutine synchronization (mpmc)
	template <typename element_t, typename warp_t, typename async_worker_t = typename warp_t::async_worker_t>
	struct iris_stock_t : iris_sync_t<warp_t, async_worker_t> {
		iris_stock_t(async_worker_t& worker) : iris_sync_t<warp_t, async_worker_t>(worker) {
			prepared_count.store(0, std::memory_order_relaxed);
			waiting_count.store(0, std::memory_order_release);
		}

		constexpr bool await_ready() const noexcept {
			return false;
		}

		void await_suspend(std::coroutine_handle<> handle) {
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

			// still not success, go starving
			prepared_count.fetch_add(1, std::memory_order_release);
		}

	protected:
		// test and fetch producer
		bool flush_prepared() noexcept {
			size_t prepared = prepared_count.load(std::memory_order_acquire);
			while (prepared != 0) {
				if (prepared_count.compare_exchange_strong(prepared, prepared - 1, std::memory_order_release)) {
					return true;
				}
			}

			return false;
		}

		// test and fetch consumer
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
	template <typename warp_t, typename value_t = bool, typename async_worker_t = typename warp_t::async_worker_t>
	struct iris_barrier_t : iris_sync_t<warp_t, async_worker_t> {
		iris_barrier_t(async_worker_t& worker, size_t yield_max_count, value_t init_value = value_t()) : iris_sync_t<warp_t, async_worker_t>(worker), yield_max(yield_max_count), value(init_value) {
			handles.resize(yield_max);
			yield_count.store(0, std::memory_order_release);
		}

		~iris_barrier_t() noexcept {
#if IRIS_DEBUG
			// no more suspended coroutines
			for (size_t k = 0; k < handles.size(); k++) {
				IRIS_ASSERT(handles[k].handle == std::coroutine_handle<>());
			}
#endif
		}

		constexpr bool await_ready() const noexcept {
			return false;
		}

		template <typename func_t>
		void dispatch(func_t&& cb) {
			callback = std::forward<func_t>(cb);
			await_suspend(std::coroutine_handle<>());
		}

		void await_suspend(std::coroutine_handle<> handle) {
			size_t index = yield_count.fetch_add(1, std::memory_order_acquire);
			IRIS_ASSERT(index < yield_max);
			auto& info = handles[index];
			info.handle = std::move(handle);

			if constexpr (!std::is_same_v<warp_t, void>) {
				info.warp = warp_t::get_current_warp();
			}

			// all finished?
			if (index + 1 == yield_max) {
				size_t old = yield_count.exchange(0, std::memory_order_release);
				IRIS_ASSERT(old == yield_max);

				// notify all coroutines
				for (size_t i = 1; i < handles.size(); i++) {
					if (handles[i].handle == std::coroutine_handle<>()) {
						std::swap(handles[i], handles[0]);
						break;
					}
				}

				if (callback) {
					callback(*this);
				}

				for (size_t i = 0; i < handles.size(); i++) {
					auto info = std::move(handles[i]);
#if IRIS_DEBUG
					handles[i].handle = std::coroutine_handle<>();
					if constexpr (!std::is_same_v<warp_t, void>) {
						info.warp = nullptr;
					}
#endif
					if (info.handle != std::coroutine_handle<>()) {
						iris_sync_t<warp_t, async_worker_t>::dispatch(std::move(info));
					}
				}
			}
		}

		template <typename type_t>
		void set_value(type_t&& new_value) {
			value = std::forward<type_t>(new_value);
		}

		const value_t& get_value() const noexcept {
			return value;
		}

		const value_t& await_resume() const noexcept {
			return get_value();
		}

	protected:
		using info_t = typename iris_sync_t<warp_t, async_worker_t>::info_t;
		size_t yield_max;
		value_t value;
		std::atomic<size_t> yield_count;
		std::vector<info_t> handles;
		std::function<void(iris_barrier_t&)> callback;
	};

	// specify warp_t = void for warp-ignored dispatch
	template <typename warp_t, typename async_worker_t>
	auto iris_barrier(async_worker_t& worker, size_t yield_max_count) {
		return iris_barrier_t<warp_t, async_worker_t>(worker, yield_max_count);
	}

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

		~iris_listen_dispatch_t() noexcept {
			IRIS_ASSERT(routine == nullptr);
		}

		template <typename... args_t>
		iris_listen_dispatch_t(async_dispatcher_t& disp, routine_t* first, args_t&&... args) : iris_listen_dispatch_t(disp, std::forward<args_t>(args)...) {
			dispatcher.order(first, routine);
			dispatcher.dispatch(first);
		}

		constexpr bool await_ready() const noexcept {
			return false;
		}

		void await_suspend(std::coroutine_handle<> handle) {
			IRIS_ASSERT(info.handle == std::coroutine_handle<>());
			info.handle = std::move(handle);

			if constexpr (!std::is_same_v<warp_t, void>) {
				IRIS_ASSERT(info.warp == warp_t::get_current_warp());
			} else {
				IRIS_ASSERT(info.warp == nullptr);
			}

			dispatcher.dispatch(routine);
			routine = nullptr;
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

		struct resource_t {
			resource_t() noexcept : host(nullptr) {}
			resource_t(iris_quota_queue_t& q, const amount_t& m) noexcept : host(&q), amount(m) {}
			resource_t(const resource_t&) = delete;
			resource_t(resource_t&& rhs) noexcept : host(rhs.host), amount(rhs.amount) { rhs.host = nullptr; }

			resource_t& operator = (const resource_t&) = delete;
			resource_t& operator = (resource_t&& rhs) noexcept {
				clear();

				host = rhs.host;
				amount = rhs.amount;
				rhs.host = nullptr;

				return *this;
			}

			~resource_t() noexcept {
				clear();
			}

			void clear() noexcept {
				if (host != nullptr) {
					host->release(amount);
					host = nullptr;
				}
			}

			void merge(resource_t&& rhs) noexcept {
				if (host == nullptr) {
					*this = std::move(rhs);
				} else {
					IRIS_ASSERT(host == rhs.host);
					acquire(rhs.amount);
					rhs.host = nullptr;
				}
			}

			// add more
			void acquire(const amount_t& delta) noexcept {
				IRIS_ASSERT(host != nullptr);
				for (size_t i = 0; i < amount.size(); i++) {
					amount[i] += delta[i];
				}
			}

			// release part of them
			void release(const amount_t& delta) {
				IRIS_ASSERT(host != nullptr);
				for (size_t i = 0; i < amount.size(); i++) {
					IRIS_ASSERT(amount[i] >= delta[i]);
					amount[i] -= delta[i];
				}

				host->release(delta);
			}

			// move ownership of all quota
			amount_t move() noexcept {
				amount_t ret = get_amount();
				host = nullptr;
				for (size_t i = 0; i < amount.size(); i++) {
					amount[i] = 0;
				}

				return ret;
			}

			// this amount may be inaccurate
			const amount_t& get_amount() const noexcept {
				return amount;
			}

			iris_quota_queue_t* get_queue() const noexcept {
				return host;
			}
			
		protected:
			iris_quota_queue_t* host;
			amount_t amount;
		};

		struct awaitable_t {
			awaitable_t(iris_quota_queue_t& q, const amount_t& m, bool r) noexcept : host(q), amount(m), ready(r) {}

			bool await_ready() const noexcept {
				return ready;
			}

			void await_suspend(std::coroutine_handle<> handle) {
				info_t info;
				info.handle = std::move(handle);

				if constexpr (!std::is_same_v<warp_t, void>) {
					info.warp = warp_t::get_current_warp();
				}

				host.acquire_queued(std::move(info), amount);
			}

			resource_t await_resume() noexcept {
				return resource_t(host, amount);
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

		amount_t get_amount() const noexcept {
			return quota.get();
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

