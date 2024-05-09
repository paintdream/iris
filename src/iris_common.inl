/*
The Iris Concurrency Framework

This software is a C++ 11 Header-Only reimplementation of core part from project PaintsNow.

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

#include "iris_common.h"

// include some platform headers for native memory allocations
// for example, allocating memory with 64KB on windows prevents application-awared fragments
#ifdef _WIN32
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>

	#ifdef min
		#undef min
	#endif

	#ifdef max
		#undef max
	#endif
#else
	#include <sys/mman.h>
	#include <malloc.h>
#endif

#if defined(USE_VLD)
#if USE_VLD
#include <vld.h>
#endif
#endif

namespace iris {
	static constexpr size_t large_page = 64 * 1024;
	IRIS_SHARED_LIBRARY_DECORATOR void* iris_alloc_aligned(size_t size, size_t alignment) {
#ifdef _WIN32
		// 64k page, use low-level allocation
		if (size >= large_page && ((size & (large_page - 1)) == 0)) {
			IRIS_ASSERT(alignment <= large_page);
			// usually, VirtualAlloc allocates memory in page with 64k
			return ::VirtualAlloc(nullptr, size, MEM_COMMIT, PAGE_READWRITE);
		} else {
			return _aligned_malloc(size, alignment);
		}
#else
		if (size >= large_page && ((size & (large_page - 1)) == 0)) {
			IRIS_ASSERT(alignment <= large_page);
			// mmap also aligns at 64k without any gaps between pages in most of implementations
			return mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		} else {
			return aligned_alloc(alignment, size);
		}
#endif
	}

	IRIS_SHARED_LIBRARY_DECORATOR void iris_free_aligned(void* data, size_t size) noexcept {
#ifdef _WIN32
		if (size >= large_page && ((size & (large_page - 1)) == 0)) {
			::VirtualFree(data, 0, MEM_RELEASE);
		} else {
			_aligned_free(data);
		}
#else
		if (size >= large_page && ((size & (large_page - 1)) == 0)) {
			munmap(data, size);
		} else {
			free(data);
		}
#endif
	}
}

