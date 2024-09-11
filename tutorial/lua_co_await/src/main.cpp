#include "lua_co_await.h"
#include "../../../src/iris_common.inl"
using namespace iris;

// run this tutorial with:
// require("lua_co_await").new():run_tutorials()

extern "C"
#if defined(_MSC_VER)
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
int luaopen_lua_co_await(lua_State * L) {
	return lua_t::forward(L, [](lua_t lua) {
		return lua.make_type<lua_co_await_t>("lua_co_await");
	});
}
