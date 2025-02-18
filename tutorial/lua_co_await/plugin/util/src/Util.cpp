#include "Util.h"

namespace iris {
	void Util::lua_registar(iris_lua_t lua) {
		lua.set_current("version", 0);
	}
}