#include "tutorial_readwrite.h"

namespace iris {
	void tutorial_readwrite_t::lua_registar(lua_t&& lua) {
		lua.set_current<&tutorial_readwrite_t::pipeline>("pipeline");
		lua.set_current("run", lua.load("local self = ...\n\
print('[turorial_readwrite] begin pipeline')\n\
local running = coroutine.running() \n\
local complete_count = 0 \n\
local waiting = false \n\
local loop_count = 20 \n\
for i = 1, loop_count do \n\
	coroutine.wrap(function () \n\
		print('[turorial_readwrite] worker ' .. tostring(i) .. ' begin') \n\
		self:pipeline() \n\
		print('[turorial_readwrite] worker ' .. tostring(i) .. ' end') \n\
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
print('[turorial_readwrite] end pipeline')\n"));
	}

	tutorial_readwrite_t::tutorial_readwrite_t(lua_async_worker_t& async_worker) : stage_warp(async_worker) {}
	tutorial_readwrite_t::~tutorial_readwrite_t() noexcept {}

	lua_coroutine_t<void> tutorial_readwrite_t::pipeline() {
		// read phase
		lua_warp_t* current = lua_warp_t::get_current_warp();

		for (int i = 0; i < 4; i++) {
			co_await iris_switch(&stage_warp, static_cast<lua_warp_t*>(nullptr), true);
			{
				auto guard = read_fence();
				printf("Read begin ... \n");
				std::this_thread::sleep_for(std::chrono::milliseconds { 200 });
				printf("Read end ... \n");
			}

			// write phase
			co_await iris_switch(&stage_warp);
			{
				auto guard = write_fence();
				printf("Write begin ... \n");
				std::this_thread::sleep_for(std::chrono::milliseconds { 50 });
				printf("Write end ... \n");
			}
		}

		co_await iris_switch(current);
	}
}