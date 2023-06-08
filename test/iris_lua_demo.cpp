#include "../src/iris_lua.h"
#include "../src/iris_common.inl"
#include <map>
#include <cstdio>

#if USE_LUA_COROUTINE
#include "../src/iris_coroutine.h"
#endif
using namespace iris;

#if USE_LUA_COROUTINE
using worker_t = iris_async_worker_t<std::thread, std::function<void()>>;
using warp_t = iris_warp_t<worker_t>;

static warp_t* warpptr = nullptr;
static warp_t* warpptr2 = nullptr;
static worker_t* workerptr = nullptr;
#else
using warp_t = void;
#endif

using lua_t = iris_lua_t<warp_t>;

struct vector3 {
	float x, y, z;
};

template <>
struct iris::iris_lua_convert_t<vector3> : std::true_type {
	static void to_lua(lua_State* L, vector3&& v) noexcept {
		lua_newtable(L);
		lua_pushnumber(L, v.z);
		lua_pushnumber(L, v.y);
		lua_pushnumber(L, v.x);
		lua_rawseti(L, -4, 1);
		lua_rawseti(L, -3, 2);
		lua_rawseti(L, -2, 3);
	}

	static vector3 from_lua(lua_State* L, int index) noexcept {
		lua_pushvalue(L, index);
		lua_rawgeti(L, -1, 1);
		lua_rawgeti(L, -2, 2);
		lua_rawgeti(L, -3, 3);
		float x = (float)lua_tonumber(L, -3);
		float y = (float)lua_tonumber(L, -2);
		float z = (float)lua_tonumber(L, -1);
		lua_pop(L, 4);
		return vector3{ x, y, z };
	}
};

struct example_t {
	static void lua_registar(lua_t lua) {
		lua.define<&example_t::value>("value");
		lua.define<&example_t::const_value>("const_value");
		lua.define<&example_t::accum_value>("accum_value");
		lua.define<&example_t::join_value>("join_value");
		lua.define<&example_t::join_value_required>("join_value_required");
		lua.define<&example_t::join_value_refptr>("join_value_refptr");
		lua.define<&example_t::join_value_required_refptr>("join_value_required_refptr");
		lua.define<&example_t::get_value>("get_value");
		lua.define<&example_t::call>("call");
		lua.define<&example_t::forward_pair>("forward_pair");
		lua.define<&example_t::forward_tuple>("forward_tuple");
		lua.define<&example_t::forward_map>("forward_map");
		lua.define<&example_t::forward_vector>("forward_vector");
		lua.define<&example_t::prime>("prime");
		lua.define<&example_t::get_vector3>("get_vector3");
#if USE_LUA_COROUTINE
		lua.define<&example_t::coro_get_int>("coro_get_int");
		lua.define<&example_t::coro_get_none>("coro_get_none");
		lua.define<&example_t::mem_coro_get_int>("mem_coro_get_int");
#endif
	}

	int call(lua_t lua, lua_t::ref_t&& r, int value) {
		int result = lua.call<int>(r, value);
		lua.deref(r);

		return result;
	}

	int get_value() const noexcept {
		return value;
	}

	void join_value(const example_t* rhs) noexcept {
		if (rhs != nullptr) {
			value += rhs->value;
		}
	}

	void join_value_required(lua_t::required_t<example_t*>&& rhs) noexcept {
		printf("Required!\n");
		value += rhs.get()->value;
	}

	void join_value_required_refptr(lua_t&& lua, lua_t::required_t<lua_t::refptr_t<example_t>>&& rhs) noexcept {
		printf("Required ptr!\n");
		value += rhs.get().get()->value;
		lua.deref(rhs.get());
	}

	void join_value_refptr(lua_t&& lua, lua_t::refptr_t<example_t>&& rhs) noexcept {
		auto guard = lua.ref_guard(rhs);
		if (rhs != nullptr) {
			value += rhs->value;
		}
	}

	vector3 get_vector3(const vector3& input) noexcept {
		return input;
	}

	int accum_value(int init) noexcept {
		return value += init;
	}

	static std::tuple<int, std::string> forward_tuple(std::tuple<int, std::string>&& v) {
		std::get<0>(v) = std::get<0>(v) + 1;
		return v;
	}

	static std::pair<int, std::string> forward_pair(std::pair<int, std::string>&& v) {
		v.first += 1;
		return v;
	}

