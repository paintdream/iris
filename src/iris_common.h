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

#include <cstdlib>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <utility>
#include <tuple>
#include <atomic>
#include <array>
#include <algorithm>
#include <memory>
#include <cmath>
#include <mutex>
#include <vector>
#include <cstring>
#include <functional>
#include <thread>

#ifndef IRIS_DEFAULT_BLOCK_SIZE
#define IRIS_DEFAULT_BLOCK_SIZE (1024)
#endif

#ifndef IRIS_DEFAULT_PAGE_SIZE
#define IRIS_DEFAULT_PAGE_SIZE (IRIS_DEFAULT_BLOCK_SIZE * 64)
#endif

#ifndef IRIS_PROFILE_THREAD
#define IRIS_PROFILE_THREAD(name, i)
#endif

#ifndef IRIS_PROFILE_SCOPE
#define IRIS_PROFILE_SCOPE(name)
#endif

#ifndef IRIS_PROFILE_PUSH
#define IRIS_PROFILE_PUSH(name)
#endif

#ifndef IRIS_PROFILE_POP
#define IRIS_PROFILE_POP()
#endif

#ifndef IRIS_SHARED_LIBRARY_DECORATOR
#define IRIS_SHARED_LIBRARY_DECORATOR
#endif

#ifndef IRIS_DEBUG
#ifdef _DEBUG
#define IRIS_DEBUG _DEBUG
#else
#define IRIS_DEBUG 0
#endif
#endif

#ifndef IRIS_ASSERT
#define IRIS_ASSERT assert
#endif

#ifndef IRIS_LOGERROR
#define IRIS_LOGERROR(...) fprintf(stderr, __VA_ARGS__)
#endif

#ifndef IRIS_LOGINFO
#define IRIS_LOGINFO(...) printf(__VA_ARGS__)
#endif

namespace iris {
	static constexpr size_t default_block_size = IRIS_DEFAULT_BLOCK_SIZE;
	static constexpr size_t default_page_size = IRIS_DEFAULT_PAGE_SIZE;

	// debug utilities for multi-thread programming
	template <typename atomic_t>
	struct iris_write_fence_t {
		operator bool() const noexcept {
			return true;
		}

#if IRIS_DEBUG
		iris_write_fence_t(std::atomic<atomic_t>& var, std::thread::id& id) noexcept : variable(var), thread_id(id) {
			acquire(variable, thread_id);
		}

		~iris_write_fence_t() noexcept {
			release(variable, thread_id);
		}

		static void acquire(std::atomic<atomic_t>& variable, std::thread::id& thread_id) noexcept {
			IRIS_ASSERT(variable.exchange(~(atomic_t)0, std::memory_order_acquire) == 0);
			thread_id = std::this_thread::get_id();
		}

		static void release(std::atomic<atomic_t>& variable, std::thread::id& thread_id) noexcept {
			thread_id = std::thread::id();
			IRIS_ASSERT(variable.exchange(0, std::memory_order_release) == ~(atomic_t)0);
		}

	private:
		std::atomic<atomic_t>& variable;
		std::thread::id& thread_id;
#endif
	};

	template <typename atomic_t>
	struct iris_read_fence_t {
		operator bool() const noexcept {
			return true;
		}

#if IRIS_DEBUG
		iris_read_fence_t(std::atomic<atomic_t>& var, std::thread::id& id) noexcept : variable(var), thread_id(id) {
			acquire(variable, thread_id);
		}

		~iris_read_fence_t() noexcept {
			release(variable, thread_id);
		}

		static void acquire(std::atomic<atomic_t>& variable, std::thread::id& thread_id) noexcept {
			IRIS_ASSERT(variable.fetch_add(1, std::memory_order_acquire) != ~(atomic_t)0);
			thread_id = std::this_thread::get_id();
		}

		static void release(std::atomic<atomic_t>& variable, std::thread::id& thread_id) noexcept {
			thread_id = std::thread::id();
			IRIS_ASSERT(variable.fetch_sub(1, std::memory_order_release) != ~(atomic_t)0);
		}

	private:
		std::atomic<atomic_t>& variable;
		std::thread::id& thread_id;
#endif
	};

	template <typename atomic_t = size_t>
	struct enable_read_write_fence_t {
#if IRIS_DEBUG
		iris_read_fence_t<atomic_t> read_fence() const noexcept {
			return iris_read_fence_t<atomic_t>(monitor, thread_id);
		}

		iris_write_fence_t<atomic_t> write_fence() const noexcept {
			return iris_write_fence_t<atomic_t>(monitor, thread_id);
		}

		void acquire_read() const noexcept {
			iris_read_fence_t<atomic_t>::acquire(monitor, thread_id);
		}

		void release_read() const noexcept {
			iris_read_fence_t<atomic_t>::release(monitor, thread_id);
		}

		void acquire_write() const noexcept {
			iris_write_fence_t<atomic_t>::acquire(monitor, thread_id);
		}

		void release_write() const noexcept {
			iris_write_fence_t<atomic_t>::release(monitor, thread_id);
		}
#else
		iris_read_fence_t<atomic_t> read_fence() const noexcept {
			return iris_read_fence_t<atomic_t>();
		}

		iris_write_fence_t<atomic_t> write_fence() const noexcept {
			return iris_write_fence_t<atomic_t>();
		}

		void acquire_read() const noexcept {}
		void release_read() const noexcept {}
		void acquire_write() const noexcept {}
		void release_write() const noexcept {}
#endif
	private:
#if IRIS_DEBUG
		mutable std::atomic<atomic_t> monitor = 0;
		mutable std::thread::id thread_id;
#endif
	};

	// in out fence is to protect dual input/output data structures such as queue_list_t or deque
	template <typename atomic_t = size_t>
	struct enable_in_out_fence_t {
#if IRIS_DEBUG
		iris_write_fence_t<atomic_t> in_fence() const noexcept {
			return iris_write_fence_t<atomic_t>(in_monitor, in_thread_id);
		}

		iris_write_fence_t<atomic_t> out_fence() const noexcept {
			return iris_write_fence_t<atomic_t>(out_monitor, out_thread_id);
		}

		void acquire_in() const noexcept {
			return iris_write_fence_t<atomic_t>::acquire(in_monitor, in_thread_id);
		}

		void release_in() const noexcept {
			return iris_write_fence_t<atomic_t>::release(in_monitor, in_thread_id);
		}

		void acquire_out() const noexcept {
			return iris_write_fence_t<atomic_t>::acquire(out_monitor, out_thread_id);
		}

		void release_out() const noexcept {
			return iris_write_fence_t<atomic_t>::release(out_monitor, out_thread_id);
		}
#else
		iris_write_fence_t<atomic_t> in_fence() const noexcept {
			return iris_write_fence_t<atomic_t>();
		}

		iris_write_fence_t<atomic_t> out_fence() const noexcept {
			return iris_write_fence_t<atomic_t>();
		}

		void acquire_in() const noexcept {}
		void release_in() const noexcept {}
		void acquire_out() const noexcept {}
		void release_out() const noexcept {}
#endif

	private:
#if IRIS_DEBUG
		mutable std::atomic<atomic_t> in_monitor = 0;
		mutable std::atomic<atomic_t> out_monitor = 0;
		mutable std::thread::id in_thread_id;
		mutable std::thread::id out_thread_id;
#endif
	};

	// check if casting from source_t to target_t is lossless
	template <typename target_t, typename source_t>
	target_t iris_verify_cast(source_t&& src) noexcept {
		target_t ret = static_cast<target_t>(src);
		IRIS_ASSERT(ret == src);
		return ret;
	}

	// static variable provider template
	template <typename type_t>
	struct iris_static_instance_base_t {
		static type_t& get_thread_local() noexcept {
			static thread_local type_t instance;
			return instance;
		}

		static type_t& get_global() noexcept {
			static type_t instance;
			return instance;
		}

		static size_t get_unique_hash() noexcept {
			static const size_t sentinel = 0;
			return (size_t)reinterpret_cast<const void*>(&sentinel);
		}
	};

	template <typename type_t>
	struct iris_static_instance_t : iris_static_instance_base_t<type_t> {};

	// gcc/clang cannot export template instance across shared library (i.e. 'extern template struct' not works but msvc does)
	// these lines are ugly but works in both of them
#define declare_shared_static_instance(type) \
	template <> \
	struct iris_static_instance_t<type> { \
		IRIS_SHARED_LIBRARY_DECORATOR static type& get_thread_local() noexcept; \
		IRIS_SHARED_LIBRARY_DECORATOR static type& get_global() noexcept; \
		IRIS_SHARED_LIBRARY_DECORATOR static size_t get_unique_hash() noexcept; \
	} \

#define implement_shared_static_instance(type) \
	type& iris_static_instance_t<type>::get_thread_local() noexcept { \
		return iris_static_instance_base_t<type>::get_thread_local(); \
	} \
	type& iris_static_instance_t<type>::get_global() noexcept { \
		return iris_static_instance_base_t<type>::get_global(); \
	} \
	size_t iris_static_instance_t<type>::get_unique_hash() noexcept { \
		return iris_static_instance_base_t<type>::get_unique_hash(); \
	}

	// legacy constexpr log2 algorithm, compatible with C++ 11
	template <size_t i>
	struct iris_log2 : std::conditional<i / 2 != 0, std::integral_constant<size_t, 1 + iris_log2<i / 2>::value>, std::integral_constant<size_t, 0>>::type {};

	template <>
	struct iris_log2<0> : std::integral_constant<size_t, 0> {}; // let log2(0) == 0, only for template reduction compiling

	// std::make_index_sequence for C++ 11
	// seq from stackoverflow http://stackoverflow.com/questions/17424477/implementation-c14-make-integer-sequence by xeo
	template <size_t...> struct iris_sequence { using type = iris_sequence; };
	template <typename s1, typename s2> struct iris_concat;
	template <size_t... i1, size_t... i2>
	struct iris_concat<iris_sequence<i1...>, iris_sequence<i2...>> : iris_sequence<i1..., (sizeof...(i1) + i2)...> {};
	template <size_t n> struct iris_make_sequence;
	template <size_t n>
	struct iris_make_sequence : iris_concat<typename iris_make_sequence<n / 2>::type, typename iris_make_sequence<n - n / 2>::type>::type {};
	template <> struct iris_make_sequence<0> : iris_sequence<> {};
	template <> struct iris_make_sequence<1> : iris_sequence<0> {};

