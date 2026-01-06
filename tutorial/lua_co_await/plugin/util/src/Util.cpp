#include "Util.h"

namespace iris {
	void Util::lua_registar(iris_lua_t lua) {
		lua.set_current_new<&iris_lua_t::place_new_object<Util>>("new");
		lua.set_current("version", 0);
	}
}