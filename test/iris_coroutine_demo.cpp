#include "../src/iris_coroutine.h"
#include "../src/iris_common.inl"
#include <chrono>
using namespace iris;

using worker_t = iris_async_worker_t<>;
using warp_t = iris_warp_t<worker_t>;
using barrier_t = iris_barrier_t<void, bool, worker_t>;
using barrier_warp_t = iris_barrier_t<warp_t>; // worker_t implicit deduced from warp_t
using coroutine_t = iris_coroutine_t<>;
using coroutine_int_t = iris_coroutine_t<int>;
using quota_t = iris_quota_t<int, 2>;
using quota_queue_t = iris_quota_queue_t<quota_t, warp_t>;
static std::atomic<size_t> pending_count = 0;

coroutine_t cascade(warp_t* warp) {
	warp_t* w = co_await iris_switch(warp);
	printf("Cascaded!\n");
	co_await iris_switch(w);
}

coroutine_int_t cascade_ret(warp_t* warp) {
	warp_t* w = co_await iris_switch(warp);
	printf("Cascaded int!\n");
	co_await iris_switch(w);
	co_return 1234;
}

coroutine_t example(warp_t::async_worker_t& async_worker, warp_t* warp, warp_t* warp_end, int value) {
	if (warp != nullptr) {
		IRIS_ASSERT(warp_end - warp >= 3);
		warp_t* current = co_await iris_switch(warp);
		printf("Switch to warp %p\n", warp);
		co_await iris_switch<warp_t>(nullptr);
		printf("Detached\n");
		co_await iris_switch(warp);
		printf("Attached\n");
		co_await iris_switch(current);
		IRIS_ASSERT(current == warp_t::get_current_warp());

		// randomly select warp
		IRIS_ASSERT(warp_end != nullptr);
		co_await iris_switch<warp_t>(nullptr); // iris_select requires warp_t::get_current_warp() == nullptr
		warp_t* selected = co_await iris_select(warp, warp_end);
		printf("Select warp: %d\n", iris_verify_cast<int>(selected - warp));

		IRIS_ASSERT(warp_end - warp > 1);
		if (warp == selected) {
			co_await iris_switch(warp + 1, warp + 2);
		} else {
			co_await iris_switch(warp, warp + 1);
		}

		printf("Paired!\n");
		co_await iris_switch(current);
	}
	pending_count.fetch_sub(1, std::memory_order_release);

	co_await cascade(warp);
	int cascade_int_result = co_await cascade_ret(warp);
	pending_count.fetch_sub(1, std::memory_order_release);

	// Step 1: test single await
	co_await iris_awaitable(warp, []() {});
	int v = co_await iris_awaitable(warp, [value]() { return value; });
	warp_t* current = warp_t::get_current_warp();
	printf("Value: %d %p\n", v, warp_t::get_current_warp());

	// Step 2: test multiple await by incrementally construction
	std::function<void()> v1 = [value]() {};
	std::function<void()> v2 = [value]() {};
	std::function<void()> v3 = [value]() {};
	pending_count.fetch_sub(1, std::memory_order_release);

	// Step 3: test multiple await by join-like construction
	if (warp == nullptr) {
		auto w2 = iris_awaitable(warp, std::move(v2));
		auto w3 = iris_awaitable(warp, std::move(v3));
		w2.dispatch();

		co_await w2;
		co_await w3;
	} else {
		co_await iris_awaitable_parallel(warp, []() {});

		auto w2 = iris_awaitable_parallel(warp, std::move(v2));
		auto w3 = iris_awaitable_parallel(warp, std::move(v3), 1);

		w3.dispatch();

		co_await w2;
		co_await w3;
	}

	pending_count.fetch_sub(1, std::memory_order_release);

	if (warp != nullptr) {
		warp_t* current = co_await iris_switch(warp);
		printf("Another switch to warp %p\n", warp);
		co_await iris_switch(current);
		IRIS_ASSERT(current == warp_t::get_current_warp());
		printf("I'm back %d\n", (int)pending_count.load(std::memory_order_acquire));
	}

	// if all tests finished, terminate the thread pool and exit the program
	if (pending_count.fetch_sub(1, std::memory_order_release) == 1) {
		async_worker.terminate();
	}
}

coroutine_int_t example_empty() {
	printf("Empty finished!\n");
	co_return 1;
}

