#include "../src/iris_coroutine.h"
#include "../src/iris_buffer.h"
#include "../src/iris_system.h"
#include "../src/iris_common.inl"
#include <chrono>
#include <cstdio>
#include <condition_variable>
using namespace iris;

using worker_t = iris_async_worker_t<>;

struct engine_t {
	using warp_t = iris_warp_t<worker_t>;
	using barrier_t = iris_barrier_t<warp_t, bool, worker_t>;
	using event_t = iris_event_t<warp_t, worker_t>;
	using pipe_t = iris_pipe_t<int, warp_t, worker_t>;
	using coroutine_t = iris_coroutine_t<>;

	engine_t() : worker(std::thread::hardware_concurrency()), frame(worker, 3u), pipe(worker), sync_event(worker),
		warp_audio(worker), warp_script(worker), warp_network(worker), warp_render(worker) {
		worker.start();

		coroutine_async().run();
		coroutine_tick().run();
	}

	~engine_t() noexcept {
		worker.terminate();
		worker.join();

		auto waiter = [] { std::this_thread::sleep_for(std::chrono::milliseconds(50)); };
		while (!warp_audio.join(waiter) || !warp_script.join(waiter) || !warp_network.join(waiter) || !warp_render.join(waiter)) {
			printf("finalizing ...\n");
		}
	}

	coroutine_t coroutine_async() {
		int index = 0;
		while (co_await frame) {
			// place a barrier here to assure the completion of sync_event.reset() in main coroutine happends before we use it
			printf("coroutine async prepare\n");
			pipe.emplace(index++);

			// pretend to do something
			printf("coroutine async ticks\n");

			co_await iris_switch(&warp_audio);
			printf("coroutine async audio ticks\n");

			co_await sync_event;
			printf("coroutine async after event\n");
			IRIS_ASSERT(warp_t::get_current_warp() != nullptr);

			co_await iris_switch(&warp_script);
			printf("coroutine async script ticks\n");

			// it will trigger a gcc 11 bug if you remove the following line but i didn't know why
			co_await iris_awaitable(&warp_render, std::function<void()>([]() { printf("coroutine async render ticks\n"); }));
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		printf("coroutine async exit\n");
	}

	template <typename element_t>
	using transient_vector = std::vector<element_t, iris_cache_allocator_t<element_t>>;
		
	template <typename element_t>
	using block_allocator_t = iris_block_allocator_t<element_t>;

	struct vector_t {
		float x, y, z;
	};

	struct position_t : vector_t {};
	struct velocity_t : vector_t {};

	struct attribute_t {
		float current_value;
		float max_value;
	};

	struct hitpoint_t : attribute_t {};
	struct manapoint_t : attribute_t {};
	struct parent_t {
		uint32_t id;
	};

	coroutine_t coroutine_tick() {
		iris_cache_t<uint8_t> frame_cache;
		iris_system_t<uint32_t, block_allocator_t, parent_t, position_t, velocity_t> geo_system;
		iris_system_t<uint32_t, block_allocator_t, parent_t, hitpoint_t, manapoint_t> attribute_system;

		for (uint32_t i = 0; i < 32; i++) {
			geo_system.insert(i, parent_t{ i - 1 }, position_t{ 0.0f, 0.0f, float(i) }, velocity_t{ 0.0f, float(i), 0.0f });
			attribute_system.insert(i, parent_t{ i + 1 }, hitpoint_t{ 0.0f, float(i) }, manapoint_t{ 0.0f, float(i) });
		}

		while (co_await frame) {
			sync_event.reset();
			printf("coroutine main prepare\n");

			int value = co_await pipe;
			printf("coroutine main pipe value %d\n", value);

			frame_cache.reset();
			printf("coroutine main ticks\n");
			transient_vector<std::function<void()>> callbacks(&frame_cache);
			callbacks.emplace_back([]() { printf("callback A\n"); });

			co_await iris_switch(&warp_script);
			printf("coroutine main script ticks\n");
			callbacks.emplace_back([]() { printf("callback B\n"); });

			printf("coroutine main before event\n");
			sync_event.notify();

			co_await iris_awaitable(&warp_render, std::function<void()>([]() { printf("coroutine parallel render ticks\n"); }));
			co_await iris_awaitable(&warp_network, std::function<void()>([]() { printf("coroutine parallel network ticks\n"); }));
			
			for (auto&& callback : callbacks) {
				callback();
			}
		}

		printf("coroutine main exit\n");
	}

	barrier_t& get_frame() noexcept {
		return frame;
	}

protected:
	worker_t worker;
	barrier_t frame;

	pipe_t pipe;
	event_t sync_event;
	warp_t warp_audio;
	warp_t warp_script;
	warp_t warp_network;
	warp_t warp_render;
};

int main(void) {
	engine_t engine;
	auto& frame = engine.get_frame();
	frame.set_value(true);
	std::condition_variable cv;
	std::mutex mutex;
	while (frame.get_value()) {
		bool state = false;
		frame.dispatch([&cv, &mutex, &state](auto& frame) {
			if (rand() % 11 == 0) {
				frame.set_value(false);
			}

			std::lock_guard<std::mutex> guard(mutex);
			state = true;
			cv.notify_one();
		});

		std::unique_lock<std::mutex> guard(mutex);
		if (!state) {
			cv.wait(guard);
		}
	}

	return 0;
}
