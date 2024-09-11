#include "tutorial_quota.h"

namespace iris {
	void tutorial_quota_t::lua_registar(lua_t&& lua) {
		lua.set_current<&tutorial_quota_t::pipeline>("pipeline");
		lua.set_current<&tutorial_quota_t::get_remaining>("get_remaining");
		lua.set_current("run", lua.load("local self = ...\n\
print('[tutorial_quota] begin pipeline')\n\
local running = coroutine.running() \n\
local complete_count = 0 \n\
local waiting = false \n\
local loop_count = 20 \n\
for i = 1, loop_count do \n\
	coroutine.wrap(function () \n\
		print('[tutorial_quota] worker ' .. tostring(i) .. ' begin') \n\
		print('[tutorial_quota] remaining ' .. tostring(self:get_remaining())) \n\
		self:pipeline(33) \n\
		print('[tutorial_quota] worker ' .. tostring(i) .. ' end') \n\
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
print('[tutorial_quota] end pipeline')\n"));
	}

	tutorial_quota_t::tutorial_quota_t(lua_async_worker_t& async_worker, size_t capacity) : quota({ capacity }), quota_queue(async_worker, quota) {}
	tutorial_quota_t::~tutorial_quota_t() noexcept {
	}

	size_t tutorial_quota_t::get_remaining() const noexcept {
		return quota.get()[0];
	}

	lua_coroutine_t<void> tutorial_quota_t::pipeline(size_t cost) {
		// first, switch to any worker in thread poll
		lua_warp_t* current = co_await iris_switch(static_cast<lua_warp_t*>(nullptr));
		// acquire quota from quota_queue
		// co_await will not return until quota is availble and acquired successfully
		auto occupy = co_await quota_queue.guard({ cost });

		// simulate working
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		co_await iris_switch(current);
		// occupy will be automatically destructed here and its quota will be returned to quota_queue.
	}
}