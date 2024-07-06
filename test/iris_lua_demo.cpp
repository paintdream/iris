#include "../src/iris_lua.h"
#include "../src/iris_common.inl"
#include <map>
#include <chrono>
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

using lua_t = iris_lua_t;

struct vector3 {
	float x, y, z;
};

template <>
struct iris::iris_lua_convert_t<vector3> : std::true_type {
	static int to_lua(lua_State* L, vector3&& v) noexcept {
		lua_newtable(L);
		lua_pushnumber(L, v.z);
		lua_pushnumber(L, v.y);
		lua_pushnumber(L, v.x);
		lua_rawseti(L, -4, 1);
		lua_rawseti(L, -3, 2);
		lua_rawseti(L, -2, 3);
		return 1;
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

struct example_base_t {
	static void lua_registar(lua_t lua) {
		lua.set_current<&example_base_t::base_value>("base_value");
		lua.set_current<&example_base_t::base_func>("base_func");
	}

	void base_func() {
		printf("base func\n");
	}

	int base_value = 2222;
};

struct example_t : example_base_t {
	static void lua_registar(lua_t lua) {
		lua.set_current("lambda", [lua](int v) {
			IRIS_ASSERT(v == 4);
			return 4;
		});
		lua.set_current<&example_t::value>("value");
		lua.set_current<&example_t::value_raw>("value_raw");
		lua.set_current<&example_t::const_value>("const_value");
		lua.set_current<&example_t::accum_value>("accum_value");
		lua.set_current<&example_t::join_value>("join_value");
		lua.set_current<&example_t::join_value_required>("join_value_required");
		lua.set_current<&example_t::join_value_refptr>("join_value_refptr");
		lua.set_current<&example_t::join_value_required_refptr>("join_value_required_refptr");
		lua.set_current<&example_t::get_value>("get_value");
		lua.set_current<&example_t::get_value_raw>("get_value_raw");
		lua.set_current<&example_t::get_value_raw_lambda>("get_value_raw_lambda");
		lua.set_current<&example_t::call>("call");
		lua.set_current<&example_t::forward_pair>("forward_pair");
		lua.set_current<&example_t::forward_tuple>("forward_tuple");
		lua.set_current<&example_t::forward_tuple_raw>("forward_tuple_raw");
		lua.set_current<&example_t::forward_map>("forward_map");
		lua.set_current<&example_t::forward_vector>("forward_vector");
		lua.set_current<&example_t::prime>("prime");
		lua.set_current<&example_t::get_vector3>("get_vector3");
		lua.set_current<&example_t::native_call>("native_call");
		lua.set_current<&example_t::native_call_noexcept>("native_call_noexcept");
#if USE_LUA_COROUTINE
		lua.set_current<&example_t::coro_get_int>("coro_get_int");
		lua.set_current<&example_t::coro_get_none>("coro_get_none");
		lua.set_current<&example_t::mem_coro_get_int>("mem_coro_get_int");
		lua.set_current<&example_t::mem_coro_get_int_raw>("mem_coro_get_int_raw");
#endif
	}

	example_t() noexcept {}
	example_t(const example_t& rhs) noexcept {
		printf("copy construct!!\n");
		value = rhs.value;
	}
	example_t(example_t&& rhs) noexcept {
		printf("move construct!!\n");
		value = rhs.value;
	}

	int call(lua_t lua, lua_t::ref_t&& r, int value) {
		auto result = lua.call<int>(r, value);
		lua.deref(std::move(r));

		return result.value_or(0);
	}

	static int native_call(lua_State* L) {
		printf("native call!\n");
		return 0;
	}

	static int native_call_noexcept(lua_State* L) noexcept {
		printf("native call noexcept!\n");
		return 0;
	}

	void lua_initialize(lua_t lua, int index) {
		printf("initialize!\n");
	}

	int get_value() const noexcept {
		return value;
	}

	static int value_raw(lua_State* L) {
		return lua_t::forward(L, &example_t::value);
	}

	static int get_value_raw(lua_State* L) {
		return lua_t::forward(L, &example_t::get_value);
	}

	static int get_value_raw_lambda(lua_State* L) {
		return lua_t::forward(L, [L](lua_t lua, example_t* t) {
			bool same = L == lua.get_state();
			IRIS_ASSERT(same);
			return t->get_value();
		});
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
		std::string s = lua.get_context<std::string>(lua_t::context_stack_where_t { 1 });
		printf("Required ptr! %s\n", s.c_str());
		value += rhs.get().get()->value;
		lua.deref(std::move(rhs.get()));
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

	// will cause lua error "C-function execution error"
	/*
	std::optional<int> accum_value(int init) noexcept {
		return std::nullopt;
	}*/

	static std::tuple<int, std::string> forward_tuple(std::tuple<int, std::string>&& v) {
		std::get<0>(v) = std::get<0>(v) + 1;
		return v;
	}

	static int forward_tuple_raw(lua_State* L) {
		return lua_t::forward(L, &example_t::forward_tuple);
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
			lua.set_current("name", "prime");
			lua.set_current(1, 2);
			lua.set_current(2, 3);
			lua.set_current(3, 5);
		});
	}

	void lua_finalize(lua_State* L, int index) noexcept {
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

	iris::iris_coroutine_t<iris::iris_lua_t::optional_result_t<int>> mem_coro_get_int(std::string&& s) noexcept {
		// co_return iris::iris_lua_t::result_error_t("test error 1");
		co_await iris::iris_switch(warpptr2);
		co_await iris::iris_switch(warpptr);
		// co_return iris::iris_lua_t::result_error_t("test error 2");
		co_return 2;
	}

	static int mem_coro_get_int_raw(lua_State* L) {
		return lua_t::forward(L, &example_t::mem_coro_get_int);
	}
#endif

	int value = 10;
	const int const_value = 0;
};

static int error_handler(lua_State* L) {
	return lua_t::forward(L, [](std::string_view message) {
		printf("ERROR_HANDLER %s\n", message.data());
	});
}

int main(void) {
	lua_State* L = luaL_newstate();
	luaL_openlibs(L);

	lua_t lua(L);
	auto example_type = lua.make_type<example_t>("example_t").make_registry(lua);
	auto example_base_type = lua.make_type<example_base_t>("example_base_t");
	lua.cast_type(std::move(example_base_type), example_type);
	lua.set_global("example_t", std::move(example_type));
	int capture = 2;

	struct lambda {
		lambda() {
			printf("lambda constructor!\n");
		}

		lambda(lambda&& l) noexcept {
			printf("lambda move constructor!\n");
		}
		
		lambda(const lambda&) = delete;

		~lambda() {
			printf("lambda destructor!\n");
		}

		int operator () () {
			return 3;
		}
	};

	lua.set_global("fmt_string", lua.make_string("hello %s", "world!"));
	IRIS_ASSERT(lua.get_global<std::string>("fmt_string") == "hello world!");

	lua.set_global("fmt_string_lambda", lua.make_string([](auto&& buff) {
		buff << "hello world!";
	}));

	IRIS_ASSERT(lua.get_global<std::string>("fmt_string_lambda") == "hello world!");

	lua.set_global("functor", [capture]() noexcept {
		return capture;
	});
	int retcapture = lua.call<int>(lua.get_global<iris_lua_t::ref_t>("functor")).value();
	IRIS_ASSERT(retcapture == capture);

	lua.set_global("functor2", lambda());
	int retcapture2 = lua.call<int>(lua.get_global<iris_lua_t::ref_t>("functor2")).value();
	IRIS_ASSERT(retcapture2 == 3);
	
#if USE_LUA_COROUTINE
	worker_t worker(1);
	warp_t warp(worker);
	warp_t warp2(worker);

	workerptr = &worker;
	warpptr = &warp;
	warpptr2 = &warp2;
	worker.start();
	warp_t::preempt_guard_t preempt_guard(warp, 0);
#endif

	lua_State* T = luaL_newstate();
	luaL_openlibs(T);
	lua_t target(T);
	target.call<void>(target.load("\n\
function test(a, b, c) \n\
	b:base_func() \n\
	print('equal value ======== ' .. tostring(b == c)) \n\
	print('base value ======== ' .. tostring(b:base_value())) \n\
	print('cross ' .. tostring(a)) \n\
	print('cross value ' .. b:value()) \n\
	print('lambda value ' .. b.lambda(4)) \n\
	print('cross value ' .. c:value()) \n\
	c:value(3333) \n\
	print('cross value ' .. c:value()) \n\
	return a \n\
end\n\
function test2() \n\
	local a = { b = {}, text = 'text' } \n\
	a.b.self = a\n\
	return a \n\
end\n\
").value());
	lua_t::ref_t test = target.get_global<lua_t::ref_t>("test");
	example_t existing_object;
	existing_object.value = 2222;
	auto temp_type = target.make_type<example_t>("example_temp_t").make_registry(target);
	{
		auto example_base_type = target.make_type<example_base_t>("example_base_t");
		target.cast_type(std::move(example_base_type), temp_type);
	}

	target.call<void>(test, "existing", target.make_registry_object_view<example_t>(&existing_object), target.make_object_view<example_t>(temp_type, &existing_object));
	target.deref(std::move(temp_type));
	IRIS_ASSERT(existing_object.value == 3333);
	existing_object.value = 2222;

	lua.native_push_variable(1234);
	lua.native_push_variable(lua.make_registry_object<example_t>());
	lua.native_push_variable(lua.make_object_view<example_t>(lua.make_type<example_t>("example_duplicate_view_t"), &existing_object));
	lua.native_cross_transfer_variable<true>(target, -3);
	lua.native_cross_transfer_variable<true>(target, -2);
	lua.native_cross_transfer_variable<false>(target, -1);
	auto* g = target.native_get_variable<example_t*>(-1);
	lua.native_pop_variable(3);

	int result = target.native_call(std::move(test), 3).value();
	IRIS_ASSERT(existing_object.value == 3333);
	int ret_val = target.native_get_variable<int>(-1);
	IRIS_ASSERT(ret_val == 1234);
	target.native_pop_variable(1);

	auto test2 = target.get_global<lua_t::ref_t>("test2");
	int result2 = target.native_call(std::move(test2), 0).value();
	target.native_cross_transfer_variable<true>(lua, -1);
	target.native_pop_variable(1);
	lua_close(T);

	lua.call<void>(lua.load("\n\
	function print2(a) \n\
		print(a.text) \n\
		assert(a.b.self == a) \n\
	end\n").value());

	auto print2 = lua.get_global<lua_t::ref_t>("print2");
	lua.native_call(std::move(print2), 1);

	lua.native_push_variable(1234);
	int v = lua.native_get_variable<int>(-1);
	lua_pop(L, 1);
	IRIS_ASSERT(v == 1234);

	lua_t::refptr_t<example_t> example = lua.make_object<example_t>(lua.get_global<lua_t::ref_t>("example_t"));
	example->value = 5;
	lua.deref(std::move(example));
	auto temp_tab = lua.make_table([](lua_t lua) {
		lua.set_current("first", 1);
	});

	lua.with(temp_tab, [](lua_t lua) {
		lua.set_current("second", 2);
	});

	lua.set_global("test_tab", std::move(temp_tab));

	auto success = lua.call<void>(lua.load("\
		print(_VERSION)\n\
		example_t.native_call() \n\
		example_t.native_call_noexcept() \n\
		local a = example_t.new()\n\
		local b = example_t.new()\n\
		b:base_func() \n\
		print('base value ' .. tostring(b:base_value())) \n\
		b:join_value_required(a)\n\
		--b:join_value_required()\n\
		b:join_value_required_refptr(a)\n\
		--b:join_value_required_refptr()\n\
		print(a:const_value())\n\
		b:value(1)\n\
		assert(b:value() == 1)\n\
		assert(b:value_raw() == 1)\n\
		assert(b:get_value_raw() == 1)\n\
		assert(b:get_value_raw_lambda() == 1)\n\
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
		print(a.forward_tuple_raw({1, 'tuple'})[1])\n\
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
		end\n").value());
	IRIS_ASSERT(success);
	auto tab = lua.make_table([](lua_t&& lua) {
		lua.set_current("key", "value");
		lua.set_current(1, "number");
		lua.set_current(2, 2);
	});

	tab.set(lua, "set", "newvalue");
	IRIS_ASSERT(*tab.get<int>(lua, 2) == 2);
	auto r = tab.get<lua_t::ref_t>(lua, 2);
	IRIS_ASSERT(r);
	IRIS_ASSERT((*r).as<int>(lua) == 2);
	IRIS_ASSERT(tab.size(lua) == 2);
	lua.deref(std::move(*r));

	tab.for_each<std::string_view, std::string_view>(lua, [](auto key, auto value) {
		printf("key = %s, value = %s\n", key.data(), value.data());
		return false;
	});
	lua.deref(std::move(tab));

	lua.set_global<&lua_error>("other_error");
	lua.set_registry<&lua_error>("other_error");
	// lua.native_push_variable<&lua_error>();
	// lua.load("a").set<&lua_error>(lua, "test");
	IRIS_ASSERT(!lua.load("err"));
	printf("Error message: %s\n", lua.load("err").message.c_str());

#if USE_LUA_COROUTINE
	lua.call<void>(lua.load("\n\
		local a = example_t.new()\n\
		local coro = coroutine.create(function() \n\
			local status, message = pcall(function() \n\
			print('coro get ' .. a.coro_get_int('hello')) \n\
			print('memcoro get ' .. a:mem_coro_get_int('world')) \n\
			print('memcoro get second ' .. a:mem_coro_get_int('world')) \n\
			print('memcoro get second ' .. a:mem_coro_get_int_raw('world')) \n\
			end) \n\
			print(status, message) \n\
			a.coro_get_none()\n\
			print('coro finished')\n\
		end)\n\
		coroutine.resume(coro)\n").value());

	warp.yield();
	worker.join();
	warp.join([] { std::this_thread::sleep_for(std::chrono::milliseconds(50)); });
	warp2.join([] { std::this_thread::sleep_for(std::chrono::milliseconds(50)); });
	preempt_guard.cleanup();
#endif
	lua_close(L);
	return 0;
}
