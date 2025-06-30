// lua_co_await.h
// PaintDream (paintdream@paintdream.com)
// 2023-06-01

#pragma once
#include "common.h"

namespace iris {
	class lua_co_await_t : enable_read_write_fence_t<> {
	public:
		static void lua_registar(iris_lua_t&& lua, std::nullptr_t);

		lua_co_await_t();
		~lua_co_await_t() noexcept;

		// inspect internal async worker
		void* __async_worker__(void* new_async_worker_ptr);

		// public functions
		std::string_view get_version() const noexcept;
		bool is_running() const noexcept;
		bool start(size_t thread_count);
		bool terminate() noexcept;
		bool poll(size_t delay_in_milliseconds);
		void reset();

		// examples
		iris_lua_t::ref_t tutorial_binding(iris_lua_t&& lua);
		iris_lua_t::ref_t tutorial_async(iris_lua_t&& lua);
		iris_lua_t::ref_t tutorial_warp(iris_lua_t&& lua);
		iris_lua_t::ref_t tutorial_quota(iris_lua_t&& lua, size_t capacity);
		iris_lua_t::ref_t tutorial_readwrite(iris_lua_t&& lua);
		static void run_tutorials(iris_lua_t::refptr_t<lua_co_await_t>&& self, iris_lua_t&& lua);

	protected:
		bool set_async_worker(std::shared_ptr<iris_async_worker_t<>> worker);
		static void native_post_main(void* context, iris_async_worker_t<>::task_base_t* task);
		static void native_set_async_worker(void* context, void* async_worker_ptr);

	protected:
		std::shared_ptr<iris_async_worker_t<>> async_worker;
		std::unique_ptr<iris_warp_t<iris_async_worker_t<>>> main_warp;
		std::unique_ptr<iris_warp_t<iris_async_worker_t<>>::preempt_guard_t> main_warp_guard;
	};
}