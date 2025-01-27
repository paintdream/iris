/*
The Iris Concurrency Framework

This software is a C++ 11 Header-Only reimplementation of core part from project PaintsNow.

The MIT License (MIT)

copyright (c) 2014-2025 PaintDream

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
#include <cstring>

namespace iris {
	template <typename element_t, size_t storage_size = (sizeof(element_t*) * 4 - sizeof(size_t)) / sizeof(element_t)>
	struct iris_buffer_t {
		static constexpr size_t ext_store_mask = (size_t(1) << (sizeof(size_t) * 8 - 1)); // has external storage?
		static constexpr size_t data_view_mask = (size_t(1) << (sizeof(size_t) * 8 - 2)); // is data view?

		using iterator = element_t*;
		using const_iterator = const element_t*;
		using value_type = element_t;

		iris_buffer_t() noexcept : encode_size(0) {
			buffer = nullptr;
			static_assert(storage_size >= 3 * sizeof(size_t) / sizeof(element_t), "must has stock storage of at least 3 pointer size.");
			static_assert(std::is_trivially_constructible<element_t>::value, "must be trivially constructible.");
			static_assert(std::is_trivially_destructible<element_t>::value, "must be trivially destructible.");
		}

		explicit iris_buffer_t(size_t init_size) noexcept(noexcept(std::declval<iris_buffer_t>().resize(init_size))) : encode_size(0) {
			if (init_size != 0) {
				resize(init_size);
			}
		}

		iris_buffer_t(const element_t* p, size_t init_size) noexcept(noexcept(std::declval<iris_buffer_t>().resize(init_size))) : encode_size(0) {
			if (init_size != 0) {
				resize(init_size);
				std::memcpy(data(), p, init_size * sizeof(element_t));
			}
		}

		iris_buffer_t(const iris_buffer_t& rhs) noexcept(noexcept(std::declval<iris_buffer_t>().copy(rhs))) : encode_size(0) {
			copy(rhs);
		}

		~iris_buffer_t() noexcept {
			if (is_managed_storage()) {
				IRIS_ASSERT(buffer != nullptr);
				free(buffer);
			}
		}

		iterator begin() noexcept {
			return data();
		}

		const_iterator begin() const noexcept {
			return data();
		}

		iterator end() noexcept {
			return begin() + size();
		}

		const_iterator end() const noexcept {
			return begin() + size();
		}

		iterator insert(iterator pos, const element_t& e) noexcept(noexcept(std::declval<iris_buffer_t>().push(e))) {
			IRIS_ASSERT(!is_view_storage());
			size_t offset = pos - begin();
			push(e);
			iterator ptr = begin() + offset;

			if (offset != size()) {
				iterator last = end();
				for (iterator it = begin() + offset + 1; it != last; ++it) {
					*it = *(it - 1);
				}

				*ptr = e;
			}

			return ptr;
		}

		void erase(iterator pos) noexcept(noexcept(std::declval<iris_buffer_t>().resize(1))) {
			IRIS_ASSERT(!is_view_storage());
			IRIS_ASSERT(pos != end());

			iterator last = end();
			for (iterator it = pos; it + 1 < last; ++it) {
				*it = *(it + 1);
			}

			pop();
		}

		void clear() noexcept {
			if (is_managed_storage()) {
				IRIS_ASSERT(buffer != nullptr);
				free(buffer);
			}

			encode_size = 0;
		}

		iris_buffer_t& operator = (const iris_buffer_t& rhs) noexcept(noexcept(std::declval<iris_buffer_t>().copy(rhs))) {
			if (this != &rhs) {
				copy(rhs);
			}

			return *this;
		}

		iris_buffer_t(iris_buffer_t&& rhs) noexcept {
			std::memcpy(this, &rhs, sizeof(*this));
			rhs.encode_size = 0;
		}

		iris_buffer_t& operator = (iris_buffer_t&& rhs) noexcept {
			if (this != &rhs) {
				clear();

				std::memcpy(this, &rhs, sizeof(*this));
				rhs.encode_size = 0;
			}

			return *this;
		}

		static iris_buffer_t& null() noexcept {
			static iris_buffer_t empty_buffer;
			return empty_buffer;
		}

		// danger! be aware at your own risk!
		static iris_buffer_t make_view(element_t* data, size_t length) noexcept {
			iris_buffer_t buffer;
			buffer.encode_size = length | (ext_store_mask | data_view_mask);
			buffer.buffer = data;
			buffer.next = nullptr;
			buffer.tail = nullptr;

			return buffer;
		}

		static const iris_buffer_t make_view(const element_t* data, size_t length) noexcept {
			return make_view(const_cast<element_t*>(data), length);
		}

		static iris_buffer_t make_view(const_iterator from, const_iterator to) noexcept {
			return make_view(from, to - from);
		}

		const iris_buffer_t view() const noexcept {
			IRIS_ASSERT(!is_view_storage());
			return make_view(data(), size());
		}

		iris_buffer_t view() noexcept {
			IRIS_ASSERT(!is_view_storage());
			return make_view(data(), size());
		}

		bool test(size_t offset) const noexcept {
			IRIS_ASSERT(offset < size() * sizeof(element_t) * 8);
			return !!(data()[offset / (sizeof(element_t) * 8)] & (1u << (offset % (sizeof(element_t) * 8))));
		}

		void set(size_t offset) noexcept {
			IRIS_ASSERT(offset < size() * sizeof(element_t) * 8);
			data()[offset / (sizeof(element_t) * 8)] |= (1u << (offset % (sizeof(element_t) * 8)));
		}

		bool is_managed_storage() const noexcept { return (encode_size & (data_view_mask | ext_store_mask)) == ext_store_mask; }
		bool is_view_storage() const noexcept { return !!(encode_size & data_view_mask); }
		bool is_stock_storage() const noexcept { return !(encode_size & ext_store_mask); }
		size_t size() const noexcept { IRIS_ASSERT(encode_size <= storage_size || (encode_size & ~ext_store_mask) > storage_size); return encode_size & ~(ext_store_mask | data_view_mask); }
		const element_t* data() const noexcept { return is_stock_storage() ? stock_storage : buffer; }
		element_t* data() noexcept { return is_stock_storage() ? stock_storage : buffer; }

		size_t get_view_size() const noexcept {
			if (is_view_storage()) {
				const iris_buffer_t* p = this;
				size_t s = 0;
				while (p != nullptr) {
					s += p->size();
					p = p->next;
				}

				return s;
			} else {
				return size();
			}
		}

		// copy data from continous region (`ptr`, `size`) to this buffer starting at `offset` with `repeat` count
		void copy(size_t offset, const element_t* ptr, size_t input_size, size_t repeat = 1) noexcept {
			if (is_view_storage()) { // parted? copy by segments
				iris_buffer_t* p = this;
				IRIS_ASSERT(offset + input_size * repeat <= get_view_size());
				while (repeat-- != 0) {
					size_t k = 0;
					while (p != nullptr && k < input_size) {
						size_t len = p->size();
						// copy part
						if (offset < len) {
							size_t r = std::min(len - offset, input_size - k);
							std::memcpy(p->data() + offset, ptr + k, r * sizeof(element_t));
							k += r;
							offset = 0;
						} else {
							offset -= len;
						}

						// lookup for next segment
						p = p->next;
					}
				}
			} else {
				// continous, go plain copy
				element_t* p = data();
				while (repeat-- != 0) {
					std::memcpy(p + offset, ptr, input_size * sizeof(element_t));
					offset += input_size;
				}
			}
		}

		// copy data from another buffer to this buffer starting at `offset` with `repeat` count
		void copy(size_t dst_offset, const iris_buffer_t& buffer, size_t repeat = 1) noexcept {
			if (buffer.is_view_storage()) { // source is data view
				if (is_view_storage()) { // target is data view
					IRIS_ASSERT(get_view_size() >= dst_offset + buffer.get_view_size() * repeat);
					iris_buffer_t* p = this;

					while (repeat-- != 0) {
						IRIS_ASSERT(p != nullptr);  // must got enough space
						// select minimal segment
						const iris_buffer_t* q = &buffer;
						size_t src_size = q->size();
						const element_t* src = q->data();
						size_t dst_size = p->size();
						element_t* dst = p->data();
						size_t src_offset = 0;

						do {
							// copy segment data
							if (dst_offset < dst_size) {
								size_t r = std::min(dst_size - dst_offset, src_size - src_offset);
								std::memcpy(dst + dst_offset, src + src_offset, r * sizeof(element_t));
								dst_offset += r;
								src_offset += r;

								if (dst_offset >= dst_size) {
									p = p->next;
									if (p != nullptr) {
										dst_size = p->size();
										dst = p->data();
										dst_offset = 0;
									}
								}

								if (src_offset >= src_size) {
									q = q->next; // step source if needed
									if (q != nullptr) {
										src_size = q->size();
										src = q->data();
										src_offset = 0;
									}
								}
							} else {
								dst_offset -= dst_size;
								p = p->next; // step target if needed
							}
						} while (p != nullptr && q != nullptr);
					}
				} else {
					// only source is data view
					IRIS_ASSERT(size() >= dst_offset + buffer.get_view_size() * repeat);
					element_t* target = data() + dst_offset;

					while (repeat-- != 0) {
						const iris_buffer_t* p = &buffer;

						while (p != nullptr) {
							size_t n = p->size();
							std::memcpy(target, p->data(), n * sizeof(element_t));
							target += n;
							p = p->next; // step source
						}
					}
				}
			} else {
				// only target is data view, go another version of copy
				copy(dst_offset, buffer.data(), buffer.size(), repeat);
			}
		}

		bool empty() const noexcept { return encode_size == 0; }

		bool operator == (const iris_buffer_t& rhs) const noexcept {
			IRIS_ASSERT(is_view_storage() == rhs.is_view_storage());
			IRIS_ASSERT(!is_view_storage() || (next == nullptr && rhs.next == nullptr));
			if (encode_size != rhs.encode_size) return false;
			if (encode_size == 0) return true;

			return std::memcmp(data(), rhs.data(), size() * sizeof(element_t)) == 0;
		}

		bool operator < (const iris_buffer_t& rhs) const noexcept {
			IRIS_ASSERT(is_view_storage() == rhs.is_view_storage());
			IRIS_ASSERT(!is_view_storage() || (next == nullptr && rhs.next == nullptr));
			if (encode_size == 0) {
				return rhs.encode_size != 0;
			} else {
				bool less = encode_size < rhs.encode_size;
				// select common range
				size_t min_size = (less ? encode_size : rhs.encode_size) & (~(ext_store_mask | data_view_mask));
				int result = std::memcmp(data(), rhs.data(), min_size * sizeof(element_t));
				return result != 0 ? result < 0 : less;
			}
		}

		const element_t& operator [] (size_t index) const noexcept {
			IRIS_ASSERT(index < size());
			return data()[index];
		}

		element_t& operator [] (size_t index) noexcept {
			IRIS_ASSERT(index < size());
			return data()[index];
		}

		void resize(size_t s, const element_t& init) noexcept(noexcept(std::declval<iris_buffer_t>().resize(s))) {
			size_t org_size = size();
			resize(s);

			if (s > org_size) {
				element_t* ptr = data();
				std::fill(ptr + org_size, ptr + s, init);
			}
		}

		void resize(size_t s) {
			IRIS_ASSERT(!is_view_storage());
			if (is_stock_storage()) {
				if (s > storage_size) { // out of bound
					element_t* new_buffer = reinterpret_cast<element_t*>(malloc(s * sizeof(element_t)));
					if (new_buffer == nullptr) {
						throw std::bad_alloc();
					}

					std::memcpy(new_buffer, stock_storage, size() * sizeof(element_t));
					buffer = new_buffer;
					encode_size = s | ext_store_mask;
				} else {
					encode_size = s;
				}
			} else {
				if (s > storage_size) {
					if (s > size()) {
						element_t* new_buffer = reinterpret_cast<element_t*>(realloc(buffer, s * sizeof(element_t)));
						if (new_buffer == nullptr) {
							throw std::bad_alloc();
						}

						buffer = new_buffer;
					}

					encode_size = s | ext_store_mask;
				} else {
					// shrink
					element_t* org_buffer = buffer;
					std::memcpy(stock_storage, org_buffer, s * sizeof(element_t));
					free(org_buffer);

					encode_size = s;
				}
			}
		}

		void swap(iris_buffer_t& rhs) noexcept {
			std::swap(encode_size, rhs.encode_size);
			for (size_t i = 0; i < storage_size; i++) {
				std::swap(stock_storage[i], rhs.stock_storage[i]);
			}
		}

		iris_buffer_t& append(const iris_buffer_t& rhs) noexcept(noexcept(std::declval<iris_buffer_t>().append(rhs.data(), rhs.size()))) {
			if (empty()) {
				*this = rhs;
				return *this;
			} else if (is_view_storage()) {
				// concat buffer, not copying
				IRIS_ASSERT(rhs.is_view_storage());
				iris_buffer_t* p = this;

				while (true) {
					size_t cur_size = p->size();
					if (cur_size == 0) {
						*p = rhs;
						return *this;
					} else if (rhs.buffer == p->buffer + cur_size && p->next == nullptr) { // continuous?
						p->encode_size += rhs.size();
						p->next = rhs.next;
						tail = rhs.tail == nullptr ? tail : rhs.tail;
						return *this;
					} else {
						if (p->tail == nullptr) {
							IRIS_ASSERT(p->next == nullptr);
							p->next = const_cast<iris_buffer_t*>(&rhs);
							tail = rhs.tail == nullptr ? p->next : rhs.tail;
							return *this;
						} else {
							p = p->tail;
						}
					}
				}

				// never reach here
				return *this;
			} else {
				// must copy here
				IRIS_ASSERT(!rhs.is_view_storage() || rhs.next == nullptr);
				return append(rhs.data(), rhs.size());
			}
		}

		// plain data appending
		iris_buffer_t& append(const element_t* buffer, size_t append_size) noexcept(noexcept(std::declval<iris_buffer_t>().resize(append_size))) {
			if (append_size != 0) {
				size_t org_size = size();
				resize(org_size + append_size);
				std::memcpy(data() + org_size, buffer, append_size * sizeof(element_t));
			}

			return *this;
		}

		void push(const element_t& element) noexcept(noexcept(std::declval<iris_buffer_t>().append(std::declval<const element_t*>(), 1))) {
			append(&element, 1);
		}

		void pop() noexcept(noexcept(std::declval<iris_buffer_t>().resize(1))) {
			resize(size() - 1);
		}

		// plain data assignment
		iris_buffer_t& assign(const element_t* buffer, size_t n) noexcept(noexcept(std::declval<iris_buffer_t>().resize(n))) {
			resize(n);
			if (n != 0) {
				std::memcpy(data(), buffer, n * sizeof(element_t));
			}

			return *this;
		}

	protected:
		// plain data copying
		void copy(const iris_buffer_t& rhs) noexcept(noexcept(std::declval<iris_buffer_t>().resize(rhs.size()))) {
			if (rhs.is_view_storage()) {
				clear();
				std::memcpy(this, &rhs, sizeof(rhs));
			} else {
				size_t s = rhs.size();
				resize(s);
				std::memcpy(data(), rhs.data(), s * sizeof(element_t));
			}
		}

		size_t encode_size;
		union {
			struct {
				element_t* buffer;
				iris_buffer_t* next;
				iris_buffer_t* tail;
			};
			element_t stock_storage[storage_size];
		};
	};

	using iris_bytes_t = iris_buffer_t<uint8_t>;

	template <typename element_t, size_t block_size = default_block_size, template <typename...> class base_allocator_t = iris_default_block_allocator_t>
	struct iris_cache_t : protected iris_queue_list_t<element_t, base_allocator_t, false> {
		using storage_t = iris_queue_list_t<element_t, base_allocator_t, false>;

		iris_cache_t() {}
		template <typename allocator_t>
		iris_cache_t(allocator_t&& alloc) : storage_t(std::forward<allocator_t>(alloc)) {}

		iris_buffer_t<element_t> allocate(size_t size, size_t alignment = 16) {
			size_t pack = storage_t::pack_size(alignment);
			static_assert(alignof(iris_buffer_t<element_t>) % sizeof(element_t) == 0, "iris_buffer_t<element_t> must be aligned at least sizeof(element_t).");
			const size_t head_count = sizeof(iris_buffer_t<element_t>) / sizeof(element_t);

			if (size > pack) {
				element_t* slice = allocate_linear(pack, alignment);
				iris_buffer_t<element_t> head = iris_buffer_t<element_t>::make_view(slice, pack);
				iris_buffer_t<element_t>* p = &head;
				size -= pack;
				pack = storage_t::full_pack_size() - head_count;
				alignment = std::max(alignment, iris_verify_cast<size_t>(alignof(iris_buffer_t<element_t>) / sizeof(element_t)));

				while (size != 0) {
					size_t alloc_count = std::min(size, pack);
					slice = allocate_linear(alloc_count + head_count, alignment);
					iris_buffer_t<element_t>* next = new (slice) iris_buffer_t<element_t>();
					*next = iris_buffer_t<element_t>::make_view(slice + head_count, pack);
					size -= alloc_count;

					p->append(*next);
					p = next;
				}

				return head;
			} else {
				return iris_buffer_t<element_t>::make_view(allocate_linear(size, alignment), size);
			}
		}

		// only support const for_each
		// count skipped elements
		template <typename operation_t>
		void for_each(operation_t&& op) const noexcept(noexcept(std::declval<storage_t::node_t>().for_each(op))) {
			auto guard = storage_t::out_fence();
			for (typename storage_t::node_t* p = storage_t::pop_head; p != storage_t::push_head->next; p = p->next) {
				p->for_each([&op, p](element_t* t, size_t n) {
					op(t, p == storage_t::push_head ? n : storage_t::element_count);
				});
			}
		}

		static constexpr size_t full_pack_size() {
			return storage_t::full_pack_size();
		}

		void link(iris_buffer_t<element_t>& from, const iris_buffer_t<element_t>& to) {
			if (from.empty()) {
				from = to;
			} else {
				IRIS_ASSERT(from.is_view_storage() && to.is_view_storage());
				iris_buffer_t<element_t> storage = allocate(sizeof(iris_buffer_t<element_t>) / sizeof(element_t), alignof(iris_buffer_t<element_t>) / sizeof(element_t));
				from.append(*new (storage.data()) iris_buffer_t<element_t>(to));
			}
		}

		void reset() noexcept {
			storage_t::reset(~size_t(0));
		}

		void clear() noexcept {
			storage_t::reset(0);
		}

		size_t offset() const noexcept {
			return storage_t::offset();
		}

		// allocate continuous array from queue_list
		// may lead holes in low-level storage if current node is not enough
		element_t* allocate_linear(size_t count, size_t alignment) {
			auto guard = storage_t::in_fence();

			element_t* address;
			while ((address = storage_t::push_head->allocate(count, alignment)) == nullptr) {
				if (storage_t::push_head->next == nullptr) {
					auto* p = storage_t::node_allocator_t::allocate(1);
					new (p) typename storage_t::node_t(static_cast<typename storage_t::node_allocator_t&>(*this), storage_t::iterator_counter);
					storage_t::iterator_counter = storage_t::node_t::step_counter(storage_t::iterator_counter, storage_t::element_count);

					address = p->allocate(count, alignment); // must success
					IRIS_ASSERT(address != nullptr);

					storage_t::push_head->next = p;
					// no memory fence
					storage_t::push_head = p;
					break;
				}

				storage_t::push_head = storage_t::push_head->next;
			}

			return address;
		}
	};

	using bytes_cache_t = iris_cache_t<uint8_t>;

	template <typename element_t, typename base_t = uint8_t, size_t block_size = default_block_size, template <typename...> class large_allocator_t = std::allocator>
	struct iris_cache_allocator_t : private large_allocator_t<element_t> {
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
		struct rebind { using other = iris_cache_allocator_t<morph_t, base_t>; };
		using allocator_t = iris_cache_t<base_t>;
		allocator_t* allocator;

		iris_cache_allocator_t(allocator_t* alloc) noexcept : allocator(alloc) {}
		iris_cache_allocator_t(const iris_cache_allocator_t& al) noexcept : allocator(al.allocator) {}

#ifdef _MSC_VER
		// maybe a bug of vc (debug), just add these lines to make compiler happy
		template <typename morph_t>
		iris_cache_allocator_t(const iris_cache_allocator_t<morph_t, base_t>& rhs) noexcept : allocator(rhs.allocator) {}
#endif

		template <typename morph_t>
		bool operator == (const morph_t& rhs) const noexcept {
			return allocator == rhs.allocator;
		}

		template <typename morph_t>
		bool operator != (const morph_t& rhs) const noexcept {
			return allocator != rhs.allocator;
		}

		pointer address(reference x) const noexcept {
			return &x;
		};

		const_pointer address(const_reference x) const noexcept {
			return &x;
		}

		size_type max_size() const {
			return ~(size_type)0 / sizeof(element_t);
		}

		void construct(pointer p) {
			new (static_cast<void*>(p)) element_t();
		}

		void construct(pointer p, const_reference val) {
			new (static_cast<void*>(p)) element_t(val);
		}

		template <typename morph_t, typename... args_t>
		void construct(morph_t* p, args_t&&... args) {
			new (static_cast<void*>(p)) morph_t(std::forward<args_t>(args)...);
		}

		template <typename morph_t>
		void destroy(morph_t* p) noexcept {
			p->~morph_t();
		}

		pointer allocate(size_type n, const void* hint = nullptr) {
			IRIS_ASSERT(allocator != nullptr);
			static_assert(sizeof(element_t) % sizeof(base_t) == 0, "must be aligned.");
			size_t count = n * sizeof(element_t) / sizeof(base_t);
			if (count <= allocator_t::full_pack_size()) {
				return reinterpret_cast<pointer>(allocator->allocate_linear(count, alignof(element_t)));
			} else {
				return reinterpret_cast<pointer>(large_allocator_t<element_t>::allocate(n));
			}
		}

		void deallocate(element_t* p, size_t n) noexcept {
			size_t count = n * sizeof(element_t) / sizeof(base_t);
			if (count <= allocator_t::full_pack_size()) {
				// do not deallocate in cache allocator.
			} else {
				large_allocator_t<element_t>::deallocate(p, n);
			}
		}
	};
}

