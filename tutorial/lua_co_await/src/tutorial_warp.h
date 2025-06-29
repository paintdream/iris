// tutorial_warp.h
// PaintDream (paintdream@paintdream.com)
// 2023-06-01

#pragma once
#include "common.h"

namespace iris {
	class tutorial_warp_t : enable_read_write_fence_t<> {
	public:
		static void lua_registar(iris_lua_t&& lua, std::nullptr_t);

		tutorial_warp_t(iris_async_worker_t<>& async_worker);
		~tutorial_warp_t() noexcept;

		iris_coroutine_t<void> pipeline();

	protected:
		iris_warp_t<iris_async_worker_t<>> stage_warp;
		int warp_variable = 0;
		int free_variable = 0;
	};
}