// tutorial_async.h
// PaintDream (paintdream@paintdream.com)
// 2023-06-01

#pragma once
#include "common.h"

namespace iris {
	class tutorial_async_t : enable_read_write_fence_t<> {
	public:
		static void lua_registar(iris_lua_t&& lua, std::nullptr_t);

		tutorial_async_t();
		~tutorial_async_t() noexcept;

		iris_coroutine_t<void> wait(size_t millseconds);
	};
}