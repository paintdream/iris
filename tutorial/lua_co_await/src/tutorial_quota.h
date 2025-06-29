// tutorial_quota.h
// PaintDream (paintdream@paintdream.com)
// 2023-06-01

#pragma once
#include "common.h"

namespace iris {
	class tutorial_quota_t : enable_read_write_fence_t<> {
	public:
		static void lua_registar(iris_lua_t&& lua, std::nullptr_t);

		tutorial_quota_t(iris_async_worker_t<>& async_worker, size_t capacity);
		~tutorial_quota_t() noexcept;

		iris_coroutine_t<void> pipeline(size_t cost);
		size_t get_remaining() const noexcept;

	protected:
		iris_quota_t<size_t, 1> quota;
		iris_quota_queue_t<iris_quota_t<size_t, 1>, iris_warp_t<iris_async_worker_t<>>> quota_queue;
	};
}