	template <typename type_t, typename = void>
	struct iris_is_iterable : std::false_type {};

	template< typename... args_t>
	struct iris_make_void { typedef void type; };
 
	template< typename... args_t>
	using iris_void_t = typename iris_make_void<args_t...>::type;

	template <typename type_t>
	struct iris_is_reference_wrapper : std::false_type {};

	template <typename type_t>
	struct iris_is_reference_wrapper<std::reference_wrapper<type_t>> : std::true_type {};

	template <typename type_t>
	struct iris_is_iterable<type_t,
		iris_void_t<decltype(std::begin(std::declval<type_t>())), decltype(std::end(std::declval<type_t>()))>
	> : std::true_type {};

	template <typename type_t, typename = void>
	struct iris_is_map : std::false_type {};

	template <typename type_t>
	struct iris_is_map<type_t, iris_void_t<typename type_t::mapped_type>> : std::true_type {};

	template <typename type_t, typename = void>
	struct iris_is_tuple : std::false_type {};

	template <typename type_t>
	struct iris_is_tuple<type_t, iris_void_t<typename std::tuple_size<type_t>::value_type>> : std::true_type {};

	template <typename type_t, typename = void>
	struct iris_is_coroutine : std::false_type {};

	template <typename type_t>
	struct iris_is_coroutine<type_t, iris_void_t<typename type_t::promise_type>> : std::true_type {};

	template <typename value_t>
	constexpr value_t iris_get_alignment(value_t a) noexcept {
		return a & (~a + 1); // the same as a & -a, but no compiler warnings.
	}

	// binary find / insert / remove extension of std::vector<> like containers.
	template <typename key_t, typename value_t>
	struct iris_key_value_t : std::pair<key_t, value_t> {
		using base = std::pair<key_t, value_t>;
		template <typename key_args_t, typename value_args_t>
		iris_key_value_t(key_args_t&& k, value_args_t&& v) : std::pair<key_t, value_t>(std::forward<key_args_t>(k), std::forward<value_args_t>(v)) {}
		iris_key_value_t(const key_t& k) : std::pair<key_t, value_t>(k, value_t()) {}
		iris_key_value_t() {}

		bool operator == (const iris_key_value_t& rhs) const {
			return base::first == rhs.first;
		}

		bool operator < (const iris_key_value_t& rhs) const {
			return base::first < rhs.first;
		}
	};

	template <typename key_t, typename value_t>
	iris_key_value_t<typename std::decay<key_t>::type, typename std::decay<value_t>::type> iris_make_key_value(key_t&& k, value_t&& v) {
		return iris_key_value_t<typename std::decay<key_t>::type, typename std::decay<value_t>::type>(std::forward<key_t>(k), std::forward<value_t>(v));
	}

	template <typename iterator_t, typename value_t, typename pred_t>
	iterator_t iris_binary_find(iterator_t begin, iterator_t end, value_t&& value, pred_t&& pred) {
		if (begin == end) {
			return end;
		}

		typename std::decay<decltype(*begin)>::type element(std::forward<value_t>(value));
		iterator_t ip = end;
		if (pred(*--ip, element)) {
			return end; // fast path for inserting at end
		}

		iterator_t it = std::lower_bound(begin, end, element, pred);
		return it != end && !pred(std::move(element), *it) ? it : end;
	}

	template <typename iterator_t, typename value_t>
	iterator_t iris_binary_find(iterator_t begin, iterator_t end, value_t&& value) {
		if (begin == end) {
			return end;
		}

		typename std::decay<decltype(*begin)>::type element(std::forward<value_t>(value));
		iterator_t ip = end;
		if (*--ip < element) {
			return end; // fast path for inserting at end
		}

		iterator_t it = std::lower_bound(begin, end, element);
		return it != end && !(std::move(element) < *it) ? it : end;
	}

	template <typename container_t, typename value_t, typename pred_t>
	typename container_t::iterator iris_binary_insert(container_t& container, value_t&& value, pred_t&& pred) {
		typename container_t::value_type element(std::forward<value_t>(value));
		typename container_t::iterator it = std::upper_bound(container.begin(), container.end(), element, pred);
		typename container_t::iterator ip = it;

		if (it != container.begin() && !pred(*--ip, element)) {
			*ip = std::move(element);
			return ip;
		} else {
			return container.insert(it, std::move(element));
		}
	}

	template <typename container_t, typename value_t>
	typename container_t::iterator iris_binary_insert(container_t& container, value_t&& value) {
		typename container_t::value_type element(std::forward<value_t>(value));
		typename container_t::iterator it = std::upper_bound(container.begin(), container.end(), element);
		typename container_t::iterator ip = it;

		if (it != container.begin() && !(*--ip < element)) {
			*ip = std::move(element);
			return ip;
		} else {
			return container.insert(it, std::move(element));
		}
	}

	template <typename container_t, typename value_t, typename pred_t>
	bool iris_binary_erase(container_t& container, value_t&& value, pred_t&& pred) {
		typename container_t::value_type element(std::forward<value_t>(value));
		typename container_t::iterator it = std::upper_bound(container.begin(), container.end(), element, pred);
		typename container_t::iterator ip = it;

		if (it != container.begin() && !pred(*--ip, std::move(element))) {
			container.erase(ip);
			return true;
		} else {
			return false;
		}
	}

	template <typename container_t, typename value_t>
	bool iris_binary_erase(container_t& container, value_t&& value) {
		typename container_t::value_type element(std::forward<value_t>(value));
		typename container_t::iterator it = std::upper_bound(container.begin(), container.end(), element);
		typename container_t::iterator ip = it;

		if (it != container.begin() && !(*--ip < std::move(element))) {
			container.erase(ip);
			return true;
		} else {
			return false;
		}
	}

	inline uint32_t iris_get_trailing_zeros(uint32_t value) noexcept {
		IRIS_ASSERT(value != 0);
#if defined(_MSC_VER)
		unsigned long index;
		_BitScanForward(&index, value);
		return index;
#else
		return __builtin_ctz(value);
#endif
	}

	inline uint32_t iris_get_trailing_zeros(uint64_t value) noexcept {
		IRIS_ASSERT(value != 0);
#if defined(_MSC_VER)
#if !defined(_M_AMD64)
		uint32_t lowPart = (uint32_t)(value & 0xffffffff);
		return lowPart == 0 ? iris_get_trailing_zeros((uint32_t)((value >> 31) >> 1)) + 32 : iris_get_trailing_zeros(lowPart);
#else
		unsigned long index;
		_BitScanForward64(&index, value);
		return index;
#endif
#else
		return __builtin_ctzll(value);
#endif
	}

	template <typename value_t>
	uint32_t iris_get_trailing_zeros_general(value_t value) noexcept {
		if /* constexpr */ (sizeof(value_t) == sizeof(uint32_t)) {
			return iris_get_trailing_zeros((uint32_t)value);
		} else {
			return iris_get_trailing_zeros((uint64_t)value);
		}
	}

	template <typename type_t, typename index_t>
	void iris_union_set_init(type_t&& vec, index_t from, index_t to) {
		while (from != to) {
			vec[from] = from;
			from++;
		}
	}

	template <typename type_t, typename index_t>
	index_t iris_union_set_find(type_t&& vec, index_t pos) {
		index_t next = pos;
		if (next != vec[next]) {
			do {
				next = vec[next];
			} while (next != vec[next]);

			while (pos != next) {
				index_t i = vec[pos];
				vec[pos] = next;
				pos = i;
			}
		}

		return next;
	}

	template <typename type_t, typename index_t>
	void iris_union_set_join(type_t&& vec, index_t from, index_t to) {
		vec[iris_union_set_find(vec, to)] = iris_union_set_find(vec, from);
	}

	extern IRIS_SHARED_LIBRARY_DECORATOR void* iris_alloc_aligned(size_t size, size_t alignment);
	extern IRIS_SHARED_LIBRARY_DECORATOR void iris_free_aligned(void* data, size_t size) noexcept;

	// global allocator that allocates memory blocks to local allocators.
	template <size_t alloc_size, size_t total_count>
	struct iris_root_allocator_t {
		static constexpr size_t bitmap_count = (total_count + sizeof(size_t) * 8 - 1) / (sizeof(size_t) * 8);

		~iris_root_allocator_t() noexcept {
			IRIS_ASSERT(blocks.empty());
		}

		void* allocate() {
			// do fast operations in critical section
			do {
				std::lock_guard<std::mutex> guard(lock);
				for (size_t i = 0; i < blocks.size(); i++) {
					block_t& block = blocks[i];
					for (size_t n = 0; n < bitmap_count; n++) {
						size_t& bitmap = block.bitmap[n];
						size_t bit = bitmap + 1;
						bit = bit & (~bit + 1);
						if (bit != 0) {
							size_t index = iris_verify_cast<size_t>(iris_get_trailing_zeros_general(bit)) + n * sizeof(size_t) * 8;
							if (index < total_count) {
								bitmap |= bit;
								return block.address + index * alloc_size;
							}
						}
					}
				}
			} while (false);

			// real allocation, release the critical.
			block_t block;
			block.address = reinterpret_cast<uint8_t*>(iris_alloc_aligned(alloc_size * total_count, alloc_size));
			std::memset(block.bitmap, 0, sizeof(block.bitmap));
			block.bitmap[0] = 1;

			// write result back
			do {
				std::lock_guard<std::mutex> guard(lock);
				blocks.emplace_back(block);
			} while (false);

			return block.address;
		}

		void deallocate(void* p) {
			void* t = nullptr;

			do {
				std::lock_guard<std::mutex> guard(lock);

				// loop to find required one.
				for (size_t i = 0; i < blocks.size(); i++) {
					block_t& block = blocks[i];
					if (p >= block.address && p < block.address + alloc_size * total_count) {
						size_t index = (reinterpret_cast<uint8_t*>(p) - block.address) / alloc_size;
						size_t page = index / (sizeof(size_t) * 8);
						size_t offset = index & (sizeof(size_t) * 8 - 1);
						IRIS_ASSERT(page < bitmap_count);
						size_t& bitmap = block.bitmap[page];
						bitmap &= ~(size_t(1) << offset);

						if (bitmap == 0) {
							size_t n;
							for (n = 0; n < bitmap_count; n++) {
								if (block.bitmap[n] != 0)
									break;
							}

							if (n == bitmap_count) {
								t = block.address;
								blocks.erase(blocks.begin() + i);
							}
						}

						break;
					}
				}
			} while (false);

			if (t != nullptr) {
				// do free
				iris_free_aligned(t, alloc_size * total_count);
			}
		}

