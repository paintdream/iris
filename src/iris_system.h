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

namespace iris {
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

		template <typename iterators_t, typename operation_t, size_t... i>
		static void step_operation(iterators_t& iterators, operation_t& op, iris_sequence<i...>) {
			op(*std::get<i>(iterators)++...);
		}

		template <typename iterators_t>
		void step_iterators(iterators_t&&...) {}

		template <typename iterators_t, typename operation_t, size_t... i>
		static void step_operation(iterators_t& iterators, operation_t& op, iris_sequence<i...>, size_t count) {
			op(count, std::get<i>(iterators)...);
			step_iterators((std::get<i>(iterators) += static_cast<ptrdiff_t>(count))...);
		}
	}

	// components_t is not allowed to contain repeated types
	template <typename entity_t, template <typename...> class allocator_t, typename... components_t>
	struct iris_system_t : protected enable_read_write_fence_t<> {
		template <typename target_t>
		struct fetch_index : fetch_index_impl<target_t, components_t...> {};
		static constexpr size_t block_size = allocator_t<entity_t>::block_size;
		using index_t = entity_t; // just for alignment
		using components_tuple_t = std::tuple<entity_t, components_t...>;

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
				if (entity_components.capacity() <= entity_components.size() + 1) {
					entity_components.reserve(entity_components.size() * 3 / 2);
				}

				emplace_components<sizeof...(components_t)>(std::forward<elements_t>(t)...);
				entities.push(entity);

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
			IRIS_ASSERT(valid(entity));
			return std::get<fetch_index<component_t>::value>(components).get(iris_binary_find(entity_components.begin(), entity_components.end(), entity)->second);
		}

		template <typename... for_components_t, typename operation_t>
		bool for_entity(entity_t entity, operation_t&& op) {
			auto guard = read_fence();

			auto ip = iris_binary_find(entity_components.begin(), entity_components.end(), entity);
			if (ip != entity_components.end()) {
				index_t index = ip->second;
				op(std::get<fetch_index<for_components_t>::value>(components).get(index)...);
				return true;
			} else {
				return false;
			}
		}

		template <typename component_t>
		const component_t& get(entity_t entity) const noexcept {
			auto guard = read_fence();

			IRIS_ASSERT(valid(entity));
			return std::get<fetch_index<component_t>::value>(components).get(iris_binary_find(entity_components.begin(), entity_components.end(), entity)->second);
		}

		// entity-based component removal
		bool remove(entity_t entity) {
			auto guard = write_fence();
			IRIS_ASSERT(!entities.empty());
			entity_t top_entity = entities.top();
			if (entity != top_entity) {
				// swap the top element (component_t, entity_t) with removed one
				auto ip = iris_binary_find(entity_components.begin(), entity_components.end(), entity);
				if (ip == entity_components.end())
					return false; // not found

				// move!!
				auto it = iris_binary_find(entity_components.begin(), entity_components.end(), top_entity);

				index_t index = ip->second;
				it->second = index; // reuse space!
				ip->second = ~(index_t)0;

				move_components(index, placeholder<components_t...>());
			}

			pop_components(placeholder<components_t...>());
			entities.pop();
			return true;
		}

		void clear() {
			auto guard = write_fence();
			clear_components(placeholder<components_t...>());
			entities.clear();
			entity_components.clear();
		}

		// iterate components
		template <typename... for_components_t, typename operation_t>
		void for_each(operation_t&& op) {
			IRIS_PROFILE_SCOPE(__FUNCTION__);

			auto guard = read_fence();
			auto iterators_begin = std::make_tuple(component<for_components_t>().begin()...);
			const auto iterators_end = std::make_tuple(component<for_components_t>().end()...);

			while (std::get<0>(iterators_begin) != std::get<0>(iterators_end)) {
				step_operation(iterators_begin, op, iris_make_sequence<std::tuple_size<decltype(iterators_begin)>::value>());
			}
		}

		template <typename... for_components_t, typename operation_t>
		void for_each_batch(size_t batch_count, operation_t&& op) {
			IRIS_PROFILE_SCOPE(__FUNCTION__);

			auto guard = read_fence();
			auto iterators_begin = std::make_tuple(component<for_components_t>().begin()...);
			const auto iterators_end = std::make_tuple(component<for_components_t>().end()...);

			size_t counter = 0;
			auto iterators_current = iterators_begin;
			size_t total = iris_verify_cast<size_t>(std::get<0>(iterators_end) - std::get<0>(iterators_begin));
			for (size_t i = 0; i < total; i += batch_count) {
				step_operation(iterators_begin, op, iris_make_sequence<std::tuple_size<decltype(iterators_begin)>::value>(), std::min(i + batch_count, total) - i);
			}
		}

		template <typename component_t>
		typename std::enable_if<!std::is_same<component_t, entity_t>::value, iris_queue_list_t<component_t, allocator_t>&>::type component() noexcept {
			auto guard = read_fence();
			return std::get<fetch_index<component_t>::value>(components);
		}

		template <typename component_t>
		typename std::enable_if<std::is_same<component_t, entity_t>::value, iris_queue_list_t<component_t, allocator_t>&>::type component() noexcept {
			auto guard = read_fence();
			return entities;
		}

		template <typename component_t>
		typename std::enable_if<!std::is_same<component_t, entity_t>::value, const iris_queue_list_t<component_t, allocator_t>&>::type component() const noexcept {
			auto guard = read_fence();
			return std::get<fetch_index<component_t>::value>(components);
		}

		template <typename component_t>
		typename std::enable_if<std::is_same<component_t, entity_t>::value, const iris_queue_list_t<component_t, allocator_t>&>::type component() const noexcept {
			auto guard = read_fence();
			return entities;
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
			std::get<sizeof...(components_t) - i>(components).push(std::forward<first_t>(first));
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
		void pop_components(placeholder<first_t, elements_t...>) noexcept {
			std::get<sizeof...(elements_t)>(components).pop();
			pop_components(placeholder<elements_t...>());
		}

		void pop_components(placeholder<>) noexcept {}

		template <typename first_t, typename... elements_t>
		void clear_components(placeholder<first_t, elements_t...>) noexcept {
			std::get<sizeof...(elements_t)>(components).clear();
			clear_components(placeholder<elements_t...>());
		}

		void clear_components(placeholder<>) noexcept {}

	protected:
		std::tuple<iris_queue_list_t<components_t, allocator_t>...> components;
		std::vector<iris_key_value_t<entity_t, index_t>> entity_components;
		iris_queue_list_t<entity_t, allocator_t> entities;
	};

	template <typename entity_t, template <typename...> class allocator_t = iris_default_block_allocator_t>
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

	template <typename entity_t, template <typename...> class allocator_t>
	struct iris_systems_t : protected enable_read_write_fence_t<> {
		template <typename type_t>
		struct component_info_t {};

		template <typename type_t>
		static size_t get_type_hash() noexcept {
			return iris_static_instance_t<component_info_t<type_t>>::get_unique_hash();
		}

		struct system_info_t {
			void* address = nullptr;
			bool (*remove)(void*, entity_t) = nullptr;
			void (*clear)(void*) = nullptr;
			std::vector<iris_key_value_t<size_t, void*>> components;
		};

		template <typename system_t>
		void attach(system_t& sys) {
			auto guard = write_fence();
			system_info_t info;
			info.address = &sys;
			info.remove = &remove_handler<system_t>;
			info.clear = &clear_handler<system_t>;
			info.components = generate_info(info, sys, iris_make_sequence<std::tuple_size<typename system_t::components_tuple_t>::value>());
			std::sort(info.components.begin(), info.components.end());

			system_infos.emplace_back(std::move(info));
		}

		template <typename system_t>
		void detach(system_t& sys) {
			auto guard = write_fence();
			for (size_t i = 0; i < system_infos.size(); i++) {
				if (system_infos[i].address == &sys) {
					system_infos.erase(system_infos.begin() + i);
					break;
				}
			}
		}

		size_t remove(entity_t entity) {
			auto guard = write_fence();
			size_t count = 0;
			for (size_t i = 0; i < system_infos.size(); i++) {
				auto& system_info = system_infos[i];
				count += system_info.remove(system_info.address, entity) ? 1u : 0u;
			}

			return count;
		}

		void reset() {
			system_infos.clear();
		}

		void clear() {
			auto guard = write_fence();
			for (size_t i = 0; i < system_infos.size(); i++) {
				auto& system_info = system_infos[i];
				system_info.clear(system_info.address);
			}
		}

		template <typename... for_components_t, typename operation_t>
		void for_each(operation_t&& op) {
			IRIS_PROFILE_SCOPE(__FUNCTION__);
			auto guard = read_fence();

			for (size_t i = 0; i < system_infos.size(); i++) {
				auto& system_info = system_infos[i];
				auto iterators_begin = std::make_tuple(typename iris_queue_list_t<for_components_t, allocator_t>::iterator()...);
				auto iterators_end = std::make_tuple(typename iris_queue_list_t<for_components_t, allocator_t>::iterator()...);

				if (match_iterators<decltype(iterators_begin), 0, for_components_t...>(iterators_begin, iterators_end, system_info)) {
					while (std::get<0>(iterators_begin) != std::get<0>(iterators_end)) {
						step_operation(iterators_begin, op, iris_make_sequence<std::tuple_size<decltype(iterators_begin)>::value>());
					}
				}
			}
		}

		template <typename... for_components_t, typename operation_t>
		void for_each_batch(size_t batch_count, operation_t&& op) {
			IRIS_PROFILE_SCOPE(__FUNCTION__);
			auto guard = read_fence();
			for (size_t i = 0; i < system_infos.size(); i++) {
				auto& system_info = system_infos[i];
				auto iterators_begin = std::make_tuple(typename iris_queue_list_t<for_components_t, allocator_t>::iterator()...);
				auto iterators_end = std::make_tuple(typename iris_queue_list_t<for_components_t, allocator_t>::iterator()...);

				if (match_iterators<decltype(iterators_begin), 0, for_components_t...>(iterators_begin, iterators_end, system_info)) {
					size_t total = iris_verify_cast<size_t>(std::get<0>(iterators_end) - std::get<0>(iterators_begin));
					for (size_t i = 0; i < total; i += batch_count) {
						step_operation(iterators_begin, op, iris_make_sequence<std::tuple_size<decltype(iterators_begin)>::value>(), std::min(i + batch_count, total) - i);
					}
				}
			}
		}

	protected:
		template <typename system_t>
		static bool remove_handler(void* address, entity_t entity) {
			return reinterpret_cast<system_t*>(address)->remove(entity);
		}

		template <typename system_t>
		static void clear_handler(void* address) {
			reinterpret_cast<system_t*>(address)->clear();
		}

		template <typename system_t, size_t... i>
		std::vector<iris_key_value_t<size_t, void*>> generate_info(system_info_t& info, system_t& sys, iris_sequence<i...>) {
			return std::vector<iris_key_value_t<size_t, void*>>{ iris_make_key_value(get_type_hash<typename std::tuple_element<i, typename system_t::components_tuple_t>::type>(), reinterpret_cast<void*>(&sys.template component<typename std::tuple_element<i, typename system_t::components_tuple_t>::type>()))... };
		}

		template <typename iterators_t, size_t i>
		bool match_iterators(iterators_t& iterators_begin, iterators_t& iterators_end, const system_info_t& system_info) noexcept {
			return true;
		}

		template <typename iterators_t, size_t i, typename first_component_t, typename... components_t>
		bool match_iterators(iterators_t& iterators_begin, iterators_t& iterators_end, const system_info_t& system_info) noexcept {
			size_t hash = get_type_hash<first_component_t>();
			auto it = iris_binary_find(system_info.components.begin(), system_info.components.end(), hash);
			if (it != system_info.components.end()) {
				auto* list = reinterpret_cast<iris_queue_list_t<first_component_t, allocator_t>*>(it->second);
				std::get<i>(iterators_begin) = list->begin();
				std::get<i>(iterators_end) = list->end();

				return match_iterators<iterators_t, i + 1, components_t...>(iterators_begin, iterators_end, system_info);
			} else {
				return false;
			}
		}

	protected:
		std::vector<system_info_t> system_infos;
	};
}
