#include "../src/iris_dispatcher.h"
#include "../src/iris_common.inl"
#include <cstdio>
#include <future>
#include <chrono>
using namespace iris;

static void external_poll();
static void stack_op();
static void not_pow_two();
static void framed_data();
static void simple_explosion();
static void garbage_collection();
static void acquire_release();
static void graph_dispatch();

int main(void) {
	external_poll();
	stack_op();
	not_pow_two();
	framed_data();
	simple_explosion();
	garbage_collection();
	acquire_release();
	graph_dispatch();

	return 0;
}

template <typename element_t>
using worker_allocator_t = iris_object_allocator_t<element_t>;

void external_poll() {
	static constexpr size_t thread_count = 4;
	static constexpr size_t warp_count = 8;

	using worker_t = iris_async_worker_t<std::thread, std::function<void()>, worker_allocator_t>;
	using warp_t = iris_warp_t<worker_t>;

	worker_t worker(thread_count);
	std::promise<bool> started;
	std::future<bool> future = started.get_future();
	size_t i = worker.get_thread_count();
	worker.append([&future, &started, &worker, i]() mutable {
		// copied from iris_async_worker_t<>::start() thread routine
		try {
			future.get();

			worker.make_current(i);
			printf("[[ external thread running ... ]]\n");

			while (!worker.is_terminated()) {
				if (!worker.poll_delay(0, std::chrono::milliseconds(20))) {
					// there is no 0 priority task, assert it
					IRIS_ASSERT(false);
				}
			}

			printf("[[ external thread exited ... ]]\n");
		} catch (std::bad_alloc&) {
			throw; // by default, terminate
		} catch (std::exception&) {
			throw;
		}
	});

	worker.start();
	started.set_value(true);

	std::vector<warp_t> warps;
	warps.reserve(warp_count);
	for (size_t w = 0; w < warp_count; w++) {
		warps.emplace_back(worker, 1);
	}

	warps[0].queue_routine_post([&worker]() {
		worker.terminate();
	});
	worker.finalize();
}

void stack_op() {
	static constexpr size_t thread_count = 4;
	static constexpr size_t warp_count = 8;
	iris_async_worker_t<> worker(thread_count);
	using warp_t = iris_warp_t<iris_async_worker_t<>>;
	std::vector<warp_t> warps;
	warps.reserve(warp_count);
	for (size_t i = 0; i < warp_count; i++) {
		warps.emplace_back(worker);
	}

	worker.append(std::thread()); // add a dummy thread
	worker.start();

	std::atomic<size_t> counter;
	counter.store(warp_count, std::memory_order_relaxed);
	for (size_t i = 0; i < warp_count; i++) {
		warps[i].queue_routine_post([&, i]() {
			for (size_t k = 0; k < warp_count; k++) {
				IRIS_ASSERT(i == warp_t::get_current() - &warps[0]);
				warp_t::preempt_guard_t guard(warps[k], 0);
				printf("take warp %d based on %d %s\n", int(k), int(i), guard ? "success!" : "failed!");
			}

			if (counter.fetch_sub(1, std::memory_order_release) == 1) {
				worker.terminate();
			}
		});
	}

	worker.finalize();
}

void not_pow_two() {
	struct pos_t {
		pos_t(float xx, float yy, float zz) : x(xx), y(yy), z(zz) {}
		float x, y, z;
	};

	iris_queue_list_t<pos_t> data;
	data.push(pos_t(1, 2, 3));
	data.push(pos_t(1, 2, 3));
	pos_t d = data.top();
	data.pop();
	d.x = 2;
}

void framed_data() {
	printf("[[ demo for iris dispatcher : framed_data ]]\n");

	iris_queue_list_t<int> data;

	int temp[4] = { 5, 8, 13, 21 };
	for (size_t j = 0; j < 256; j++) {
		data.push(temp, temp + 4);

		// thread 1
		iris_queue_frame_t<decltype(data)> q(data);
		q.push(1);
		q.push(2);
		q.release();
		q.push(3);
		q.push(4);
		q.push(5);
		q.release();
		q.push(6);
		q.release();

		int other[4];
		data.pop(other, other + 4);
		for (size_t k = 0; k < 4; k++) {
			IRIS_ASSERT(other[k] == temp[k]);
		}

		// thread 2
		int i = 0;
		while (q.acquire()) {
			printf("frame %d\n", i++);

			for (auto&& x : q) {
				printf("%d\n", x);
			}
		}
	}
}

