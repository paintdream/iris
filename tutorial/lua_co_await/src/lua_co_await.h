// lua_co_await.h
// PaintDream (paintdream@paintdream.com)
// 2023-06-01

#pragma once
#include "common.h"

namespace iris {
	class lua_co_await_t : enable_read_write_fence_t<> {
	public:
		static void lua_registar(lua_t&& lua);

		lua_co_await_t();
		~lua_co_await_t() noexcept;
		std::string_view get_version() const noexcept;
		bool is_running() const noexcept;
		bool start(size_t thread_count);
		bool terminate() noexcept;
		bool poll(size_t delayInMilliseonds);

		// examples
		lua_ref_t tutorial_binding(lua_t&& lua);
		lua_ref_t tutorial_async(lua_t&& lua);
		lua_ref_t tutorial_warp(lua_t&& lua);
		lua_ref_t tutorial_quota(lua_t&& lua, size_t capacity);
		lua_ref_t tutorial_readwrite(lua_t&& lua);
		static void run_tutorials(lua_refptr_t<lua_co_await_t>&& self, lua_t&& lua);

	protected:
		std::unique_ptr<lua_async_worker_t> async_worker;
		std::unique_ptr<lua_warp_t> main_warp;
		std::unique_ptr<lua_warp_preempt_guard_t> main_guard;
	};
}