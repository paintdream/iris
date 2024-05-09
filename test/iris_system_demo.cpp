#include "../src/iris_system.h"
#include "../src/iris_common.inl"
using namespace iris;

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

	iris_system_t<entity_t, block_allocator_t, iris_component_matrix_t, uint8_t> matrix_system;
	std::vector<entity_t> entities;
	iris_entity_allocator_t<entity_t> allocator;

	for (size_t k = 0; k < 128; k++) {
		entity_t entity = allocator.allocate();
		matrix_system.insert(entity, iris_component_matrix_t(), (uint8_t)k);
		entities.emplace_back(entity);
	}

	for (size_t m = 0; m < 32; m++) {
		entity_t entity = entities[m] * 4;
		matrix_system.remove(entity);
		allocator.free(entity);
	}

	for (size_t m = 0; m < 64; m++) {
		matrix_system.insert(allocator.allocate(), iris_component_matrix_t(), (uint8_t)m);
	}

	float sum = 0;
	for (auto&& item : matrix_system.component<iris_component_matrix_t>()) {
		sum += item.values[0][0];
	}

	allocator.reset();
	printf("Sum should be zero: %f\n", sum);
	
	matrix_system.for_each<iris_component_matrix_t, uint8_t>([](iris_component_matrix_t& matrix, uint8_t& v) {
		// initialize with identity matrix
		matrix.values[0][0] = 1; matrix.values[0][1] = 0; matrix.values[0][2] = 0; matrix.values[0][3] = 0;
		matrix.values[1][0] = 0; matrix.values[1][1] = 1; matrix.values[1][2] = 0; matrix.values[1][3] = 0;
		matrix.values[2][0] = 0; matrix.values[2][1] = 0; matrix.values[2][2] = 1; matrix.values[2][3] = 0;
		matrix.values[3][0] = 0; matrix.values[3][1] = 0; matrix.values[3][2] = 0; matrix.values[3][3] = 1;
	});

	matrix_system.for_each_batch<iris_component_matrix_t>(4, [](size_t count, iris_queue_list_t<iris_component_matrix_t, block_allocator_t>::iterator it) {
		while (count-- != 0) {
			it->values[3][3] = 2;
			it++;
		}
	});

	matrix_system.for_each<entity_t, iris_component_matrix_t>([](entity_t entity, iris_component_matrix_t& matrix) {
		// initialize with identity matrix
		IRIS_ASSERT(matrix.values[0][0] == 1);
	});

	matrix_system.for_entity<iris_component_matrix_t>(0, [](iris_component_matrix_t& matrix) {
		matrix.values[1][1] = 2;
	});

	iris_system_t<entity_t, block_allocator_t, float, uint8_t> other_system;
	for (size_t k = 0; k < 5; k++) {
		other_system.insert(iris::iris_verify_cast<entity_t>(k), 0.1f, (uint8_t)k);
	}

	iris_systems_t<entity_t, block_allocator_t> systems;
	systems.attach(other_system);
	systems.attach(matrix_system);
	int counter = 0;
	systems.for_each<uint8_t>([&counter](uint8_t& i) {
		i = counter++;
	});

	systems.for_each_batch<iris_component_matrix_t>(4, [](size_t count, iris_queue_list_t<iris_component_matrix_t, block_allocator_t>::iterator it) {
		while (count-- != 0) {
			IRIS_ASSERT(it->values[3][3] == 2);
			it++;
		}
	});

	systems.detach(other_system);
	systems.for_each<entity_t, uint8_t>([&counter](entity_t e, uint8_t& i) {
		counter--;
	});
	IRIS_ASSERT(counter == 5);
	systems.attach(other_system);

	systems.for_each<float>([&counter](float& i) {
		counter--;
	});

	IRIS_ASSERT(counter == 0);
	
	iris_system_t<entity_t, block_allocator_t, uint8_t> re_system;
	systems.attach(re_system);
	re_system.insert(0, 1u);
	systems.remove(0);
	re_system.for_each<entity_t>([](entity_t entity) {
		IRIS_ASSERT(false); // already removed
	});

	re_system.clear();
	systems.clear();
	systems.detach(re_system);

	return 0;
}

