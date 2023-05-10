#include "../src/optional/iris_lua.h"
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
#endif

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
	static void lua_registar(lua_State* L) {
		iris_lua_t::define<&example_t::value>(L, "value");
		iris_lua_t::define<&example_t::const_value>(L, "const_value");
		iris_lua_t::define<&example_t::accum_value>(L, "accum_value");
		iris_lua_t::define<&example_t::join_value>(L, "join_value");
		iris_lua_t::define<&example_t::join_value_required>(L, "join_value_required");
		iris_lua_t::define<&example_t::join_value_refptr>(L, "join_value_refptr");
		iris_lua_t::define<&example_t::join_value_required_refptr>(L, "join_value_required_refptr");
		iris_lua_t::define<&example_t::get_value>(L, "get_value");
		iris_lua_t::define<&example_t::call>(L, "call");
		iris_lua_t::define<&example_t::forward_pair>(L, "forward_pair");
		iris_lua_t::define<&example_t::forward_tuple>(L, "forward_tuple");
		iris_lua_t::define<&example_t::forward_map>(L, "forward_map");
		iris_lua_t::define<&example_t::forward_vector>(L, "forward_vector");
		iris_lua_t::define<&example_t::prime>(L, "prime");
		iris_lua_t::define<&example_t::get_vector3>(L, "get_vector3");
#if USE_LUA_COROUTINE
		iris_lua_t::define<&example_t::coro_get_int>(L, "coro_get_int");
		iris_lua_t::define<&example_t::coro_get_none>(L, "coro_get_none");
		iris_lua_t::define<&example_t::mem_coro_get_int>(L, "mem_coro_get_int");
#endif
	}

	int call(lua_State* L, iris_lua_t::ref_t&& r, int value) {
		int result = iris_lua_t::call<int>(L, r, value);
		iris_lua_t::deref(L, r);
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

	void join_value_required(iris_lua_t::required_t<example_t*>&& rhs) noexcept {
		printf("Required!\n");
		value += rhs.get()->value;
	}

	void join_value_required_refptr(lua_State* L, iris_lua_t::required_t<iris_lua_t::refptr_t<example_t>>&& rhs) noexcept {
		printf("Required ptr!\n");
		value += rhs.get().get()->value;
		iris_lua_t::deref(L, rhs.get());
	}

	void join_value_refptr(lua_State* L, iris_lua_t::refptr_t<example_t>&& rhs) noexcept {
		auto guard = iris_lua_t::refguard(L, rhs);
		if (rhs != nullptr) {
			value += rhs->value;
		}
	}

	vector3 get_vector3(vector3&& input) noexcept {
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

	iris_lua_t::ref_t prime(lua_State* L) const {
		return iris_lua_t(L).make_table([](lua_State* L) noexcept {
			iris_lua_t::define(L, "name", "prime");
			iris_lua_t::define(L, 1, 2);
			iris_lua_t::define(L, 2, 3);
			iris_lua_t::define(L, 3, 5);
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

	iris_lua_t lua(L);
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

	bool ret = lua.run<bool>("\
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
		return true\n");
	assert(ret);
	auto tab = lua.make_table([](lua_State* L) {
		iris_lua_t::define(L, "key", "value");
		iris_lua_t::define(L, 1, "number");
		iris_lua_t::define(L, 2, 2);
	});

	tab.set(lua, "set", "newvalue");
	assert(tab.get<int>(lua, 2) == 2);
	auto r = tab.get<iris_lua_t::ref_t>(lua, 2);
	assert(r.as<int>(lua) == 2);
	assert(tab.size(lua) == 2);
	lua.deref(r);

	tab.for_each<std::string_view, std::string_view>(lua, [](auto key, auto value) {
		printf("key = %s, value = %s\n", key.data(), value.data());
		return false;
	});
	lua.deref(tab);

#if USE_LUA_COROUTINE
	lua.run<>("\n\
		local a = example_t.create()\n\
		local coro = coroutine.create(function() \n\
			print('coro get ' .. a.coro_get_int('hello')) \n\
			print('memcoro get ' .. a:mem_coro_get_int('world')) \n\
			print('memcoro get second ' .. a:mem_coro_get_int('world')) \n\
			a.coro_get_none()\n\
			print('coro finished')\n\
		end)\n\
		coroutine.resume(coro)\n");

	warp.yield();
	worker.join();
	warp.join();
	warp2.join();
#endif

	lua_close(L);
	return 0;
}

