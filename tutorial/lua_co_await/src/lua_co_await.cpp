#include "lua_co_await.h"

// register tutorial types, remove it freely
// to run tutorial, just launch the lua console, type:
// require("lua_co_await").new():run_tutorials()
//

#include "tutorial_binding.h"
#include "tutorial_async.h"
#include "tutorial_quota.h"
#include "tutorial_warp.h"
#include "tutorial_readwrite.h"

namespace iris {
	void lua_co_await_t::lua_registar(iris_lua_t&& lua, std::nullptr_t) {
		lua.set_current<&lua_co_await_t::get_version>("get_version");
		lua.set_current<&lua_co_await_t::start>("start");
		lua.set_current<&lua_co_await_t::terminate>("terminate");
		lua.set_current<&lua_co_await_t::poll>("poll");
		lua.set_current<&lua_co_await_t::tutorial_binding>("tutorial_binding");
		lua.set_current<&lua_co_await_t::tutorial_async>("tutorial_async");
		lua.set_current<&lua_co_await_t::tutorial_warp>("tutorial_warp");
		lua.set_current<&lua_co_await_t::tutorial_quota>("tutorial_quota");
		lua.set_current<&lua_co_await_t::tutorial_readwrite>("tutorial_readwrite");
		lua.set_current<&lua_co_await_t::run_tutorials>("run_tutorials");

		// shared-library crossing
		lua.set_current<&lua_co_await_t::__async_worker__>("__async_worker__");
	}

	lua_co_await_t::lua_co_await_t() : async_worker(std::make_shared<iris_async_worker_t<>>()) {
		reset();

		async_worker->set_priority_task_handler([this](iris_async_worker_t<>::task_base_t* task, size_t& priority) {
			main_warp->queue_routine([this, task]() {
				async_worker->execute_task(task);
			});

			return true;
		});
	}

	lua_co_await_t::~lua_co_await_t() noexcept {
		// force terminate on destructing
		terminate();
	}

	void* lua_co_await_t::__async_worker__(void* new_async_worker_ptr) {
		if (new_async_worker_ptr != nullptr && set_async_worker(*reinterpret_cast<std::shared_ptr<iris_async_worker_t<>>*>(new_async_worker_ptr))) {
			return new_async_worker_ptr;
		} else {
			return reinterpret_cast<void*>(&async_worker);
		}
	}

	bool lua_co_await_t::set_async_worker(std::shared_ptr<iris_async_worker_t<>> worker) {
		if (is_running())
			return false;

		std::swap(async_worker, worker);
		reset();
		return true;
	}

	void lua_co_await_t::reset() {
		if (main_warp_guard) {
			main_warp_guard.reset();
		}

		main_warp = std::make_unique<iris_warp_t<iris_async_worker_t<>>>(*async_worker);
		main_warp_guard = std::make_unique<iris_warp_t<iris_async_worker_t<>>::preempt_guard_t>(*main_warp, 0);
	}

	std::string_view lua_co_await_t::get_version() const noexcept {
		return "lua_co_await 0.0.0";
	}

	bool lua_co_await_t::is_running() const noexcept {
		return async_worker->get_thread_count() != 0;
	}

	bool lua_co_await_t::start(size_t thread_count) {
		if (!is_running()) {
			async_worker->resize(thread_count);
			// add current thread as an external worker
			size_t thread_index = async_worker->append(std::thread());
			async_worker->make_current(thread_index);
			async_worker->start();
			reset();

			return true;
		} else {
			return false;
		}
	}

	bool lua_co_await_t::terminate() noexcept {
		if (is_running()) {
			async_worker->terminate();

			// manually polling events
			async_worker->make_current(~(size_t)0);

			while (!async_worker->join() || !main_warp->join([]() {
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
				return false;
			})) {}

			async_worker->make_current(~(size_t)0);
			reset();
			async_worker->finalize();

			return true;
		} else {
			return false;
		}
	}

