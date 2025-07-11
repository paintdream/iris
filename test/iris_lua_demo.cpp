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
struct iris::iris_lua_traits_t<vector3> : std::true_type {
	using type = iris::iris_lua_traits_t<vector3>;

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
	template <typename traits_t>
	static void lua_registar(lua_t lua, traits_t) {
		lua.set_current<&example_base_t::base_value>("base_value");
		lua.set_current<&example_base_t::base_func>("base_func");
		lua.set_current<&example_base_t::base_bind_static>("base_bind_static", 1.0);
		lua.set_current<&example_base_t::base_bind>("base_bind", 1.0);
	}

	void base_bind(double value) {
		printf("base bind\n");
	}

	static void base_bind_static(double value) {
		printf("base bind static\n");
	}

	void base_func() {
		printf("base func\n");
	}

	static void lua_view_initialize(lua_t lua, int index, void* t) {
		example_base_t** p = (example_base_t**)t;
		p[1] = p[0];
		p[0] = (example_base_t*)(size_t)0x1234;
		printf("view initialize\n");
	}

	static void lua_view_finalize(lua_t lua, int index, void* p) {
		printf("view join\n");
	}

	static size_t lua_view_payload(lua_t lua, void* p) {
		printf("view payload\n");
		return sizeof(size_t);
	}

	static example_base_t* lua_view_extract(lua_t lua, int index, void* t) {
		example_base_t** p = (example_base_t**)t;
		printf("view extractor\n");
		return ((example_base_t**)p)[1];
	}

	int base_value = 2222;
};

