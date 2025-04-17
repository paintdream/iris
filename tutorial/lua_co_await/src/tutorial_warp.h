// tutorial_warp.h
// PaintDream (paintdream@paintdream.com)
// 2023-06-01

#pragma once
#include "common.h"

namespace iris {
	class tutorial_warp_t : enable_read_write_fence_t<> {
	public:
		static void lua_registar(lua_t&& lua, std::nullptr_t);

		tutorial_warp_t(lua_async_worker_t& async_worker);
		~tutorial_warp_t() noexcept;

		lua_coroutine_t<void> pipeline();

	protected:
		lua_warp_t stage_warp;
		int warp_variable = 0;
		int free_variable = 0;
	};
}