	bool lua_co_await_t::poll(size_t delay_in_milliseconds) {
		auto guard = write_fence();
		// try to poll tasks of main_warp, also poll other tasks in given time if there is no task in main_warp.
		if (async_worker != nullptr && main_warp != nullptr) {
			// try poll
			auto waiter = [] { std::this_thread::sleep_for(std::chrono::milliseconds(50)); return false; };
			if (main_warp->join(waiter)) {
				return true;
			} else if (delay_in_milliseconds == 0) {
				return false;
			} else {
				async_worker->poll_delay(0, std::chrono::milliseconds(delay_in_milliseconds));
				// try poll again
				return main_warp->join(waiter);
			}
		} else {
			return false;
		}
	}

	// tutorials
	iris_lua_t::ref_t lua_co_await_t::tutorial_binding(iris_lua_t&& lua) {
		return lua.make_type<tutorial_binding_t>("tutorial_binding");
	}

	iris_lua_t::ref_t lua_co_await_t::tutorial_async(iris_lua_t&& lua) {
		return lua.make_type<tutorial_async_t>("tutorial_async");
	}

	iris_lua_t::ref_t lua_co_await_t::tutorial_warp(iris_lua_t&& lua) {
		assert(async_worker != nullptr);
		return lua.make_type<tutorial_warp_t>("tutorial_warp", +[](iris_lua_t lua, tutorial_warp_t* object, std::reference_wrapper<iris_async_worker_t<>> async_worker) -> iris_lua_t::optional_result_t<tutorial_warp_t*> {
			return new (object) tutorial_warp_t(async_worker);
		}, std::ref(*async_worker));
	}

	iris_lua_t::ref_t lua_co_await_t::tutorial_quota(iris_lua_t&& lua, size_t capacity) {
		assert(async_worker != nullptr);
		return lua.make_type<tutorial_quota_t>("tutorial_quota", +[](iris_lua_t lua, tutorial_quota_t* object, std::reference_wrapper<iris_async_worker_t<>> async_worker, size_t capacity) -> iris_lua_t::optional_result_t<tutorial_quota_t*> {
			return new (object) tutorial_quota_t(async_worker, capacity);
		}, std::ref(*async_worker), capacity);
	}

	iris_lua_t::ref_t lua_co_await_t::tutorial_readwrite(iris_lua_t&& lua) {
		assert(async_worker != nullptr);
		return lua.make_type<tutorial_readwrite_t>("tutorial_readwrite", +[](iris_lua_t lua, tutorial_readwrite_t* object, std::reference_wrapper<iris_async_worker_t<>> async_worker) -> iris_lua_t::optional_result_t<tutorial_readwrite_t*> {
			return new (object) tutorial_readwrite_t(async_worker);
		}, std::ref(*async_worker));
	}

	void lua_co_await_t::run_tutorials(iris_lua_t::refptr_t<lua_co_await_t>&& self, iris_lua_t&& lua) {
		lua.call<void>(lua.load("local co_await = ... \n\
co_await:start(4) \n\
co_await:tutorial_binding().new():run() \n\
local complete_count = 0 \n\
coroutine.wrap(function () \n\
	co_await:tutorial_async().new():run() \n\
	print('complete async') \n\
	complete_count = complete_count + 1 \n\
end)() \n\
coroutine.wrap(function () \n\
	co_await:tutorial_warp().new():run() \n\
	print('complete warp') \n\
	complete_count = complete_count + 1 \n\
end)() \n\
coroutine.wrap(function () \n\
	co_await:tutorial_quota(100).new():run() \n\
	print('complete quota') \n\
	complete_count = complete_count + 1 \n\
end)() \n\
coroutine.wrap(function () \n\
	co_await:tutorial_readwrite().new():run() \n\
	print('complete readwrite') \n\
	complete_count = complete_count + 1 \n\
end)() \n\
while complete_count < 4 do \n\
	co_await:poll(1000) \n\
end \n\
co_await:terminate() \n\
print('inspect:') \n\
for k, v in pairs(co_await:__inspect__()) do \n\
	print(k .. ' = ' .. tostring(v)) \n\
end \n\
collectgarbage() \n\
print('all completed')\n\
"), std::move(self));
	}
}