void simple_explosion(void) {
	static constexpr size_t thread_count = 4;
	static constexpr size_t warp_count = 8;

	using worker_t = iris_async_worker_t<std::thread, std::function<void()>, worker_allocator_t>;
	using warp_t = iris_warp_t<worker_t>;

	worker_t worker(thread_count);
	iris_async_balancer_t<worker_t> balancer(worker);
	balancer.down();
	balancer.up();

	std::promise<bool> started;
	std::future<bool> future = started.get_future();
	size_t i = worker.get_thread_count();
	worker.append([&future, &started, &worker, i]() mutable {
		// copied from iris_async_worker_t<>::start() thread routine
		try {
			future.get();
			worker.make_current(i);
			printf("[[ external thread running ... ]]\n");

			while (!worker.is_terminated()) {
				if (!worker.poll_delay(0, std::chrono::milliseconds(20))) {
					printf("[[ external thread has polled a task ... ]]\n");
				}
			}

			printf("[[ external thread exited ... ]]\n");
		} catch (std::bad_alloc&) {
			throw; // by default, terminate
		} catch (std::exception&) {
			throw;
		}
	});

	worker.start();
	started.set_value(true);

	std::vector<warp_t> warps;
	warps.reserve(warp_count);
	for (size_t w = 0; w < warp_count; w++) {
		warps.emplace_back(worker);
	}

	srand((unsigned int)time(nullptr));
	printf("[[ demo for iris dispatcher : simple_explosion ]] \n");

	int32_t warp_data[warp_count] = { 0 };
	enable_read_write_fence_t<> fences[warp_count] = {};
	static constexpr size_t split_count = 4;
	static constexpr size_t terminate_factor = 100;
	static constexpr size_t parallel_factor = 11;
	static constexpr size_t parallel_count = 6;

	std::function<void()> explosion;

	// queue tasks randomly to test if dispatcher could handle them correctly.
	explosion = [&warps, &explosion, &worker, &warp_data, &fences]() {
		if (worker.is_terminated())
			return;

		warp_t& current_warp = *warp_t::get_current();
		size_t warp_index = &current_warp - &warps[0];
		do {
			auto write_guard = fences[warp_index].write_fence();

			warp_data[warp_index]++;

			// simulate working
			std::this_thread::sleep_for(std::chrono::milliseconds(rand() % 40));
			warp_data[warp_index]++;

			if (rand() % terminate_factor == 0) {
				// randomly terminates
				worker.terminate();
			}

			warp_data[warp_index]++;
		} while (false);

		// randomly dispatch to warp
		for (size_t i = 0; i < split_count; i++) {
			// notice that we cannot use queue_routine() here since it may suspend the current warp within calling
			warps[rand() % warp_count].queue_routine_post(explosion);
		}

		do {
			auto write_guard = fences[warp_index].write_fence();
			warp_data[warp_index] -= 3;
		} while (false);

		if (rand() % parallel_factor == 0) {
			// read-write lock example: multiple reading blocks writing
			std::shared_ptr<std::atomic<int32_t>> shared_value = std::make_shared<std::atomic<int32_t>>(-0x7fffffff);
			for (size_t i = 0; i < parallel_count; i++) {
				current_warp.queue_routine_parallel_post([shared_value, warp_index, &warp_data, &fences, &warps]() {
					auto read_guard = fences[warp_index].read_fence();
					// only read operations
					std::this_thread::sleep_for(std::chrono::milliseconds(rand() % 40));
					int32_t v = shared_value->exchange(warp_data[warp_index], std::memory_order_release);
					IRIS_ASSERT(v == warp_data[warp_index] || v == -0x7fffffff);
				});

				if (i == parallel_count / 2) {
					current_warp.yield();
				}
			}
		}
	};

	// invoke explosion from external thread (current thread is external to the threads in thread pool)
	warps[0].queue_routine_post(explosion);
	worker.finalize();

	// finished!
	while (!worker.join() || !warp_t::join(warps.begin(), warps.end(), [] { std::this_thread::sleep_for(std::chrono::milliseconds(50)); return true; }) || !worker.empty()) {}

	printf("after: \n");
	for (size_t k = 0; k < warp_count; k++) {
		printf("warp %d : %d\n", int(k), warp_data[k]);
	}
}

struct count_warp_t : iris_warp_t<iris_async_worker_t<>, false, count_warp_t> {
	template <typename... args_t>
	count_warp_t(args_t&&... args) : iris_warp_t<iris_async_worker_t<>, false, count_warp_t>(std::forward<args_t>(args)...) {}
	~count_warp_t() {
		assert(counter == 0);
	}

	int counter = 0;
	void enter_warp() {
		counter++;
	}

	void leave_warp() {
		counter--;
	}

	size_t enter_join_warp(bool execute_remaining, bool finalize) {
		return 0;
	}

	size_t leave_join_warp(bool execute_remaining, bool finalize) {
		return 0;
	}