template <typename barrier_type_t>
coroutine_t example_barrier(warp_t::async_worker_t& async_worker, barrier_type_t& barrier, int index) {
	printf("Example barrier %d begin running!\n", index);

	co_await barrier;
	std::this_thread::sleep_for(std::chrono::milliseconds(rand() % 50));
	printf("Example barrier %d mid running!\n", index);

	co_await barrier;
	std::this_thread::sleep_for(std::chrono::milliseconds(rand() % 50));
	printf("Example barrier %d end running!\n", index);

	if (pending_count.fetch_sub(1, std::memory_order_release) == 1) {
		async_worker.terminate();
	}
}

static coroutine_t example_listen(iris_dispatcher_t<warp_t>& dispatcher) {
	auto* prev = dispatcher.allocate(nullptr, []() {
		printf("prev task!");
	});

	co_await iris_listen_dispatch(dispatcher, prev);
	printf("next task!");

	if (pending_count.fetch_sub(1, std::memory_order_release) == 1) {
		dispatcher.get_async_worker().terminate();
	}
}

static coroutine_t example_quota(quota_queue_t& q) {
	{
		auto guard = co_await q.guard({ 1, 3 });
		std::array<int, 2> req = { 2, 2 };
		bool b = q.acquire(req);
		IRIS_ASSERT(b);
		q.get_async_worker().queue([&q, req]() mutable {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			printf("Release quota holder!\n");
			q.release(req);
		});
	}

	auto guard2 = co_await q.guard({ 3, 4 });
	guard2.acquire({ 1, 1 });
	guard2.release({ 1, 1 });
	guard2.clear();
	printf("Acquire quota holder!\n");
	auto guard3 = co_await q.guard({ 1, 1 });
	guard3 = std::move(guard2);

	if (pending_count.fetch_sub(1, std::memory_order_release) == 1) {
		q.get_async_worker().terminate();
	}
}

int main(void) {
	static constexpr size_t thread_count = 8;
	static constexpr size_t warp_count = 16;

	iris_async_worker_t<> worker(thread_count);
	size_t main_thread_index = worker.append(std::thread());
	worker.start();
	pending_count.fetch_add(1, std::memory_order_release);

	pending_count.fetch_add(1, std::memory_order_release);

	iris_dispatcher_t<warp_t> dispatcher(worker);
	example_listen(dispatcher).run();
	quota_t quota({ 4, 5 });

	pending_count.fetch_add(1, std::memory_order_release);
	quota_queue_t quota_queue(worker, quota);
	example_quota(quota_queue).run();

	std::vector<warp_t> warps;
	warps.reserve(warp_count);
	for (size_t i = 0; i < warp_count; i++) {
		warps.emplace_back(worker);
	}

	// test for barrier
	pending_count.fetch_add(8, std::memory_order_release);

	barrier_t barrier(worker, 4);
	example_barrier(worker, barrier, 0).run();
	example_barrier(worker, barrier, 1).run();
	example_barrier(worker, barrier, 2).run();
	example_barrier(worker, barrier, 3).run();

	barrier_warp_t barrier_warp(worker, 4);
	warps[0].queue_routine_external([&worker, &barrier_warp]() {
		example_barrier(worker, barrier_warp, 5).run();
		example_barrier(worker, barrier_warp, 6).run();
		example_barrier(worker, barrier_warp, 7).run();
		example_barrier(worker, barrier_warp, 8).run();
	});

	// initialize pending count with `example` call count
	pending_count.fetch_add(30, std::memory_order_acq_rel);

	// test for running example from an external thread
	example(worker, &warps[0], &warps[3], 1).complete([]() {
		printf("Complete!\n");
	}).run();

	example_empty().complete([](int&& value) {
		printf("Complete empty %d!\n", value);
	}).run();

	example_empty().run();
	example(worker, nullptr, nullptr, 2).run();

	warps[0].queue_routine_external([&worker, &warps]() {
		// test for running example from an warp
		example(worker, &warps[0], &warps[3], 3).run();
		example(worker, nullptr, nullptr, 4).run();
	});

	// test for running example from thread pool
	worker.queue([&worker, &warps]() {
		// test for running example from an warp
		example(worker, &warps[0], &warps[3], 5).run();
		example(worker, nullptr, nullptr, 6).run();
	});

	if (pending_count.fetch_sub(1, std::memory_order_release) == 1) {
		worker.terminate();
	}

	worker.thread_loop(main_thread_index);
	worker.join();

	// finished!
	while (!worker.finalize() || !warp_t::join(warps.begin(), warps.end(), [] { std::this_thread::sleep_for(std::chrono::milliseconds(50)); })) {}

	return 0;
}

