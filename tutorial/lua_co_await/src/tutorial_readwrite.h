// tutorial_readwrite.h
// PaintDream (paintdream@paintdream.com)
// 2023-06-01

#pragma once
#include "common.h"

namespace iris {
	class tutorial_readwrite_t : enable_read_write_fence_t<> {
	public:
		static void lua_registar(lua_t&& lua, std::nullptr_t);

		tutorial_readwrite_t(lua_async_worker_t& async_worker);
		~tutorial_readwrite_t() noexcept;

		lua_coroutine_t<void> pipeline();

	protected:
		lua_warp_t stage_warp;
	};
}