static std::atomic<int> ref_count = 0;
struct example_t : example_base_t {
	static void lua_registar(lua_t lua, std::nullptr_t) {
		lua.set_current("lambda", [lua](int v) mutable {
			IRIS_ASSERT(v == 4);
			return 4;
		});
		lua.set_current<&example_t::value>("value");
		lua.set_current<iris_add_member_const(&example_t::value_as_const)> ("value_as_const");
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
		printf("call type %d\n", r.get_type(lua));
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

	static void lua_initialize(lua_t lua, int index, example_t* p) {
		ref_count.fetch_add(1, std::memory_order_acquire);
		printf("initialize!\n");
	}

	static void lua_finalize(lua_State* L, int index, example_t* p) noexcept {
		printf("join!\n");
		ref_count.fetch_sub(1, std::memory_order_release);
	}

	int get_value() noexcept {
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
		if (rhs) {
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

	lua_t::ref_t prime(lua_t lua) {
		return lua.make_table([](lua_t lua) noexcept {
			lua.set_current("name", "prime");
			lua.set_current(1, 2);
			lua.set_current(2, 3);
			lua.set_current(3, 5);
		});
	}

#if USE_LUA_COROUTINE
	static iris::iris_coroutine_t<int> coro_get_int(const std::string& s) noexcept {
		co_return 1;
	}

	static iris::iris_coroutine_t<> coro_get_none() noexcept {
		workerptr->terminate();
		co_return;
	}

	iris::iris_coroutine_t<iris::iris_lua_t::optional_result_t<int>> mem_coro_get_int(iris_lua_t&& t, std::string&& s) noexcept {
		// co_return iris::iris_lua_t::result_error_t("test error 1");
		iris_lua_t tt = t;
		co_await iris::iris_switch(warpptr2);
		co_await iris::iris_switch(warpptr);
		tt.native_push_variable(1);
		// co_return iris::iris_lua_t::result_error_t("test error 2");
		co_return 2;
	}

	static int mem_coro_get_int_raw(lua_State* L) {
		return lua_t::forward(L, &example_t::mem_coro_get_int);
	}
#endif

	int value = 10;
	const int const_value = 0;
	int value_as_const = 2;
};

struct shared_data_t : std::enable_shared_from_this<shared_data_t> {
	std::string data = "shared data";
};

template <typename type_t>
struct iris::iris_lua_traits_t<type_t, std::enable_if_t<std::is_base_of_v<std::enable_shared_from_this<type_t>, type_t>>> {
	using type = iris::iris_lua_traits_t<shared_data_t>;
	static_assert(alignof(std::weak_ptr<type_t>) <= alignof(type_t*), "Unexpected alignment.");

	static void lua_view_initialize(lua_t lua, int index, void* t) {
		type_t** p = reinterpret_cast<type_t**>(t);
		new (p + 1) std::weak_ptr<type_t>((*p)->weak_from_this());
	}

	static void lua_view_finalize(lua_t lua, int index, void* t) {
		type_t** p = reinterpret_cast<type_t**>(t);
		reinterpret_cast<std::weak_ptr<type_t>*>(p + 1)->~weak_ptr();
	}

	static size_t lua_view_payload(lua_t lua, void* p) {
		return sizeof(std::weak_ptr<type_t>);
	}

	static type_t* lua_view_extract(lua_t lua, int index, void* t) {
		type_t** p = reinterpret_cast<type_t**>(t);
		auto* ptr = reinterpret_cast<std::weak_ptr<type_t>*>(p + 1);
		return ptr->expired() ? nullptr : *p;
	}
};

static int error_handler(lua_State* L) {
	return lua_t::forward(L, [](std::string_view message) {
		printf("ERROR_HANDLER %s\n", message.data());
	});
}

static void use_expired(shared_data_t* t, shared_data_t* s) {
	printf("expired object: %p\nhold object: %p\n", t, s);
}

static void env_test(std::string title, std::string hi) {
	printf("env_test %s - %s\n", title.c_str(), hi.c_str());
}

struct shared_object_example_t : lua_t::shared_object_t<shared_object_example_t> {
	static void lua_registar(lua_t lua, std::nullptr_t) {
		lua.set_current<&shared_object_example_t::foo>("foo");
		lua.set_current<&shared_object_example_t::other>("other");
	}

	shared_object_example_t() : index(0) {}
	shared_object_example_t(int i) : index(i) {}

	~shared_object_example_t() {
		printf("shared_object_example_t destruct %d\n", index);
	}

	shared_object_example_t* foo(lua_t::shared_ref_t<shared_object_example_t> ptr, lua_t::required_t<lua_t::shared_ref_t<shared_object_example_t>> req, shared_object_example_t* ptr2) {
		printf("foo: %p\n", other.get());
		return this;
	}

	lua_t::shared_ref_t<shared_object_example_t> other;
	int index;
};

struct shared_object_example_sub_t : shared_object_example_t {};

int main(void) {
	lua_State* L = luaL_newstate();
	luaL_openlibs(L);

	lua_t lua(L);
	auto shared_object_example_type = lua.make_type<shared_object_example_t>("shared_object_example_t");
	auto shared_ptr_instance = lua.make_object<shared_object_example_t>(shared_object_example_type, 1);
	lua.set_global("shared_ptrinstance", std::move(shared_ptr_instance));
	{
		auto instance_copy1 = lua.get_global<lua_t::shared_ref_t<shared_object_example_t>>("shared_ptrinstance");
		auto casted = instance_copy1.cast<shared_object_example_sub_t>();
		lua.deref(std::move(casted));
		auto instance_copy2 = instance_copy1;
		{
			auto instance_copy3 = instance_copy2;
		}
		// lua.deref(instance_copy1);
		lua.set_global("shared_deref_auto", std::move(instance_copy1));
		lua.deref(instance_copy2);

		shared_object_example_type.make_registry(lua, true);
		auto instance_construct = lua.make_registry_shared<shared_object_example_t>(2);
		instance_construct->foo(nullptr, nullptr, nullptr);
		lua.deref(instance_construct);

		auto instance_offline = lua_t::shared_ref_t<shared_object_example_t>::make(3);
		lua.set_global("failed_instance", std::move(instance_offline));
		IRIS_ASSERT(lua.get_global<shared_object_example_t*>("failed_instance") == nullptr);
	
		{
			auto instance_offline2 = lua_t::shared_ref_t<shared_object_example_t>::make(4);
			lua.set_global("success_instance", lua.make_object_view(shared_object_example_type, instance_offline2.get()));
			IRIS_ASSERT(lua.get_global<shared_object_example_t*>("success_instance") != nullptr);
			lua.deref(std::move(instance_offline2));
		}
	}

	lua.deref(std::move(shared_object_example_type));
	lua.set_global<&env_test>("env_test", "lua_env");
	lua.call<void>(lua.get_global<lua_t::ref_t>("env_test"), "extra");
	lua.set_global<&use_expired>("use_expired");
	std::shared_ptr<shared_data_t> expired_object = std::make_shared<shared_data_t>();
	std::shared_ptr<shared_data_t> hold_object = std::make_shared<shared_data_t>();
	auto shared_data_type = lua.make_type<shared_data_t>("shared_data_t");
	lua.set_global("expired_instance", lua.make_object_view(shared_data_type, expired_object.get()));
	lua.set_global("hold_instance", lua.make_object_view(shared_data_type, hold_object.get()));
	lua.deref(std::move(shared_data_type));
	expired_object = nullptr;
	lua.call<void>(lua.load("use_expired(expired_instance, hold_instance)"));

	lua.deref(iris_lua_t::ref_t().once(lua, [](iris_lua_t lua) -> iris_lua_t::ref_t {
		return lua.make_table([](iris_lua_t lua) { lua.set_current("once", 1); });
	}).with(lua, [](iris_lua_t lua) {
		printf("once value = %d\n", lua.get_current<int>("once"));
	}));

	auto example_type = lua.make_type<example_t>("example_t").make_registry(lua);
	auto example_base_type = lua.make_type<example_base_t>("example_base_t");
	lua.cast_type(std::move(example_base_type), example_type);
	lua.set_global("example_t", std::move(example_type));
	int capture = 2;

	lua.set_global("refstr", "shared_string_object");
	auto p = lua.get_global<lua_t::refview_t<std::string_view>>("refstr");
	std::map<int, lua_t::refview_t<std::string_view>> vv;
	vv[1234] = std::move(p);
	for (auto&& v : vv) {
		lua.deref(std::move(v.second));
	}

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

	lua.set_global("functor", [capture]() mutable noexcept {
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

	lua.native_push_variable([T](const char* text) mutable noexcept {
		printf("No metatable %s\n", text);
		return "no metatable";
	});

	lua.native_cross_transfer_variable<true>(target, -1);
	auto f = target.native_get_variable<lua_t::ref_t>(-1);
	target.native_pop_variable(1);
	target.call<void>(std::move(f), "transferred!");

	target.call<void>(target.load("\n\
function test(a, b, c) \n\
	b:base_func() \n\
	b:base_bind() \n\
	b.base_bind_static() \n\
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
		// target.cast_type(std::move(example_base_type), temp_type);
		temp_type.make_cast(target, target.make_type<example_base_t>("example_base_t"));
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
	auto* g = target.native_get_variable<example_t*>(-1).value();
	lua.native_pop_variable(3);

	int result = target.native_call(std::move(test), 3).value();
	IRIS_ASSERT(existing_object.value == 3333);
	int ret_val = target.native_get_variable<int>(-1).value();
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

	auto encode_text = lua.encode<std::string>("haha");
	auto decode_text = lua.decode<iris_lua_t::ref_t>(std::move(encode_text)).value();
	auto decode_textview = decode_text.as<std::string_view>(lua);
	IRIS_ASSERT(decode_textview == "haha");
	lua.deref(std::move(decode_text));

	auto complex = lua.make_table([](lua_t lua) {
		lua.set_current("key", "value");
		lua.set_current("recursion", "recursion");
		lua.set_current(1, true);
		lua.set_current(2, 2);
		lua.set_current("tab", lua.make_table([](lua_t lua) {
			lua.set_current("key2", lua.make_table([](lua_t) {}));
			lua.set_current(1, 6.0);
			lua.set_current(2, &error_handler);
			lua.set_current(3, lua.call<iris_lua_t::ref_t>(lua.load("local b = 1 return function () return tostring(b + 1) end")));
		}));
	});

	auto recursive_tab = complex.get<lua_t::ref_t>(lua, "tab");
	recursive_tab->set(lua, "recursive", recursive_tab);
	lua.deref(std::move(recursive_tab.value()));

	auto encode_complex = lua.encode<std::string>(std::move(complex), [](iris_lua_t lua, iris_queue_list_t<uint8_t>& bytes, int index, int type) {
		if (type == LUA_TFUNCTION) {
			if (lua_iscfunction(lua.get_state(), index)) {
				void* p = (void*)&error_handler;
				bytes.push(0);
				bytes.push(reinterpret_cast<const char*>(&p), reinterpret_cast<const char*>(&p) + sizeof(p));
				return true;
			} else {
				bytes.push(1);
				return false;
			}
		}

		return false;
	}, iris_queue_list_t<uint8_t>());

	auto decode_complex = std::move(lua.decode<iris_lua_t::ref_t>(std::move(encode_complex), [](iris_lua_t lua, const char*& from, const char* to, int type) {
		if (type == LUA_TFUNCTION) {
			if (*from++ == 0) {
				void* p = nullptr;
				memcpy(&p, from, sizeof(p));
				lua_pushcclosure(lua.get_state(), (lua_CFunction)p, 0);
				from += sizeof(p);
				return true;
			} else {
				return false;
			}
		} else {
			return false;
		}
	}).value());

	IRIS_ASSERT(decode_complex.get<std::string_view>(lua, "key") == "value");
	IRIS_ASSERT(decode_complex.get<bool>(lua, 1));
	IRIS_ASSERT(decode_complex.get<int>(lua, 2) == 2);
	auto decode_tab = decode_complex.get<lua_t::ref_t>(lua, "tab");
	IRIS_ASSERT(decode_tab->get<double>(lua, 1) == 6.0);
	auto decode_tab2 = decode_tab->get<lua_t::ref_t>(lua, "key2");
	IRIS_ASSERT(decode_tab2->get_type(lua) == LUA_TTABLE);

	lua.native_push_variable(decode_tab->get<lua_t::ref_t>(lua, 2));
	void* pfunc = (void*)lua_tocfunction(lua.get_state(), -1);
	IRIS_ASSERT(pfunc == error_handler);
	lua.native_pop_variable(1);

	IRIS_ASSERT(lua.call<std::string>(decode_tab->get<lua_t::ref_t>(lua, 3)).value() == "2");
	recursive_tab = decode_tab->get<lua_t::ref_t>(lua, "recursive");
	IRIS_ASSERT(recursive_tab->get_type(lua) == LUA_TTABLE);
	lua.native_push_variable(recursive_tab->get<lua_t::ref_t>(lua, "recursive"));
	lua.native_push_variable(std::move(recursive_tab.value()));
	IRIS_ASSERT(lua_rawequal(lua.get_state(), -1, -2));
	lua.native_pop_variable(2);

	lua.deref(std::move(decode_tab2.value()));
	lua.deref(std::move(decode_tab.value()));
	lua.deref(std::move(decode_complex));

	auto print2 = lua.get_global<lua_t::ref_t>("print2");
	lua.native_call(std::move(print2), 1);

	lua.native_push_variable(1234);
	int v = lua.native_get_variable<int>(-1).value();
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

#if LUA_VERSION_NUM <= 501
	// lua 5.1 do not accept yield from pcall
	lua.call<void>(lua.load("\n\
		local a = example_t.new()\n\
		local coro = coroutine.create(function() \n\
			print('coro get ' .. a.coro_get_int('hello')) \n\
			local x1, x2 = a:mem_coro_get_int('world') \n\
			print('memcoro get ' .. x1 .. ', ' .. x2) \n\
			print('memcoro get second ' .. a:mem_coro_get_int('world')) \n\
			print('memcoro get second ' .. a:mem_coro_get_int_raw('world')) \n\
			a.coro_get_none()\n\
			print('coro finished')\n\
		end)\n\
		coroutine.resume(coro)\n").value());
#else
	lua.call<void>(lua.load("\n\
		local a = example_t.new()\n\
		local coro = coroutine.create(function() \n\
			local status, message = pcall(function() \n\
			print('coro get ' .. a.coro_get_int('hello')) \n\
			local x1, x2 = a:mem_coro_get_int('world') \n\
			print('memcoro get ' .. x1 .. ', ' .. x2) \n\
			print('memcoro get second ' .. a:mem_coro_get_int('world')) \n\
			print('memcoro get second ' .. a:mem_coro_get_int_raw('world')) \n\
			end) \n\
			print(status, message) \n\
			a.coro_get_none()\n\
			print('coro finished')\n\
		end)\n\
		coroutine.resume(coro)\n").value());
#endif

	warp.yield();
	worker.join();

	auto waiter = []() {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		return false;
	};

	while (warp_t::poll({ std::ref(warp), std::ref(warp2) })) {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	preempt_guard.cleanup();
#endif
	lua_close(L);

	IRIS_ASSERT(ref_count.load(std::memory_order_acquire) == 0);
	return 0;
}
