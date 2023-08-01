/*
The Iris Concurrency Framework

This software is a C++ 11 Header-Only reimplementation of core part from project PaintsNow.

The MIT License (MIT)

Copyright (c) 2014-2023 PaintDream

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
#include "iris_dispatcher.h"
#include <array>

namespace iris {
	// a compile-time typed entity-component-system
	// thread safety:
	// 1. [no ] insert/insert
	// 2. [no ] remove/remove
	// 3. [no ] insert/iterate
	// 4. [no ] remove/iterate
	// 5. [yes] iterate/iterate
	// 6. [yes] iterate/insert
	// 

	namespace {
		// find type index for given type
		// https://stackoverflow.com/questions/26169198/how-to-get-the-index-of-a-type-in-a-variadic-type-pack
		template <typename target_t, typename... types_t>
		struct fetch_index_impl : std::integral_constant<size_t, 0> {};

		template <typename target_t, typename... types_t>
		struct fetch_index_impl<target_t, target_t, types_t...> : std::integral_constant<size_t, 0> {};

		template <typename target_t, typename next_t, typename... types_t>
		struct fetch_index_impl<target_t, next_t, types_t...> : std::integral_constant<size_t, 1 + fetch_index_impl<target_t, types_t...>::value> {};

		template <size_t n, typename... args_t>
		using locate_type = typename std::tuple_element<n, std::tuple<args_t...>>::type;

		template <typename type_t>
		constexpr bool check_duplicated_components_one() { return true; }

		template <typename type_t, typename first_t, typename... check_types_t>
		constexpr bool check_duplicated_components_one() {
			return !std::is_same<type_t, first_t>::value && check_duplicated_components_one<type_t, check_types_t...>();
		}

		template <typename first_t>
		constexpr bool check_duplicated_components() { return true; }

		template <typename first_t, typename second_t, typename... check_types_t>
		constexpr bool check_duplicated_components() {
			return check_duplicated_components_one<first_t, second_t, check_types_t...>() && check_duplicated_components<second_t, check_types_t...>();
		}

		template <typename... type_t>
		struct placeholder {};
	}

	// components_t is not allowed to contain repeated types
	template <typename entity_t, template <typename...> typename allocator_t, typename... components_t>
	struct iris_system_t : protected enable_read_write_fence_t<> {
		template <typename target_t>
		struct fetch_index : fetch_index_impl<target_t, components_t...> {};
		static constexpr size_t block_size = allocator_t<entity_t>::block_size;
		using index_t = entity_t; // just for alignment

		iris_system_t() {
			// check if there are duplicated types
			static_assert(check_duplicated_components<components_t...>(), "duplicated component detected!");
		}

		// entity-based component insertion
		bool valid(entity_t entity) const noexcept {
			auto guard = read_fence();
			return iris_binary_find(entity_components.begin(), entity_components.end(), entity) != entity_components.end();
		}

		// returns true if the existing entity was replaced, or false if new entity was created
		template <typename... elements_t>
		bool insert(entity_t entity, elements_t&&... t) {
			auto guard = write_fence();

			auto iterator = iris_binary_find(entity_components.begin(), entity_components.end(), entity);
			if (iterator != entity_components.end()) {
				index_t index = iterator->second;
				if (index == ~(index_t)0) {
					index = iris_verify_cast<index_t>(entities.end_index());
					replace_components<sizeof...(components_t)>(index, std::forward<elements_t>(t)...);
					iterator->second = index;
				} else {
					replace_components<sizeof...(components_t)>(index, std::forward<elements_t>(t)...);
				}

				return true;
			} else {
				preserve_entity(placeholder<components_t...>());
				emplace_components<sizeof...(components_t)>(std::forward<elements_t>(t)...);
				entities.emplace(entity);
				iris_binary_insert(entity_components, iris_make_key_value(entity, iris_verify_cast<index_t>(entities.end_index())));

				return false;
			}
		}

		void compress() noexcept {
			auto guard = write_fence();

			size_t j = 0;
			for (size_t i = 0; i < entity_components.size(); i++) {
				auto& item = entity_components[i];
				if (item.second != ~(index_t)0) {
					entity_components[j++] = item;
				}
			}

			entity_components.resize(j);
		}

		size_t size() const noexcept {
			auto guard = read_fence();
			return entities.size();
		}

		// get specified component of given entity
		template <typename component_t>
		component_t& get(entity_t entity) noexcept {
			auto guard = read_fence();
			assert(valid(entity));
			return std::get<fetch_index<component_t>::value>(components).get(iris_binary_find(entity_components.begin(), entity_components.end(), entity)->second);
		}

		template <typename component_t>
		const component_t& get(entity_t entity) const noexcept {
			auto guard = read_fence();

			assert(valid(entity));
			return std::get<fetch_index<component_t>::value>(components).get(iris_binary_find(entity_components.begin(), entity_components.end(), entity)->second);
		}

		// entity-based component removal
		void remove(entity_t entity) {
			assert(valid(entity));
			auto guard = write_fence();
			assert(!entities.empty());

			entity_t top_entity = entities.top();

			// swap the top element (component_t, entity_t) with removed one
			if (entity != top_entity) {
				// move!!
				auto it = iris_binary_find(entity_components.begin(), entity_components.end(), top_entity);
				auto ip = iris_binary_find(entity_components.begin(), entity_components.end(), entity);

				index_t index = ip->second;
				it->second = index; // reuse space!
				ip->second = ~(index_t)0;

				move_components(index, placeholder<components_t...>());
			}

			pop_components(placeholder<components_t...>());
			entities.pop();
		}

		// iterate components
		template <typename component_t, typename operation_t>
		void for_each(operation_t&& op) noexcept(noexcept(std::declval<iris_queue_list_t<component_t, allocator_t>>().for_each(op))) {
			IRIS_PROFILE_SCOPE(__FUNCTION__);

			auto guard = read_fence();
			std::get<fetch_index<component_t>::value>(components).for_each(op);
		}

		// n is the expected group size
		template <typename component_t, typename warp_t, typename operand_t, typename queue_list_t = iris_queue_list_t<component_t, allocator_t>>
		void for_each_parallel(operand_t&& op, size_t n = queue_list_t::element_count) {
			IRIS_PROFILE_SCOPE(__FUNCTION__);

			auto guard = read_fence();
			auto& target_components = std::get<fetch_index<component_t>::value>(components);
			warp_t* warp = warp_t::get_current_warp();
			assert(warp != nullptr);

			using node_t = typename queue_list_t::node_t;
			if (n <= node_t::element_count) {
				// one node per group, go fast path
				target_components.for_each_queue([warp, &op](node_t* p) {
					warp->queue_routine_parallel([p, op]() mutable {
						p->for_each(std::move(op));
					});
				});
			} else {
				// use cache list
				std::vector<node_t*> cache;
				size_t count = 0;
				target_components.for_each_queue([&cache, &count, n, &op, warp](node_t* p) {
					cache.emplace_back(p);
					count += p->size();
					if (count >= n) {
						warp->queue_routine_parallel([cache, op]() mutable {
							for (auto&& p : cache) {
								p->for_each(op);
							}
						});

						cache.clear();
						count = 0;
					}
				});

				// dispatch the remaining
				if (count != 0) {
					warp->queue_routine_parallel([cache, op]() mutable {
						for (auto&& p : cache) {
							p->for_each(op);
						}
					});
				}
			}
		}

		template <typename component_t>
		iris_queue_list_t<component_t, allocator_t>& component() noexcept {
			auto guard = read_fence();
			return std::get<fetch_index<component_t>::value>(components);
		}

		template <typename component_t>
		const iris_queue_list_t<component_t, allocator_t>& component() const noexcept {
			auto guard = read_fence();
			return std::get<fetch_index<component_t>::value>(components);
		}

		template <typename component_t>
		static constexpr bool has() noexcept {
			return fetch_index<component_t>::value < sizeof...(components_t);
		}

	protected:
		template <size_t i>
		void emplace_components() {}

		template <size_t i, typename first_t, typename... elements_t>
		void emplace_components(first_t&& first, elements_t&&... remaining) {
			std::get<sizeof...(components_t) - i>(components).emplace(std::forward<first_t>(first));
			emplace_components<i - 1>(std::forward<elements_t>(remaining)...);
		}

		template <size_t i>
		void replace_components(index_t id) {}

		template <size_t i, typename first_t, typename... elements_t>
		void replace_components(index_t id, first_t&& first, elements_t&&... remaining) {
			std::get<sizeof...(components_t) - i>(components).get(id) = std::forward<first_t>(first);
			replace_components<i - 1>(id, std::forward<elements_t>(remaining)...);
		}

		template <typename first_t, typename... elements_t>
		void move_components(index_t index, placeholder<first_t, elements_t...>) noexcept {
			auto& comp = std::get<sizeof...(elements_t)>(components);
			auto& top = comp.top();
			comp.get(index) = std::move(top);

			move_components(index, placeholder<elements_t...>());
		}

		void move_components(index_t& index, placeholder<>) noexcept {}

		template <typename first_t, typename... elements_t>
		void preserve_entity(placeholder<first_t, elements_t...>) {
			std::get<sizeof...(elements_t)>(components).preserve();
			preserve_entity(placeholder<elements_t...>());
		}

		void preserve_entity(placeholder<>) {
			entities.preserve();

			if (entity_components.capacity() <= entity_components.size() + 1) {
				entity_components.reserve(entity_components.size() * 3 / 2);
			}
		}

		template <typename first_t, typename... elements_t>
		void pop_components(placeholder<first_t, elements_t...>) noexcept {
			std::get<sizeof...(elements_t)>(components).pop();
			pop_components(placeholder<elements_t...>());
		}

		void pop_components(placeholder<>) noexcept {}

	protected:
		std::tuple<iris_queue_list_t<components_t, allocator_t>...> components;
		std::vector<iris_key_value_t<entity_t, index_t>> entity_components;
		iris_queue_list_t<entity_t, allocator_t> entities;
	};

	template <typename entity_t, template <typename...> typename allocator_t = iris_default_block_allocator_t>
	struct iris_entity_allocator_t : protected enable_in_out_fence_t<> {
		entity_t allocate() noexcept(noexcept(std::declval<iris_entity_allocator_t>().free_entities.pop())) {
			auto guard = in_fence();
			if (!free_entities.empty()) {
				entity_t entity = free_entities.top();
				free_entities.pop();
				return entity;
			} else {
				return max_allocated_entity++;
			}
		}

		void free(entity_t entity) noexcept(noexcept(std::declval<iris_entity_allocator_t>().free_entities.push(entity_t()))) {
			auto guard = out_fence();
			free_entities.push(entity);
		}

		void reset() noexcept(noexcept(std::declval<iris_entity_allocator_t>().free_entities.reset(0))) {
			auto in_guard = in_fence();
			auto out_guard = out_fence();

			free_entities.reset(0);
			max_allocated_entity = 0;
		}

	protected:
		iris_queue_list_t<entity_t, allocator_t> free_entities;
		entity_t max_allocated_entity = 0;
	};

	template <template <typename...> typename allocator_t, typename... subsystems_t>
	struct iris_systems_t {
		iris_systems_t(subsystems_t&... args) : subsystems(args...) {}
		static constexpr size_t block_size = locate_type<0, subsystems_t...>::block_size;

		template <typename component_t>
		using component_queue_t = iris_queue_list_t<component_t, allocator_t>;

		template <size_t system_count, typename... components_t>
		struct component_view {
			using queue_tuple_t = std::tuple<iris_queue_list_t<components_t, allocator_t>*...>;
			using queue_iterator_t = std::tuple<typename iris_queue_list_t<components_t, allocator_t>::iterator...>;

			struct iterator {
				using difference_type = ptrdiff_t;
				using value_type = typename std::conditional<sizeof...(components_t) == 1, locate_type<0, components_t...>, std::tuple<std::reference_wrapper<components_t>...>>::type;
				using reference = value_type&;
				using pointer = value_type*;
				using iterator_category = std::input_iterator_tag;

				template <typename... iter_t>
				iterator(component_view& view_host, size_t i, iter_t&&... iter) : host(&view_host), index(i), it(std::forward<iter_t>(iter)...) {}

				template <size_t... s>
				static iterator make_iterator_begin(component_view& view_host, size_t i, queue_tuple_t& sub, iris_sequence<s...>) noexcept {
					return iterator(view_host, i, std::get<s>(sub)->begin()...);
				}

				template <size_t... s>
				static iterator make_iterator_end(component_view& view_host, size_t i, queue_tuple_t& sub, iris_sequence<s...>) noexcept {
					return iterator(view_host, i, std::get<s>(sub)->end()...);
				}

				iterator& operator ++ () noexcept {
					step();
					return *this;
				}

				iterator operator ++ (int) noexcept {
					iterator r = *this;
					step();

					return r;
				}

				bool operator == (const iterator& rhs) const noexcept {
					return index == rhs.index && std::get<0>(it) == std::get<0>(rhs.it);
				}

				bool operator != (const iterator& rhs) const noexcept {
					return index != rhs.index || std::get<0>(it) != std::get<0>(rhs.it);
				}

				template <size_t... s>
				std::tuple<std::reference_wrapper<components_t>...> make_value(iris_sequence<s...>) const noexcept {
					return std::make_tuple<std::reference_wrapper<components_t>...>(*std::get<s>(it)...);
				}

				reference filter_value(std::true_type) const noexcept {
					return *std::get<0>(it);
				}

				std::tuple<std::reference_wrapper<components_t>...> filter_value(std::false_type) const noexcept {
					return make_value(iris_make_sequence<sizeof...(components_t)>());
				}

				typename std::conditional<sizeof...(components_t) == 1, reference, value_type>::type operator * () const noexcept {
					return filter_value(std::integral_constant<bool, sizeof...(components_t) == 1>());
				}

				template <typename operation_t>
				void invoke(operation_t&& op) const {
					invoke_impl(std::forward<operation_t>(op), iris_make_sequence<sizeof...(components_t)>());
				}

			protected:
				template <typename operation_t, size_t... s>
				void invoke_impl(operation_t&& op, iris_sequence<s...>) const {
					op(*std::get<s>(it)...);
				}

				template <typename first_t>
				static bool reduce(first_t f) noexcept { return f; }
				template <typename first_t, typename... args_t>
				static bool reduce(first_t f, args_t&&...) noexcept { return f; }

				template <size_t... s>
				bool step_impl(iris_sequence<s...>) noexcept {
					return reduce(std::get<s>(it).step()...);
				}

				void step() noexcept {
					if (!step_impl(iris_make_sequence<sizeof...(components_t)>())) {
						while (index + 1 < system_count) {
							if (!std::get<0>(host->subcomponents[++index])->empty()) {
								*this = make_iterator_begin(*host, index, host->subcomponents[index], iris_make_sequence<sizeof...(components_t)>());

								return;
							}
						}

						*this = make_iterator_end(*host, index, host->subcomponents[index], iris_make_sequence<sizeof...(components_t)>());
					}
				}

				component_view* host;
				size_t index;
				queue_iterator_t it;
			};

			iterator begin() noexcept {
				for (size_t i = 0; i < subcomponents.size(); i++) {
					if (!std::get<0>(subcomponents[i])->empty()) {
						return iterator::make_iterator_begin(*this, i, subcomponents[i], iris_make_sequence<sizeof...(components_t)>());
					}
				}

				return end();
			}

			iterator end() noexcept {
				return iterator::make_iterator_end(*this, system_count - 1, subcomponents[system_count - 1], iris_make_sequence<sizeof...(components_t)>());
			}

			template <typename operation_t>
			void for_each(operation_t&& op) {
				IRIS_PROFILE_SCOPE(__FUNCTION__);

				for_each_impl(std::forward<operation_t>(op), std::integral_constant<bool, sizeof...(components_t) == 1>());
			}

			template <typename operation_t>
			void for_each_system(operation_t&& op) {
				IRIS_PROFILE_SCOPE(__FUNCTION__);

				for_each_system_impl(std::forward<operation_t>(op), iris_make_sequence<sizeof...(components_t)>());
			}

			std::array<queue_tuple_t, system_count> subcomponents;

		protected:
			template <typename operation_t>
			void for_each_impl(operation_t&& op, std::true_type) {
				// simple path
				for (size_t i = 0; i < subcomponents.size(); i++) {
					std::get<0>(subcomponents[i])->for_each(op);
				}
			}

			template <typename operation_t>
			void for_each_impl(operation_t&& op, std::false_type) {
				// complex path
				for (auto it = begin(); it != end(); ++it) {
					it.invoke(op);
				}
			}

			template <typename operation_t, size_t... s>
			void for_each_system_impl(operation_t&& op, iris_sequence<s...>) {
				for (size_t i = 0; i < subcomponents.size(); i++) {
					op(*std::get<s>(subcomponents[i])...);
				}
			}
		};

		template <size_t system_count, typename... components_t>
		struct const_component_view {
			using queue_tuple_t = std::tuple<const iris_queue_list_t<components_t, allocator_t>*...>;
			using queue_iterator_t = std::tuple<typename iris_queue_list_t<components_t, allocator_t>::const_iterator...>;

			struct iterator {
				using difference_type = ptrdiff_t;
				using value_type = typename std::conditional<sizeof...(components_t) == 1, const locate_type<0, components_t...>, std::tuple<std::reference_wrapper<const components_t>...>>::type;
				using reference = value_type&;
				using pointer = value_type*;
				using iterator_category = std::input_iterator_tag;

				template <typename... iter_t>
				iterator(const_component_view& view_host, size_t i, iter_t&&... iter) : host(&view_host), index(i), it(std::forward<iter_t>(iter)...) {}

				template <size_t... s>
				static iterator make_iterator_begin(const_component_view& view_host, size_t i, queue_tuple_t& sub, iris_sequence<s...>) noexcept {
					return iterator(view_host, i, std::get<s>(sub)->begin()...);
				}

				template <size_t... s>
				static iterator make_iterator_end(const_component_view& view_host, size_t i, queue_tuple_t& sub, iris_sequence<s...>) noexcept {
					return iterator(view_host, i, std::get<s>(sub)->end()...);
				}

				iterator& operator ++ () noexcept {
					step();
					return *this;
				}

				iterator operator ++ (int) noexcept {
					iterator r = *this;
					step();

					return r;
				}

				bool operator == (const iterator& rhs) const noexcept {
					return index == rhs.index && std::get<0>(it) == std::get<0>(rhs.it);
				}

				bool operator != (const iterator& rhs) const noexcept {
					return index != rhs.index || std::get<0>(it) != std::get<0>(rhs.it);
				}

				template <size_t... s>
				std::tuple<std::reference_wrapper<const components_t>...> make_value(iris_sequence<s...>) const noexcept {
					return std::make_tuple<std::reference_wrapper<const components_t>...>(*std::get<s>(it)...);
				}

				reference filter_value(std::true_type) const noexcept {
					return *std::get<0>(it);
				}

				std::tuple<std::reference_wrapper<const components_t>...> filter_value(std::false_type) const noexcept {
					return make_value(iris_make_sequence<sizeof...(components_t)>());
				}

				typename std::conditional<sizeof...(components_t) == 1, reference, value_type>::type operator * () const noexcept {
					return filter_value(std::integral_constant<bool, sizeof...(components_t) == 1>());
				}

				template <typename operation_t>
				void invoke(operation_t&& op) const {
					invoke_impl(std::forward<operation_t>(op), iris_make_sequence<sizeof...(components_t)>());
				}

			protected:
				template <typename operation_t, size_t... s>
				void invoke_impl(operation_t&& op, iris_sequence<s...>) const {
					op(*std::get<s>(it)...);
				}

				template <typename first_t>
				static bool reduce(first_t f) noexcept { return f; }
				template <typename first_t, typename... args_t>
				static bool reduce(first_t f, args_t&&...) noexcept { return f; }

				template <size_t... s>
				bool step_impl(iris_sequence<s...>) noexcept {
					return reduce(std::get<s>(it).step()...);
				}

				void step() noexcept {
					if (!step_impl(iris_make_sequence<sizeof...(components_t)>())) {
						while (index + 1 < system_count) {
							if (!std::get<0>(host->subcomponents[++index])->empty()) {
								*this = make_iterator_begin(*host, index, host->subcomponents[index], iris_make_sequence<sizeof...(components_t)>());

								return;
							}
						}

						*this = make_iterator_end(*host, index, host->subcomponents[index], iris_make_sequence<sizeof...(components_t)>());
					}
				}

				const_component_view* host;
				size_t index;
				queue_iterator_t it;
			};

			iterator begin() noexcept {
				for (size_t i = 0; i < subcomponents.size(); i++) {
					if (!std::get<0>(subcomponents[i])->empty()) {
						return iterator::make_iterator_begin(*this, i, subcomponents[i], iris_make_sequence<sizeof...(components_t)>());
					}
				}

				return end();
			}

			iterator end() noexcept {
				return iterator::make_iterator_end(*this, system_count - 1, subcomponents[system_count - 1], iris_make_sequence<sizeof...(components_t)>());
			}

			template <typename operation_t>
			void for_each(operation_t&& op) {
				IRIS_PROFILE_SCOPE(__FUNCTION__);

				for_each_impl(std::forward<operation_t>(op), std::integral_constant<bool, sizeof...(components_t) == 1>());
			}

			template <typename operation_t>
			void for_each_system(operation_t&& op) {
				IRIS_PROFILE_SCOPE(__FUNCTION__);

				for_each_system_impl(std::forward<operation_t>(op), iris_make_sequence<sizeof...(components_t)>());
			}

			std::array<queue_tuple_t, system_count> subcomponents;

		protected:
			template <typename operation_t>
			void for_each_impl(operation_t&& op, std::true_type) {
				// simple path
				for (size_t i = 0; i < subcomponents.size(); i++) {
					std::get<0>(subcomponents[i])->for_each(op);
				}
			}

			template <typename operation_t>
			void for_each_impl(operation_t&& op, std::false_type) {
				// complex path
				for (auto it = begin(); it != end(); ++it) {
					it.invoke(op);
				}
			}

			template <typename operation_t, size_t... s>
			void for_each_system_impl(operation_t&& op, iris_sequence<s...>) {
				for (size_t i = 0; i < subcomponents.size(); i++) {
					op(*std::get<s>(subcomponents[i])...);
				}
			}
		};

		template <size_t n, typename target_t, typename system_t>
		struct count_components : std::integral_constant<size_t, (system_t::template has<typename std::tuple_element<n - 1, target_t>::type>() ? 1 : 0) + count_components<n - 1, target_t, system_t>::value> {};

		template <typename target_t, typename system_t>
		struct count_components<0, target_t, system_t> : std::integral_constant<size_t, 0> {};

		template <typename target_t, typename... types_t>
		struct count_match : std::integral_constant<size_t, 0> {};

		template <typename target_t, typename next_t, typename... types_t>
		struct count_match<target_t, next_t, types_t...> : std::integral_constant<size_t, 
			// must have all components required
			(count_components<std::tuple_size<target_t>::value, target_t, next_t>::value == std::tuple_size<target_t>::value)
			+ count_match<target_t, types_t...>::value> {};

		template <bool fill, size_t i, size_t n, typename components_tuple_t, typename view_t>
		struct fill_view_impl {
			template <typename system_t, size_t... s>
			static void execute_impl(view_t& view, system_t& sys, iris_sequence<s...>) {
				view.subcomponents[n] = std::make_tuple(&sys.template component<typename std::tuple_element<s, components_tuple_t>::type>()...);
			}

			template <typename sub_t>
			static void execute(view_t& view, sub_t& subsystems) noexcept {
				execute_impl(view, std::get<i>(subsystems), iris_make_sequence<std::tuple_size<components_tuple_t>::value>());
			}
		};

		template <size_t i, size_t n, typename components_tuple_t, typename view_t>
		struct fill_view_impl<false, i, n, components_tuple_t, view_t> {
			template <typename sub_t>
			static void execute(view_t& view, sub_t& subsystems) noexcept {}
		};

		template <size_t i, size_t n, typename components_tuple_t, typename view_t>
		struct fill_view {
			template <typename sub_t>
			static void execute(view_t& view, sub_t& subsystems) noexcept {
				constexpr bool fill = count_components<std::tuple_size<components_tuple_t>::value, components_tuple_t, locate_type<i - 1, subsystems_t...>>::value == std::tuple_size<components_tuple_t>::value;
				fill_view_impl<fill, i - 1, n, components_tuple_t, view_t>::execute(view, subsystems);
				fill_view<i - 1, n + fill, components_tuple_t, view_t>::execute(view, subsystems);
			}
		};

		template <size_t n, typename components_tuple_t, typename view_t>
		struct fill_view<0, n, components_tuple_t, view_t> {
			template <typename sub_t>
			static void execute(view_t& view, sub_t& subsystems) noexcept {}
		};

		template <typename... components_t>
		component_view<count_match<std::tuple<components_t...>, subsystems_t...>::value, components_t...> components() noexcept {
			static_assert(check_duplicated_components<components_t...>(), "duplicated components detected!");
			constexpr size_t system_count = count_match<std::tuple<components_t...>, subsystems_t...>::value;
			static_assert(system_count != 0, "specified component types not found. use constexpr has() before calling me!");
			component_view<system_count, components_t...> view;
			fill_view<sizeof...(subsystems_t), 0, std::tuple<components_t...>, component_view<system_count, components_t...>>::execute(view, subsystems);
			return view;
		}

		template <typename... components_t>
		const_component_view<count_match<std::tuple<components_t...>, subsystems_t...>::value, components_t...> components() const noexcept {
			static_assert(check_duplicated_components<components_t...>(), "duplicated components detected!");
			constexpr size_t system_count = count_match<std::tuple<components_t...>, subsystems_t...>::value;
			static_assert(system_count != 0, "specified component types not found. use constexpr has() before calling me!");
			const_component_view<system_count, components_t...> view;
			fill_view<sizeof...(subsystems_t), 0, std::tuple<components_t...>, const_component_view<system_count, components_t...>>::execute(view, subsystems);
			return view;
		}

		template <typename... components_t>
		static constexpr bool has() noexcept {
			return check_duplicated_components<components_t...>() && count_match<std::tuple<components_t...>, subsystems_t...>::value != 0;
		}

	protected:
		std::tuple<subsystems_t&...> subsystems;
	};
}
