#include "Util.h"
using namespace iris;

extern "C" UTIL_API int luaopen_util(lua_State* L) {
	return iris_lua_t::forward(L, [](iris_lua_t luaState) {
		return luaState.make_type<Util>();
	});
}