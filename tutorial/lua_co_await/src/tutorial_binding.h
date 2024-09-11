// tutorial_binding.h
// PaintDream (paintdream@paintdream.com)
// 2023-06-01

#pragma once
#include "common.h"
#include <unordered_map>

namespace iris {
	class tutorial_binding_t : enable_read_write_fence_t<> {
	public:
		static void lua_registar(lua_t&& lua);

		tutorial_binding_t();
		~tutorial_binding_t() noexcept;

		void init(int int_param, float float_param, double double_param, std::string_view string_param, const std::vector<std::string>& string_vector_param, std::unordered_map<std::string, int>&& string_int_map_param);

		// lua_t&& lua is optional, just for acquiring current lua context
		void copy(lua_t&& lua, lua_refptr_t<tutorial_binding_t>&& rhs);

		// use lua_refptr_t<T> to acquire object with an reference holder, or just T* to acquire object instance only.
		static void copy_static(lua_t&& lua, lua_refptr_t<tutorial_binding_t>&& lhs, tutorial_binding_t* rhs);

		void load(lua_t&& lua, lua_t::ref_t&& param);
		lua_t::ref_t save(lua_t&& lua);

	protected:
		int int_value = 0;
		float float_value = 0.0f;
		double double_value = 0.0;
		std::string string_value;
		std::vector<std::string> string_vector_value;
		std::unordered_map<std::string, int> string_int_map_value;
	};
}