#include "tutorial_warp.h"

namespace iris {
	void tutorial_warp_t::lua_registar(lua_t&& lua) {
		lua.set_current<&tutorial_warp_t::pipeline>("pipeline");
		lua.set_current<&tutorial_warp_t::warp_variable>("warp_variable");
		lua.set_current<&tutorial_warp_t::free_variable>("free_variable");
		lua.set_current("run", lua.load("local self = ...\n\
print('[tutorial_warp] begin pipeline')\n\
local running = coroutine.running() \n\
local complete_count = 0 \n\
local waiting = false \n\
local loop_count = 20 \n\
for i = 1, loop_count do \n\
	coroutine.wrap(function () \n\
		print('[tutorial_warp] worker ' .. tostring(i) .. ' begin') \n\
		self:pipeline() \n\
		print('[tutorial_warp] worker ' .. tostring(i) .. ' end') \n\
		complete_count = complete_count + 1 \n\
		if complete_count == loop_count and waiting then \n\
			coroutine.resume(running) \n\
		end \n\
	end)() \n\
end \n\
if complete_count ~= loop_count then \n\
	waiting = true \n\
	coroutine.yield() \n\
end \n\
print('[tutorial_warp] final warp_variable = ' .. tostring(self:warp_variable())) \n\
print('[tutorial_warp] final free_variable = ' .. tostring(self:free_variable())) \n\
print('[tutorial_warp] end pipeline')\n"));
	}

	tutorial_warp_t::tutorial_warp_t(lua_async_worker_t& async_worker) : stage_warp(async_worker) {}
	tutorial_warp_t::~tutorial_warp_t() noexcept {
	}

	lua_coroutine_t<void> tutorial_warp_t::pipeline() {
		// switch to stage_warp
		lua_warp_t* current = co_await iris_switch(&stage_warp);

		// operations on `warp_variable` will be executed on only one thread at the same time!
		int warp_value = warp_variable;
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		warp_variable = warp_value + 1;
		warp_value = warp_variable;
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		warp_variable = warp_value - 1;

		// switch to any worker of thread poll
		co_await iris_switch(static_cast<lua_warp_t*>(nullptr));

		// operations on `free` may be executed on multiple threads at the same time!
		int free_value = free_variable;
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		free_variable = free_value + 1;
		free_value = free_variable;
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		free_variable = free_value - 1;

		// don't forget to switch back!
		co_await iris_switch(current);
	}
}
