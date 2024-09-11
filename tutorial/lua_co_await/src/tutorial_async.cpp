#include "tutorial_async.h"

namespace iris {
	void tutorial_async_t::lua_registar(lua_t&& lua) {
		lua.set_current<&tutorial_async_t::wait>("wait");
		lua.set_current("run", lua.load("local self = ...\n\
print('[tutorial_async] wait for 1000 ms ' .. tostring(self))\n\
self:wait(1000)\n\
print('[tutorial_async] wait complete')\n"));
	}

	tutorial_async_t::tutorial_async_t() {}
	tutorial_async_t::~tutorial_async_t() noexcept {
	}

	lua_coroutine_t<void> tutorial_async_t::wait(size_t millseconds) {
		// switch to any worker thread of thread poll, without taking any warps
		// return the current warp before switching
		lua_warp_t* current = co_await iris_switch(static_cast<lua_warp_t*>(nullptr));

		// pretend doing some works
		std::this_thread::sleep_for(std::chrono::milliseconds(millseconds));

		// finished, just go back to the original warp
		co_await iris_switch(current);
	}
}