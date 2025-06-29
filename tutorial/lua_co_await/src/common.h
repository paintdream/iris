/*
lua_co_await

The MIT License (MIT)

Copyright (c) 2023-2025 PaintDream

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

#include <iris_common.h>
#include <iris_coroutine.h>
#include <iris_dispatcher.h>
#include <iris_lua.h>
#include <chrono>

namespace iris {
	using lua_async_worker_t = iris_async_worker_t<>;
	using lua_warp_t = iris_warp_t<lua_async_worker_t>;
	using lua_warp_preempt_guard_t = lua_warp_t::preempt_guard_t;
	using lua_t = iris_lua_t;
	using lua_ref_t = iris_lua_t::ref_t;
	template <typename type_t>
	using lua_result_t = iris_lua_t::optional_result_t<type_t>;
	using lua_error_t = iris_lua_t::result_error_t;
	template <typename type_t>
	using lua_refptr_t = iris_lua_t::template refptr_t<type_t>;
	template <typename type_t>
	using lua_coroutine_t = iris_coroutine_t<type_t>;
	using lua_quota_t = iris_quota_t<size_t, 1>;
	using lua_quota_queue_t = iris_quota_queue_t<lua_quota_t, lua_warp_t>;
}