	std::map<std::string, int> forward_map(std::map<std::string, int>&& v) {
		v["abc"] = 123;
		return v;
	}

	std::vector<std::string> forward_vector(std::vector<std::string>&& v) {
		v.emplace_back("str");
		return std::move(v);
	}

	lua_t::ref_t prime(lua_t lua) const {
		return lua.make_table([](lua_t lua) noexcept {
			lua.define("name", "prime");
			lua.define(1, 2);
			lua.define(2, 3);
			lua.define(3, 5);
		});
	}

	void lua_finalize(lua_State* L) noexcept {
		printf("finalize!\n");
	}

#if USE_LUA_COROUTINE
	static iris::iris_coroutine_t<int> coro_get_int(const std::string& s) noexcept {
		co_return 1;
	}

	static iris::iris_coroutine_t<> coro_get_none() noexcept {
		workerptr->terminate();
		co_return;
	}

	iris::iris_coroutine_t<int> mem_coro_get_int(std::string&& s) noexcept {
		co_await iris::iris_switch(warpptr2);
		co_await iris::iris_switch(warpptr);
		co_return 2;
	}
#endif

	int value = 0;
	const int const_value = 0;
};

int main(void) {
	lua_State* L = luaL_newstate();
	luaL_openlibs(L);

	lua_t lua(L);
	lua.register_type<example_t>("example_t");
	
#if USE_LUA_COROUTINE
	worker_t worker(1);
	warp_t warp(worker);
	warp_t warp2(worker);

	workerptr = &worker;
	warpptr = &warp;
	warpptr2 = &warp2;
	worker.start();
	warp.preempt();
#endif

	bool ret = lua.call<bool>(lua.load("\
		print(_VERSION)\n\
		local a = example_t.create()\n\
		local b = example_t.create()\n\
		b:join_value_required(a)\n\
		--b:join_value_required()\n\
		b:join_value_required_refptr(a)\n\
		--b:join_value_required_refptr()\n\
		print(a:const_value())\n\
		local base = b:value(1)\n\
		print(base)\n\
		local base2 = b:value()\n\
		print(base2)\n\
		b:accum_value(1000)\n\
		local sum = 0\n\
		for i = 1, 10 do\n\
			sum = sum + a:accum_value(i)\n\
		end\n\
		b:join_value(a)\n\
		b:join_value_refptr(a)\n\
		local p = a:call(function (v) return v + 1 end, 1)\n\
		print(p) \n\
		print(a:get_value())\n\
		print(b:get_value())\n\
		print(sum)\n\
		print(a.forward_tuple({1, 'tuple'})[1])\n\
		print(a.forward_pair({2, 'pair'})[1])\n\
		print(a:forward_map({ type = 'map'})['abc'])\n\
		print(a:forward_vector({ '3', 4, '5' })[4])\n\
		local prime = a:prime()\n\
		print(prime.name)\n\
		for i = 1, #prime do\n\
			print(prime[i])\n\
		end\n\
		local t = a:get_vector3({11, 22, 33 })\n\
		for i = 1, #t do\n\
			print(t[i])\n\
		end\n\
		return true\n"));
	assert(ret);
	auto tab = lua.make_table([](lua_t&& lua) {
		lua.define("key", "value");
		lua.define(1, "number");
		lua.define(2, 2);
	});

	tab.set(lua, "set", "newvalue");
	assert(tab.get<int>(lua, 2) == 2);
	auto r = tab.get<lua_t::ref_t>(lua, 2);
	assert(r.as<int>(lua) == 2);
	assert(tab.size(lua) == 2);
	lua.deref(r);

	tab.for_each<std::string_view, std::string_view>(lua, [](auto key, auto value) {
		printf("key = %s, value = %s\n", key.data(), value.data());
		return false;
	});
	lua.deref(tab);

#if USE_LUA_COROUTINE
	lua.call<void>(lua.load("\n\
		local a = example_t.create()\n\
		local coro = coroutine.create(function() \n\
			print('coro get ' .. a.coro_get_int('hello')) \n\
			print('memcoro get ' .. a:mem_coro_get_int('world')) \n\
			print('memcoro get second ' .. a:mem_coro_get_int('world')) \n\
			a.coro_get_none()\n\
			print('coro finished')\n\
		end)\n\
		coroutine.resume(coro)\n"));

	warp.yield();
	worker.join();
	warp.join();
	warp2.join();
#endif

	lua_close(L);
	return 0;
}