	void suspend_warp() {}
	void resume_warp() {}
	void flush_warp() {}
};

void garbage_collection() {
	static constexpr size_t thread_count = 8;
	static constexpr size_t warp_count = 16;
	using warp_t = count_warp_t;

	for (size_t m = 0; m < 4; m++) {
		iris_async_worker_t<> worker(thread_count);
		worker.start();

		std::vector<warp_t> warps;
		warps.reserve(warp_count);
		for (size_t i = 0; i < warp_count; i++) {
			warps.emplace_back(worker);
		}

		srand((unsigned int)time(nullptr));
		printf("[[ demo for iris dispatcher : garbage_collection ]]\n");
		struct node_t {
			size_t warp_index = 0;
			size_t visit_count = 0; // we do not use std::atomic<> here.
			std::vector<size_t> references;
		};

		struct graph_t {
			std::vector<node_t> nodes;
		};

		// randomly initialize connections.
		static constexpr size_t node_count = 4096;
		static constexpr size_t max_node_connection = 5;
		static constexpr size_t extra_node_connection_root = 20;

		graph_t graph;
		graph.nodes.reserve(node_count);

		for (size_t i = 0; i < node_count; i++) {
			node_t node;
			node.warp_index = rand() % warp_count;

			size_t connection = rand() % max_node_connection;
			node.references.reserve(connection);

			for (size_t k = 0; k < connection; k++) {
				node.references.emplace_back(rand() % node_count); // may connected to it self
			}

			graph.nodes.emplace_back(std::move(node));
		}

		// select random root
		size_t root_index = rand() % node_count;

		// ok now let's start collect from root!
		std::function<void(size_t)> collector;
		std::atomic<size_t> collecting_count;
		collecting_count.store(0, std::memory_order_release);

		collector = [&warps, &collector, &worker, &graph, &collecting_count](size_t node_index) {
			count_warp_t& current_warp = *static_cast<count_warp_t*>(warp_t::get_current());
			size_t warp_index = &current_warp - &warps[0];

			node_t& node = graph.nodes[node_index];
			IRIS_ASSERT(node.warp_index == warp_index);
			current_warp.validate();

			if (node.visit_count == 0) {
				node.visit_count++;

				for (size_t i = 0; i < node.references.size(); i++) {
					size_t next_node_index = node.references[i];
					size_t next_node_warp = graph.nodes[next_node_index].warp_index;
					collecting_count.fetch_add(1, std::memory_order_acquire);
					current_warp.validate();
					warps[next_node_warp].queue_routine(std::bind(collector, next_node_index));
				}
			}

			if (collecting_count.fetch_sub(1, std::memory_order_release) == 1) {
				// all work finished.
				size_t collected_count = 0;
				for (size_t k = 0; k < graph.nodes.size(); k++) {
					node_t& subnode = graph.nodes[k];
					IRIS_ASSERT(subnode.visit_count < 2);
					collected_count += subnode.visit_count;
					subnode.visit_count = 0;
				}

				printf("garbage_collection finished. %d of %d collected.\n", int(collected_count), int(graph.nodes.size()));
				worker.terminate();
			}
		};

		collecting_count.fetch_add(1, std::memory_order_acquire);
		// add more references to root
		for (size_t j = 0; j < extra_node_connection_root; j++) {
			graph.nodes[root_index].references.emplace_back(rand() % node_count);
		}

		// invoke explosion from external thread (current thread is external to the threads in thread pool)
		warps[graph.nodes[root_index].warp_index].queue_routine_post(std::bind(collector, root_index));
		worker.finalize();

		// finished!
		while (!worker.join() || !warp_t::join(warps.begin(), warps.end(), []() {
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			return false;
		}) || !worker.empty()) {}
	}
}

