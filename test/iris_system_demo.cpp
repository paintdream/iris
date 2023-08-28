#include "../src/iris_system.h"
#include "../src/iris_common.inl"
using namespace iris;

using warp_t = iris_warp_t<iris_async_worker_t<>>;
using entity_t = uint32_t;
using entity_allocator = iris_entity_allocator_t<entity_t>;

struct iris_component_matrix_t {
	float values[4][4];
};

template <typename element_t>
using block_allocator_t = iris_block_allocator_t<element_t>;

struct int_interface : iris_pool_t<int_interface, iris_queue_list_t<int*>> {
	template <typename element_t>
	element_t acquire_element();

	template <typename element_t>
	void release_element(element_t&& element);
};

template <>
inline int* int_interface::acquire_element<int*>() {
	return new int();
}

template <>
inline void int_interface::release_element<int*>(int*&& object) {
	delete object;
}

int main(void) {
	iris::iris_quota_t<int, 2> quota({ 5, 3 });
	bool u1 = quota.acquire({ 1,2 });
	IRIS_ASSERT(u1);
	bool u2 = quota.acquire({ 2,3 });
	IRIS_ASSERT(!u2);
	{
		auto v = quota.guard({ 4, 4 });
		IRIS_ASSERT(!v);
		auto w = quota.guard({ 1, 1 });
		IRIS_ASSERT(w);
	}

	quota.release({ 1,2 });

	using queue = iris_queue_list_t<int, iris_default_block_allocator_t, iris_default_relaxed_shared_object_allocator_t>;
	queue::node_allocator_t::allocator_t alloc;
	queue q(alloc);
	q.push(1);
	q.pop();

	int_interface poll;
	std::vector<int*> allocated;
	for (size_t i = 0; i < 0x1234; i++) {
		allocated.emplace_back(poll.acquire());
	}

	for (size_t i = 0; i < 0x1234; i++) {
		poll.release(std::move(allocated[i]));
	}

	poll.clear();

	static constexpr size_t thread_count = 8;
	static constexpr size_t warp_count = 16;
	iris_async_worker_t<> worker(thread_count);
	worker.start();

	std::vector<warp_t> warps;
	warps.reserve(warp_count);
	for (size_t i = 0; i < warp_count; i++) {
		warps.emplace_back(worker);
	}

	iris_system_t<entity_t, block_allocator_t, iris_component_matrix_t, size_t> matrix_system;
	std::vector<entity_t> entities;
	iris_entity_allocator_t<entity_t> allocator;

	for (size_t k = 0; k < 128; k++) {
		entity_t entity = allocator.allocate();
		matrix_system.insert(entity, iris_component_matrix_t(), k);
		entities.emplace_back(entity);
	}

	for (size_t m = 0; m < 32; m++) {
		entity_t entity = entities[m] * 4;
		matrix_system.remove(entity);
		allocator.free(entity);
	}

	for (size_t m = 0; m < 64; m++) {
		matrix_system.insert(allocator.allocate(), iris_component_matrix_t(), m);
	}

	float sum = 0;
	for (auto&& item : matrix_system.component<iris_component_matrix_t>()) {
		sum += item.values[0][0];
	}

	allocator.reset();
	printf("Sum should be zero: %f\n", sum);
	
	// test for running example from thread pool
	std::atomic<size_t> counter;
	counter.store(0, std::memory_order_release);

	warps[0].queue_routine_external([&worker, &matrix_system, &counter]() {
		counter.fetch_add(matrix_system.size(), std::memory_order_release);
		matrix_system.for_each_parallel<iris_component_matrix_t, warp_t>([&worker, &counter](iris_component_matrix_t& matrix) {
			// initialize with identity matrix
			printf("[%d] Initialize matrix: %p\n", (int)worker.get_current_thread_index(), &matrix);

			matrix.values[0][0] = 1; matrix.values[0][1] = 0; matrix.values[0][2] = 0; matrix.values[0][3] = 0;
			matrix.values[1][0] = 0; matrix.values[1][1] = 1; matrix.values[1][2] = 0; matrix.values[1][3] = 0;
			matrix.values[2][0] = 0; matrix.values[2][1] = 0; matrix.values[2][2] = 1; matrix.values[2][3] = 0;
			matrix.values[3][0] = 0; matrix.values[3][1] = 0; matrix.values[3][2] = 0; matrix.values[3][3] = 1;

			if (counter.fetch_sub(1, std::memory_order_acquire) == 1) {
				worker.terminate();
			}
		}, 64);
	});

	iris_system_t<entity_t, block_allocator_t, float, size_t> other_system;
	for (size_t k = 0; k < 5; k++) {
		other_system.insert(iris::iris_verify_cast<entity_t>(k), 0.1f, k);
	}

	using sys_t = iris_systems_t<block_allocator_t, decltype(matrix_system), decltype(other_system)>;
	sys_t systems(matrix_system, other_system);
	size_t count = 0;
	for (auto&& v : systems.components<size_t>()) {
		count++;
	}

	size_t batch_count = 0;
	systems.components<size_t>().for_each([&](const size_t* v, size_t n) { batch_count += n; }); // much faster
	IRIS_ASSERT(count == batch_count);

	{
		auto& w = systems;
		for (auto&& v : w.components<float>()) {
			float& f = v;
			count++;
		}

		w.components<float, size_t>().for_each([&](float, size_t) {});
		
		for (auto&& v : w.components<float, size_t>()) {
			float& f = std::get<0>(v);
			f = 1.0f;
			size_t& s = std::get<1>(v);
			s = 2;
			count++;
		}

		w.components<float>().for_each([&count](float) {
			count++;
		});

		w.components<size_t, float>().for_each([&count](size_t, float) {
			count++;
		});

		w.components<size_t>().for_each_system([&count](sys_t::component_queue_t<size_t>& s) {
			count++;
		});
	}
	{
		const auto& w = systems;
		for (auto&& v : w.components<float>()) {
			const float& f = v;
			count++;
		}

		for (auto&& v : w.components<float, size_t>()) {
			const float& f = std::get<0>(v);
			const size_t& s = std::get<1>(v);
			count++;
		}

		w.components<float>().for_each([&count](float) {
			count++;
		});

		w.components<size_t, float>().for_each([&count](size_t, float) {
			count++;
		});

		w.components<size_t>().for_each_system([&count](const sys_t::component_queue_t<size_t>& s) {
			count++;
		});
	}

	worker.join();

	// finished!
	while (!warp_t::join(warps.begin(), warps.end())) {}
	return 0;
}

