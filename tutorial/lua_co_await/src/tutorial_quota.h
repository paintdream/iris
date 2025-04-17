// tutorial_quota.h
// PaintDream (paintdream@paintdream.com)
// 2023-06-01

#pragma once
#include "common.h"

namespace iris {
	class tutorial_quota_t : enable_read_write_fence_t<> {
	public:
		static void lua_registar(lua_t&& lua, std::nullptr_t);

		tutorial_quota_t(lua_async_worker_t& async_worker, size_t capacity);
		~tutorial_quota_t() noexcept;

		lua_coroutine_t<void> pipeline(size_t cost);
		size_t get_remaining() const noexcept;

	protected:
		lua_quota_t quota;
		lua_quota_queue_t quota_queue;
	};
}