void graph_dispatch() {
	static constexpr size_t thread_count = 8;
	static constexpr size_t warp_count = 16;
	iris_async_worker_t<> worker(thread_count);
	using warp_t = iris_warp_t<iris_async_worker_t<>>;
	worker.start();

	std::vector<warp_t> warps;
	warps.reserve(warp_count);
	for (size_t i = 0; i < warp_count; i++) {
		warps.emplace_back(worker);
	}

	printf("[[ demo for iris dispatcher : graph_dispatch ]]\n");

	std::atomic<size_t> task_count;
	static constexpr size_t total_pass = 716;
	task_count.store(0, std::memory_order_relaxed);

	struct dispatcher_t : iris_dispatcher_t<warp_t, dispatcher_t> {
		dispatcher_t(iris_async_worker_t<>& w) : iris_dispatcher_t<warp_t, dispatcher_t>(w) {}
		void dispatcher_complete() {
			async_worker.terminate();
		}

		void dispatcher_enter_execute(const routine_handle_t&) {}
		void dispatcher_leave_execute(const routine_handle_t&) {}
	};

	dispatcher_t dispatcher(worker);

	using routine_handle_t = dispatcher_t::routine_handle_t;
	routine_handle_t last = dispatcher.allocate(nullptr);
	for (size_t k = 0; k < total_pass; k++) {
		routine_handle_t d = dispatcher.allocate(&warps[2], [&task_count](const auto& handle) {
			task_count.fetch_sub(1, std::memory_order_release);
			printf("Warp 2 task [4]\n");
		});

		routine_handle_t a = dispatcher.allocate(&warps[0], [&task_count](const auto& handle) {
			task_count.fetch_sub(1, std::memory_order_release);
			printf("Warp 0 task [1]\n");
		});

		routine_handle_t b = dispatcher.allocate(&warps[1], [&dispatcher, dd = dispatcher.defer(d).move(), &task_count](const auto& handle) {
			routine_handle_t d(dd);
			task_count.fetch_sub(1, std::memory_order_release);
			printf("Warp 1 task [2]\n");
			dispatcher.dispatch(std::move(d));
		});

		dispatcher.order(a, b);
		// dispatcher.order(b, a); // trigger validate assertion

		auto c = dispatcher.allocate(nullptr, [&task_count](const auto& handle) {
			task_count.fetch_sub(1, std::memory_order_release);
			printf("Warp nil task [3]\n");
		});
		dispatcher.order(b, c);
		// dispatcher.order(c, a);// trigger validate assertion
		dispatcher.order(b, d);

		worker.queue([&dispatcher, aa = a.move(), bb = b.move(), cc = c.move(), dd = d.move(), &task_count]() {
			routine_handle_t a(aa), b(bb), c(cc), d(dd);
			task_count.fetch_add(4, std::memory_order_release);
			dispatcher.dispatch(std::move(a));
			dispatcher.dispatch(std::move(b));
			dispatcher.dispatch(std::move(c));
			dispatcher.dispatch(std::move(d));
		});
	}

	static constexpr size_t max_task_count = 0x1126;
	uint8_t executed[max_task_count] = { 0 };
	routine_handle_t tasks[max_task_count];
	size_t sum_factors = 0;

	for (size_t n = 0; n < max_task_count; n++) {
		warp_t* warp = &warps[0];
		tasks[n] = dispatcher.allocate(nullptr, [&dispatcher, n, warp, &executed, &sum_factors](const auto& handle) {
			size_t sum = 0;
			for (size_t m = 2; m < n; m++) {
				if (n % m == 0) {
					IRIS_ASSERT(executed[m]);
					sum += n;
				}
			}

			executed[n]++;
			dispatcher.dispatch(dispatcher.allocate(warp, [sum, &sum_factors](const auto& handle) {
				// it's thread safe because we are in warp 0
				sum_factors += sum;
			}));
		});

		for (size_t m = 2; m < n; m++) {
			if (n % m == 0) {
				dispatcher.order(tasks[m], tasks[n]);
			}
		}
	}

	for (size_t n = max_task_count; n != 0; n--) {
		dispatcher.dispatch(std::move(tasks[n - 1]));
	}

	dispatcher.dispatch(std::move(last));
	worker.finalize();
	IRIS_ASSERT(task_count.load(std::memory_order_acquire) == 0);
	printf("sum of factors: %d\n", (int)sum_factors);

	// finished!
	while (!worker.join() || !warp_t::join(warps.begin(), warps.end(), []() {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		return false;
	}) || !worker.empty()) {}
}

void acquire_release() {
	static constexpr size_t thread_count = 8;
	using warp_t = iris_warp_t<iris_async_worker_t<>>;

	iris_async_worker_t<> worker(thread_count);
	worker.start();

	warp_t main_warp(worker, 0);
	std::atomic<int> counter;
	counter.store(0, std::memory_order_relaxed);

	for (size_t i = 0; i < 1000; i++) {
		worker.queue([&counter, &worker, &main_warp]() {
			std::shared_ptr<bool> shared = std::make_shared<bool>(false);
			main_warp.queue_routine_post([shared]() {
				*shared = true;
			});

			worker.queue([&counter, &worker, &main_warp, shared]() {
				// place a barrier here so that the queue_routine_post below must be scheduled after the one above
				main_warp.queue_barrier();
				main_warp.queue_routine_post([&worker, shared, &counter]() {
					IRIS_ASSERT(*shared == true);
					if (counter.fetch_add(1, std::memory_order_acquire) + 1 == 1000)
						worker.terminate();
				});
			});

			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		});
	}

	worker.finalize();
	main_warp.join([] {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		return true;
	});
}