		// we are not dll-friendly, as always.
		static iris_root_allocator_t& get() {
			return iris_static_instance_t<iris_root_allocator_t>::get_global();
		}

	protected:
		struct block_t {
			uint8_t* address;
			size_t bitmap[bitmap_count];
		};

		std::mutex lock;
		std::vector<block_t> blocks;
	};

	// local allocator, allocate memory with specified alignment requirements.
	// k = element size, m = block size, r = max recycled block count, 0 for not limited, w = control block count
	template <size_t k, size_t m = default_block_size, size_t r = 8, size_t s = default_page_size / m, size_t w = 8>
	struct iris_allocator_t : protected enable_read_write_fence_t<> {
		static constexpr size_t block_size = m;
		static constexpr size_t item_count = m / k;
		static constexpr size_t bits = 8 * sizeof(size_t);
		static constexpr size_t bitmap_block_size = (item_count + bits - 1) / bits;
		static constexpr size_t mask = bits - 1;

		struct control_block_t {
			iris_allocator_t* allocator;
			control_block_t* next;
			std::atomic<size_t> ref_count;
			std::atomic<size_t> managed;
			std::atomic<size_t> bitmap[bitmap_block_size];
		};

		static constexpr size_t offset = (sizeof(control_block_t) + k - 1) / k;

		iris_allocator_t() noexcept {
			static_assert(item_count / 2 * k > sizeof(control_block_t), "item_count is too small");
			recycle_count.store(0, std::memory_order_relaxed);
			for (size_t n = 0; n < sizeof(control_blocks) / sizeof(control_blocks[0]); n++) {
				control_blocks[n].store(nullptr, std::memory_order_relaxed);
			}

			recycled_head.store(nullptr, std::memory_order_release);
		}

		static iris_root_allocator_t<m, s>& get_root_allocator() {
			return iris_root_allocator_t<m, s>::get();
		}

		~iris_allocator_t() noexcept {
			// deallocate all caches
			iris_root_allocator_t<m, s>& allocator = get_root_allocator();

			for (size_t n = 0; n < sizeof(control_blocks) / sizeof(control_blocks[0]); n++) {
				control_block_t* p = control_blocks[n].load(std::memory_order_acquire);
				if (p != nullptr) {
					allocator.deallocate(p);
				}
			}

			control_block_t* p = recycled_head.load(std::memory_order_acquire);
			while (p != nullptr) {
				control_block_t* t = p->next;
				allocator.deallocate(p);
				p = t;
			}
		}

