/*
The Iris Concurrency Framework

This software is a C++ 17 Header-Only reimplementation of core part from project PaintsNow.

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

#include "iris_common.h"
#include <array>

extern "C" {
#if !USE_LUA_LIBRARY
	#include "lua/src/lua.h"
	#include "lua/src/lualib.h"
	#include "lua/src/lauxlib.h"
#else
	#include "lua.h"
	#include "lualib.h"
	#include "lauxlib.h"
#endif
}

// compatible with old lua versions
#if LUA_VERSION_NUM < 501
#error "Unsupported lua version!"
#endif

#if LUA_VERSION_NUM <= 501
#define LUA_OK 0
#define lua_rawlen lua_objlen
#endif

#if LUA_VERSION_NUM <= 502
#endif

#if LUA_VERSION_NUM <= 503
#define lua_newuserdatauv(L, size, uv) lua_newuserdata(L, size)
#endif

namespace iris {
	template <typename type_t>
	struct iris_lua_convert_t : std::false_type {};

	// A simple lua binding with C++17
	struct iris_lua_t : enable_read_write_fence_t<> {
		// borrow from an existing state
		explicit iris_lua_t(lua_State* L) noexcept : state(L) {}
		iris_lua_t(iris_lua_t&& rhs) noexcept : state(rhs.state) { rhs.state = nullptr; }
		iris_lua_t(const iris_lua_t& rhs) noexcept : state(rhs.state) {}

		operator lua_State* () const noexcept {
			return get_state();
		}

		lua_State* get_state() const noexcept {
			return state;
		}

		// run a piece of code
		template <typename return_t = void>
		return_t run(std::string_view code) {
			auto guard = write_fence();
			lua_State* L = state;
			stack_guard_t stack_guard(L);

			if (luaL_loadbuffer(L, code.data(), code.size(), "run") != LUA_OK) {
				fprintf(stderr, "[ERROR] iris_lua_t::run() -> load code error: %s\n", lua_tostring(L, -1));
				lua_pop(L, 1);
				if constexpr (!std::is_void_v<return_t>) {
					return return_t();
				}
			}

			if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
				fprintf(stderr, "[ERROR] iris_lua_t::run() -> run code error: %s\n", lua_tostring(L, -1));
				lua_pop(L, 1);
				if constexpr (!std::is_void_v<return_t>) {
					return return_t();
				}
			} else {
				if constexpr (!std::is_void_v<return_t>) {
					return_t ret = get_variable<return_t>(L, -1);
					lua_pop(L, 1);
					return ret;
				} else {
					lua_pop(L, 1);
				}
			}
		}

		// holding lua value
		struct ref_t {
			explicit ref_t(int v = LUA_REFNIL) noexcept : value(v) { assert(LUA_REFNIL == 0 || v != 0); }
			~ref_t() noexcept { assert(value == LUA_REFNIL); }
			ref_t(ref_t&& rhs) noexcept : value(rhs.value) { rhs.value = LUA_REFNIL; }
			ref_t(const ref_t& rhs) = delete;
			ref_t& operator = (const ref_t& rhs) = delete;
			ref_t& operator = (ref_t&& rhs) noexcept { assert(value == LUA_REFNIL); std::swap(rhs.value, value); return *this; }

			using internal_type_t = void;

			operator bool() const noexcept {
				return value != LUA_REFNIL;
			}

			int get() const noexcept {
				return value;
			}

			template <typename value_t>
			value_t as(lua_State* L) const {
				stack_guard_t stack_guard(L);

				push_variable(L, *this);
				value_t ret = iris_lua_t::get_variable<value_t>(L, -1);
				lua_pop(L, 1);

				return ret; // named return value optimization
			}

			template <typename value_t = ref_t, typename key_t>
			value_t get(lua_State* L, key_t&& key) const {
				stack_guard_t stack_guard(L);

				push_variable(L, *this);
				if (lua_istable(L, -1)) {
					push_variable(L, std::forward<key_t>(key));
					lua_rawget(L, -2);
					value_t ret = get_variable<value_t>(L, -1);
					lua_pop(L, 2);

					return ret;
				} else {
					lua_pop(L, 1);
					return value_t();
				}
			}

			template <typename key_t, typename value_t>
			void set(lua_State* L, key_t&& key, value_t&& value) const {
				stack_guard_t stack_guard(L);

				push_variable(L, *this);
				if (lua_istable(L, -1)) {
					push_variable(L, std::forward<key_t>(key));
					push_variable(L, std::forward<value_t>(value));
					lua_rawset(L, -3);
				}

				lua_pop(L, 1);
			}

			template <typename key_t, typename value_t, typename func_t>
			void for_each(lua_State* L, func_t&& func) const {
				stack_guard_t stack_guard(L);
				push_variable(L, *this);

				lua_pushnil(L);
				while (lua_next(L, -2) != 0) {
					if (func(get_variable<key_t>(L, -2), get_variable<value_t>(L, -1))) {
						lua_pop(L, 1);
						break;
					}

					lua_pop(L, 1);
				}

				lua_pop(L, 1);
			}

			size_t size(lua_State* L) const {
				stack_guard_t stack_guard(L);

				push_variable(L, *this);
				size_t len = lua_rawlen(L, -1);
				lua_pop(L, 1);

				return len;
			}

			friend struct iris_lua_t;

		private:
			int value;
		};

		template <typename type_t>
		struct refptr_t : ref_t {
			refptr_t(int v = LUA_REFNIL, type_t* p = nullptr) noexcept : ref_t(v), ptr(p) {}
			refptr_t(refptr_t&& rhs) noexcept : ref_t(std::move(static_cast<ref_t&>(rhs))), ptr(rhs.ptr) {}
			refptr_t(const refptr_t& rhs) = delete;
			refptr_t& operator = (const refptr_t& rhs) = delete;
			refptr_t& operator = (refptr_t&& rhs) noexcept { ref_t::operator = (std::move(static_cast<ref_t&>(rhs))); std::swap(ptr, rhs.ptr); return *this; }

			using internal_type_t = type_t*;

			operator bool() const noexcept {
				return value != 0 && ptr != nullptr;
			}

			operator type_t* () const noexcept {
				return get();
			}

			type_t* operator -> () const noexcept {
				return get();
			}

			type_t* get() const noexcept {
				return ptr;
			}

			friend struct iris_lua_t;
		protected:
			type_t* ptr;
		};

		struct require_base_t {};

		template <typename type_t>
		struct required_t : require_base_t {
			using internal_type_t = type_t;
			template <typename... args_t>
			required_t(args_t&&... args) : value(std::forward<args_t>(args)...) {}

			type_t& get() noexcept {
				return value;
			}

		private:
			type_t value;
		};

		template <size_t count>
		struct refguard_t {
			template <typename... args_t>
			refguard_t(lua_State* L, args_t&... r) noexcept : state(L), refs{ &r... } {}
			~refguard_t() noexcept {
				for (auto&& r : refs) {
					iris_lua_t::deref(state, *r);
				}
			}

			lua_State* state;
			std::array<ref_t*, count> refs;
		};

		template <typename... args_t>
		auto ref_guard(args_t&... args) noexcept {
			return refguard_t<sizeof...(args_t)>(state, args...);
		}

		// a guard for checking stack balance
		struct stack_guard_t {
#ifdef _DEBUG
			stack_guard_t(lua_State* state, int offset = 0) noexcept : L(state) { top = lua_gettop(L) + offset; }
			~stack_guard_t() noexcept { assert(top == lua_gettop(L)); }

			lua_State* L;
			int top;
#else
			stack_guard_t(lua_State* state, int offset = 0) noexcept {}
#endif
		};

		auto stack_guard(int offset = 0) noexcept {
			return stack_guard_t(state, offset);
		}

		template <typename type_t>
		struct has_registar {
			template <typename> static std::false_type test(...);
			template <typename impl_t> static auto test(int)
				-> decltype(std::declval<impl_t>().lua_registar(std::declval<iris_lua_t>()), std::true_type());
			static constexpr bool value = std::is_same<decltype(test<type_t>(0)), std::true_type>::value;
			static void default_registar(iris_lua_t) {}
			static constexpr auto get_registar() {
				if constexpr (value) {
					return &type_t::lua_registar;
				} else {
					return &default_registar;
				}
			}
		};

		// register a new type, taking registar from &type_t::lua_registar by default, and you could also specify your own registar.
		template <typename type_t, int user_value_count = 0, auto registar = has_registar<type_t>::get_registar(), typename... args_t>
		ref_t make_type(std::string_view name, args_t&... args) {
			auto guard = write_fence();

			lua_State* L = state;
			stack_guard_t stack_guard(L);
			lua_newtable(L);

			// hash code is to check types when passing as a argument to C++
			push_variable(L, "__hash");
			push_variable(L, reinterpret_cast<void*>(typeid(type_t).hash_code() & 0xffffffff)); // avoid luajit checks
			lua_rawset(L, -3);

			// readable name
			push_variable(L, "__name");
			push_variable(L, name);
			lua_rawset(L, -3);

			// create __gc for collecting objects
			push_variable(L, "__gc");
			lua_pushcfunction(L, &iris_lua_t::delete_object<type_t>);
			lua_rawset(L, -3);

			push_variable(L, "__index");
			lua_pushvalue(L, -2);
			lua_rawset(L, -3);

			push_variable(L, "create");
			lua_pushvalue(L, -2);

			// pass constructor arguments by references
			push_argument_references(L, args...);
			lua_pushcclosure(L, &iris_lua_t::create_object<type_t, user_value_count, args_t...>, 1 + sizeof...(args));
			lua_rawset(L, -3);

			// call custom registar if needed
			registar(iris_lua_t(L));
			return ref_t(luaL_ref(L, LUA_REGISTRYINDEX));
		}

		// quick way for registering a type into global space
		template <typename type_t, int user_value_count = 0, auto registar = has_registar<type_t>::get_registar(), typename... args_t>
		void register_type(std::string_view name, args_t&... args) {
			ref_t r = make_type<type_t, user_value_count, registar, args_t...>(name, args...);

			auto guard = write_fence();
			lua_State* L = state;
			stack_guard_t stack_guard(L);

			push_variable(L, r);
			lua_setglobal(L, name.data());
			deref(L, r);
		}

		// define a variable by value
		template <typename key_t, typename value_t>
		void define(key_t&& key, value_t&& value) {
			auto guard = write_fence();

			lua_State* L = state;
			stack_guard_t stack_guard(L);

			push_variable(L, std::forward<key_t>(key));
			push_variable(L, std::forward<value_t>(value));
			lua_rawset(L, -3);
		}

		// define a bound member function/property
		template <auto ptr, typename key_t>
		void define(key_t&& key) {
			auto guard = write_fence();

			lua_State* L = state;
			stack_guard_t stack_guard(L);

			if constexpr (std::is_member_function_pointer_v<decltype(ptr)>) {
				define_method<ptr>(L, std::forward<key_t>(key), ptr);
			} else if constexpr (std::is_member_object_pointer_v<decltype(ptr)>) {
				define_property<ptr>(L, std::forward<key_t>(key), ptr);
			} else {
				define_function<ptr>(L, std::forward<key_t>(key), ptr);
			}
		}

		template <typename func_t>
		ref_t make_table(func_t&& func) {
			auto guard = write_fence();

			lua_State* L = state;
			stack_guard_t stack_guard(L);
			lua_newtable(L);

			func(iris_lua_t(L));
			return ref_t(luaL_ref(L, LUA_REGISTRYINDEX));
		}

		// dereference a ref
		void deref(ref_t& r) noexcept {
			auto guard = write_fence();
			deref(state, r);
		}

		// call function in protect mode
		template <typename return_t, typename callable_t, typename... args_t>
		return_t call(callable_t&& reference, args_t&&... args) {
			auto guard = write_fence();

			lua_State* L = state;
			stack_guard_t stack_guard(L);
			push_variable(L, std::forward<callable_t>(reference));
			push_arguments(L, std::forward<args_t>(args)...);

			if (lua_pcall(L, sizeof...(args_t), std::is_void_v<return_t> ? 0 : 1, 0) == LUA_OK) {
				if constexpr (!std::is_void_v<return_t>) {
					return_t result = get_variable<return_t>(L, -1);
					lua_pop(L, 1);
					return result;
				}
			} else {
				fprintf(stderr, "[ERROR] iris_lua_t::call() -> call function failed! %s\n", lua_tostring(L, -1));
				lua_pop(L, 1);

				if constexpr (!std::is_void_v<return_t>) {
					return return_t();
				}
			}
		}

		template <auto method, typename return_t, typename type_t, typename... args_t>
		static return_t method_function_adapter(required_t<type_t*>&& object, std::remove_reference_t<args_t>&&... args) {
			if constexpr (!std::is_void_v<return_t>) {
				return (object.get()->*method)(std::move(args)...);
			} else {
				(object.get()->*method)(std::move(args)...);
			}
		}

	protected:
		static void deref(lua_State* L, ref_t& r) noexcept {
			if (r.value != LUA_REFNIL) {
				luaL_unref(L, LUA_REGISTRYINDEX, r.value);
				r.value = LUA_REFNIL;
			}
		}

		static void push_arguments(lua_State* L) {}
		template <typename first_t, typename... args_t>
		static void push_arguments(lua_State* L, first_t&& first, args_t&&... args) {
			push_variable(L, std::forward<first_t>(first));
			push_arguments(L, std::forward<args_t>(args)...);
		}

		static void push_argument_references(lua_State* L) {}
		template <typename first_t, typename... args_t>
		static void push_argument_references(lua_State* L, first_t& first, args_t&... args) {
			lua_pushlightuserdata(L, const_cast<void*>(static_cast<const void*>(&first)));
			push_argument_references(L, std::forward<args_t>(args)...);
		}

		// four specs for [const][noexcept] method definition

		template <auto method, typename key_t, typename return_t, typename type_t, typename... args_t>
		static void define_method(lua_State* L, key_t&& key, return_t(type_t::*)(args_t...)) {
			define_function_internal<method_function_adapter<method, return_t, type_t, args_t...>, key_t, return_t, required_t<type_t*>&&, args_t...>(L, std::forward<key_t>(key));
		}

		template <auto method, typename key_t, typename return_t, typename type_t, typename... args_t>
		static void define_method(lua_State* L, key_t&& key, return_t(type_t::*)(args_t...) noexcept) {
			define_function_internal<method_function_adapter<method, return_t, type_t, args_t...>, key_t, return_t, required_t<type_t*>&&, args_t...>(L, std::forward<key_t>(key));
		}

		template <auto method, typename key_t, typename return_t, typename type_t, typename... args_t>
		static void define_method(lua_State* L, key_t&& key, return_t(type_t::*)(args_t...) const) {
			define_function_internal<method_function_adapter<method, return_t, type_t, args_t...>, key_t, return_t, required_t<type_t*>&&, args_t...>(L, std::forward<key_t>(key));
		}

		template <auto method, typename key_t, typename return_t, typename type_t, typename... args_t>
		static void define_method(lua_State* L, key_t&& key, return_t(type_t::*)(args_t...) const noexcept) {
			define_function_internal<method_function_adapter<method, return_t, type_t, args_t...>, key_t, return_t, required_t<type_t*>&&, args_t...>(L, std::forward<key_t>(key));
		}

		template <auto function, typename key_t, typename return_t, typename... args_t>
		static void define_function(lua_State* L, key_t&& key, return_t(*)(args_t...)) {
			define_function_internal<function, key_t, return_t, args_t...>(L, std::forward<key_t>(key));
		}

		template <auto function, typename key_t, typename return_t, typename... args_t>
		static void define_function(lua_State* L, key_t&& key, return_t(*)(args_t...) noexcept) {
			define_function_internal<function, key_t, return_t, args_t...>(L, std::forward<key_t>(key));
		}

		template <auto function, typename key_t, typename return_t, typename... args_t>
		static void define_function_internal(lua_State* L, key_t&& key) {
			push_variable(L, std::forward<key_t>(key));
			if constexpr (iris_is_coroutine_v<return_t>) {
				lua_pushcclosure(L, &iris_lua_t::function_coroutine_proxy<function, return_t, args_t...>, 0);
			} else {
				lua_pushcclosure(L, &iris_lua_t::function_proxy<function, return_t, args_t...>, 0);
			}

			lua_rawset(L, -3);
		}

		template <auto prop, typename key_t, typename type_t, typename value_t>
		static void define_property(lua_State* L, key_t&& key, value_t type_t::*) {
			define_property_internal<prop, key_t, type_t>(L, std::forward<key_t>(key));
		}

		template <auto prop, typename key_t, typename type_t>
		static void define_property_internal(lua_State* L, key_t&& key) {
			push_variable(L, std::forward<key_t>(key));
			lua_pushcclosure(L, &iris_lua_t::property_proxy<prop, type_t>, 0);
			lua_rawset(L, -3);
		}

		template <typename type_t>
		struct has_finalize {
			template <typename> static std::false_type test(...);
			template <typename impl_t> static auto test(int)
				-> decltype(std::declval<impl_t>().lua_finalize(std::declval<iris_lua_t>()), std::true_type());
			static constexpr bool value = std::is_same<decltype(test<type_t>(0)), std::true_type>::value;
		};

		// will be called when __gc triggerred
		template <typename type_t>
		static int delete_object(lua_State* L) {
			type_t* p = reinterpret_cast<type_t*>(lua_touserdata(L, 1));

			// call lua_finalize if needed
			if constexpr (has_finalize<type_t>::value) {
				p->lua_finalize(iris_lua_t(L));
			}

			assert(p != nullptr);
			// do not free the memory (let lua gc it), just call the destructor
			p->~type_t();

			return 0;
		}

		// pass argument by upvalues
		template <typename type_t, typename... args_t, size_t... k>
		static void invoke_create(type_t* p, lua_State* L, std::index_sequence<k...>) {
			new (p) type_t(*reinterpret_cast<args_t*>(lua_touserdata(L, lua_upvalueindex(2 + int(k))))...);
		}

		// create a lua managed object
		template <typename type_t, int user_value_count, typename... args_t>
		static int create_object(lua_State* L) {
			stack_guard_t guard(L, 1);
			type_t* p = reinterpret_cast<type_t*>(lua_newuserdatauv(L, sizeof(type_t), user_value_count));
			invoke_create<type_t, args_t...>(p, L, std::make_index_sequence<sizeof...(args_t)>());
			lua_pushvalue(L, lua_upvalueindex(1));
			lua_setmetatable(L, -2);

			return 1;
		}

		// get multiple variables from a lua table and pack them into a tuple/pair 
		template <bool positive, int index, typename type_t, typename... args_t>
		static type_t get_tuple_variables(lua_State* L, int stack_index, args_t&&... args) {
			if constexpr (index < std::tuple_size_v<type_t>) {
				lua_rawgeti(L, stack_index, index + 1);
				// notice that we need minus one if stack_index is negetive, since  lua_rawgeti will add one temporal variable on stack
				return get_tuple_variables<positive, index + 1, type_t>(L, positive ? stack_index : stack_index - 1, std::forward<args_t>(args)..., get_variable<std::tuple_element_t<index, type_t>>(L, -1));
			} else {
				// pop all temporal variables and construct tuple
				lua_pop(L, index);
				return type_t({ std::forward<args_t>(args)... });
			}
		}

		template <typename type_t>
		struct has_reserve {
			template <typename> static std::false_type test(...);
			template <typename impl_t> static auto test(int)
				-> decltype(std::declval<impl_t>().reserve(1), std::true_type());
			static constexpr bool value = std::is_same<decltype(test<type_t>(0)), std::true_type>::value;
		};

		// get C++ variable from lua stack with given index
		template <typename type_t, bool skip_checks = false>
		static type_t get_variable(lua_State* L, int index) {
			using value_t = std::remove_volatile_t<std::remove_const_t<std::remove_reference_t<type_t>>>;
			stack_guard_t guard(L);

			if constexpr (std::is_base_of_v<ref_t, value_t>) {
				using internal_type_t = typename value_t::internal_type_t;
				if constexpr (!std::is_void_v<internal_type_t>) {
					auto internal_value = get_variable<internal_type_t, skip_checks>(L, index);
					if (internal_value) {
						lua_pushvalue(L, index);
						return value_t(luaL_ref(L, LUA_REGISTRYINDEX), internal_value);
					} else {
						return value_t();
					}
				} else {
					lua_pushvalue(L, index);
					return value_t(luaL_ref(L, LUA_REGISTRYINDEX));
				}
			} else if constexpr (iris_lua_convert_t<value_t>::value) {
				return iris_lua_convert_t<value_t>::from_lua(L, index);
			} else if constexpr (std::is_same_v<value_t, bool>) {
				return static_cast<value_t>(lua_toboolean(L, index));
			} else if constexpr (std::is_same_v<value_t, void*> || std::is_same_v<value_t, const void*>) {
				return static_cast<value_t>(lua_touserdata(L, index));
			} else if constexpr (std::is_integral_v<value_t> || std::is_enum_v<value_t>) {
				return static_cast<value_t>(lua_tointeger(L, index));
			} else if constexpr (std::is_floating_point_v<value_t>) {
				return static_cast<value_t>(lua_tonumber(L, index));
			} else if constexpr (std::is_same_v<value_t, std::string_view> || std::is_same_v<value_t, std::string>) {
				size_t len = 0;
				// do not accept implicit __tostring casts
				if (lua_type(L, index) == LUA_TSTRING) {
					const char* str = lua_tolstring(L, index, &len);
					return value_t(str, len);
				} else {
					return "";
				}
			} else if constexpr (iris_is_tuple_v<value_t>) {
				if (index < 0) {
					return get_tuple_variables<false, 0, value_t>(L, index);
				} else {
					return get_tuple_variables<true, 0, value_t>(L, index);
				}
			} else if constexpr (iris_is_iterable_v<value_t>) {
				type_t result;
				if (lua_istable(L, index)) {
					if constexpr (iris_is_map_v<value_t>) {
						// for map-like containers, convert to lua hash table 
						using key_type = typename value_t::key_type;
						using mapped_type = typename value_t::mapped_type;

						lua_pushnil(L);
						while (lua_next(L, -2) != 0) {
							result[get_variable<key_type>(L, -2)] = get_variable<mapped_type>(L, -1);
							lua_pop(L, 1);
						}
					} else {
						// otherwise convert to lua array table
						int size = static_cast<int>(lua_rawlen(L, index));
						if constexpr (has_reserve<value_t>::value) {
							result.reserve(size);
						}

						for (int i = 0; i < size; i++) {
							lua_rawgeti(L, index, i + 1);
							result.emplace_back(get_variable<typename type_t::value_type>(L, -1));
							lua_pop(L, 1);
						}
					}
				}

				return result;
			} else if constexpr (std::is_pointer_v<value_t>) {
				if constexpr (!skip_checks) {
					// try to extract object
					if (lua_getmetatable(L, index) == LUA_TNIL) {
						return value_t();
					}

#if LUA_VERSION_NUM <= 502
					lua_getfield(L, index, "__hash");
					if (lua_type(L, -1) == LUA_TNIL) {
#else
					if (lua_getfield(L, index, "__hash") == LUA_TNIL) {
#endif
						lua_pop(L, 2);
						return value_t();
					}

					// returns empty if hashes are not equal!
					static const size_t hash_code = typeid(std::remove_volatile_t<std::remove_const_t<std::remove_reference_t<std::remove_pointer_t<value_t>>>>).hash_code();
					if (lua_touserdata(L, -1) != reinterpret_cast<void*>(hash_code & 0xffffffff)) {
						lua_pop(L, 2);
						return value_t();
					}

					lua_pop(L, 2);
				}

				return reinterpret_cast<type_t>(lua_touserdata(L, index));
			} else if constexpr (std::is_base_of_v<require_base_t, value_t>) {
				return get_variable<typename value_t::internal_type_t, true>(L, index);
			}

			return value_t();
		}

		// invoke C++ function from lua stack
		template <auto function, int index, typename return_t, typename tuple_t, typename... params_t>
		static int function_invoke(lua_State* L, int stack_index, params_t&&... params) {
			if constexpr (index < std::tuple_size_v<tuple_t>) {
				if constexpr (std::is_same_v<iris_lua_t, std::remove_volatile_t<std::remove_const_t<std::remove_reference_t<std::tuple_element_t<index, tuple_t>>>>>) {
					return function_invoke<function, index + 1, return_t, tuple_t>(L, stack_index, std::forward<params_t>(params)..., iris_lua_t(L));
				} else {
					return function_invoke<function, index + 1, return_t, tuple_t>(L, stack_index + 1, std::forward<params_t>(params)..., get_variable<std::tuple_element_t<index, tuple_t>>(L, stack_index));
				}
			} else {
				if constexpr (std::is_void_v<return_t>) {
					function(std::forward<params_t>(params)...);
					return 0;
				} else {
					push_variable(L, function(std::forward<params_t>(params)...));
					return 1;
				}
			}
		}

		template <int index>
		static void check_required_parameters(lua_State* L) {}

		template <int index, typename first_t, typename... args_t>
		static void check_required_parameters(lua_State* L) {
			using value_t = std::remove_volatile_t<std::remove_const_t<std::remove_reference_t<first_t>>>;
			if constexpr (std::is_base_of_v<require_base_t, value_t>) {
				using internal_type_t = typename value_t::internal_type_t;
				auto var = get_variable<internal_type_t>(L, index);
				if (!var) {
					luaL_error(L, "Required parameter %d of type %s is invalid.\n", index, typeid(first_t).name());
				}

				if constexpr (std::is_base_of_v<ref_t, internal_type_t>) {
					deref(L, var);
				}
			}

			check_required_parameters<index + (std::is_same_v<iris_lua_t, std::remove_volatile_t<std::remove_const_t<std::remove_reference_t<first_t>>>> ? 0 : 1), args_t...>(L);
		}

		template <auto function, typename return_t, typename... args_t>
		static int function_proxy(lua_State* L) {
			check_required_parameters<1, args_t...>(L);
			return function_invoke<function, 0, return_t, std::tuple<std::remove_volatile_t<std::remove_const_t<std::remove_reference_t<args_t>>>...>>(L, 1);
		}

		template <auto function, int index, typename coroutine_t, typename tuple_t, typename... params_t>
		static bool function_coroutine_invoke(lua_State* L, int stack_index, params_t&&... params) {
			if constexpr (index < std::tuple_size_v<tuple_t>) {
				if constexpr (std::is_same_v<iris_lua_t, std::remove_volatile_t<std::remove_const_t<std::remove_reference_t<std::tuple_element_t<index, tuple_t>>>>>) {
					return function_coroutine_invoke<function, index + 1, coroutine_t, tuple_t>(L, stack_index, std::forward<params_t>(params)..., iris_lua_t(L));
				} else {
					return function_coroutine_invoke<function, index + 1, coroutine_t, tuple_t>(L, stack_index + 1, std::forward<params_t>(params)..., get_variable<std::tuple_element_t<index, tuple_t>>(L, stack_index));
				}
			} else {
				// mark state
				using return_t = typename coroutine_t::return_type_t;
				auto coroutine = function(std::forward<params_t>(params)...);

				if constexpr (!std::is_void_v<return_t>) {
					coroutine.complete([L, p = static_cast<void*>(&coroutine)](return_t&& value) {
						push_variable(L, std::move(value));
						push_variable(L, p);
						coroutine_continuation(L);
					}).run();
				} else {
					coroutine.complete([L, p = static_cast<void*>(&coroutine)]() {
						lua_pushnil(L);
						push_variable(L, p);
						coroutine_continuation(L);
					}).run();
				}

				// already completed?
				if (lua_touserdata(L, -1) == &coroutine) {
					lua_pop(L, 1);
					return true;
				} else {
					return false;
				}
			}
		}

		static void coroutine_continuation(lua_State* L) {
			if (lua_status(L) == LUA_YIELD) {
				lua_pop(L, 1);
#if LUA_VERSION_NUM <= 501
				int ret = lua_resume(L, 1);
#elif LUA_VERSION_NUM <= 503
				int ret = lua_resume(L, nullptr, 1);
#else
				int nres = 0;
				int ret = lua_resume(L, nullptr, 1, &nres);
#endif
				if (ret != LUA_OK && ret != LUA_YIELD) {
					// error!
					fprintf(stderr, "[ERROR] iris_lua_t::function_coutine_proxy() -> resume error: %s\n", lua_tostring(L, -1));
					lua_pop(L, 1);
				}
			}
		}

		template <auto function, typename coroutine_t, typename... args_t>
		static int function_coroutine_proxy(lua_State* L) {
			check_required_parameters<1, args_t...>(L);
			if (function_coroutine_invoke<function, 0, coroutine_t, std::tuple<std::remove_volatile_t<std::remove_const_t<std::remove_reference_t<args_t>>>...>>(L, 1)) {
				return 1;
			} else {
				return lua_yield(L, 0);
			}
		}

		// for properties, define a function as:
		// proxy([newvalue]) : oldvalue
		// will not assigned to newvalue if newvalue is not provided
		template <auto prop, typename type_t>
		static int property_proxy(lua_State* L) {
			type_t* object = get_variable<type_t*>(L, 1);
			if (object == nullptr) {
				luaL_error(L, "The first parameter of a property must be a C++ instance of type %s", typeid(type_t).name());
				return 0;
			}

			stack_guard_t guard(L, 1);
			using value_t = decltype(object->*prop);
			if constexpr (std::is_const_v<std::remove_reference_t<value_t>>) {
				push_variable(L, object->*prop); // return the original value 
			} else {
				bool assign = !lua_isnone(L, 2);
				push_variable(L, object->*prop);

				if (assign) {
					object->*prop = get_variable<std::remove_reference_t<value_t>>(L, 2);
				}
			}

			return 1;
		}

		// push variables from a tuple into a lua table
		template <int index, typename type_t>
		static void push_tuple_variables(lua_State* L, type_t&& variable) {
			using value_t = std::remove_volatile_t<std::remove_const_t<std::remove_reference_t<type_t>>>;
			if constexpr (index < std::tuple_size_v<value_t>) {
				if constexpr (std::is_rvalue_reference_v<type_t&&>) {
					push_variable(L, std::move(std::get<index>(variable)));
				} else {
					push_variable(L, std::get<index>(variable));
				}

				lua_rawseti(L, -2, index + 1);
				push_tuple_variables<index + 1>(L, std::forward<type_t>(variable));
			}
		}

		template <typename type_t>
		static void push_variable(lua_State* L, type_t&& variable) {
			using value_t = std::remove_volatile_t<std::remove_const_t<std::remove_reference_t<type_t>>>;
			stack_guard_t guard(L, 1);

			if constexpr (std::is_base_of_v<ref_t, value_t>) {
				lua_rawgeti(L, LUA_REGISTRYINDEX, variable.value);
				// deference if it's never used
				if constexpr (std::is_rvalue_reference_v<type_t&&>) {
					deref(L, variable);
				}
			} else if constexpr (iris_lua_convert_t<value_t>::value) {
				iris_lua_convert_t<value_t>::to_lua(L, std::forward<type_t>(variable));
			} else if constexpr (std::is_same_v<value_t, void*> || std::is_same_v<value_t, const void*>) {
				lua_pushlightuserdata(L, const_cast<value_t>(variable));
			} else if constexpr (std::is_same_v<value_t, bool>) {
				lua_pushboolean(L, static_cast<bool>(variable));
			} else if constexpr (std::is_integral_v<value_t> || std::is_enum_v<value_t>) {
				lua_pushinteger(L, static_cast<lua_Integer>(variable));
			} else if constexpr (std::is_floating_point_v<value_t>) {
				lua_pushnumber(L, static_cast<lua_Number>(variable));
			} else if constexpr (std::is_constructible_v<std::string_view, value_t>) {
				std::string_view view = variable;
				lua_pushlstring(L, view.data(), view.length());
			} else if constexpr (iris_is_tuple_v<value_t>) {
				lua_createtable(L, std::tuple_size_v<value_t>, 0);
				push_tuple_variables<0>(L, std::forward<type_t>(variable));
			} else if constexpr (iris_is_iterable_v<value_t>) {
				if constexpr (iris_is_map_v<value_t>) {
					lua_createtable(L, 0, static_cast<int>(variable.size()));

					for (auto&& pair : variable) {
						if constexpr (std::is_rvalue_reference_v<type_t&&>) {
							push_variable(L, std::move(pair.first));
							push_variable(L, std::move(pair.second));
						} else {
							push_variable(L, pair.first);
							push_variable(L, pair.second);
						}

						lua_rawset(L, -3);
					}
				} else {
					lua_createtable(L, static_cast<int>(variable.size()), 0);

					int i = 0;
					for (auto&& value : variable) {
						if constexpr (std::is_rvalue_reference_v<type_t&&>) {
							push_variable(L, std::move(value));
						} else {
							push_variable(L, value);
						}

						lua_rawseti(L, -2, ++i);
					}
				}
			} else if constexpr (std::is_null_pointer_v<value_t>) {
				lua_pushnil(L);
			} else {
				lua_pushnil(L);
				assert(false);
			}
		}

	protected:
		lua_State* state = nullptr;
	};
}

