/*
The Iris Concurrency Framework

This software is a C++ 17 Header-Only reimplementation of core part from project PaintsNow.

The MIT License (MIT)

Copyright (c) 2014-2025 PaintDream

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
#include <optional>

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
#define lua_absindex(L, index) ((index > 0) || (index <= LUA_REGISTRYINDEX) ? (index) : (lua_gettop(L) + 1 + index))
#endif

#if LUA_VERSION_NUM <= 502
#define LUA_ENABLE_YIELDK 0
#define LUA_CLEAR_STACK_ON_YIELD 1
#else
#define LUA_ENABLE_YIELDK 1
#define LUA_CLEAR_STACK_ON_YIELD 0
#endif

#if LUA_VERSION_NUM <= 503
#define lua_newuserdatauv(L, size, uv) lua_newuserdata(L, size)
#endif

namespace iris {
	template <typename type_t, typename = void>
	struct iris_lua_traits_t : std::false_type {
		using type = type_t;
	};

	// A simple lua binding with C++17
	struct iris_lua_t : enable_read_write_fence_t<> {
		static constexpr size_t size_mask_view = 1u;
		static constexpr size_t size_mask_alignment = 2u;

		// borrow from an existing state
		explicit iris_lua_t(lua_State* L) noexcept : state(L) {}
		iris_lua_t(iris_lua_t&& rhs) noexcept : state(rhs.state) { rhs.state = nullptr; }
		iris_lua_t(const iris_lua_t& rhs) noexcept : state(rhs.state) {}
		iris_lua_t& operator = (const iris_lua_t& rhs) noexcept {
			state = rhs.state;
			return *this;
		}

		iris_lua_t& operator = (iris_lua_t&& rhs) noexcept {
			if (this != &rhs) {
				state = rhs.state;
				rhs.state = nullptr;
			}

			return *this;
		}

		// get a compiler-related hash from a type
		template <typename type_t>
		static size_t get_hash() noexcept {
			static size_t hash = std::hash<std::string_view>()(typeid(type_t).name()) & 0xFFFFFFFF; // compatible with tagged pointer trick by LuaJIT
			return hash;
		}

		// get raw lua_State*, avoid using raw lua apis on it if possible, since it may broken stack layers in context
		operator lua_State* () const noexcept {
			return get_state();
		}

		lua_State* get_state() const noexcept {
			return state;
		}

		template <typename... args_t>
		void systrap(const char* category, const char* format, args_t&&... args) const {
			iris_lua_t::systrap(state, category, format, std::forward<args_t>(args)...);
		}

		// systrap is a low level error-capturing machanism
		// usually you can get errors from result_error_t<> when calling lua
		// but in LuaJIT and Lua 5.1, it is impossible to retrieve errors from C-lua mixed coroutines
		// so you could declare a __iris_systrap__ lua variable to make a workaround.
		template <typename... args_t>
		static void systrap(lua_State* L, const char* category, const char* format, args_t&&... args) {
			stack_guard_t stack_guard(L);
			lua_getglobal(L, "__iris_systrap__");

			if (lua_type(L, -1) == LUA_TFUNCTION) {
				lua_pushstring(L, category);
				lua_pushfstring(L, format, std::forward<args_t>(args)...);
				if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
					IRIS_LOGERROR("iris_lua_t::systrap() -> Unresolved systrap function!\n");
					lua_pop(L, 1);
				}
			} else {
				IRIS_LOGERROR(format, std::forward<args_t>(args)...);
				lua_pop(L, 1);
			}
		}

		// any return value with not-null message will cause a lua error
		struct result_error_t {
			template <typename value_t>
			result_error_t(value_t&& value) : message(std::forward<value_t>(value)) {}
			std::string message;
		};

		template <typename return_t>
		struct optional_result_t : std::conditional_t<std::is_void_v<return_t>, std::optional<std::nullptr_t>, std::optional<return_t>> {
			using base_t = std::conditional_t<std::is_void_v<return_t>, std::optional<std::nullptr_t>, std::optional<return_t>>;
			using value_t = std::conditional_t<std::is_void_v<return_t>, std::nullptr_t, return_t>;

			optional_result_t() : base_t(value_t()) {}
			optional_result_t(result_error_t&& err) : message(std::move(err.message)) {}

			template <typename... args_t>
			optional_result_t(args_t&&... args) : base_t(std::forward<args_t>(args)...) {}

			std::string message;
		};

		// holding a lua value
		// be aware of lua.deref() it before destruction!
		struct ref_t {
			explicit ref_t(int v = LUA_REFNIL) noexcept : ref_index(v) { IRIS_ASSERT(LUA_REFNIL == 0 || v != 0); }
			~ref_t() noexcept { IRIS_ASSERT(ref_index == LUA_REFNIL); }
			ref_t(ref_t&& rhs) noexcept : ref_index(rhs.ref_index) { rhs.ref_index = LUA_REFNIL; }
			ref_t(const ref_t& rhs) = delete;
			ref_t& operator = (const ref_t& rhs) = delete;
			ref_t& operator = (ref_t&& rhs) noexcept {
				if (this != &rhs) {
					IRIS_ASSERT(ref_index == LUA_REFNIL);
					std::swap(rhs.ref_index, ref_index);
				}
				
				return *this;
			}

			using internal_type_t = void;

			operator bool() const noexcept {
				return ref_index != LUA_REFNIL;
			}

			int get_ref_index() const noexcept {
				return ref_index;
			}

			void deref(iris_lua_t lua) noexcept {
				lua.deref(std::move(*this));
			}

			int get_type(iris_lua_t lua) noexcept {
				lua_State* L = lua.get_state();
				stack_guard_t stack_guard(L);

				push_variable(L, *this);
				int type = lua_type(L, -1);
				lua_pop(L, 1);

				return type;
			}

			// call f if this is nil, and update this with its return value
			template <typename func_t>
			ref_t& once(iris_lua_t lua, func_t&& f) & {
				if (!*this) {
					*this = f(lua);
				}

				return *this;
			}

			template <typename func_t>
			ref_t&& once(iris_lua_t lua, func_t&& f) && {
				static_cast<ref_t*>(this)->once(lua, std::forward<func_t>(f));
				return std::move(*this);
			}

			// call f with this as the current context table
			template <typename func_t>
			ref_t& with(iris_lua_t lua, func_t&& f) & {
				lua.with(*this, std::forward<func_t>(f));
				return *this;
			}

			template <typename func_t>
			ref_t&& with(iris_lua_t lua, func_t&& f) && {
				static_cast<ref_t*>(this)->with(lua, std::forward<func_t>(f));
				return std::move(*this);
			}

			// convert another value
			// note: use as<ref_t>(lua) to duplicate current variable
			template <typename ref_index_t>
			ref_index_t as(iris_lua_t lua) const {
				lua_State* L = lua.get_state();
				stack_guard_t stack_guard(L);

				push_variable(L, *this);
				ref_index_t ret = iris_lua_t::get_variable<ref_index_t>(L, -1);
				lua_pop(L, 1);

				return ret; // named return ref_index optimization
			}

			// get value from this table
			template <typename ref_index_t = ref_t, typename key_t>
			optional_result_t<ref_index_t> get(iris_lua_t lua, key_t&& key) const {
				lua_State* L = lua.get_state();
				stack_guard_t stack_guard(L);

				push_variable(L, *this);
				if (lua_istable(L, -1)) {
					push_variable(L, std::forward<key_t>(key));
#if LUA_VERSION_NUM <= 502
					lua_rawget(L, -2);
					if (lua_type(L, -1) != LUA_TNIL) {
#else
					if (lua_rawget(L, -2) != LUA_TNIL) {
#endif
						ref_index_t ret = get_variable<ref_index_t>(L, -1);
						lua_pop(L, 2);
						return ret;
					} else {
						lua_pop(L, 2);
						return result_error_t("Invalid key");
					}
				} else {
					lua_pop(L, 1);
					return result_error_t("Not a table");
				}
			}

			// set value to this table
			template <typename ref_index_t, typename key_t>
			ref_t& set(iris_lua_t lua, key_t&& key, ref_index_t&& ref_index) & {
				lua_State* L = lua.get_state();
				stack_guard_t stack_guard(L);

				push_variable(L, *this);
				if (lua_istable(L, -1)) {
					push_variable(L, std::forward<key_t>(key));
					push_variable(L, std::forward<ref_index_t>(ref_index));
					lua_rawset(L, -3);
				}

				lua_pop(L, 1);
				return *this;
			}

			template <typename ref_index_t, typename key_t>
			ref_t&& set(iris_lua_t lua, key_t&& key, ref_index_t&& ref_index) && {
				static_cast<ref_t*>(this)->set(lua, std::forward<key_t>(key), std::forward<ref_index_t>(ref_index));
				return std::move(*this);
			}

			template <auto ref_index_t, typename key_t>
			ref_t& set(iris_lua_t lua, key_t&& key) & {
				lua_State* L = lua.get_state();
				stack_guard_t stack_guard(L);

				push_variable(L, *this);
				if (lua_istable(L, -1)) {
					push_variable(L, std::forward<key_t>(key));
					push_variable<ref_index_t>(L);
					lua_rawset(L, -3);
				}

				lua_pop(L, 1);
				return *this;
			}

			template <auto ref_index_t, typename key_t>
			ref_t&& set(iris_lua_t lua, key_t&& key) && {
				static_cast<ref_t*>(this)->set<ref_index_t>(lua, std::forward<key_t>(key));
				return std::move(*this);
			}

			// iterate this table, returning this
			template <typename ref_index_t, typename key_t, typename func_t>
			ref_t& for_each(iris_lua_t lua, func_t&& func) & {
				lua_State* L = lua.get_state();
				stack_guard_t stack_guard(L);
				push_variable(L, *this);

				lua_pushnil(L);
				while (lua_next(L, -2) != 0) {
					// since we do not allow implicit lua_tostring conversion, so it's safe to extract key without duplicating it
					if (func(get_variable<key_t>(L, -2), get_variable<ref_index_t>(L, -1))) {
						lua_pop(L, 1);
						break;
					}

					lua_pop(L, 1);
				}

				lua_pop(L, 1);
				return *this;
			}

			template <typename ref_index_t, typename key_t, typename func_t>
			ref_t&& for_each(iris_lua_t lua, func_t&& func) && {
				static_cast<ref_t*>(this)->for_each(lua, std::forward<func_t>(func));
				return std::move(*this);
			}

			// get raw length of this object
			size_t size(iris_lua_t lua) const {
				lua_State* L = lua.get_state();
				stack_guard_t stack_guard(L);

				push_variable(L, *this);
				size_t len = static_cast<size_t>(lua_rawlen(L, -1));
				lua_pop(L, 1);

				return len;
			}

			friend struct iris_lua_t;

		private:
			int ref_index;
		};

		// ref_t for types
		template <typename subtype_t>
		struct reftype_t : ref_t {
			using type_t = subtype_t;
			reftype_t(int v = LUA_REFNIL) noexcept : ref_t(v) {}
			reftype_t(reftype_t&& rhs) noexcept : ref_t(std::move(static_cast<ref_t&>(rhs))) {}
			reftype_t(const reftype_t& rhs) = delete;
			reftype_t& operator = (const reftype_t& rhs) = delete;
			reftype_t& operator = (reftype_t&& rhs) noexcept {
				if (this != &rhs) {
					ref_t::operator = (std::move(static_cast<ref_t&>(rhs)));
				}

				return *this;
			}

			static const void* get_type_hash() noexcept {
				return reinterpret_cast<const void*>(get_hash<type_t>());
			}

			// register current table as global type to registry table.
			reftype_t& make_registry(iris_lua_t lua, bool enable = true) & {
				const void* hash = get_type_hash();
				if (hash != nullptr) {
					if (enable) {
						lua.set_registry(hash, *this);
					} else {
						lua.set_registry(hash, nullptr);
					}
				}

				return *this;
			}

			// for chain expressions
			reftype_t&& make_registry(iris_lua_t lua, bool enable = true) && {
				static_cast<reftype_t*>(this)->make_registry(lua, enable);
				return std::move(*this);
			}

			static ref_t get_registry(iris_lua_t lua) {
				return lua.get_registry<ref_t>(get_type_hash());
			}

			template <typename refbase_t>
			reftype_t& make_cast(iris_lua_t lua, refbase_t&& base) & {
				lua.cast_type(std::forward<refbase_t>(base), *this);
				return *this;
			}

			template <typename refbase_t>
			reftype_t&& make_cast(iris_lua_t lua, refbase_t&& base) && {
				lua.cast_type(std::forward<refbase_t>(base), *this);
				return std::move(*this);
			}
		};

		// ref_t for custom types
		template <typename type_t, typename value_t>
		struct refvalue_t : ref_t {
			refvalue_t(int v = LUA_REFNIL) noexcept : ref_t(v) {}
			template <typename input_t>
			refvalue_t(int v = LUA_REFNIL, input_t&& i = input_t()) noexcept : ref_t(v), value(std::forward<input_t>(i)) {}
			refvalue_t(refvalue_t&& rhs) noexcept : ref_t(std::move(static_cast<ref_t&>(rhs))), value(std::move(rhs.value)) {}
			refvalue_t(const refvalue_t& rhs) = delete;
			refvalue_t& operator = (const refvalue_t& rhs) = delete;
			refvalue_t& operator = (refvalue_t&& rhs) noexcept {
				if (this != &rhs) {
					ref_t::operator = (std::move(static_cast<ref_t&>(rhs)));
					std::swap(value, rhs.value);
				}

				return *this;
			}

			using internal_type_t = value_t;

			const value_t& operator -> () const noexcept {
				return get();
			}

			value_t& operator -> () noexcept {
				return get();
			}

			const value_t& get() const noexcept {
				return value;
			}

			value_t& get() noexcept {
				return value;
			}

			friend struct iris_lua_t;

		protected:
			value_t value;
		};

		template <typename type_t>
		using refptr_t = refvalue_t<type_t, type_t*>;

		template <typename type_t>
		using refview_t = refvalue_t<type_t, type_t>;

		// requried_t is for validating parameters before actually call the C++ stub
		// will raise a lua error if it fails
		struct required_base_t {};

		template <typename type_t>
		struct required_t : required_base_t {
			using required_type_t = type_t;

			template <typename... args_t>
			required_t(args_t&&... args) : value(std::forward<args_t>(args)...) {}

			operator type_t& () noexcept {
				return value;
			}

			operator const type_t& () const noexcept {
				return value;
			}

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
					iris_lua_t::deref(state, std::move(*r));
				}
			}

			lua_State* state;
			std::array<ref_t*, count> refs;
		};

		template <typename... args_t>
		auto ref_guard(args_t&... args) noexcept {
			return refguard_t<sizeof...(args_t)>(state, args...);
		}

		// load a code thunk and return an callable function
		optional_result_t<ref_t> load(std::string_view code, std::string_view name = "") {
			auto guard = write_fence();
			lua_State* L = state;
			stack_guard_t stack_guard(L);

			if (luaL_loadbuffer(L, code.data(), code.size(), name.data()) != LUA_OK) {
				iris_lua_t::systrap(L, "error.load", "iris_lua_t::run() -> load code error: %s\n", luaL_optstring(L, -1, ""));
				result_error_t ret(luaL_optstring(L, -1, ""));
				lua_pop(L, 1);

				return ret;
			}

			return ref_t(luaL_ref(L, LUA_REGISTRYINDEX));
		}

		// a guard for checking stack balance
		struct stack_guard_t {
#if IRIS_DEBUG
			stack_guard_t(lua_State* state, int offset = 0) noexcept : L(state) { top = lua_gettop(L) + offset; }
			~stack_guard_t() noexcept { IRIS_ASSERT(top == lua_gettop(L)); }
			void append(int diff) noexcept { top += diff; }

			lua_State* L;
			int top;
#else
			stack_guard_t(lua_State* state, int offset = 0) noexcept {}
			void append(int diff) noexcept {}
#endif
		};

		auto stack_guard(int offset = 0) noexcept {
			return stack_guard_t(state, offset);
		}

		template <typename type_t, typename = void>
		struct has_lua_registar : std::false_type {
			static void default_registar(iris_lua_t) {}
			static constexpr auto get_registar() noexcept {
				return &default_registar;
			}
		};

		template <typename type_t>
		struct has_lua_registar<type_t, iris_void_t<decltype(&iris_lua_traits_t<type_t>::type::lua_registar)>> : std::true_type {
			static constexpr auto get_registar() noexcept {
				return &iris_lua_traits_t<type_t>::type::lua_registar;
			}
		};

		template <typename type_t, typename = void>
		struct is_functor : std::false_type {};

		template <typename type_t>
		struct is_functor<type_t, iris_void_t<decltype(&type_t::operator ())>> : std::true_type {};

		// create a lua managed object
		template <typename type_t>
		static optional_result_t<type_t*> trivial_object_creator(iris_lua_t, type_t* object) {
			return new (object) type_t();
		}

		// register a new type, taking registar from &type_t::lua_registar by default, and you could also specify your own registar.
		template <typename type_t, int user_value_count = 0, auto registar = has_lua_registar<type_t>::get_registar(), typename... args_t, typename... envs_t>
		reftype_t<type_t> make_type(std::string_view name, optional_result_t<type_t*> (*creator)(iris_lua_t, type_t*, args_t...) = &trivial_object_creator<type_t>, envs_t&&... envs) {
			IRIS_PROFILE_SCOPE(__FUNCTION__);
			auto guard = write_fence();

			lua_State* L = state;
			stack_guard_t stack_guard(L);
			lua_newtable(L);

			make_uniform_meta_internal<type_t, user_value_count>(L);

			// hash code is to check types when passing as a argument to C++
			push_variable(L, "__hash");
			push_variable(L, reinterpret_cast<void*>(get_hash<type_t>()));
			lua_rawset(L, -3);

			push_variable(L, "__eq");
			push_variable(L, &equal_stub);
			lua_rawset(L, -3);

			// readable name
			push_variable(L, "__name");
			push_variable(L, name);
			lua_rawset(L, -3);

			push_variable(L, "__index");
			lua_pushvalue(L, -2);
			lua_rawset(L, -3);

			push_variable(L, "new");
			lua_pushvalue(L, -2);
			lua_pushlightuserdata(L, reinterpret_cast<void*>(creator));

			if constexpr (sizeof...(envs_t) > 0) {
				check_matched_parameters<std::tuple<envs_t...>, std::tuple<args_t...>, sizeof...(envs_t)>();
			}

			push_arguments(L, std::forward<envs_t>(envs)...);
			lua_pushcclosure(L, &iris_lua_t::new_object<type_t, user_value_count, sizeof...(envs), args_t...>, 2 + sizeof...(envs));
			lua_rawset(L, -3);

			// call custom registar if needed
			registar(iris_lua_t(L));
			return reftype_t<type_t>(luaL_ref(L, LUA_REGISTRYINDEX));
		}

		// build a cast relationship from target_meta to base_meta
		template <typename meta_base_t, typename meta_target_t>
		void cast_type(meta_base_t&& base_meta, meta_target_t&& target_meta) {
			IRIS_PROFILE_SCOPE(__FUNCTION__);
			static_assert(std::is_base_of<typename std::remove_reference_t<meta_base_t>::type_t, typename std::remove_reference_t<meta_target_t>::type_t>::value, "Incompatible type cast!");
			// IRIS_ASSERT(static_cast<typename std::remove_reference_t<meta_base_t>::type_t*>(reinterpret_cast<typename std::remove_reference_t<meta_target_t>::type_t*>(~size_t(0))) == reinterpret_cast<typename std::remove_reference_t<meta_base_t>::type_t*>(~size_t(0)));

			lua_State* L = state;
			stack_guard_t guard(L);

			push_variable(L, std::forward<meta_target_t>(target_meta));
			IRIS_ASSERT(lua_type(L, -1) == LUA_TTABLE);
			lua_newtable(L);
			lua_pushliteral(L, "__index");
			push_variable(L, std::forward<meta_base_t>(base_meta));
			IRIS_ASSERT(lua_type(L, -1) == LUA_TTABLE);
			lua_rawset(L, -3);
			lua_setmetatable(L, -2);
			lua_pop(L, 1);
		}

		template <typename type_t, typename... args_t>
		refptr_t<type_t> make_registry_object(args_t&&... args) {
			return make_object<type_t>(get_registry<ref_t>(reinterpret_cast<const void*>(get_hash<type_t>())), std::forward<args_t>(args)...);
		}

		// make an object with given meta
		template <typename type_t, typename meta_t, typename... args_t>
		refptr_t<type_t> make_object(meta_t&& meta, args_t&&... args) {
			IRIS_PROFILE_SCOPE(__FUNCTION__);

			lua_State* L = state;
			stack_guard_t guard(L);
			IRIS_ASSERT(*meta.template get<const void*>(*this, "__hash") == reinterpret_cast<const void*>(get_hash<type_t>()));

			static_assert(alignof(type_t) <= alignof(lua_Number), "Too large alignment for object holding.");
			type_t* p = reinterpret_cast<type_t*>(lua_newuserdatauv(L, iris_to_alignment(sizeof(type_t), size_mask_alignment), meta.template get<int>(*this, "__uservalue").value_or(0)));
			new (p) type_t(std::forward<args_t>(args)...);
			push_variable(L, std::forward<meta_t>(meta));
			lua_setmetatable(L, -2);
			initialize_object(L, lua_absindex(L, -1), p);

			return refptr_t<type_t>(luaL_ref(L, LUA_REGISTRYINDEX), p);
		}
		
		// make object view from registry meta
		template <typename type_t>
		refptr_t<type_t> make_registry_object_view(type_t* object) {
			return make_object_view<type_t>(get_registry<ref_t>(reinterpret_cast<const void*>(get_hash<type_t>())), object);
		}

		// make an object view with an exiting object
		template <typename type_t, typename meta_t, typename... args_t>
		refptr_t<type_t> make_object_view(meta_t&& meta, type_t* object) {
			IRIS_PROFILE_SCOPE(__FUNCTION__);
			IRIS_ASSERT(object != nullptr);

			lua_State* L = state;
			stack_guard_t guard(L);
			IRIS_ASSERT(*meta.template get<const void*>(*this, "__hash") == reinterpret_cast<const void*>(get_hash<type_t>()));

			static_assert(sizeof(type_t*) == sizeof(void*), "Unrecognized architecture.");
			size_t payload_size = 0;
			if constexpr (has_lua_view_payload<type_t>::value) {
				payload_size += iris_lua_traits_t<type_t>::type::lua_view_payload(iris_lua_t(L), object);
			}

			void* p = lua_newuserdatauv(L, (sizeof(type_t*) + payload_size) | size_mask_view, meta.template get<int>(*this, "__uservalue").value_or(0));
			*reinterpret_cast<type_t**>(p) = object;

			push_variable(L, std::forward<meta_t>(meta));
			lua_setmetatable(L, -2);

			// call lua_view_initialize if needed
			if constexpr (has_lua_view_initialize<type_t>::value) {
				iris_lua_traits_t<type_t>::type::lua_view_initialize(iris_lua_t(L), lua_absindex(L, -1), p);
			}

			return refptr_t<type_t>(luaL_ref(L, LUA_REGISTRYINDEX), object);
		}

		struct buffer_t {
			template <typename value_t>
			buffer_t& operator << (value_t&& value) {
				using type_t = remove_cvref_t<value_t>;
				if constexpr (std::is_convertible_v<value_t, std::string_view>) {
					std::string_view view = value;
					luaL_addlstring(buffer, view.data(), view.size());
				} else if constexpr (std::is_integral_v<type_t> && sizeof(type_t) > sizeof(char)) {
					luaL_addlstring(buffer, &value, sizeof(value));
				} else {
					luaL_addchar(buffer, value);
				}

				return *this;
			}

			friend struct iris_lua_t;

		protected:
			buffer_t(lua_State* s, luaL_Buffer* b) noexcept : L(s), buffer(b) { IRIS_ASSERT(L != nullptr && buffer != nullptr); }

			lua_State* L;
			luaL_Buffer* buffer;
		};

		// build string from C-style formatted construction
		template <typename... args_t>
		ref_t make_string(const char* fmt, args_t&&... args) {
			IRIS_PROFILE_SCOPE(__FUNCTION__);

			lua_State* L = state;
			stack_guard_t guard(L);
			lua_pushfstring(L, fmt, std::forward<args_t>(args)...);
			return ref_t(luaL_ref(L, LUA_REGISTRYINDEX));
		}

		// build string from customized function
		template <typename func_t>
		ref_t make_string(func_t&& func) {
			IRIS_PROFILE_SCOPE(__FUNCTION__);

			lua_State* L = state;
			stack_guard_t guard(L);
			luaL_Buffer buff;
			luaL_buffinit(L, &buff);
			func(buffer_t(L, &buff));
			luaL_pushresult(&buff);
			return ref_t(luaL_ref(L, LUA_REGISTRYINDEX));
		}

		// make ref from a given value
		template <typename value_t>
		ref_t make_value(value_t&& value) {
			lua_State* L = state;
			stack_guard_t guard(L);
			push_variable(L, std::forward<value_t>(value));
			return ref_t(luaL_ref(L, LUA_REGISTRYINDEX));
		}

		template <auto value_t>
		ref_t make_value() {
			lua_State* L = state;
			stack_guard_t guard(L);
			push_variable<value_t>(L);
			return ref_t(luaL_ref(L, LUA_REGISTRYINDEX));
		}

		// context pesudo keys
		struct context_this_t {};
		struct context_table_t {};
		struct context_upvalue_t {
			context_upvalue_t(int i) noexcept : index(i) {}
			int index;
		};

		struct context_stackvalue_t {
			context_stackvalue_t(int i) noexcept : index(i) {}
			int index;
		};

		struct context_stack_top_t {};
		struct context_stack_where_t {
			context_stack_where_t(int lv) noexcept : level(lv) {}
			int level;
		};

		// get from context
		template <typename value_t, typename key_t>
		value_t get_context(key_t&& key) {
			auto guard = write_fence();
			lua_State* L = state;
			stack_guard_t stack_guard(L);

			using type_t = remove_cvref_t<key_t>;
			if constexpr (std::is_same_v<type_t, context_this_t>) {
				IRIS_ASSERT(lua_isuserdata(L, 1));
				return get_variable<value_t>(L, 1);
			} else if constexpr (std::is_same_v<type_t, context_table_t>) {
				IRIS_ASSERT(lua_istable(L, -1));
				return get_variable<value_t>(L, -1);
			} else if constexpr (std::is_same_v<type_t, context_upvalue_t>) {
				return get_variable<value_t>(L, lua_upvalueindex(key.index));
			} else if constexpr (std::is_same_v<type_t, context_stackvalue_t>) {
				return get_variable<value_t>(L, key.index);
			} else if constexpr (std::is_same_v<type_t, context_stack_top_t>) {
				return lua_gettop(L);
			} else if constexpr (std::is_same_v<type_t, context_stack_where_t>) {
				luaL_where(L, key.level);
				value_t ret = get_variable<value_t>(L, -1);
				lua_pop(L, 1);
				return std::move(ret);
			} else {
				return value_t();
			}
		}

		// get from lua registry table
		template <typename value_t, typename key_t>
		value_t get_registry(key_t&& key) {
			auto guard = write_fence();
			lua_State* L = state;
			stack_guard_t stack_guard(L);

			push_variable(L, std::forward<key_t>(key));
			lua_rawget(L, LUA_REGISTRYINDEX);
			value_t value = get_variable<value_t>(L, -1);
			lua_pop(L, 1);

			return value;
		}

		// set lua registry table
		template <typename value_t, typename key_t>
		void set_registry(key_t&& key, value_t&& value) {
			auto guard = write_fence();
			lua_State* L = state;
			stack_guard_t stack_guard(L);

			push_variable(L, std::forward<key_t>(key));
			push_variable(L, std::forward<value_t>(value));
			lua_rawset(L, LUA_REGISTRYINDEX);
		}

		// get lua registry table
		template <auto value_t, typename key_t>
		void set_registry(key_t&& key) {
			auto guard = write_fence();
			lua_State* L = state;
			stack_guard_t stack_guard(L);

			push_variable(L, std::forward<key_t>(key));
			push_variable<value_t>(L);
			lua_rawset(L, LUA_REGISTRYINDEX);
		}

		// get lua global value from name
		template <typename value_t>
		value_t get_global(std::string_view key) {
			auto guard = write_fence();
			lua_State* L = state;
			stack_guard_t stack_guard(L);

			lua_getglobal(L, key.data());
			value_t value = get_variable<value_t>(L, -1);
			lua_pop(L, 1);

			return value;
		}

		// set lua global table with given name and value
		template <typename value_t, typename... envs_t>
		void set_global(std::string_view key, value_t&& value, envs_t&&... envs) {
			auto guard = write_fence();
			lua_State* L = state;
			stack_guard_t stack_guard(L);
			push_variable(L, std::forward<value_t>(value), std::forward<envs_t>(envs)...);
			lua_setglobal(L, key.data());
		}

		// set lua global table
		template <auto value_t, typename... envs_t>
		void set_global(std::string_view key, envs_t&&... envs) {
			auto guard = write_fence();
			lua_State* L = state;
			stack_guard_t stack_guard(L);
			push_variable<value_t>(L, std::forward<envs_t>(envs)...);
			lua_setglobal(L, key.data());
		}

		// set 'current' lua table, usually used in lua_registar callbacks
		template <typename value_t, typename key_t, typename... envs_t>
		void set_current(key_t&& key, value_t&& value, envs_t&&... envs) {
			auto guard = write_fence();

			lua_State* L = state;
			stack_guard_t stack_guard(L);

			push_variable(L, std::forward<key_t>(key));
			push_variable(L, std::forward<value_t>(value), std::forward<envs_t>(envs)...);
			lua_rawset(L, -3);
		}

		// set 'current' lua table, usually used in lua_registar callbacks
		// spec for constexpr ptr (methods, properties)
		template <auto ptr, typename key_t, typename... envs_t>
		void set_current(key_t&& key, envs_t&&... envs) {
			auto guard = write_fence();

			lua_State* L = state;
			stack_guard_t stack_guard(L);

			push_variable(L, std::forward<key_t>(key));
			push_variable<ptr>(L, std::forward<envs_t>(envs)...);
			lua_rawset(L, -3);
		}

		template <typename value_t, typename key_t>
		value_t get_current(key_t&& key) {
			auto guard = write_fence();
			lua_State* L = state;
			stack_guard_t stack_guard(L);

			push_variable(L, std::forward<key_t>(key));
			lua_rawget(L, -2);
			value_t value = get_variable<value_t>(L, -1);
			lua_pop(L, 1);

			return value;
		}
		
		// define metatable for current table, should be called nested in make_table call
		template <typename value_t>
		void set_current_metatable(value_t&& metatable) {
			auto guard = write_fence();

			lua_State* L = state;
			stack_guard_t stack_guard(L);
			push_variable(L, std::forward<value_t>(metatable));
			lua_setmetatable(L, -2);
		}

		template <typename value_t, typename operation_t>
		bool with(value_t&& value, operation_t&& op) {
			auto guard = write_fence();

			lua_State* L = state;
			stack_guard_t stack_guard(L);
			push_variable(L, std::forward<value_t>(value));

			if (!lua_isnoneornil(L, -1)) {
				op(*this);

				if constexpr (std::is_rvalue_reference_v<value_t&&>) {
					deref(std::move(value));
				}

				lua_pop(L, 1);

				return true;
			} else {
				lua_pop(L, 1);
				return false;
			}
		}

		template <typename function_t>
		static int forward(lua_State* L, function_t&& ptr) {
			if constexpr (std::is_convertible_v<function_t, int (*)(lua_State*)> || std::is_convertible_v<function_t, int (*)(lua_State*) noexcept>) {
				return ptr(L);
			} else if constexpr (std::is_member_function_pointer_v<function_t>) {
				return forward_method<function_t>(L, std::forward<function_t>(ptr));
			} else if constexpr (std::is_member_object_pointer_v<function_t>) {
				return forward_property<function_t>(L, std::forward<function_t>(ptr));
			} else if constexpr (std::is_pointer_v<function_t> && std::is_function_v<std::remove_pointer_t<function_t>>) {
				return forward_function<function_t>(L, std::forward<function_t>(ptr));
			} else {
				return forward_functor<function_t>(L, std::forward<function_t>(ptr), &function_t::operator ());
			}
		}

		// make a table, defining variables via set_current in callback function
		template <typename func_t>
		ref_t make_table(func_t&& func) {
			auto guard = write_fence();

			lua_State* L = state;
			stack_guard_t stack_guard(L);
			lua_newtable(L);

			func(iris_lua_t(L));
			return ref_t(luaL_ref(L, LUA_REGISTRYINDEX));
		}

		template <typename func_t>
		ref_t make_thread(func_t&& func) {
			auto guard = write_fence();

			lua_State* L = state;
			stack_guard_t stack_guard(L);
			lua_State* T = lua_newthread(L);

			func(iris_lua_t(T));
			return ref_t(luaL_ref(L, LUA_REGISTRYINDEX));
		}

		// dereference a ref
		void deref(ref_t&& r) noexcept {
			auto guard = write_fence();
			deref(state, std::move(r));
		}

		// native_* series functions may cause unbalanced stack, use them at your own risks
		template <typename value_t>
		void native_push_variable(value_t&& value) {
			auto guard = write_fence();
			push_variable(state, std::forward<value_t>(value));
		}

		template <auto ptr>
		void native_push_variable() {
			auto guard = write_fence();
			push_variable<ptr>(state);
		}

		void native_pop_variable(int count) {
			auto guard = write_fence();
			lua_pop(state, count);
		}

		int native_get_top() {
			auto guard = write_fence();
			return lua_gettop(state);
		}

		template <typename value_t>
		value_t native_get_variable(int index) {
			auto guard = write_fence();
			return get_variable<value_t>(state, index);
		}
	
		template <bool move, typename lua_t>
		void native_cross_transfer_variable(lua_t& target, int index) {
			auto guard = write_fence();

			lua_State* L = get_state();
			stack_guard_t stack_guard_source(L);
			lua_State* T = target.get_state();
			stack_guard_t stack_guard_target(T, 1);

			int src_index = lua_absindex(L, index);
			lua_newtable(L);
			lua_newtable(T);

			cross_transfer_variable<move>(state, target, src_index, lua_absindex(L, -1), lua_absindex(T, -1), 0);

			lua_replace(T, -2);
			lua_pop(L, 1);
		}

		template <typename callable_t>
		optional_result_t<int> native_call(callable_t&& reference, int param_count) {
			IRIS_PROFILE_SCOPE(__FUNCTION__);
			auto guard = write_fence();

			lua_State* L = state;
			// stack_guard_t stack_guard(L);
			int top = lua_gettop(L);
			push_variable(L, std::forward<callable_t>(reference));
			IRIS_ASSERT(lua_gettop(L) == top + 1);
			lua_insert(L, -param_count - 1);

			if (lua_pcall(L, param_count, LUA_MULTRET, 0) == LUA_OK) {
				return lua_gettop(L) - top + param_count;
			} else {
				iris_lua_t::systrap(L, "error.call", "iris_lua_t::call() -> call function failed! %s\n", luaL_optstring(L, -1, ""));
				result_error_t ret(luaL_optstring(L, -1, ""));
				lua_pop(L, 1);

				return ret;
			}
		}

		// call function in protect mode
		template <typename return_t, typename callable_t, typename... args_t>
		optional_result_t<return_t> call(callable_t&& reference, args_t&&... args) {
			IRIS_PROFILE_SCOPE(__FUNCTION__);

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
				} else {
					return {};
				}
			} else {
				iris_lua_t::systrap(L, "error.resume", "iris_lua_t::call() -> call function failed! %s\n", luaL_optstring(L, -1, ""));
				result_error_t ret(luaL_optstring(L, -1, ""));
				lua_pop(L, 1);

				return ret;
			}
		}

	protected:
		template <typename type_t>
		using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<type_t>>;

		template <typename type_t>
		using cast_arg_type_t = std::conditional_t<has_lua_registar<remove_cvref_t<type_t>>::value, remove_cvref_t<type_t>&, remove_cvref_t<type_t>>;
		
		// wrap a member function with normal function
		template <auto method, typename return_t, typename type_t, typename... args_t>
		static return_t method_function_adapter(required_t<type_t*>&& object, args_t&&... args) {
			if constexpr (!std::is_void_v<return_t>) {
				return (object.get()->*method)(std::forward<args_t>(args)...);
			} else {
				(object.get()->*method)(std::forward<args_t>(args)...);
			}
		}

		template <auto method, typename return_t, typename type_t, typename... args_t>
		static return_t method_functor_adapter(iris_lua_t lua, args_t&&... args) {
			lua_State* L = lua.get_state();
			type_t* ptr = reinterpret_cast<type_t*>(lua_touserdata(L, lua_upvalueindex(1)));
			IRIS_ASSERT(ptr != nullptr);

			if constexpr (!std::is_void_v<return_t>) {
				return (ptr->*method)(std::forward<args_t>(args)...);
			} else {
				(ptr->*method)(std::forward<args_t>(args)...);
			}
		}

		static void deref(lua_State* L, ref_t&& r) noexcept {
			if (r.ref_index != LUA_REFNIL) {
				luaL_unref(L, LUA_REGISTRYINDEX, r.ref_index);
				r.ref_index = LUA_REFNIL;
			}
		}

		static void push_arguments(lua_State* L) {}
		template <typename first_t, typename... args_t>
		static void push_arguments(lua_State* L, first_t&& first, args_t&&... args) {
			push_variable(L, std::forward<first_t>(first));
			push_arguments(L, std::forward<args_t>(args)...);
		}

		template <typename type_t = void>
		static type_t* extract_object_ptr(lua_State* L, int index) {
			size_t len = static_cast<size_t>(lua_rawlen(L, index));
			if (len & size_mask_view) {
				if constexpr (has_lua_view_extract<type_t>::value) {
					static_assert(has_lua_view_initialize<type_t>::value, "Must implement lua_view_initialize()");
					return static_cast<type_t*>(iris_lua_traits_t<type_t>::type::lua_view_extract(iris_lua_t(L), index, reinterpret_cast<type_t**>(lua_touserdata(L, index))));
				} else {
					return *reinterpret_cast<type_t**>(lua_touserdata(L, index));
				}
			} else {
				return reinterpret_cast<type_t*>(lua_touserdata(L, index));
			}
		}

		static int equal_stub(lua_State* L) noexcept {
			lua_pushboolean(L, extract_object_ptr<>(L, 1) == extract_object_ptr<>(L, 2));
			return 1;
		}

		// copy constructor stub
		template <typename type_t>
		static void construct_meta_internal(lua_State* L, type_t* p) {
			lua_pushvalue(L, -3);
			lua_setmetatable(L, -2);
			initialize_object<type_t>(L, lua_absindex(L, -1), p);
		}

		template <typename type_t, int user_value_count>
		static void copy_construct_stub(lua_State* L, const void* prototype, size_t rawlen) {
			type_t* p = new (reinterpret_cast<type_t*>(lua_newuserdatauv(L, rawlen, user_value_count))) type_t(*reinterpret_cast<const type_t*>(prototype));
			construct_meta_internal(L, p);
		}

		// copy constructor stub
		template <typename type_t, int user_value_count>
		static void move_construct_stub(lua_State* L, void* prototype, size_t rawlen) {
			type_t* p = new (reinterpret_cast<type_t*>(lua_newuserdatauv(L, rawlen, user_value_count))) type_t(std::move(*reinterpret_cast<type_t*>(prototype)));
			construct_meta_internal(L, p);
		}

		// view stub
		template <typename type_t, int user_value_count>
		static void view_construct_stub(lua_State* T, lua_State* L, int index, size_t rawlen) {
			type_t* src = extract_object_ptr<type_t>(L, index);

			type_t** p = reinterpret_cast<type_t**>(lua_newuserdatauv(T, rawlen, user_value_count));
			*p = src;

			lua_pushvalue(T, -2);
			lua_setmetatable(T, -2);

			if constexpr (has_lua_view_initialize<type_t>::value) {
				iris_lua_traits_t<type_t>::type::lua_view_initialize(iris_lua_t(T), lua_absindex(T, -1), p);
			}
		}

		// raw lua stub
		template <typename value_t>
		static void push_native(lua_State* L, value_t ptr) {
			lua_pushcfunction(L, ptr);
		}

		// four specs for [const][noexcept] method definition
		template <auto method, typename object_t, typename return_t, typename type_t, typename... args_t, typename... envs_t>
		static void push_method(lua_State* L, object_t&& object, return_t(type_t::*)(args_t...), envs_t&&... envs) {
			push_method_internal<method, object_t, return_t, type_t, args_t...>(L, std::forward<object_t>(object), std::forward<envs_t>(envs)...);
		}

		template <auto method, typename object_t, typename return_t, typename type_t, typename... args_t, typename... envs_t>
		static void push_method(lua_State* L, object_t&& object, return_t(type_t::*)(args_t...) noexcept, envs_t&&... envs) {
			push_method_internal<method, object_t, return_t, type_t, args_t...>(L, std::forward<object_t>(object), std::forward<envs_t>(envs)...);
		}

		template <auto method, typename object_t, typename return_t, typename type_t, typename... args_t, typename... envs_t>
		static void push_method(lua_State* L, object_t&& object, return_t(type_t::*)(args_t...) const, envs_t&&... envs) {
			push_method_internal<method, object_t, return_t, type_t, args_t...>(L, std::forward<object_t>(object), std::forward<envs_t>(envs)...);
		}

		template <auto method, typename object_t, typename return_t, typename type_t, typename... args_t, typename... envs_t>
		static void push_method(lua_State* L, object_t&& object, return_t(type_t::*)(args_t...) const noexcept, envs_t&&... envs) {
			push_method_internal<method, object_t, return_t, type_t, args_t...>(L, std::forward<object_t>(object), std::forward<envs_t>(envs)...);
		}

		template <auto method, typename object_t, typename return_t, typename type_t, typename... args_t, typename... envs_t>
		static void push_method_internal(lua_State* L, object_t&& object, envs_t&&... envs) {
			if constexpr (std::is_null_pointer_v<object_t>) {
				push_function_internal<method_function_adapter<method, return_t, type_t, args_t...>, return_t, true, required_t<type_t*>&&, args_t...>(L, std::forward<envs_t>(envs)...);
			} else {
				push_functor_internal<method_functor_adapter<method, return_t, type_t, args_t...>, object_t&&, return_t, type_t, args_t...>(L, std::forward<object_t>(object), std::forward<envs_t>(envs)...);
			}
		}

		template <auto function, typename return_t, typename... args_t, typename... envs_t>
		static void push_function(lua_State* L, return_t(*)(args_t...), envs_t&&... envs) {
			push_function_internal<function, return_t, false, args_t...>(L, std::forward<envs_t>(envs)...);
		}

		template <auto function, typename return_t, typename... args_t, typename... envs_t>
		static void push_function(lua_State* L, return_t(*)(args_t...) noexcept, envs_t&&... envs) {
			push_function_internal<function, return_t, false, args_t...>(L, std::forward<envs_t>(envs)...);
		}

		template <auto function, typename return_t, bool use_this, typename... args_t, typename... envs_t>
		static void push_function_internal(lua_State* L, envs_t&&... envs) {
			stack_guard_t guard(L, 1);
			if constexpr (sizeof...(envs_t) > 0) {
				if constexpr (use_this) {
					check_matched_parameters<std::tuple<std::tuple_element_t<0, std::tuple<args_t...>>, envs_t...>, std::tuple<args_t...>, sizeof...(envs_t)>();
				} else {
					check_matched_parameters<std::tuple<envs_t...>, std::tuple<args_t...>, sizeof...(envs_t)>();
				}
			}

			push_arguments(L, std::forward<envs_t>(envs)...);

			if constexpr (iris_is_coroutine<return_t>::value) {
				lua_pushcclosure(L, &iris_lua_t::function_coroutine_proxy<function, return_t, sizeof...(envs), use_this, args_t...>, sizeof...(envs));
			} else {
				lua_pushcclosure(L, &iris_lua_t::function_proxy<function, return_t, sizeof...(envs), use_this, args_t...>, sizeof...(envs));
			}
		}

		template <auto function, typename object_t, typename return_t, typename type_t, typename... args_t, typename... envs_t>
		static void push_functor_internal(lua_State* L, object_t&& object, envs_t&&... envs) {
			stack_guard_t guard(L, 1);

			type_t* ptr = reinterpret_cast<type_t*>(lua_newuserdatauv(L, iris_to_alignment(sizeof(type_t), size_mask_alignment), 0));
			new (ptr) type_t(std::forward<object_t>(object));
			static_assert(std::is_reference_v<object_t>, "Must not be a reference!");

			if constexpr (!std::is_trivially_destructible_v<object_t>) {
				lua_newtable(L);
				make_uniform_meta_internal<type_t, 0>(L);
				lua_setmetatable(L, -2);
			}

			if constexpr (iris_is_coroutine<return_t>::value) {
				lua_pushcclosure(L, &iris_lua_t::function_coroutine_proxy<function, return_t, 0, false, iris_lua_t, args_t...>, 1);
			} else {
				lua_pushcclosure(L, &iris_lua_t::function_proxy<function, return_t, 0, false, iris_lua_t, args_t...>, 1);
			}
		}
		
		template <auto prop, typename type_t, typename value_t>
		static void push_property(lua_State* L, value_t type_t::*) {
			push_property_internal<prop, type_t>(L);
		}

		template <auto prop, typename type_t>
		static void push_property_internal(lua_State* L) {
			lua_pushcclosure(L, &iris_lua_t::property_proxy<prop, type_t>, 0);
		}

		// four specs for [const][noexcept] method definition
		template <typename function_t, typename return_t, typename type_t, typename... args_t>
		static int forward_method(lua_State* L, return_t(type_t::*method)(args_t...)) {
			return forward_method_internal<function_t, return_t, type_t, args_t...>(L, method);
		}

		template <typename function_t, typename return_t, typename type_t, typename... args_t>
		static int forward_method(lua_State* L, return_t(type_t::*method)(args_t...) const) {
			return forward_method_internal<function_t, return_t, type_t, args_t...>(L, method);
		}

		template <typename function_t, typename return_t, typename type_t, typename... args_t>
		static int forward_method(lua_State* L, return_t(type_t::*method)(args_t...) noexcept) {
			return forward_method_internal<function_t, return_t, type_t, args_t...>(L, method);
		}

		template <typename function_t, typename return_t, typename type_t, typename... args_t>
		static int forward_method(lua_State* L, return_t(type_t::*method)(args_t...) const noexcept) {
			return forward_method_internal<function_t, return_t, type_t, args_t...>(L, method);
		}

		template <typename function_t, typename return_t, typename type_t, typename... args_t>
		static int forward_method_internal(lua_State* L, const function_t& method) {
			auto adapter = [&method](required_t<type_t*>&& object, args_t&&... args) {
				if constexpr (!std::is_void_v<return_t>) {
					return (object.get()->*method)(std::forward<args_t>(args)...);
				} else {
					(object.get()->*method)(std::forward<args_t>(args)...);
				}
			};

			if constexpr (iris_is_coroutine<return_t>::value) {
				return function_coroutine_proxy_dispatch<decltype(adapter), return_t, 0, true, required_t<type_t*>&&, args_t...>(L, adapter);
			} else {
				return function_proxy_dispatch<decltype(adapter), return_t, 0, true, required_t<type_t*>&&, args_t...>(L, adapter);
			}			
		}

		template <typename function_t, typename return_t, typename... args_t>
		static int forward_function(lua_State* L, return_t (*function)(args_t...)) {
			return forward_function_internal<function_t, return_t, args_t...>(L, function);
		}

		template <typename function_t, typename return_t, typename... args_t>
		static int forward_function(lua_State* L, return_t (*function)(args_t...) noexcept) {
			return forward_function_internal<function_t, return_t, args_t...>(L, function);
		}

		template <typename function_t, typename return_t, typename... args_t>
		static int forward_function_internal(lua_State* L, const function_t& function) {
			if constexpr (iris_is_coroutine<return_t>::value) {
				return function_coroutine_proxy_dispatch<function_t, return_t, 0, false, args_t...>(L, function);
			} else {
				return function_proxy_dispatch<function_t, return_t, 0, false, args_t...>(L, function);
			}
		}

		template <typename function_t, typename return_t, typename type_t, typename... args_t>
		static int forward_functor(lua_State* L, function_t&& functor, return_t(type_t::*)(args_t...)) {
			return forward_functor_internal<function_t, return_t, args_t...>(L, std::forward<function_t>(functor));
		}

		template <typename function_t, typename return_t, typename type_t, typename... args_t>
		static int forward_functor(lua_State* L, function_t&& functor, return_t(type_t::*)(args_t...) const) {
			return forward_functor_internal<function_t, return_t, args_t...>(L, std::forward<function_t>(functor));
		}

		template <typename function_t, typename return_t, typename type_t, typename... args_t>
		static int forward_functor(lua_State* L, function_t&& functor, return_t(type_t::*)(args_t...) noexcept) {
			return forward_functor_internal<function_t, return_t, args_t...>(L, std::forward<function_t>(functor));
		}

		template <typename function_t, typename return_t, typename type_t, typename... args_t>
		static int forward_functor(lua_State* L, function_t&& functor, return_t(type_t::*)(args_t...) const noexcept) {
			return forward_functor_internal<function_t, return_t, args_t...>(L, std::forward<function_t>(functor));
		}

		template <typename function_t, typename return_t, typename... args_t>
		static int forward_functor_internal(lua_State* L, function_t&& functor) {
			return forward_function_internal<function_t, return_t, args_t...>(L, std::forward<function_t>(functor));
		}

		template <typename prop_t, typename type_t, typename value_t>
		static int forward_property(lua_State* L, value_t type_t::*prop) {
			return forward_property_internal<prop_t, type_t, value_t>(L, prop);
		}

		template <typename prop_t, typename type_t, typename value_t>
		static int forward_property_internal(lua_State* L, prop_t prop) {
			return property_proxy_dispatch<prop_t, type_t>(L, prop);
		}

		template <typename type_t, int user_value_count>
		static void make_uniform_meta_internal(lua_State* L) {
			// copy constructor
			if constexpr (std::is_copy_constructible_v<type_t>) {
				push_variable(L, "__copy");
				push_variable(L, reinterpret_cast<void*>(&copy_construct_stub<type_t, user_value_count>));
				lua_rawset(L, -3);
			}

			// move constructor
			if constexpr (std::is_move_constructible_v<type_t>) {
				push_variable(L, "__move");
				push_variable(L, reinterpret_cast<void*>(&move_construct_stub<type_t, user_value_count>));
				lua_rawset(L, -3);
			}

			// move constructor
			push_variable(L, "__view");
			push_variable(L, reinterpret_cast<void*>(&view_construct_stub<type_t, user_value_count>));
			lua_rawset(L, -3);

			push_variable(L, "__uservalue");
			push_variable(L, user_value_count);
			lua_rawset(L, -3);

			// create __gc for collecting objects
			push_variable(L, "__gc");
			lua_pushcfunction(L, &iris_lua_t::delete_object<type_t>);
			lua_rawset(L, -3);
		}

		template <typename type_t, typename = void>
		struct has_lua_finalize : std::false_type {};

		template <typename type_t>
		struct has_lua_finalize<type_t, iris_void_t<decltype(&iris_lua_traits_t<type_t>::type::lua_finalize)>> : std::true_type {};

		template <typename type_t, typename = void>
		struct has_lua_view_finalize : std::false_type {};

		template <typename type_t>
		struct has_lua_view_finalize<type_t, iris_void_t<decltype(&iris_lua_traits_t<type_t>::type::lua_view_finalize)>> : std::true_type {};

		// will be called as __gc triggerred
		template <typename type_t>
		static int delete_object(lua_State* L) {
			if (lua_rawlen(L, 1) & size_mask_view) {
				// call lua_view_finalize if needed
				if constexpr (has_lua_view_finalize<type_t>::value) {
					iris_lua_traits_t<type_t>::type::lua_view_finalize(iris_lua_t(L), 1, lua_touserdata(L, 1));
				}
			} else {
				type_t* p = reinterpret_cast<type_t*>(lua_touserdata(L, 1));

				// call lua_finalize if needed
				if constexpr (has_lua_finalize<type_t>::value) {
					iris_lua_traits_t<type_t>::type::lua_finalize(iris_lua_t(L), 1, p);
				}

				IRIS_ASSERT(p != nullptr);
				// do not free the memory (let lua gc it), just call the destructor
				p->~type_t();
			}

			return 0;
		}

		// pass argument by upvalues
		template <typename type_t, int env_count, typename args_tuple_t, typename creator_t, size_t... k>
		static optional_result_t<type_t*> invoke_create(type_t* p, lua_State* L, creator_t func, std::index_sequence<k...>) {
			return func(iris_lua_t(L), p, get_variable<std::tuple_element_t<k, args_tuple_t>, true>(L, k < env_count ? lua_upvalueindex(3 + int(k)) : 1 + int(k - env_count))...);
		}

		template <typename type_t, typename = void>
		struct has_lua_initialize : std::false_type {};

		template <typename type_t>
		struct has_lua_initialize<type_t, iris_void_t<decltype(&iris_lua_traits_t<type_t>::type::lua_initialize)>> : std::true_type {};

		template <typename type_t>
		static void initialize_object(lua_State* L, int index, type_t* p) {
			// call lua_initialize if needed
			if constexpr (has_lua_initialize<type_t>::value) {
				iris_lua_traits_t<type_t>::type::lua_initialize(iris_lua_t(L), index, p);
			}
		}

		template <typename type_t, typename = void>
		struct has_lua_view_payload : std::false_type {};

		template <typename type_t>
		struct has_lua_view_payload<type_t, iris_void_t<decltype(&iris_lua_traits_t<type_t>::type::lua_view_payload)>> : std::true_type {};

		template <typename type_t, typename = void>
		struct has_lua_view_extract : std::false_type {};

		template <typename type_t>
		struct has_lua_view_extract<type_t, iris_void_t<decltype(&iris_lua_traits_t<type_t>::type::lua_view_extract)>> : std::true_type {};

		template <typename type_t, typename = void>
		struct has_lua_view_initialize : std::false_type {};

		template <typename type_t>
		struct has_lua_view_initialize<type_t, iris_void_t<decltype(&iris_lua_traits_t<type_t>::type::lua_view_initialize)>> : std::true_type {};

		template <typename type_t, typename = void>
		struct has_lua_check : std::false_type {};

		template <typename type_t>
		struct has_lua_check<type_t, iris_void_t<decltype(&iris_lua_traits_t<type_t>::type::lua_check)>> : std::true_type {};

		template <typename type_t, int user_value_count, int env_count, typename... args_t>
		static int new_object(lua_State* L) {
			IRIS_PROFILE_SCOPE(__FUNCTION__);
			check_required_parameters<env_count, 2, false, 1, args_t...>(L);

			static_assert(alignof(type_t) <= alignof(lua_Number), "Too large alignment for object holding.");
			do {
				stack_guard_t guard(L, 1);
				type_t* p = reinterpret_cast<type_t*>(lua_newuserdatauv(L, iris_to_alignment(sizeof(type_t), size_mask_alignment), user_value_count));
				auto result = invoke_create<type_t, env_count, std::tuple<cast_arg_type_t<args_t>...>>(p, L, reinterpret_cast<optional_result_t<type_t*> (*)(iris_lua_t, type_t*, args_t...)>(lua_touserdata(L, lua_upvalueindex(2))), std::make_index_sequence<sizeof...(args_t)>());
				if (result) {
					assert(result.value() == p); // must return original ptr if success
					lua_pushvalue(L, lua_upvalueindex(1));
					lua_setmetatable(L, -2);
					initialize_object(L, lua_absindex(L, -1), p);

					return 1;
				} else {
					lua_pop(L, 1);
					lua_pushlstring(L, result.message.data(), result.message.size());
				}
			} while (false);

			return luaL_error(L, "Failed to create object of type %s. %s", typeid(type_t).name(), luaL_optstring(L, -1, ""));
		}

		// get multiple variables from a lua table and pack them into a tuple/pair 
		template <int index, typename type_t, typename... args_t>
		static type_t get_tuple_variables(lua_State* L, int stack_index, args_t&&... args) {
			if constexpr (index < std::tuple_size_v<type_t>) {
				lua_rawgeti(L, stack_index, index + 1);
				// notice that we need minus one if stack_index is negetive, since lua_rawgeti will add one temporal variable on stack
				return get_tuple_variables<index + 1, type_t>(L, stack_index, std::forward<args_t>(args)..., get_variable<std::tuple_element_t<index, type_t>>(L, -1));
			} else {
				// pop all temporal variables and construct tuple
				lua_pop(L, index);
				return type_t({ std::forward<args_t>(args)... });
			}
		}

		template <typename type_t, typename = void>
		struct has_reserve : std::false_type {};

		template <typename type_t>
		struct has_reserve<type_t, iris_void_t<decltype(std::declval<type_t>().reserve(1))>> : std::true_type {};

		// get a C++ variable from lua stack with given index
		template <typename type_t, bool skip_checks = false>
		static type_t get_variable(lua_State* L, int index) {
			using value_t = remove_cvref_t<type_t>;
			stack_guard_t guard(L);

			if constexpr (iris_lua_traits_t<value_t>::value) {
				return iris_lua_traits_t<value_t>::type::from_lua(iris_lua_t(L), index);
			} else if constexpr (std::is_null_pointer_v<value_t>) {
				return nullptr;
			} else if constexpr (iris_is_reference_wrapper<type_t>::value) {
				// pass reference wrapper as plain pointer without lifetime management, usually used by new_object() internally
				return std::ref(*reinterpret_cast<typename type_t::type*>(lua_touserdata(L, index)));
			} else if constexpr (std::is_base_of_v<ref_t, value_t>) {
				using internal_type_t = typename value_t::internal_type_t;
				if constexpr (!std::is_void_v<internal_type_t>) {
					// is refptr?
					auto internal_value = get_variable<internal_type_t, skip_checks>(L, index);
					lua_pushvalue(L, index);
					return value_t(luaL_ref(L, LUA_REGISTRYINDEX), std::move(internal_value));
				} else {
					lua_pushvalue(L, index);
					return value_t(luaL_ref(L, LUA_REGISTRYINDEX));
				}
			} else if constexpr (std::is_same_v<value_t, bool>) {
				return static_cast<value_t>(lua_toboolean(L, index));
			} else if constexpr (std::is_same_v<value_t, void*> || std::is_same_v<value_t, const void*>) {
				return static_cast<value_t>(lua_touserdata(L, index));
			} else if constexpr (std::is_integral_v<value_t> || std::is_enum_v<value_t>) {
				return static_cast<value_t>(lua_tointeger(L, index));
			} else if constexpr (std::is_floating_point_v<value_t>) {
				return static_cast<value_t>(lua_tonumber(L, index));
			} else if constexpr (std::is_same_v<value_t, lua_State*> || std::is_same_v<value_t, iris_lua_t>) {
				return value_t(lua_tothread(L, index));
			} else if constexpr (std::is_same_v<value_t, char*> || std::is_same_v<value_t, const char*>) {
				size_t len = 0;
				// do not accept implicit __tostring casts
				if (lua_type(L, index) == LUA_TSTRING) {
					return lua_tostring(L, index);
				} else {
					return "";
				}
			} else if constexpr (std::is_same_v<value_t, std::string_view> || std::is_same_v<value_t, std::string>) {
				size_t len = 0;
				// do not accept implicit __tostring casts
				if (lua_type(L, index) == LUA_TSTRING) {
					const char* str = lua_tolstring(L, index, &len);
					return value_t(str, len);
				} else {
					return "";
				}
			} else if constexpr (iris_is_tuple<value_t>::value) {
				return get_tuple_variables<0, value_t>(L, lua_absindex(L, index));
			} else if constexpr (iris_is_iterable<value_t>::value) {
				value_t result;
				if (lua_istable(L, index)) {
					if constexpr (iris_is_map<value_t>::value) {
						// for map-like containers, convert to lua hash table 
						using key_type = typename value_t::key_type;
						using mapped_type = typename value_t::mapped_type;

						int absindex = lua_absindex(L, index);
						lua_pushnil(L);
						while (lua_next(L, absindex) != 0) {
							// since we do not allow implicit lua_tostring conversion, so it's safe to extract key without duplicating it
							result[get_variable<key_type>(L, -2)] = get_variable<mapped_type>(L, -1);
							lua_pop(L, 1);
						}
					} else {
						// otherwise convert to lua array table
						size_t size = static_cast<size_t>(lua_rawlen(L, index));
						if constexpr (has_reserve<value_t>::value) {
							result.reserve(size);
						}

						for (size_t i = 0; i < size; i++) {
							lua_rawgeti(L, index, static_cast<lua_Integer>(i) + 1);
							result.emplace_back(get_variable<typename value_t::value_type>(L, -1));
							lua_pop(L, 1);
						}
					}
				}

				return result;
			} else if constexpr (std::is_pointer_v<value_t>) {
				if constexpr (!skip_checks) {
					// try to extract object
					if (!lua_getmetatable(L, index)) {
						return value_t();
					}

					void* object_hash = nullptr;
					void* type_hash = reinterpret_cast<void*>(get_hash<remove_cvref_t<std::remove_pointer_t<value_t>>>());

					while (true) {
						lua_pushliteral(L, "__hash");
#if LUA_VERSION_NUM <= 502
						lua_rawget(L, -2);
						if (lua_type(L, -1) == LUA_TNIL) {
#else
						if (lua_rawget(L, -2) == LUA_TNIL) {
#endif
							lua_pop(L, 2);
							return value_t();
						}

						// returns empty if hashes are not equal!
						object_hash = lua_touserdata(L, -1);
						if constexpr (!std::is_final_v<value_t>) {
							if (object_hash != type_hash) {
								lua_pop(L, 1);
								if (lua_getmetatable(L, -1)) {
									lua_pushliteral(L, "__index");
									lua_rawget(L, -2);
									lua_replace(L, -3);
									lua_pop(L, 1);
								} else {
									lua_pop(L, 1);
									return value_t();
								}
							} else {
								break;
							}
						} else {
							break;
						}
					}

					if (object_hash != type_hash) {
						lua_pop(L, 2);
						return value_t();
					}

					lua_pop(L, 2);
				}

				return extract_object_ptr<remove_cvref_t<std::remove_pointer_t<value_t>>>(L, index);
			} else if constexpr (std::is_base_of_v<required_base_t, value_t>) {
				return get_variable<typename value_t::required_type_t, true>(L, index);
			} else if constexpr (std::is_reference_v<type_t>) {
				// returning existing reference from internal storage
				// must check before calling this
				return *get_variable<std::remove_reference_t<type_t>*>(L, index);
			} else {
				// by default, force iris_lua_traits_t
				return iris_lua_traits_t<value_t>::type::from_lua(iris_lua_t(L), index);
			}
		}

		template <typename type_t>
		struct is_optional : std::false_type {};
		template <typename type_t>
		struct is_optional<std::optional<type_t>> : std::true_type {};
		template <typename type_t>
		struct is_optional<optional_result_t<type_t>> : std::true_type {};

		template <typename type_t>
		struct is_optional_result : std::false_type {};
		template <typename type_t>
		struct is_optional_result<optional_result_t<type_t>> : std::true_type {};

		// invoke a C++ function from lua stack
		template <typename function_t, int index, typename return_t, size_t env_count, bool use_this, typename tuple_t, typename... params_t>
		static int function_invoke(lua_State* L, const function_t& function, int stack_index, params_t&&... params) {
			IRIS_PROFILE_SCOPE(__FUNCTION__);

			if constexpr (index < std::tuple_size_v<tuple_t>) {
				if constexpr (std::is_same_v<iris_lua_t, remove_cvref_t<std::tuple_element_t<index, tuple_t>>>) {
					return function_invoke<function_t, index + 1, return_t, env_count, use_this, tuple_t>(L, function, stack_index, std::forward<params_t>(params)..., iris_lua_t(L));
				} else {
					if (stack_index == 1 && use_this) {
						return function_invoke<function_t, index + 1, return_t, env_count, use_this, tuple_t>(L, function, stack_index + 1, std::forward<params_t>(params)..., get_variable<std::tuple_element_t<index, tuple_t>, true>(L, 1));
					} else if (stack_index <= env_count + (use_this ? 1 : 0)) {
						return function_invoke<function_t, index + 1, return_t, env_count, use_this, tuple_t>(L, function, stack_index + 1, std::forward<params_t>(params)..., get_variable<std::tuple_element_t<index, tuple_t>, true>(L, lua_upvalueindex(stack_index - (use_this ? 1 : 0))));
					} else {
						return function_invoke<function_t, index + 1, return_t, env_count, use_this, tuple_t>(L, function, stack_index + 1, std::forward<params_t>(params)..., get_variable<std::tuple_element_t<index, tuple_t>, true>(L, stack_index - env_count));
					}
				}
			} else {
				int top = lua_gettop(L);

				if constexpr (std::is_void_v<return_t>) {
					function(std::forward<params_t>(params)...);
				} else {
					auto ret = function(std::forward<params_t>(params)...);
					if constexpr (is_optional_result<return_t>::value) {
						if (!ret) {
							push_variable(L, std::move(ret.message));
							return -1;
						} else {
							push_variable(L, std::move(ret.value()));
						}
					} else {
						push_variable(L, std::move(ret));
					}
				}

				int count = lua_gettop(L) - top;
				IRIS_ASSERT(count >= 0);
				return count;
			}
		}

		template <typename left_tuple_t, typename right_tuple_t, size_t index>
		static void check_matched_parameters() {
			using left_t = remove_cvref_t<std::tuple_element_t<index - 1, left_tuple_t>>;
			using right_t = remove_cvref_t<std::tuple_element_t<index - 1, right_tuple_t>>;
			static_assert(std::is_convertible_v<left_t, right_t> || std::is_convertible_v<right_t, left_t>, "Parameter type must be extractly the same.");

			if constexpr (index > 1) {
				check_matched_parameters<left_tuple_t, right_tuple_t, index - 1>();
			}
		}

		static constexpr std::pair<int, bool> get_var_index(int env_count, bool use_this, int index) {
			if (use_this) {
				if (index == 1) {
					return std::make_pair(index, false);
				} else if (index <= env_count + 1) {
					return std::make_pair(index - 1, true);
				} else {
					return std::make_pair(index - env_count, false);
				}
			} else {
				return index <= env_count ? std::make_pair(index, true) : std::make_pair(index - env_count, false);
			}
		}

		template <int env_count, int up_base, bool use_this, int index>
		static void check_required_parameters(lua_State* L) {}

		template <int env_count, int up_base, bool use_this, int index, typename type_t, typename... args_t>
		static void check_required_parameters(lua_State* L) {
			using value_t = remove_cvref_t<type_t>;
			auto var_index = get_var_index(env_count, use_this, index);
			int offset_index = var_index.first;
			int stack_index = var_index.second ? lua_upvalueindex(up_base + offset_index - 1) : offset_index;
			bool check_result = true;

			if constexpr (iris_lua_traits_t<value_t>::value) {
				if constexpr (has_lua_check<value_t>::value) {
					check_result = iris_lua_traits_t<value_t>::type::lua_check(iris_lua_t(L), stack_index, nullptr);
				}
			} else if constexpr (std::is_null_pointer_v<value_t>) {
				// do not check
			} else if constexpr (iris_is_reference_wrapper<type_t>::value) {
				// do not check
			} else if constexpr (std::is_base_of_v<ref_t, value_t>) {
				using internal_type_t = typename value_t::internal_type_t;
				if constexpr (!std::is_void_v<internal_type_t>) {
					check_required_parameters<env_count, up_base, use_this, index, internal_type_t>(L);
				}
			} else if constexpr (std::is_same_v<value_t, bool>) {
				// do not check
			} else if constexpr (std::is_same_v<value_t, void*> || std::is_same_v<value_t, const void*>) {
				// do not check
			} else if constexpr (std::is_integral_v<value_t> || std::is_enum_v<value_t>) {
				// do not check
			} else if constexpr (std::is_floating_point_v<value_t>) {
				// do not check
			} else if constexpr (std::is_same_v<value_t, lua_State*> || std::is_same_v<value_t, iris_lua_t>) {
				// do not check
			} else if constexpr (std::is_same_v<value_t, char*> || std::is_same_v<value_t, const char*>) {
				// do not check
			} else if constexpr (std::is_same_v<value_t, std::string_view> || std::is_same_v<value_t, std::string>) {
				// do not check
			} else if constexpr (iris_is_tuple<value_t>::value) {
				// do not check
			} else if constexpr (iris_is_iterable<value_t>::value) {
				// do not check
			} else if constexpr (std::is_pointer_v<value_t>) {
				using internal_type_t = std::remove_pointer_t<value_t>;
				auto checked_value = get_variable<value_t, true>(L, stack_index);
				auto unchecked_value = get_variable<value_t, false>(L, stack_index);
				check_result = checked_value == unchecked_value;

				if constexpr (has_lua_check<internal_type_t>::value) {
					if (checked_value != nullptr && check_result) {
						if (!iris_lua_traits_t<internal_type_t>::type::lua_check(iris_lua_t(L), stack_index, checked_value)) {
							check_result = false;
						}
					}
				}
			} else if constexpr (std::is_base_of_v<required_base_t, value_t>) {
				using required_type_t = typename value_t::required_type_t;
				check_required_parameters<env_count, up_base, use_this, index, required_type_t>(L);

				auto var = get_variable<required_type_t>(L, stack_index);
				check_result = var;

				if constexpr (std::is_base_of_v<ref_t, required_type_t>) {
					deref(L, std::move(var));
				}
			} else if constexpr (std::is_reference_v<type_t>) {
				// returning existing reference from interval storage
				// must check before calling this
				check_required_parameters<env_count, up_base, use_this, index, required_t<std::remove_reference_t<type_t>*>>(L);
			} else {
				// do not check
			}

			if (!check_result) {
				iris_lua_t::systrap(L, "error.parameter", "Required %s parameter %d of type %s is invalid or inaccessable.", index <= env_count ? "Env" : "Stack", offset_index, typeid(type_t).name());
				luaL_error(L, "Required %s parameter %d of type %s is invalid or inaccessable.", index <= env_count ? "Env" : "Stack", offset_index, typeid(type_t).name());
			}

			check_required_parameters<env_count, up_base, use_this, index + (std::is_same_v<iris_lua_t, remove_cvref_t<type_t>> ? 0 : 1), args_t...>(L);
		}

		template <typename function_t, typename return_t, size_t env_count, bool use_this, typename... args_t>
		static int function_proxy_dispatch(lua_State* L, const function_t& function) {
			check_required_parameters<env_count, 0, use_this, 1, args_t...>(L);

			int ret = function_invoke<function_t, 0, return_t, env_count, use_this, std::tuple<cast_arg_type_t<args_t>...>>(L, function, 1);
			if (ret < 0) {
				iris_lua_t::systrap(L, "error.exec", "C-function execution error: %s", luaL_optstring(L, -1, ""));
				luaL_error(L, "C-function execution error: %s", luaL_optstring(L, -1, ""));
			}

			return ret;
		}

		template <auto function, typename return_t, size_t env_count, bool use_this, typename... args_t>
		static int function_proxy(lua_State* L) {
			return function_proxy_dispatch<decltype(function), return_t, env_count, use_this, args_t...>(L, function);
		}

		static constexpr int coroutine_state_yield = -1;
		static constexpr int coroutine_state_error = -2;

		template <typename function_t, int index, typename coroutine_t, size_t env_count, bool use_this, typename tuple_t, typename... params_t>
		static int function_coroutine_invoke(lua_State* L, const function_t& function, int stack_index, params_t&&... params) {
			if constexpr (index < std::tuple_size_v<tuple_t>) {
				if constexpr (std::is_same_v<iris_lua_t, remove_cvref_t<std::tuple_element_t<index, tuple_t>>>) {
					return function_coroutine_invoke<function_t, index + 1, coroutine_t, env_count, use_this, tuple_t>(L, function, stack_index, std::forward<params_t>(params)..., iris_lua_t(L));
				} else {
					if (stack_index == 1 && use_this) {
						return function_coroutine_invoke<function_t, index + 1, coroutine_t, env_count, use_this, tuple_t>(L, function, stack_index + 1, std::forward<params_t>(params)..., get_variable<std::tuple_element_t<index, tuple_t>, true>(L, stack_index));
					} else if (stack_index <= env_count + (use_this ? 1 : 0)) {
						return function_coroutine_invoke<function_t, index + 1, coroutine_t, env_count, use_this, tuple_t>(L, function, stack_index + 1, std::forward<params_t>(params)..., get_variable<std::tuple_element_t<index, tuple_t>, true>(L, lua_upvalueindex(stack_index - (use_this ? 1 : 0))));
					} else {
						return function_coroutine_invoke<function_t, index + 1, coroutine_t, env_count, use_this, tuple_t>(L, function, stack_index + 1, std::forward<params_t>(params)..., get_variable<std::tuple_element_t<index, tuple_t>, true>(L, stack_index - env_count));
					}
				}
			} else {
				IRIS_PROFILE_SCOPE(__FUNCTION__);
				// mark state
				using return_t = typename coroutine_t::return_type_t;
				auto coroutine = function(std::forward<params_t>(params)...);
				void* address = coroutine.get_handle().address();

				// save current thread to registry in case of gc
				lua_pushlightuserdata(L, address);
				lua_pushthread(L);
				lua_rawset(L, LUA_REGISTRYINDEX);

				int top = lua_gettop(L);
				if constexpr (!std::is_void_v<return_t>) {
#if LUA_CLEAR_STACK_ON_YIELD
					coroutine.complete([L, address](return_t&& value) {
#else
					coroutine.complete([L, address, top](return_t&& value) {
#endif
						IRIS_PROFILE_SCOPE(__FUNCTION__);
						void* context = address;

						if constexpr (is_optional_result<return_t>::value) {
							if (value) {
								push_variable(L, std::move(value.value()));
							} else {
								// error!
#if LUA_ENABLE_YIELDK
								push_variable(L, std::move(value.message));
								context = reinterpret_cast<char*>(L) + 1;
#else
								iris_lua_t::systrap(L, "error.resume.legacy", "C-function execution error: %s", value.message.c_str());
								coroutine_cleanup(L, address);
								return;
#endif
							}
						} else {
							push_variable(L, std::move(value));
						}

#if LUA_CLEAR_STACK_ON_YIELD
						int count = lua_gettop(L); // old lua will clear input parameters as coroutine yield
#else
						int count = lua_gettop(L) - top;
#endif
						IRIS_ASSERT(count >= 0);
						push_variable(L, context);
						coroutine_continuation(L, count, address);
					}).run();
				} else {
					coroutine.complete([L, address]() {
						IRIS_PROFILE_SCOPE(__FUNCTION__);
						lua_pushnil(L);
						push_variable(L, address);
						coroutine_continuation(L, 1, address);
					}).run();
				}

				// already completed?
				void* mark = lua_touserdata(L, -1);
				if (mark == address) {
					lua_pop(L, 1);
					int count = lua_gettop(L) - top;
					IRIS_ASSERT(count >= 0);
					return count;
				} else if (mark == reinterpret_cast<char*>(L) + 1) {
					lua_pop(L, 1);
					return coroutine_state_error;
				} else {
					return coroutine_state_yield;
				}
			}
		}

		static void coroutine_cleanup(lua_State* L, void* address) {
			// clear thread reference to allow gc collecting
			lua_pushlightuserdata(L, address);
			lua_pushnil(L);
			lua_rawset(L, LUA_REGISTRYINDEX);
		}

		static void coroutine_continuation(lua_State* L, int count, void* address) {
			if (lua_status(L) == LUA_YIELD) {
				bool ret_error = lua_touserdata(L, -1) == reinterpret_cast<char*>(L) + 1;
				if (!ret_error) {
					lua_pop(L, 1);
				} else {
					count++;
				}
#if LUA_VERSION_NUM <= 501
				int ret = lua_resume(L, count);
#elif LUA_VERSION_NUM <= 503
				int ret = lua_resume(L, nullptr, count);
#else
				int nres = 0;
				int ret = lua_resume(L, nullptr, count, &nres);
#endif
				if (ret != LUA_OK && ret != LUA_YIELD) {
					// error!
					iris_lua_t::systrap(L, "error.resume", "iris_lua_t::function_coutine_proxy() -> resume error: %s\n", luaL_optstring(L, -1, ""));
					lua_pop(L, 1);
				}
			}

			coroutine_cleanup(L, address);
		}

#if LUA_ENABLE_YIELDK
		static int function_coroutine_continuation(lua_State* L, int status, lua_KContext context) {
			IRIS_ASSERT(status == LUA_YIELD);
			// detect error
			if (lua_touserdata(L, -1) == reinterpret_cast<char*>(L) + 1) {
				// error happened
				lua_pop(L, 1);
				return lua_error(L);
			} else {
				return lua_gettop(L) - static_cast<int>(context);
			}
		}
#endif

		template <typename function_t, typename coroutine_t, size_t envs_count, bool use_this, typename... args_t>
		static int function_coroutine_proxy_dispatch(lua_State* L, const function_t& function) {
			check_required_parameters<0, 0, use_this, 1, args_t...>(L);
			int count = 0;
			if ((count = function_coroutine_invoke<function_t, 0, coroutine_t, envs_count, use_this, std::tuple<cast_arg_type_t<args_t>...>>(L, function, 1)) >= 0) {
				return count;
			} else {
				if (count == coroutine_state_error) {
					// error message already on stack
					return lua_error(L);
				} else {
					// coroutine_state_yield
#if LUA_ENABLE_YIELDK
					// after Lua 5.3, we can throw errors on C-coroutine via lua_yieldk directly
					// so it can be captured within current pcall() context
					return lua_yieldk(L, 0, lua_gettop(L), &iris_lua_t::function_coroutine_continuation);
#else
					// however if you use a lower version (including LuaJIT),
					// since C-continuation from yielding point is not possible, we cannot resume the coroutine anymore as C-error happends,
					// try using __iris_systrap__ to capture it
					return lua_yield(L, 0);
#endif
				}
			}
		}

		template <auto function, typename coroutine_t, size_t envs_count, bool use_this, typename... args_t>
		static int function_coroutine_proxy(lua_State* L) {
			return function_coroutine_proxy_dispatch<decltype(function), coroutine_t, envs_count, use_this, args_t...>(L, function);
		}

		// for a property, define a function as:
		// proxy([newvalue]) : oldvalue
		// will not assigned to newvalue if newvalue is not provided
		template <typename prop_t, typename type_t>
		static int property_proxy_dispatch(lua_State* L, prop_t prop) {
			type_t* object = get_variable<type_t*>(L, 1);
			if (object == nullptr) {
				iris_lua_t::systrap(L, "error.parameter", "The first parameter of a property must be a C++ instance of type %s.", typeid(type_t).name());
				return luaL_error(L, "The first parameter of a property must be a C++ instance of type %s.", typeid(type_t).name());
			}

			using value_t = decltype(object->*prop);
			bool assign = !lua_isnone(L, 2);

			if constexpr (std::is_const_v<std::remove_reference_t<value_t>>) {
				if (!assign) {
					stack_guard_t guard(L, 1);
					push_variable(L, object->*prop); // return the original value
				}
			} else {
				if (assign) {
					object->*prop = get_variable<std::remove_reference_t<value_t>>(L, 2);
				} else {
					stack_guard_t guard(L, 1);
					push_variable(L, object->*prop);
				}
			}

			return assign ? 0 : 1;
		}

		template <auto prop, typename type_t>
		static int property_proxy(lua_State* L) {
			return property_proxy_dispatch<decltype(prop), type_t>(L, prop);
		}

		// push variables from a tuple into a lua table
		template <int index, typename type_t>
		static void push_tuple_variables(lua_State* L, type_t&& variable) {
			using value_t = remove_cvref_t<type_t>;
			if constexpr (index < std::tuple_size_v<value_t>) {
				do {
					stack_guard_t stack_guard(L);
					if constexpr (std::is_rvalue_reference_v<type_t&&>) {
						push_variable(L, std::move(std::get<index>(variable)));
					} else {
						push_variable(L, std::get<index>(variable));
					}

					lua_rawseti(L, -2, index + 1);
				} while (false);
				push_tuple_variables<index + 1>(L, std::forward<type_t>(variable));
			}
		}

		// spec for constexpr ptr
		template <auto ptr, typename... envs_t>
		static void push_variable(lua_State* L, envs_t&&... envs) {
			if constexpr (std::is_convertible_v<decltype(ptr), int (*)(lua_State*)> || std::is_convertible_v<decltype(ptr), int (*)(lua_State*) noexcept>) {
				push_native(L, ptr, std::forward<envs_t>(envs)...);
			} else if constexpr (std::is_member_function_pointer_v<decltype(ptr)>) {
				push_method<ptr>(L, std::nullptr_t(), ptr, std::forward<envs_t>(envs)...);
			} else if constexpr (std::is_member_object_pointer_v<decltype(ptr)>) {
				push_property<ptr>(L, ptr, std::forward<envs_t>(envs)...);
			} else {
				push_function<ptr>(L, ptr, std::forward<envs_t>(envs)...);
			}
		}

		template <typename type_t, typename first_t, typename... envs_t>
		static void push_variable(lua_State* L, type_t&& variable, first_t&& first, envs_t&&... envs) {
			push_method<&type_t::operator ()>(L, std::forward<type_t>(variable), &type_t::operator (), std::forward<first_t>(first), std::forward<envs_t>(envs)...);
		}

		template <typename type_t>
		static void push_variable(lua_State* L, type_t&& variable) {
			using value_t = remove_cvref_t<type_t>;
			stack_guard_t guard(L, 1);

			if constexpr (iris_lua_traits_t<value_t>::value) {
				guard.append(iris_lua_traits_t<value_t>::type::to_lua(L, std::forward<type_t>(variable)) - 1);
			} else if constexpr (is_optional<value_t>::value) {
				if (variable) {
					if constexpr (std::is_rvalue_reference_v<type_t&&>) {
						push_variable(L, std::move(variable.value()));
					} else {
						push_variable(L, variable.value());
					}
				} else {
					lua_pushnil(L);
				}
			} else if constexpr (std::is_null_pointer_v<value_t>) {
				lua_pushnil(L);
			} else if constexpr (iris_is_reference_wrapper<value_t>::value) {
				lua_pushlightuserdata(L, const_cast<void*>(reinterpret_cast<const void*>(&variable.get())));
			} else if constexpr (std::is_base_of_v<ref_t, value_t>) {
				lua_rawgeti(L, LUA_REGISTRYINDEX, variable.ref_index);
				// deference if it's never used
				if constexpr (std::is_rvalue_reference_v<type_t&&>) {
					deref(L, std::move(variable));
				}
			} else if constexpr (std::is_convertible_v<value_t, int (*)(lua_State*)> || std::is_convertible_v<value_t, int (*)(lua_State*) noexcept>) {
				lua_pushcfunction(L, variable);
			} else if constexpr (std::is_same_v<value_t, void*> || std::is_same_v<value_t, const void*>) {
				lua_pushlightuserdata(L, const_cast<void*>(variable));
			} else if constexpr (std::is_same_v<value_t, bool>) {
				lua_pushboolean(L, static_cast<bool>(variable));
			} else if constexpr (std::is_integral_v<value_t> || std::is_enum_v<value_t>) {
				lua_pushinteger(L, static_cast<lua_Integer>(variable));
			} else if constexpr (std::is_floating_point_v<value_t>) {
				lua_pushnumber(L, static_cast<lua_Number>(variable));
			} else if constexpr (std::is_constructible_v<std::string_view, value_t>) {
				std::string_view view = variable;
				lua_pushlstring(L, view.data(), view.length());
			} else if constexpr (iris_is_tuple<value_t>::value) {
				lua_createtable(L, std::tuple_size_v<value_t>, 0);
				push_tuple_variables<0>(L, std::forward<type_t>(variable));
			} else if constexpr (iris_is_iterable<value_t>::value) {
				if constexpr (iris_is_map<value_t>::value) {
					lua_createtable(L, 0, static_cast<int>(variable.size()));

					for (auto&& pair : variable) {
						stack_guard_t guard(L);

						// move all elements if container is a rvalue
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
						stack_guard_t guard(L);

						// move all elements if container is a rvalue
						if constexpr (std::is_rvalue_reference_v<type_t&&>) {
							push_variable(L, std::move(value));
						} else {
							push_variable(L, value);
						}

						lua_rawseti(L, -2, ++i);
					}
				}
			} else if constexpr (std::is_base_of_v<required_base_t, value_t>) {
				// move internal value if wrapper is rvalue
				if constexpr (std::is_rvalue_reference_v<type_t&&>) {
					push_variable(L, std::move(variable.get()));
				} else {
					push_variable(L, variable.get());
				}
			} else if constexpr (is_functor<value_t>::value) {
				push_method<&type_t::operator ()>(L, std::forward<type_t>(variable), &type_t::operator ());
			} else {
				// by default, force iris_lua_traits_t
				guard.append(iris_lua_traits_t<value_t>::type::to_lua(L, std::forward<type_t>(variable)) - 1);
			}
		}

		// transfer a variable between different states
		template <bool move, typename lua_t>
		static int cross_transfer_variable(lua_State* L, lua_t& target, int index, int recursion_source, int recursion_target, int recursion_index) {
			stack_guard_t guard(L);
			lua_State* T = target.get_state();
			stack_guard_t guard_target(T, 1);

			int type = lua_type(L, index);
			switch (type) {
				case LUA_TBOOLEAN:
				{
					target.native_push_variable(get_variable<bool>(L, index));
					break;
				}
				case LUA_TLIGHTUSERDATA:
				{
					target.native_push_variable(get_variable<const void*>(L, index));
					break;
				}
				case LUA_TNUMBER:
				{
#if LUA_VERSION_NUM <= 502
					target.native_push_variable(get_variable<lua_Number>(L, index));
#else
					if (lua_isinteger(L, index)) {
						target.native_push_variable(get_variable<lua_Integer>(L, index));
					} else {
						target.native_push_variable(get_variable<lua_Number>(L, index));
					}
#endif
					break;
				}
				case LUA_TSTRING:
				{
					target.native_push_variable(get_variable<std::string_view>(L, index));
					break;
				}
				case LUA_TTABLE:
				{
					// try avoid recursion
					if (recursion_source != 0) {
						lua_pushvalue(L, index);
#if LUA_VERSION_NUM <= 502
						lua_rawget(L, recursion_source);
						if (lua_type(L, -1) != LUA_TNIL) {
#else
						if (lua_rawget(L, recursion_source) != LUA_TNIL) {
#endif
							int target_index = static_cast<int>(reinterpret_cast<size_t>(lua_touserdata(L, -1)));
							lua_pop(L, 1);
							lua_rawgeti(T, recursion_target, target_index);
							break;
						}

						lua_pop(L, 1);
					}

					// not found, try creating a new table
					lua_newtable(T);

					if (recursion_source != 0) {
						lua_pushvalue(L, index);
						lua_pushlightuserdata(L, reinterpret_cast<void*>((size_t)recursion_index));
						lua_rawset(L, recursion_source);

						lua_pushvalue(T, -1);
						lua_rawseti(T, recursion_target, recursion_index);
						recursion_index++;
					}

					int absindex = lua_absindex(L, index);
					lua_pushnil(L);

					while (lua_next(L, absindex) != 0) {
						// since we do not allow implicit lua_tostring conversion, so it's safe to extract key without duplicating it
						recursion_index = cross_transfer_variable<move>(L, target, -2, recursion_source, recursion_target, recursion_index);
						recursion_index = cross_transfer_variable<move>(L, target, -1, recursion_source, recursion_target, recursion_index);
						lua_rawset(T, -3);
						lua_pop(L, 1);
					}

					break;
				}
				case LUA_TFUNCTION:
				{
					if (lua_iscfunction(L, index)) {
						lua_CFunction proxy = lua_tocfunction(L, index);
						// copy upvalues
						int absindex = lua_absindex(L, index);
						int n = 1;
						while (lua_getupvalue(L, absindex, n) != nullptr) {
							recursion_index = cross_transfer_variable<false>(L, target, -1, recursion_source, recursion_target, recursion_index);
							lua_pop(L, 1);
							n++;
						}

						lua_pushcclosure(T, proxy, n - 1);
					} else {
						// do not convert lua functions
						target.native_push_variable(nullptr);
					}
					break;
				}
				case LUA_TUSERDATA:
				{
					// get metatable
					void* src = lua_touserdata(L, index);
					int absindex = lua_absindex(L, index);
					if (src == nullptr) {
						target.native_push_variable(nullptr);
					} else if (!lua_getmetatable(L, index)) {
						if (lua_islightuserdata(L, absindex)) {
							lua_pushlightuserdata(T, src);
						} else {
							// copy raw data if no metatable provided
							size_t len = static_cast<size_t>(lua_rawlen(L, absindex));
							void* dst = lua_newuserdatauv(T, len, 0);
							std::memcpy(dst, src, len);
						}
					} else {
						stack_guard_t guard(T, 1);
						if (cross_transfer_metatable(L, target, recursion_source, recursion_target, recursion_index) != -1) {
							size_t rawlen = static_cast<size_t>(lua_rawlen(L, absindex));
							if (rawlen & size_mask_view) {
								lua_pushliteral(T, "__view");
								lua_rawget(T, -2);
								void* ptr = lua_touserdata(T, -1);
								lua_pop(T, 1);

								if (ptr != nullptr) {
									reinterpret_cast<decltype(&view_construct_stub<void*, 0>)>(ptr)(T, L, index, rawlen);
								} else {
									lua_pushnil(T);
								}

								lua_replace(T, -2);
							} else {
								// now metatable prepared
								if constexpr (move) {
									lua_pushliteral(T, "__move");
								} else {
									lua_pushliteral(T, "__copy");
								}

								lua_rawget(T, -2);
								void* ptr = lua_touserdata(T, -1);
								if (ptr == nullptr) {
									lua_pop(T, 2);
									target.native_push_variable(nullptr);
								} else {
									if constexpr (move) {
										reinterpret_cast<decltype(&copy_construct_stub<void*, 0>)>(ptr)(T, src, rawlen);
									} else {
										reinterpret_cast<decltype(&move_construct_stub<void*, 0>)>(ptr)(T, src, rawlen);
									}

									// notice that we do not copy user values
									lua_replace(T, -3);
									lua_pop(T, 1);
								}
							}
						} else {
							target.native_push_variable(nullptr);
						}

						lua_pop(L, 1);
					}

					break;
				}
				case LUA_TTHREAD:
				{
					target.native_push_variable(nullptr);
					break;
				}
				default:
				{
					target.native_push_variable(nullptr);
					break;
				}
			}

			return recursion_index;
		}

	protected:
		template <typename lua_t>
		static int cross_transfer_metatable(lua_State* L, lua_t& target, int recursion_source, int recursion_target, int recursion_index) {
			stack_guard_t guard(L);
			lua_State* T = target.get_state();
			stack_guard_t guard_target(T, 1);

			lua_pushliteral(L, "__hash");
#if LUA_VERSION_NUM <= 502
			lua_rawget(L, -2);
			if (lua_type(L, -1) != LUA_TNIL) {
#else
			if (lua_rawget(L, -2) != LUA_TNIL) {
#endif
				void* hash = lua_touserdata(L, -1);

#if LUA_VERSION_NUM <= 502
				lua_pushlightuserdata(T, hash);
				lua_rawget(T, LUA_REGISTRYINDEX);
				if (lua_type(T, -1) == LUA_TNIL) {
#else
				if (lua_rawgetp(T, LUA_REGISTRYINDEX, hash) == LUA_TNIL) {
#endif
					lua_pop(T, 1);

					// copy table
					recursion_index = cross_transfer_variable<false>(L, target, -2, recursion_source, recursion_target, recursion_index);

					// copy metatable
					if (lua_getmetatable(L, -2)) {
						int new_index = cross_transfer_metatable(L, target, recursion_source, recursion_target, recursion_index);
						if (new_index != -1) {
							recursion_index = new_index;
							lua_setmetatable(T, -2);
						}
						
						lua_pop(L, 1);
					}

					lua_pushlightuserdata(T, hash);
					lua_pushvalue(T, -2);
					lua_rawset(T, LUA_REGISTRYINDEX);
				}

				lua_pop(L, 1);
				return recursion_index;
			} else {
				lua_pop(L, 1);
				return -1;
			}
		}

	protected:
		lua_State* state = nullptr;
	};
}
