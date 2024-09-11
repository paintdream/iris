#include "tutorial_binding.h"

namespace iris {
	void tutorial_binding_t::lua_registar(lua_t&& lua) {
		lua.set_current<&tutorial_binding_t::init>("init");
		lua.set_current<&tutorial_binding_t::save>("save");
		lua.set_current<&tutorial_binding_t::load>("load");
		lua.set_current<&tutorial_binding_t::copy>("copy");
		lua.set_current<&tutorial_binding_t::copy_static>("copy_static");
		lua.set_current<&tutorial_binding_t::int_value>("int_value");
		lua.set_current<&tutorial_binding_t::float_value>("float_value");
		lua.set_current<&tutorial_binding_t::double_value>("double_value");
		lua.set_current<&tutorial_binding_t::string_value>("string_value");
		lua.set_current<&tutorial_binding_t::string_vector_value>("string_vector_value");
		lua.set_current<&tutorial_binding_t::string_int_map_value>("string_int_map_value");
		lua.set_current("run", lua.load("local self = ...\n\
print('[tutorial_binding] ')\n\
local printfields = function (object) \n\
	print('\tint_value = ' .. tostring(object:int_value())) \n\
	print('\tfloat_value = ' .. tostring(object:float_value())) \n\
	print('\tdouble_value = ' .. tostring(object:double_value())) \n\
	print('\tstring_value = ' .. tostring(object:string_value())) \n\
	for k, v in ipairs(object:string_vector_value()) do \n\
		print('\tstring_vector_value[' .. tostring(k) .. '] = ' .. tostring(v)) \n\
	end \n\
	for k, v in pairs(object:string_int_map_value()) do \n\
		print('\tstring_int_map_value[' .. tostring(k) .. '] = ' .. tostring(v)) \n\
	end \n\
end \n\
self:init(1, 2.0, 3.0, 'hello', { 'alpha', 'beta', 'gamma' }, { x = 0, y = 1, z = 2}) \n\
print('old values: ') \n\
printfields(self) \n\
self:int_value(4) \n\
self:string_value('world') \n\
print('new values: ') \n\
printfields(self) \n\
local tab = self:save() \n\
print('save self:') \n\
for k, v in pairs(tab) do \n\
	print('key ' .. tostring(k) .. ' = ' .. tostring(v)) \n\
end \n\
tab.int_value = 10 \n\
self:load(tab) \n\
local other = getmetatable(self).new() \n\
local other2 = getmetatable(self).new() \n\
other:copy(self) \n\
other2:copy_static(self) \n\
print('other values ') \n\
printfields(other) \n\
print('other2 values ') \n\
other2:string_int_map_value({ x = 1, y = 2, z = 3}) \n\
printfields(other2) \n\
print('[tutorial_binding] complete!')\n"));
	}

	tutorial_binding_t::tutorial_binding_t() {}
	tutorial_binding_t::~tutorial_binding_t() noexcept {
	}

	// basic types and compound type of them are supported naturally
	void tutorial_binding_t::init(int int_param, float float_param, double double_param, std::string_view string_param, const std::vector<std::string>& string_vector_param, std::unordered_map<std::string, int>&& string_int_map_param) {
		int_value = int_param;
		float_value = float_param;
		double_value = double_param;
		string_value = string_param;
		string_vector_value = string_vector_param;
		string_int_map_value = std::move(string_int_map_param);
	}

	// you can use refptr_t<T> to acquire a struct/class instance and holding its lifetime by a reference at the same time
	void tutorial_binding_t::copy(lua_t&& lua, lua_refptr_t<tutorial_binding_t>&& rhs) {
		if (rhs) {
			int_value = rhs->int_value;
			float_value = rhs->float_value;
			double_value = rhs->double_value;
			string_value = rhs->string_value;
			string_vector_value = rhs->string_vector_value;
			string_int_map_value = rhs->string_int_map_value;

			// we should deref lua_refptr_t<T> manually
			lua.deref(std::move(rhs));

			// if you want to save it somewhere, just try:
			// other_value = std::move(rhs);
			// then the instance will be held by `other_value` now.
		}
	}

	// or you can use T* to take it temporarily
	void tutorial_binding_t::copy_static(lua_t&& lua, lua_refptr_t<tutorial_binding_t>&& lhs, tutorial_binding_t* rhs) {
		if (lhs && rhs != nullptr) {
			tutorial_binding_t& self = *lhs.get();
			tutorial_binding_t& other = *rhs;

			self.int_value = other.int_value;
			self.float_value = other.float_value;
			self.double_value = other.double_value;
			self.string_value = other.string_value;
			self.string_vector_value = other.string_vector_value;
			self.string_int_map_value = other.string_int_map_value;

			lua.deref(std::move(lhs));
		}
	}

	void tutorial_binding_t::load(lua_t&& lua, lua_t::ref_t&& param) {
		// ref_t::get returns a std::optional<> value
		if (auto value = param.get<int>(lua, "int_value")) {
			int_value = *value;
		}

		if (auto value = param.get<float>(lua, "float_value")) {
			float_value = *value;
		}

		if (auto value = param.get<double>(lua, "double_value")) {
			double_value = *value;
		}

		if (auto value = param.get<std::string_view>(lua, "string_value")) {
			string_value = *value;
		}

		if (auto value = param.get<std::vector<std::string>>(lua, "string_vector_value")) {
			string_vector_value = std::move(*value);
		}

		if (auto value = param.get<std::unordered_map<std::string, int>>(lua, "string_int_map_value")) {
			string_int_map_value = std::move(*value);
		}

		// don't forget to deref param!
		lua.deref(std::move(param));
	}

	lua_t::ref_t tutorial_binding_t::save(lua_t&& lua) {
		return lua.make_table([this](lua_t&& lua) {
			lua.set_current("int_value", int_value);
			lua.set_current("float_value", float_value);
			lua.set_current("double_value", double_value);
			lua.set_current("string_value", string_value);
			lua.set_current("string_vector_value", string_vector_value);
			lua.set_current("string_int_map_value", string_int_map_value);
		});
	}
}