		void* allocate_unsafe() {
			auto guard = write_fence();

			while (true) {
				control_block_t* p = nullptr;
				for (size_t n = 0; p == nullptr && n < sizeof(control_blocks) / sizeof(control_blocks[0]); n++) {
					std::atomic<control_block_t*>& t = control_blocks[n];
					p = t.load(std::memory_order_relaxed);
					if (p == nullptr) {
						t.store(nullptr, std::memory_order_relaxed);
					}
				}

				if (p == nullptr) {
					// need a new block
					p = recycled_head.load(std::memory_order_relaxed);
					if (p != nullptr) {
						control_block_t* t = p->next;
						recycled_head.store(t, std::memory_order_relaxed);

						p->next = nullptr;
						IRIS_ASSERT(p->ref_count.load(std::memory_order_relaxed) >= 1);
						recycle_count.store(recycle_count.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
						IRIS_ASSERT(p->managed.load(std::memory_order_relaxed) == 1);
						p->managed.store(0, std::memory_order_relaxed);
					} else {
						p = reinterpret_cast<control_block_t*>(get_root_allocator().allocate());
						std::memset(p, 0, sizeof(control_block_t));
						p->next = nullptr;
						p->allocator = this;
						p->ref_count.store(1, std::memory_order_relaxed); // newly allocated one, just set it to 1
					}
				} else {
					p->managed.store(0, std::memory_order_relaxed);
				}

				// search for an empty slot
				for (size_t n = 0; n < bitmap_block_size; n++) {
					std::atomic<size_t>& b = p->bitmap[n];
					size_t mask = b.load(std::memory_order_relaxed);
					if (mask != ~size_t(0)) {
						size_t bit = iris_get_alignment(mask + 1);
						if (!(mask & bit)) {
							b.store(mask | bit, std::memory_order_relaxed);
							// get index of bitmap
							size_t index = iris_verify_cast<size_t>(iris_get_trailing_zeros_general(bit)) + offset + n * 8 * sizeof(size_t);
							if (index < item_count) {
								p->ref_count.store(p->ref_count.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
								// add to recycle system if needed
								recycle_unsafe(p);

								return reinterpret_cast<char*>(p) + index * k;
							}
						}
					}
				}

				// full?
				try_free_unsafe(p);
			}
		}

		void* allocate_safe() {
			auto guard = read_fence();

			while (true) {
				control_block_t* p = nullptr;
				for (size_t n = 0; p == nullptr && n < sizeof(control_blocks) / sizeof(control_blocks[0]); n++) {
					std::atomic<control_block_t*>& t = control_blocks[n];
					if (t.load(std::memory_order_acquire) != nullptr) {
						p = t.exchange(nullptr, std::memory_order_relaxed);
					}
				}

				if (p == nullptr) {
					// need a new block
					p = recycled_head.exchange(nullptr, std::memory_order_acquire);
					if (p != nullptr) {
						control_block_t* t = p->next;
						control_block_t* expected = nullptr;
						if (!recycled_head.compare_exchange_strong(expected, t, std::memory_order_release, std::memory_order_relaxed)) {
							while (t != nullptr) {
								control_block_t* q = t->next;
								control_block_t* h = recycled_head.load(std::memory_order_relaxed);
								do {
									t->next = h;
								} while (!recycled_head.compare_exchange_weak(h, t, std::memory_order_release, std::memory_order_relaxed));

								t = q;
							}
						}

						p->next = nullptr;
						IRIS_ASSERT(p->ref_count.load(std::memory_order_acquire) >= 1);
						recycle_count.fetch_sub(1, std::memory_order_relaxed);
						IRIS_ASSERT(p->managed.load(std::memory_order_acquire) == 1);
						p->managed.store(0, std::memory_order_release);
					} else {
						p = reinterpret_cast<control_block_t*>(get_root_allocator().allocate());
						std::memset(p, 0, sizeof(control_block_t));
						p->next = nullptr;
						p->allocator = this;
						p->ref_count.store(1, std::memory_order_relaxed); // newly allocated one, just set it to 1
					}
				} else {
					p->managed.store(0, std::memory_order_release);
				}

				// search for an empty slot
				for (size_t n = 0; n < bitmap_block_size; n++) {
					std::atomic<size_t>& b = p->bitmap[n];
					size_t mask = b.load(std::memory_order_acquire);
					if (mask != ~size_t(0)) {
						size_t bit = iris_get_alignment(mask + 1);
						if (!(b.fetch_or(bit, std::memory_order_relaxed) & bit)) {
							// get index of bitmap
							size_t index = iris_verify_cast<size_t>(iris_get_trailing_zeros_general(bit)) + offset + n * 8 * sizeof(size_t);
							if (index < item_count) {
								p->ref_count.fetch_add(1, std::memory_order_relaxed);
								// add to recycle system if needed
								recycle_safe(p);

								return reinterpret_cast<char*>(p) + index * k;
							}
						}
					}
				}

				// full?
				try_free_safe(p);
			}

			IRIS_ASSERT(false);
			return nullptr; // never reach here
		}

		static void deallocate_safe(void* ptr) {
			size_t t = reinterpret_cast<size_t>(ptr);
			control_block_t* p = reinterpret_cast<control_block_t*>(t & ~(block_size - 1));
			size_t id = (t - reinterpret_cast<size_t>(p)) / k - offset;
			auto guard = p->allocator->read_fence();

			IRIS_ASSERT(p->allocator != nullptr);
			p->bitmap[id / bits].fetch_and(~(size_t(1) << (id & mask)));
			p->allocator->recycle_safe(p);
		}

		static void deallocate_unsafe(void* ptr) {
			size_t t = reinterpret_cast<size_t>(ptr);
			control_block_t* p = reinterpret_cast<control_block_t*>(t & ~(block_size - 1));
			size_t id = (t - reinterpret_cast<size_t>(p)) / k - offset;
			auto guard = p->allocator->write_fence();

			IRIS_ASSERT(p->allocator != nullptr);
			p->bitmap[id / bits].store(p->bitmap[id / bits].load(std::memory_order_relaxed) & ~(size_t(1) << (id & mask)));
			p->allocator->recycle_unsafe(p);
		}

	protected:
		void try_free_safe(control_block_t* p) {
			IRIS_ASSERT(p->ref_count.load(std::memory_order_acquire) != 0);
			if (p->ref_count.fetch_sub(1, std::memory_order_release) == 1) {
				for (size_t n = 0; n < sizeof(control_blocks) / sizeof(control_blocks[0]); n++) {
					IRIS_ASSERT(control_blocks[n].load(std::memory_order_acquire) != p);
				}

				get_root_allocator().deallocate(p);
			}
		}

		void try_free_unsafe(control_block_t* p) {
			IRIS_ASSERT(p->ref_count.load(std::memory_order_relaxed) != 0);
			size_t count = p->ref_count.load(std::memory_order_relaxed);
			p->ref_count.store(count - 1, std::memory_order_relaxed);
			if (count == 1) {
				for (size_t n = 0; n < sizeof(control_blocks) / sizeof(control_blocks[0]); n++) {
					IRIS_ASSERT(control_blocks[n].load(std::memory_order_relaxed) != p);
				}

				get_root_allocator().deallocate(p);
			}
		}

		void recycle_safe(control_block_t* p) {
			IRIS_ASSERT(p->ref_count.load(std::memory_order_acquire) != 0);

			// search for recycled
			if (p->managed.load(std::memory_order_acquire) == 0 && recycle_count < r && p->managed.exchange(1, std::memory_order_acquire) == 0) {
				for (size_t n = 0; n < sizeof(control_blocks) / sizeof(control_blocks[0]); n++) {
					control_block_t* expected = nullptr;
					if (control_blocks[n].compare_exchange_strong(expected, p, std::memory_order_release, std::memory_order_relaxed)) {
						return;
					}
				}

				recycle_count.fetch_add(1, std::memory_order_relaxed);

				IRIS_ASSERT(p->next == nullptr);
				control_block_t* h = recycled_head.load(std::memory_order_relaxed);
				do {
					p->next = h;
				} while (!recycled_head.compare_exchange_weak(h, p, std::memory_order_release, std::memory_order_relaxed));
			} else {
				try_free_safe(p);
			}
		}

		void recycle_unsafe(control_block_t* p) {
			IRIS_ASSERT(p->ref_count.load(std::memory_order_relaxed) != 0);

			// search for recycled
			if (p->managed.load(std::memory_order_relaxed) == 0 && recycle_count < r) {
				p->managed.store(1, std::memory_order_relaxed);

				for (size_t n = 0; n < sizeof(control_blocks) / sizeof(control_blocks[0]); n++) {
					control_block_t* expected = nullptr;
					if (control_blocks[n].load(std::memory_order_relaxed) == nullptr) {
						control_blocks[n].store(p, std::memory_order_relaxed);
						return;
					}
				}

				recycle_count.store(recycle_count.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);

				IRIS_ASSERT(p->next == nullptr);
				p->next = recycled_head.load(std::memory_order_relaxed);
				recycled_head.store(p, std::memory_order_relaxed);
			} else {
				try_free_unsafe(p);
			}
		}

	protected:
		std::atomic<control_block_t*> recycled_head;
		std::atomic<size_t> recycle_count;
		std::atomic<control_block_t*> control_blocks[w];
	};

	template <typename element_t, size_t allocator_block_size = default_block_size>
	struct iris_object_allocator_t : iris_allocator_t<sizeof(element_t), allocator_block_size> {
		static constexpr size_t block_size = allocator_block_size;
		using value_type = element_t;
		using pointer = element_t*;
		using const_pointer = const element_t*;
		using reference = element_t&;
		using const_reference = const element_t&;
		using size_type = size_t;
		using difference_type = ptrdiff_t;
		using propagate_on_container_move_assignment = std::true_type;
		using is_always_equal = std::false_type;

		template <typename morph_t>
		struct rebind { using other = iris_object_allocator_t<morph_t, block_size>; };
		using allocator_t = iris_allocator_t<sizeof(element_t), block_size>;

		element_t* allocate(size_t n) {
			IRIS_ASSERT(n == 1);
			return reinterpret_cast<element_t*>(allocator_t::allocate_safe());
		}

		template <typename... args_t>
		void construct(element_t* p, args_t&&... args) {
			new (p) element_t(std::forward<args_t>(args)...);
		}

		void destroy(element_t* p) {
			p->~element_t();
		}

		void deallocate(element_t* p, size_t n) {
			IRIS_ASSERT(n == 1);
			allocator_t::deallocate_safe(p);
		}
	};

	template <typename element_t, size_t allocator_block_size = default_block_size>
	struct iris_relaxed_object_allocator_t : iris_allocator_t<sizeof(element_t), allocator_block_size> {
		static constexpr size_t block_size = allocator_block_size;
		using value_type = element_t;
		using pointer = element_t*;
		using const_pointer = const element_t*;
		using reference = element_t&;
		using const_reference = const element_t&;
		using size_type = size_t;
		using difference_type = ptrdiff_t;
		using propagate_on_container_move_assignment = std::true_type;
		using is_always_equal = std::false_type;

		template <typename morph_t>
		struct rebind { using other = iris_object_allocator_t<morph_t, block_size>; };
		using allocator_t = iris_allocator_t<sizeof(element_t), block_size>;

		element_t* allocate(size_t n) {
			IRIS_ASSERT(n == 1);
			return reinterpret_cast<element_t*>(allocator_t::allocate_unsafe());
		}

		template <typename... args_t>
		void construct(element_t* p, args_t&&... args) {
			new (p) element_t(std::forward<args_t>(args)...);
		}

		void destroy(element_t* p) {
			p->~element_t();
		}

		void deallocate(element_t* p, size_t n) {
			IRIS_ASSERT(n == 1);
			allocator_t::deallocate_unsafe(p);
		}
	};

	template <typename element_t, size_t _block_size = default_block_size>
	struct iris_shared_object_allocator_t {
		static constexpr size_t block_size = _block_size;
		using value_type = element_t;
		using pointer = element_t*;
		using const_pointer = const element_t*;
		using reference = element_t&;
		using const_reference = const element_t&;
		using size_type = size_t;
		using difference_type = ptrdiff_t;
		using propagate_on_container_move_assignment = std::true_type;
		using is_always_equal = std::false_type;

		template <typename morph_t>
		struct rebind { using other = iris_shared_object_allocator_t<morph_t, block_size>; };
		using allocator_t = iris_allocator_t<sizeof(element_t)>;

		iris_shared_object_allocator_t(allocator_t& alloc) noexcept : allocator(alloc) {}

		element_t* allocate(size_t n) {
			IRIS_ASSERT(n == 1);
			return reinterpret_cast<element_t*>(allocator.allocate_safe());
		}

		template <typename... args_t>
		void construct(element_t* p, args_t&&... args) {
			new (p) element_t(std::forward<args_t>(args)...);
		}

		void destroy(element_t* p) {
			p->~element_t();
		}

		void deallocate(element_t* p, size_t n) {
			IRIS_ASSERT(n == 1);
			allocator.deallocate_unsafe(p);
		}

		allocator_t& allocator;
	};

	template <typename element_t, size_t _block_size = default_block_size>
	struct iris_relaxed_shared_object_allocator_t {
		static constexpr size_t block_size = _block_size;
		using value_type = element_t;
		using pointer = element_t*;
		using const_pointer = const element_t*;
		using reference = element_t&;
		using const_reference = const element_t&;
		using size_type = size_t;
		using difference_type = ptrdiff_t;
		using propagate_on_container_move_assignment = std::true_type;
		using is_always_equal = std::false_type;

		template <typename morph_t>
		struct rebind { using other = iris_shared_object_allocator_t<morph_t, block_size>; };
		using allocator_t = iris_allocator_t<sizeof(element_t)>;

		iris_relaxed_shared_object_allocator_t(allocator_t& alloc) noexcept : allocator(alloc) {}

		element_t* allocate(size_t n) {
			IRIS_ASSERT(n == 1);
			return reinterpret_cast<element_t*>(allocator.allocate_unsafe());
		}

		template <typename... args_t>
		void construct(element_t* p, args_t&&... args) {
			new (p) element_t(std::forward<args_t>(args)...);
		}

		void destroy(element_t* p) {
			p->~element_t();
		}

		void deallocate(element_t* p, size_t n) {
			IRIS_ASSERT(n == 1);
			allocator.deallocate_unsafe(p);
		}

		allocator_t& allocator;
	};

	template <typename element_t, size_t alloc_size = default_block_size, size_t page_size = default_page_size / default_block_size, template <typename...> class single_allocator_t = std::allocator>
	struct iris_block_allocator_t : single_allocator_t<element_t> {
		using value_type = element_t;
		using pointer = element_t*;
		using const_pointer = const element_t*;
		using reference = element_t&;
		using const_reference = const element_t&;
		using size_type = size_t;
		using difference_type = ptrdiff_t;
		using propagate_on_container_move_assignment = std::true_type;
		using is_always_equal = std::false_type;

		static constexpr size_t block_size = alloc_size;

		template <typename morph_t>
		struct rebind { using other = iris_block_allocator_t<morph_t, block_size, page_size>; };
		using allocator_t = iris_root_allocator_t<block_size, page_size>;

		element_t* allocate(size_t n) {
			if (n == block_size / sizeof(element_t)) {
				return reinterpret_cast<element_t*>(allocator_t::get().allocate());
			} else {
				IRIS_ASSERT(n == 1);
				return single_allocator_t<element_t>::allocate(1);
			}
		}

		template <typename... args_t>
		void construct(element_t* p, args_t&&... args) {
			new (p) element_t(std::forward<args_t>(args)...);
		}

		void destroy(element_t* p) {
			p->~type();
		}

		void deallocate(element_t* p, size_t n) {
			if (n == block_size / sizeof(element_t)) {
				allocator_t::get().deallocate(p);
			} else {
				IRIS_ASSERT(n == 1);
				single_allocator_t<element_t>::deallocate(p, 1);
			}
		}
	};

	// iris_default_block_allocator is the most useful allocator
	// and we make it as the default parameter type for these containers
	template <typename element_t>
	using iris_default_block_allocator_t = iris_block_allocator_t<element_t>;

	template <typename element_t>
	using iris_default_object_allocator_t = iris_object_allocator_t<element_t>;

	template <typename element_t>
	using iris_default_relaxed_object_allocator_t = iris_relaxed_object_allocator_t<element_t>;

	template <typename element_t>
	using iris_default_shared_object_allocator_t = iris_shared_object_allocator_t<element_t>;

	template <typename element_t>
	using iris_default_relaxed_shared_object_allocator_t = iris_relaxed_shared_object_allocator_t<element_t>;

	template <typename element_t, template <typename...> class allocator_t>
	struct iris_extract_block_size {
		static constexpr size_t value = allocator_t<element_t>::block_size;
	};

	template <typename element_t>
	struct iris_extract_block_size<element_t, std::allocator> {
		static constexpr size_t value = default_block_size;
	};

	namespace impl {
		template <typename element_t>
		struct alignas(alignof(element_t)) element_slot_t {
			uint8_t data[sizeof(element_t)];
		};
	}

	// basic queue structure for tasks or stream-based data structures
	template <typename value_t, template <typename...> class allocator_t = iris_default_block_allocator_t, bool enable_memory_fence = true, template <typename...> class _allocator_t = allocator_t>
	struct iris_queue_t : private allocator_t<impl::element_slot_t<value_t>>, protected enable_in_out_fence_t<> {
		using element_t = value_t;
		using storage_t = impl::element_slot_t<element_t>;
		using node_allocator_t = _allocator_t<storage_t>;

		static constexpr size_t block_size = iris_extract_block_size<element_t, _allocator_t>::value;
		static_assert(block_size >= sizeof(element_t), "block_size is too small!");
		static constexpr size_t element_count = block_size / sizeof(element_t);
		static constexpr bool element_count_pow_two = (size_t(1) << iris_log2<element_count>::value) == element_count;
		static constexpr size_t counter_limit = element_count * (size_t(1) << (sizeof(size_t) * 8 - 1 - iris_log2<element_count>::value));
		static_assert((counter_limit & (size_t(1) << (sizeof(size_t) * 8 - 1))) != 0, "not max limit!");

		explicit iris_queue_t(const node_allocator_t& alloc, size_t init_count = 0) noexcept(noexcept(std::declval<node_allocator_t>().allocate(1))) : node_allocator_t(alloc), push_count(init_count), pop_count(init_count), ring_buffer(node_allocator_t::allocate(element_count)) {}

		explicit iris_queue_t(size_t init_count = 0) noexcept(noexcept(std::declval<node_allocator_t>().allocate(1))) : push_count(init_count), pop_count(init_count), ring_buffer(node_allocator_t::allocate(element_count)) {
			new (ring_buffer) storage_t();
		}

		iris_queue_t(const iris_queue_t& rhs) = delete;
		iris_queue_t(iris_queue_t&& rhs) noexcept : node_allocator_t(std::move(static_cast<node_allocator_t&>(rhs))), ring_buffer(rhs.ring_buffer), push_count(rhs.push_count), pop_count(rhs.pop_count) {
			rhs.ring_buffer = nullptr;
		}

		iris_queue_t& operator = (const iris_queue_t& rhs) = delete;
		iris_queue_t& operator = (iris_queue_t&& rhs) noexcept {
			if (this != &rhs) {
				static_cast<node_allocator_t&>(*this) = std::move(static_cast<node_allocator_t&>(rhs));
				ring_buffer = rhs.ring_buffer;
				push_count = rhs.push_count;
				pop_count = rhs.pop_count;

				rhs.ring_buffer = nullptr;
			}

			return *this;
		}

		~iris_queue_t() noexcept {
			if (ring_buffer != nullptr) {
				for_each([](element_t& e) noexcept { e.~element_t(); });
				node_allocator_t::deallocate(ring_buffer, element_count);
			}
		}

		template <typename input_element_t>
		element_t* push(input_element_t&& t) noexcept(noexcept(element_t(std::forward<input_element_t>(t)))) {
			auto in_guard = in_fence();

			if (full()) {
				return nullptr; // this queue is full, push failed
			}

			element_t* result = new (&ring_buffer[push_count % element_count]) element_t(std::forward<input_element_t>(t));

			// place a thread_fence here to ensure that change of ring_buffer[push_index]
			//   must be visible to other threads after push_count being updated.
			if /* constexpr */ (enable_memory_fence) {
				std::atomic_thread_fence(std::memory_order_release);
			}

			push_count = step_counter(push_count, 1);
			return result;
		}

		element_t& top() noexcept {
			auto guard = out_fence();
			IRIS_ASSERT(!empty()); // must checked before calling me (memory fence acquire implicited)
			return *reinterpret_cast<element_t*>(&ring_buffer[pop_count % element_count]);
		}

		const element_t& top() const noexcept {
			auto guard = out_fence();
			IRIS_ASSERT(!empty()); // must checked before calling me (memory fence acquire implicited)
			return *reinterpret_cast<const element_t*>(&ring_buffer[pop_count % element_count]);
		}

		element_t& get(size_t index) noexcept {
			auto guard = out_fence();
			IRIS_ASSERT(!empty()); // must checked before calling me (memory fence acquire implicited)
			return *reinterpret_cast<element_t*>(&ring_buffer[index % element_count]);
		}

		const element_t& get(size_t index) const noexcept {
			auto guard = out_fence();
			IRIS_ASSERT(!empty()); // must checked before calling me (memory fence acquire implicited)
			return *reinterpret_cast<const element_t*>(&ring_buffer[index % element_count]);
		}

		template <typename iterator_t>
		iterator_t push(iterator_t from, iterator_t to) noexcept(noexcept(element_t(*from))) {
			auto guard = in_fence();
			if (full()) {
				return from;
			}

			iterator_t org = from;
			size_t windex = push_count % element_count;
			size_t rindex = pop_count % element_count;

			if (rindex <= windex) {
				while (from != to && windex < element_count) {
					new (&ring_buffer[windex++]) element_t(*from++);
				}

				windex = 0;
			}

			while (from != to && windex < rindex) {
				new (&ring_buffer[windex++]) element_t(*from++);
			}

			// place a thread_fence here to ensure that change of ring_buffer[windex]
			//   must be visible to other threads after push_count being updated.
			if /* constexpr */ (enable_memory_fence) {
				std::atomic_thread_fence(std::memory_order_release);
			}

			push_count = step_counter(push_count, from - org);
			return from;
		}

		template <typename iterator_t>
		iterator_t pop(iterator_t from, iterator_t to) noexcept {
			auto guard = out_fence();
			if (empty()) {
				return from;
			}

			iterator_t org = from;
			size_t windex = push_count % element_count;
			size_t rindex = pop_count % element_count;

			if (windex <= rindex) {
				while (from != to && rindex < element_count) {
					element_t& element = *reinterpret_cast<element_t*>(&ring_buffer[rindex++]);
					*from++ = std::move(element);
					element.~element_t();
				}

				rindex = 0;
			}

			while (from != to && rindex < windex) {
				element_t& element = *reinterpret_cast<element_t*>(&ring_buffer[rindex++]);
				*from++ = std::move(element);
				element.~element_t();
			}

			// place a thread_fence here to ensure that change of ring_buffer[rindex]
			//   must be visible to other threads after pop_count being updated.
			if /* constexpr */ (enable_memory_fence) {
				std::atomic_thread_fence(std::memory_order_release);
			}

			pop_count = step_counter(pop_count, from - org);
			return from;
		}

		void pop() noexcept {
			auto guard = out_fence();

			IRIS_ASSERT(!empty());  // must checked before calling me (memory fence acquire implicited)
			reinterpret_cast<element_t*>(&ring_buffer[pop_count % element_count])->~element_t();

			// place a thread_fence here to ensure that change of ring_buffer[pop_count]
			//   must be visible to other threads after pop_count being updated.
			if /* constexpr */ (enable_memory_fence) {
				std::atomic_thread_fence(std::memory_order_release);
			}

			pop_count = step_counter(pop_count, 1);
		}

		size_t pop(size_t n) noexcept {
			auto guard = out_fence();

			size_t m = std::min(n, size());
			size_t rindex = pop_count % element_count;
			size_t k = m;
			while (rindex < element_count && k != 0) {
				reinterpret_cast<element_t*>(&ring_buffer[rindex++])->~element_t();
				k--;
			}

			if (k != 0) {
				rindex = 0;
				do {
					reinterpret_cast<element_t*>(&ring_buffer[rindex++])->~element_t();
				} while (--k != 0);
			}

			// place a thread_fence here to ensure that change of ring_buffer[rindex]
			//   must be visible to other threads after pop_count being updated.
			if /* constexpr */ (enable_memory_fence) {
				std::atomic_thread_fence(std::memory_order_release);
			}

			pop_count = step_counter(pop_count, (ptrdiff_t)m);
			return n - m;
		}

		bool full() const noexcept {
			return step_counter(pop_count, (ptrdiff_t)element_count) == push_count;
		}

		bool empty() const noexcept {
			if /* constexpr */ (enable_memory_fence) {
				bool result = pop_count == push_count;
				if (!result) {
					// not sure if it is needed, but the kfifo of linux kernel has missed it.
					std::atomic_thread_fence(std::memory_order_acquire);
				}
				
				return result;
			} else {
				return pop_count == push_count;
			}
		}

		size_t size() const noexcept {
			ptrdiff_t diff = diff_counter(push_count, pop_count);
			IRIS_ASSERT(diff >= 0);
			return iris_verify_cast<size_t>(diff);
		}

		// returns remaining possible available size with specified alignment
		size_t pack_size(size_t alignment) const noexcept {
			IRIS_ASSERT(element_count >= alignment);
			size_t index = push_count + iris_verify_cast<size_t>((alignment - iris_get_alignment(push_count % element_count)) & (alignment - 1));

			return iris_verify_cast<size_t>(std::min(std::max(index, pop_count + element_count) - index, element_count - (index % element_count)));
		}

		template <typename operation_t>
		typename std::enable_if<std::is_constructible<std::function<void(element_t*, size_t)>, operation_t>::value>::type for_each(operation_t&& op) noexcept(noexcept(op(std::declval<element_t*>(), 0))) {
			auto guard = out_fence();

			if (empty())
				return;

			size_t windex = push_count % element_count;
			size_t rindex = pop_count % element_count;

			if (rindex >= windex) {
				size_t n = element_count - rindex;
				if (n != 0) {
					op(reinterpret_cast<element_t*>(ring_buffer) + rindex, n);
				}

				rindex = 0;
			}

			if (rindex < windex) {
				op(reinterpret_cast<element_t*>(ring_buffer) + rindex, windex - rindex);
			}
		}

		template <typename operation_t>
		typename std::enable_if<std::is_constructible<std::function<void(element_t&)>, operation_t>::value>::type for_each(operation_t&& op) noexcept(noexcept(op(std::declval<element_t&>()))) {
			auto guard = out_fence();

			if (empty())
				return;

			size_t windex = push_count % element_count;
			size_t rindex = pop_count % element_count;

			if (rindex >= windex) {
				while (rindex < element_count) {
					op(*reinterpret_cast<element_t*>(&ring_buffer[rindex++]));
				}

				rindex = 0;
			}

			while (rindex < windex) {
				op(*reinterpret_cast<element_t*>(&ring_buffer[rindex++]));
			}
		}

		template <typename operation_t>
		typename std::enable_if<std::is_constructible<std::function<void(const element_t*, size_t)>, operation_t>::value>::type for_each(operation_t&& op) const noexcept(noexcept(op(std::declval<const element_t*>(), 0))) {
			auto guard = out_fence();

			if (empty())
				return;

			size_t windex = push_count % element_count;
			size_t rindex = pop_count % element_count;

			if (rindex >= windex) {
				size_t n = element_count - rindex;
				if (n != 0) {
					op(reinterpret_cast<const element_t*>(ring_buffer) + rindex, n);
				}

				rindex = 0;
			}

			if (rindex < windex) {
				op(reinterpret_cast<const element_t*>(ring_buffer) + rindex, windex - rindex);
			}
		}

		template <typename operation_t>
		typename std::enable_if<std::is_constructible<std::function<void(const element_t&)>, operation_t>::value>::type for_each(operation_t&& op) const noexcept(noexcept(op(std::declval<const element_t&>()))) {
			auto guard = out_fence();

			if (empty())
				return;

			size_t windex = push_count % element_count;
			size_t rindex = pop_count % element_count;

			if (rindex >= windex) {
				while (rindex < element_count) {
					op(*reinterpret_cast<const element_t*>(&ring_buffer[rindex++]));
				}

				rindex = 0;
			}

			while (rindex < windex) {
				op(*reinterpret_cast<const element_t*>(&ring_buffer[rindex++]));
			}
		}

		element_t* allocate(size_t count, size_t alignment) {
			auto guard = in_fence();

			IRIS_ASSERT(count >= alignment);
			IRIS_ASSERT(count <= element_count);
			// make alignment
			size_t push_index = push_count % element_count;
			count += iris_verify_cast<size_t>(alignment - iris_get_alignment(push_index)) & (alignment - 1);
			if (count > element_count - size()) return nullptr;

			size_t next_index = push_index + count;
			if (count != 1 && next_index > element_count) return nullptr; // non-continous!

			size_t ret_index = push_index;
			for (size_t i = push_index; i != next_index; i++) {
				new (&ring_buffer[i]) element_t();
			}

			// place a thread_fence here to ensure that change of ring_buffer[i]
			//   must be visible to other threads after pop_count being updated.
			if /* constexpr */ (enable_memory_fence) {
				std::atomic_thread_fence(std::memory_order_release);
			}

			push_count = step_counter(push_count, (ptrdiff_t)count);
			return reinterpret_cast<element_t*>(ring_buffer + ret_index);
		}

		void deallocate(size_t count, size_t alignment) noexcept {
			auto guard = out_fence();
			IRIS_ASSERT(count >= alignment);
			IRIS_ASSERT(count <= element_count);

			// make alignment
			size_t pop_index = pop_count % element_count;
			count += iris_verify_cast<size_t>(alignment - iris_get_alignment(pop_index)) & (alignment - 1);
			IRIS_ASSERT(count <= size());

			size_t next_index = pop_index + count;

			for (size_t i = pop_index; i != next_index; i++) {
				reinterpret_cast<element_t*>(&ring_buffer[i])->~element_t();
			}

			// place a thread_fence here to ensure that change of ring_buffer[i]
			//   must be visible to other threads after pop_count being updated.
			if /* constexpr */ (enable_memory_fence) {
				std::atomic_thread_fence(std::memory_order_release);
			}

			pop_count = step_counter(pop_count, (ptrdiff_t)count);
		}

		void reset(size_t init_count) noexcept {
			auto in_guard = in_fence();

			if (ring_buffer != nullptr) {
				for_each([](element_t& e) { e.~element_t(); });
			}

			auto out_guard = out_fence();

			// place a thread_fence here to ensure that change of ring_buffer
			//   must be visible to other threads after pop_count & pop_count being updated.
			if /* constexpr */ (enable_memory_fence) {
				std::atomic_thread_fence(std::memory_order_release);
			}

			push_count = pop_count = init_count;
		}

		struct iterator {
			using difference_type = ptrdiff_t;
			using value_type = element_t;
			using reference = element_t&;
			using pointer = element_t*;
			using iterator_category = std::forward_iterator_tag;

			iterator(iris_queue_t* q, size_t i) noexcept : queue(q), index(i) {}

			iterator& operator ++ () noexcept {
				index = step_counter(index, 1);
				return *this;
			}

			iterator operator ++ (int) noexcept {
				return iterator(*queue, step_counter(index, 1));
			}

			iterator& operator += (ptrdiff_t count) noexcept {
				index = step_counter(index, count);
				return *this;
			}

			iterator operator + (ptrdiff_t count) const noexcept {
				return iterator(*queue, step_counter(index, count));
			}

			ptrdiff_t operator - (const iterator& it) const noexcept {
				return diff_counter(index, it.index);
			}

			bool operator == (const iterator& rhs) const noexcept {
				return index == rhs.index;
			}

			bool operator != (const iterator& rhs) const noexcept {
				return index != rhs.index;
			}

			element_t* operator -> () const noexcept {
				return reinterpret_cast<element_t*>(&queue->ring_buffer[index % element_count]);
			}

			element_t& operator * () const noexcept {
				return *reinterpret_cast<element_t*>(&queue->ring_buffer[index % element_count]);
			}

			friend struct const_iterator;

		private:
			size_t index;
			iris_queue_t* queue;
		};

		friend struct iterator;

		struct const_iterator {
			using difference_type = ptrdiff_t;
			using value_type = const element_t;
			using reference = const element_t&;
			using pointer = const element_t*;
			using iterator_category = std::forward_iterator_tag;

			const_iterator(const iris_queue_t* q, size_t i) noexcept : queue(q), index(i) {}

			const_iterator& operator ++ () noexcept {
				index = step_counter(index, 1);
				return *this;
			}

			const_iterator operator ++ (int) noexcept {
				return const_iterator(*queue, step_counter(index, 1));
			}

			const_iterator& operator += (ptrdiff_t count) noexcept {
				index = step_counter(index, count);
				return *this;
			}

			const_iterator operator + (ptrdiff_t count) const noexcept {
				return const_iterator(*queue, step_counter(index, count));
			}

			ptrdiff_t operator - (const const_iterator& it) const noexcept {
				return diff_counter(index, it.index);
			}

			bool operator == (const const_iterator& rhs) const noexcept {
				return index == rhs.index;
			}

			bool operator != (const const_iterator& rhs) const noexcept {
				return index != rhs.index;
			}

			const element_t* operator -> () const noexcept {
				return reinterpret_cast<const element_t*>(&queue->ring_buffer[index % element_count]);
			}

			const element_t& operator * () const noexcept {
				return *reinterpret_cast<const element_t*>(&queue->ring_buffer[index % element_count]);
			}

		private:
			size_t index;
			const iris_queue_t* queue;
		};

		friend struct const_iterator;

		iterator begin() noexcept {
			return iterator(this, pop_count);
		}

		iterator end() noexcept {
			return iterator(this, push_count);
		}

		const_iterator begin() const noexcept {
			return const_iterator(this, pop_count);
		}

		const_iterator end() const noexcept {
			return const_iterator(this, push_count);
		}

		size_t begin_index() const noexcept {
			return pop_count;
		}

		size_t end_index() const noexcept {
			return push_count;
		}

		// why step_counter and diff_count except simple mod ?
		// if element_count_pow_two is false (e.g. max element count = 3), the overflow of integer index (2^n) is not compatible with mod operation
		static size_t step_counter(size_t count, ptrdiff_t delta) {
			size_t result = count + delta;
			if /* constexpr */ (!element_count_pow_two) {
				result = result >= counter_limit ? iris_verify_cast<size_t>(result - counter_limit) : result; // cmov is faster than mod
			}

			return result;
		}

		static ptrdiff_t diff_counter(size_t lhs, size_t rhs) {
			if /* constexpr */ (element_count_pow_two) {
				return (ptrdiff_t)lhs - (ptrdiff_t)rhs;
			} else {
				ptrdiff_t diff = lhs + counter_limit - rhs;
				return diff >= counter_limit ? diff - counter_limit : diff;  // cmov is faster than mod
			}
		}

	protected:
		size_t push_count; // write count
		size_t pop_count; // read count
		storage_t* ring_buffer;
	};

	namespace impl {
		template <typename element_t, template <typename...> class allocator_t, bool enable_memory_fence>
		using sub_queue_t = iris_queue_t<element_t, allocator_t, enable_memory_fence>;

		template <typename element_t, template <typename...> class allocator_t, bool enable_memory_fence, template <typename...> class debug_allocator_t = allocator_t>
		struct node_t : sub_queue_t<element_t, debug_allocator_t, enable_memory_fence> {
			explicit node_t(size_t init_count) : sub_queue_t<element_t, debug_allocator_t, enable_memory_fence>(init_count), next(nullptr) {}
			node_t* next; // chain next queue
		};
	}

	// chain kfifos to make variant capacity.
	// debug_allocator_t is for bypassing vs 2015's compiler bug.
	template <typename value_t, template <typename...> class allocator_t = iris_default_block_allocator_t, template <typename...> class top_allocator_t = allocator_t, bool enable_memory_fence = true, template <typename...> class debug_allocator_t = allocator_t>
	struct iris_queue_list_t : private top_allocator_t<impl::node_t<value_t, allocator_t, enable_memory_fence>>, protected enable_in_out_fence_t<> {
		using element_t = value_t;
		using node_t = impl::node_t<element_t, debug_allocator_t, enable_memory_fence>;
		using node_allocator_t = top_allocator_t<node_t>;

		static constexpr size_t block_size = iris_extract_block_size<element_t, debug_allocator_t>::value;
		static constexpr size_t element_count = block_size / sizeof(element_t);

		// do not copy this structure, only to move
		iris_queue_list_t(const iris_queue_list_t& rhs) = delete;
		iris_queue_list_t& operator = (const iris_queue_list_t& rhs) = delete;

		explicit iris_queue_list_t(const node_allocator_t& allocator) noexcept(noexcept(std::declval<node_allocator_t>().allocate(1))) : node_allocator_t(allocator), push_head(nullptr), pop_head(nullptr) {
			node_t* p = node_allocator_t::allocate(1);
			new (p) node_t(0);
			push_head = pop_head = p;
			iterator_counter = element_count;
		}

		iris_queue_list_t() noexcept(noexcept(std::declval<node_allocator_t>().allocate(1))) : push_head(nullptr), pop_head(nullptr) {
			node_t* p = node_allocator_t::allocate(1);
			new (p) node_t(0);
			push_head = pop_head = p;
			iterator_counter = element_count;
		}

		iris_queue_list_t(iris_queue_list_t&& rhs) noexcept {
			IRIS_ASSERT(static_cast<node_allocator_t&>(*this) == static_cast<node_allocator_t&>(rhs));
			push_head = rhs.push_head;
			pop_head = rhs.pop_head;
			iterator_counter = rhs.iterator_counter;

			rhs.push_head = nullptr;
			rhs.pop_head = nullptr;
		}

		iris_queue_list_t& operator = (iris_queue_list_t&& rhs) noexcept {
			if (this != &rhs) {
				IRIS_ASSERT(static_cast<node_allocator_t&>(*this) == static_cast<node_allocator_t&>(rhs));
				// just swap pointers.
				std::swap(push_head, rhs.push_head);
				std::swap(pop_head, rhs.pop_head);
				std::swap(iterator_counter, rhs.iterator_counter);
			}

			return *this;
		}

		~iris_queue_list_t() noexcept {
			if (pop_head != nullptr) {
				node_t* q = pop_head;
				while (q != nullptr) {
					node_t* p = q;
					q = q->next;

					p->~node_t();
					node_allocator_t::deallocate(p, 1);
				}
			}
		}
	
		template <typename input_element_t>
		element_t* push(input_element_t&& t) noexcept(noexcept(std::declval<node_t>().push(std::forward<input_element_t>(t)))) {
			auto guard = in_fence();

			if (push_head->full()) {
				node_t* p = node_allocator_t::allocate(1);
				new (p) node_t(iterator_counter);
				iterator_counter = node_t::step_counter(iterator_counter, element_count);
				element_t* w = p->push(std::forward<input_element_t>(t));

				// chain new node_t at head.
				push_head->next = p;

				if (enable_memory_fence) {
					std::atomic_thread_fence(std::memory_order_release);
				}

				push_head = p;
				return w;
			} else {
				return push_head->push(std::forward<input_element_t>(t));
			}
		}

		template <typename iterator_t>
		iterator_t push(iterator_t from, iterator_t to) noexcept(noexcept(std::declval<node_t>().push(from, to))) {
			auto guard = in_fence();

			while (true) {
				iterator_t next = push_head->push(from, to);
				if (next == to) {
					return next;
				}

				// full
				node_t* p = node_allocator_t::allocate(1);
				new (p) node_t(iterator_counter);
				iterator_counter = node_t::step_counter(iterator_counter, element_count);
				next = p->push(from, to);

				// chain new node_t at head.
				push_head->next = p;

				if (enable_memory_fence) {
					std::atomic_thread_fence(std::memory_order_release);
				}

				push_head = p;
				from = next;
			}
		}

		size_t end_index() const noexcept {
			return push_head->end_index();
		}

		size_t begin_index() const noexcept {
			return pop_head->begin_index();
		}

		const element_t& get(size_t index) const noexcept {
			auto guard = out_fence();

			for (const node_t* p = pop_head; p != push_head; p = p->next) {
				if (p->end_index() - index > 0) {
					return p->get(index);
				}
			}

			return push_head->get(index);
		}

		element_t& get(size_t index) noexcept {
			auto guard = out_fence();
			for (node_t* p = pop_head; p != push_head; p = p->next) {
				if (p->end_index() - index > 0) {
					return p->get(index);
				}
			}

			return push_head->get(index);
		}

		template <typename iterator_t>
		iterator_t pop(iterator_t from, iterator_t to) noexcept {
			auto guard = out_fence();

			while (true) {
				iterator_t next = pop_head->pop(from, to);
				if (from == next) {
					cleanup_empty();

					if (next == to)
						return next;
				}

				from = next;
			}
		}

		bool cleanup_empty() noexcept {
			// current queue is empty, remove it from list.
			if (pop_head->empty() && pop_head != push_head) {
				node_t* p = pop_head;
				pop_head = pop_head->next;

				p->~node_t();
				node_allocator_t::deallocate(p, 1);
				return true;
			} else {
				return false;
			}
		}

		element_t& top() noexcept {
			auto guard = out_fence();
			if /* constexpr */ (enable_memory_fence) {
				cleanup_empty();
			}

			return pop_head->top();
		}

		const element_t& top() const noexcept {
			auto guard = out_fence();
			if /* constexpr */ (enable_memory_fence) {
				const_cast<iris_queue_list_t*>(this)->cleanup_empty();
			}

			return pop_head->top();
		}

		void pop() noexcept {
			auto guard = out_fence();

			if /* constexpr */ (enable_memory_fence) {
				cleanup_empty();
			}

			pop_head->pop();
			cleanup_empty();
		}

		size_t pop(size_t n) noexcept {
			auto guard = out_fence();

			if /* constexpr */ (enable_memory_fence) {
				cleanup_empty();
			}

			while (n != 0) {
				size_t m = std::min(n, pop_head->size());
				pop_head->pop(m);
				n -= m;

				// current queue is empty, remove it from list.
				if (!cleanup_empty()) {
					break;
				}
			}

			return n;
		}

		bool empty() const noexcept {
			if (pop_head->empty()) {
				if (pop_head != push_head) {
					if /* constexpr */ (enable_memory_fence) {
						std::atomic_thread_fence(std::memory_order_acquire);
					}

					return push_head->empty();
				} else {
					return true;
				}
			} else {
				return false;
			}
		}

		bool probe(size_t request_size) const noexcept {
			size_t counter = 0;
			// sum up all sub queues
			for (node_t* p = pop_head; p != nullptr; p = p->next) {
				counter += p->size();
				if (counter >= request_size) {
					return true;
				}
			}

			return false;
		}

		size_t size() const noexcept {
			size_t counter = 0;
			// sum up all sub queues
			for (node_t* p = pop_head; p != nullptr; p = p->next) {
				counter += p->size();
			}

			return counter;
		}

		// returns pack size in current node
		size_t pack_size(size_t alignment) const noexcept {
			size_t v = push_head->pack_size(alignment);
			return v == 0 ? full_pack_size() : v;
		}

		static constexpr size_t full_pack_size() noexcept {
			return element_count;
		}

		// allocate continuous array from queue_list
		// may lead holes in low-level storage if current node is not enough
		std::pair<element_t*, size_t> allocate(size_t count, size_t alignment) {
			auto guard = in_fence();

			element_t* address;
			while ((address = push_head->allocate(count, alignment)) == nullptr) {
				if (push_head->next == nullptr) {
					node_t* p = node_allocator_t::allocate(1);
					new (p) node_t(iterator_counter);
					iterator_counter = node_t::step_counter(iterator_counter, element_count);

					address = p->allocate(count, alignment); // must success
					IRIS_ASSERT(address != nullptr);

					push_head->next = p;
					push_head = p;
					break;
				}

				push_head = push_head->next;
			}

			return std::make_pair(address, iterator_counter + push_head->size() - count);
		}

		// must call deallocate() exactly the same order and parameters as calling allocate()
		void deallocate(size_t size, size_t alignment) noexcept {
			auto guard = out_fence();
			if /* constexpr */ (enable_memory_fence) {
				cleanup_empty();
			}

			pop_head->deallocate(size, alignment);
			cleanup_empty();
		}

		// reset all nodes
		void reset(size_t reserved) noexcept {
			auto in_guard = in_fence();
			auto out_guard = out_fence();

			node_t* p = push_head = pop_head;
			p->reset(0); // always reserved
			iterator_counter = element_count;

			node_t* q = p;
			p = p->next;

			while (p != nullptr && iterator_counter < reserved) {
				p->reset(iterator_counter);
				iterator_counter = node_t::step_counter(iterator_counter, element_count);
				q = p;
				p = p->next;
			}

			while (p != nullptr) {
				node_t* t = p;
				p = p->next;

				t->~node_t();
				node_allocator_t::deallocate(t, 1);
			}

			q->next = nullptr;
		}

		void clear() noexcept {
			reset(0);
		}

		template <typename operation_t>
		void for_each(operation_t&& op) noexcept(noexcept(std::declval<node_t>().for_each(op))) {
			auto guard = out_fence();
			for (node_t* p = pop_head; p != nullptr; p = p->next) {
				p->for_each(op);
			}
		}

		template <typename operation_t>
		void for_each(operation_t&& op) const noexcept(noexcept(std::declval<node_t>().for_each(op))) {
			auto guard = out_fence();
			for (node_t* p = pop_head; p != nullptr; p = p->next) {
				p->for_each(op);
			}
		}

		template <typename operation_t>
		void for_each_queue(operation_t&& op) noexcept(noexcept(op(std::declval<iris_queue_list_t>().pop_head))) {
			auto guard = out_fence();
			for (node_t* p = pop_head; p != nullptr; p = p->next) {
				op(p);
			}
		}

		template <typename operation_t>
		void for_each_queue(operation_t&& op) const noexcept(noexcept(op(std::declval<iris_queue_list_t>().pop_head))) {
			auto guard = out_fence();
			for (const node_t* p = pop_head; p != nullptr; p = p->next) {
				op(p);
			}
		}

		struct iterator {
			using difference_type = ptrdiff_t;
			using value_type = element_t;
			using reference = element_t&;
			using pointer = element_t*;
			using iterator_category = std::forward_iterator_tag;

			iterator(node_t* n = nullptr, size_t i = 0u) noexcept : current_node(n), it(i) {}

			iterator& operator ++ () noexcept {
				step();
				return *this;
			}

			iterator operator ++ (int) noexcept {
				iterator r = *this;
				step();

				return r;
			}

			iterator& operator += (ptrdiff_t count) noexcept {
				iterator p = *this + count;
				*this = std::move(p);
				return *this;
			}

			iterator operator + (ptrdiff_t count) const noexcept {
				node_t* n = current_node;
				size_t sub = it;
				while (true) {
					ptrdiff_t c = node_t::diff_counter(n->end_index(), sub);
					if (count >= c) {
						count -= c;
						node_t* t = n->next;
						if (t == nullptr) {
							return iterator(n, n->end_index());
						}

						n = t;
						sub = t->begin_index();
					} else {
						return iterator(n, node_t::step_counter(n->begin_index(), count));
					}
				}
			}

			ptrdiff_t operator - (const iterator& rhs) const noexcept {
				node_t* t = rhs.current_node;
				size_t sub = rhs.it;
				ptrdiff_t count = 0;

				while (t != current_node) {
					count += node_t::diff_counter(t->end_index(), sub);
					t = t->next;
					sub = t->begin_index();
				}

				count += node_t::diff_counter(it, sub);
				return count;
			}

			bool operator == (const iterator& rhs) const noexcept {
				return /* current_node == rhs.current_node && */ it == rhs.it;
			}

			bool operator != (const iterator& rhs) const noexcept {
				return /* current_node != rhs.current_node || */ it != rhs.it;
			}

			element_t* operator -> () const noexcept {
				return &current_node->get(it);
			}

			element_t& operator * () const noexcept {
				return current_node->get(it);
			}

			bool step() noexcept {
				it = node_t::step_counter(it, 1);
				if (it == current_node->end_index()) {
					node_t* n = current_node->next;
					if (n == nullptr) {
						return false;
					}

					current_node = n;
					it = n->begin_index();
				}

				return true;
			}

			friend struct const_iterator;

		private:
			size_t it;
			node_t* current_node;
		};

		friend struct iterator;

		struct const_iterator {
			using difference_type = ptrdiff_t;
			using value_type = element_t;
			using reference = element_t&;
			using pointer = element_t*;
			using iterator_category = std::forward_iterator_tag;

			const_iterator(const node_t* n = nullptr, size_t i = 0u) noexcept : current_node(n), it(i) {}

			const_iterator& operator ++ () noexcept {
				step();
				return *this;
			}

			const_iterator operator ++ (int) noexcept {
				const_iterator r = *this;
				step();

				return r;
			}

			const_iterator& operator += (ptrdiff_t count) noexcept {
				const_iterator p = *this + count;
				*this = std::move(p);
				return *this;
			}

			const_iterator operator + (ptrdiff_t count) const noexcept {
				node_t* n = current_node;
				size_t sub = it;
				while (true) {
					ptrdiff_t c = node_t::diff_counter(n->end_index(), sub);
					if (count >= c) {
						count -= c;
						node_t* t = n->next;
						if (t == nullptr) {
							return const_iterator(n, n->end_index());
						}

						n = t;
						sub = t->begin_index();
					} else {
						return const_iterator(n, node_t::step_counter(n->begin_index(), count));
					}
				}
			}

			ptrdiff_t operator - (const const_iterator& rhs) const noexcept {
				const node_t* t = rhs.current_node;
				size_t sub = rhs.it;
				ptrdiff_t count = 0;

				while (t != current_node) {
					count += node_t::diff_counter(t->end_index(), sub);
					t = t->next;
					sub = t->begin_index();
				}

				count += node_t::diff_counter(it, sub);
				return count;
			}

			bool operator == (const const_iterator& rhs) const noexcept {
				return /* current_node == rhs.current_node && */ it == rhs.it;
			}

			bool operator != (const const_iterator& rhs) const noexcept {
				return /* current_node != rhs.current_node || */ it != rhs.it;
			}

			const element_t* operator -> () const noexcept {
				return &current_node->get(it);
			}

			const element_t& operator * () const noexcept {
				return current_node->get(it);
			}

			bool step() noexcept {
				it = node_t::step_counter(it, 1);
				if (it == current_node->end_index()) {
					const node_t* n = current_node->next;
					if (n == nullptr) {
						return false;
					}

					current_node = n;
					it = n->begin_index();
				}

				return true;
			}

		private:
			size_t it;
			const node_t* current_node;
		};

		friend struct const_iterator;

		iterator begin() noexcept {
			node_t* p = pop_head;
			if /* constexpr */ (enable_memory_fence) {
				cleanup_empty();
			}

			return iterator(p, p->begin_index());
		}

		iterator end() noexcept {
			node_t* p = push_head;
			return iterator(p, p->end_index());
		}

		const_iterator begin() const noexcept {
			node_t* p = pop_head;
			if /* constexpr */ (enable_memory_fence) {
				const_cast<iris_queue_list_t*>(this)->cleanup_empty();
			}

			return const_iterator(p, (static_cast<const node_t*>(p))->begin_index());
		}

		const_iterator end() const noexcept {
			const node_t* p = push_head;
			return const_iterator(p, p->end_index());
		}

	protected:
		node_t* push_head = nullptr;
		node_t* pop_head = nullptr; // pop_head is always prior to push_head.
		size_t iterator_counter = 0;
	};

	// crtp-based resource handle reuse pool
	template <typename interface_t, typename queue_t, size_t block_size = default_block_size>
	struct iris_pool_t : protected enable_in_out_fence_t<> {
		using element_t = typename queue_t::element_t;
		iris_pool_t(size_t size = block_size / sizeof(element_t)) : max_size(size) {
#if IRIS_DEBUG
			allocated.store(0, std::memory_order_release);
#endif
		}
		~iris_pool_t() noexcept {
			clear();
		}

		void clear() noexcept {
#if IRIS_DEBUG
			IRIS_ASSERT(allocated == queue.size()); // all acquire() must release() before destruction
			allocated = 0;
#endif
			queue.for_each([this](element_t& element) {
				static_cast<interface_t*>(this)->template release_element<element_t>(std::move(element));
			});

			queue.clear();
		}

		// acquire an element from pool or by allocating a new one
		element_t acquire() noexcept(noexcept(std::declval<interface_t>().template acquire_element<element_t>()) && noexcept(std::declval<queue_t>().pop())) {
			auto guard = out_fence();

			if (queue.empty()) {
#if IRIS_DEBUG
				allocated.fetch_add(1, std::memory_order_relaxed);
#endif
				return static_cast<interface_t*>(this)->template acquire_element<element_t>();
			} else {
				element_t value = std::move(queue.top());
				queue.pop();
				return value;
			}
		}

		// release an element to pool, or destroy it if pool is already full
		void release(element_t&& element) noexcept(noexcept(std::declval<interface_t>().template release_element<element_t>(std::declval<element_t>())) && noexcept(std::declval<queue_t>().push(std::declval<element_t>()))) {
			auto guard = in_fence();

			if (queue.size() < max_size) {
				queue.push(std::move(element));
			} else {
#if IRIS_DEBUG
				allocated.fetch_sub(1, std::memory_order_relaxed);
#endif
				static_cast<interface_t*>(this)->template release_element<element_t>(std::move(element));
			}
		}

	protected:
		size_t max_size;
#if IRIS_DEBUG
		std::atomic<size_t> allocated;
#endif
		queue_t queue;
	};

	// frame adapter for iris_queue_list_t
	template <typename queue_t, size_t block_size = default_block_size, template <typename...> class allocator_t = iris_default_block_allocator_t>
	struct iris_queue_frame_t : protected enable_in_out_fence_t<> {
		explicit iris_queue_frame_t(queue_t& q) noexcept : queue(q), barrier(q.end()) {}

		using iterator = typename queue_t::iterator;
		using const_iterator = typename queue_t::const_iterator;

		iterator begin() noexcept(noexcept(std::declval<queue_t>().begin())) {
			return queue.begin();
		}

		iterator end() noexcept {
			return barrier;
		}

		const_iterator begin() const noexcept(noexcept(std::declval<queue_t>().begin())) {
			return queue.begin();
		}

		const_iterator end() const noexcept {
			return barrier;
		}

		size_t size() const noexcept {
			return static_cast<size_t>(end() - begin());
		}

		template <typename... args_t>
		void push(args_t&&... args) {
			auto guard = in_fence();
			queue.push(std::forward<args_t>(args)...);
		}

		template <typename iterator_t>
		iterator_t pop(iterator_t from, iterator_t to) noexcept {
			auto guard = out_fence();
			return queue.pop(from, to);
		}

		// acquire a frame, returns true on success, or false when there is no frame prepared
		bool acquire() noexcept(noexcept(std::declval<queue_t>().pop(1))) {
			auto guard = out_fence();
			queue.pop(barrier - begin());

			if (!frames.empty()) {
				barrier = frames.top();
				frames.pop();
				return true;
			} else {
				return false;
			}
		}

		// release a frame
		void release() noexcept(noexcept(iris_queue_list_t<iterator, allocator_t>().push(std::declval<iterator>()))) {
			auto guard = in_fence();
			frames.push(queue.end());
		}

	protected:
		queue_t& queue;
		iterator barrier;
		iris_queue_list_t<iterator, allocator_t> frames;
	};

	template <typename quantity_t, size_t n>
	struct iris_quota_t {
		using amount_t = std::array<quantity_t, n>;
		iris_quota_t(const amount_t& amount) noexcept {
			for (size_t i = 0; i < n; i++) {
				quantities[i].store(amount[i], std::memory_order_relaxed);
			}

			std::atomic_thread_fence(std::memory_order_release);
		}

		bool acquire(const amount_t& amount) noexcept {
			for (size_t i = 0; i < n; i++) {
				quantity_t m = amount[i];
				if (m == 0)
					continue;

				std::atomic<quantity_t>& q = quantities[i];
				quantity_t expected = q.load(std::memory_order_acquire);
				while (!(expected < m)) {
					if (q.compare_exchange_weak(expected, expected - m, std::memory_order_relaxed)) {
						break;
					}
				}

				if (expected < m) {
					// failed, return back
					for (size_t k = 0; k < i; k++) {
						if (amount[k] == 0)
							continue;

						quantities[k].fetch_add(amount[k], std::memory_order_release);
					}

					return false;
				}
			}

			return true;
		}

		void release(const amount_t& amount) noexcept {
			for (size_t k = 0; k < n; k++) {
				if (amount[k] == 0)
					continue;

				quantities[k].fetch_add(amount[k], std::memory_order_release);
			}
		}

		struct guard_t {
			guard_t(iris_quota_t& h, const amount_t& m) noexcept : host(&h) {
				if (host->acquire(m)) {
					amount = m;
				} else {
					host = nullptr;
					amount = amount_t();
				}
			}

			~guard_t() noexcept {
				if (host != nullptr) {
					host->release(amount);
				}
			}

			guard_t(const guard_t&) noexcept = delete;
			guard_t(guard_t&& rhs) noexcept : host(rhs.host), amount(rhs.amount) { rhs.host = nullptr; }

			operator bool() noexcept {
				return host != nullptr;
			}

		protected:
			iris_quota_t* host;
			amount_t amount;
		};

		guard_t guard(const amount_t& amount) noexcept {
			return guard_t(*this, amount);
		}

		amount_t get() const noexcept {
			amount_t ret;
			for (size_t i = 0; i < n; i++) {
				ret[i] = quantities[i].load(std::memory_order_acquire);
			}

			return ret;
		}

	protected:
		std::array<std::atomic<quantity_t>, n> quantities;
	};
}

