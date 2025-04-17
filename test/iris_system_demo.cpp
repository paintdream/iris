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

	using queue = iris_queue_list_t<int, iris_default_block_allocator_t>;
	iris_default_block_allocator_t<int> alloc;
	queue q(alloc);
	q.push(1);
	q.pop();

	int_interface pool;
	std::vector<int*> allocated;
	for (size_t i = 0; i < 0x1234; i++) {
		allocated.emplace_back(pool.acquire());
	}

	for (size_t i = 0; i < 0x1234; i++) {
		pool.release(std::move(allocated[i]));
	}

	pool.clear();

	iris_system_t<entity_t, block_allocator_t, std::allocator, iris_component_matrix_t, uint8_t> matrix_system(alloc, std::allocator<entity_t>());
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
	
	matrix_system.iterate<iris_component_matrix_t, uint8_t>([](iris_component_matrix_t& matrix, uint8_t& v) {
		// initialize with identity matrix
		matrix.values[0][0] = 1; matrix.values[0][1] = 0; matrix.values[0][2] = 0; matrix.values[0][3] = 0;
		matrix.values[1][0] = 0; matrix.values[1][1] = 1; matrix.values[1][2] = 0; matrix.values[1][3] = 0;
		matrix.values[2][0] = 0; matrix.values[2][1] = 0; matrix.values[2][2] = 1; matrix.values[2][3] = 0;
		matrix.values[3][0] = 0; matrix.values[3][1] = 0; matrix.values[3][2] = 0; matrix.values[3][3] = 1;
	});

	matrix_system.iterate_batch<iris_component_matrix_t>(4, [](size_t count, iris_queue_list_t<iris_component_matrix_t, block_allocator_t>::iterator it) {
		while (count-- != 0) {
			it->values[3][3] = 2;
			it++;
		}
	});

	matrix_system.iterate<entity_t, iris_component_matrix_t>([](entity_t entity, iris_component_matrix_t& matrix) {
		// initialize with identity matrix
		IRIS_ASSERT(matrix.values[0][0] == 1);
	});

	matrix_system.filter<iris_component_matrix_t>(0, [](iris_component_matrix_t& matrix) {
		matrix.values[1][1] = 2;
	});

	int arr[2] = { 4, 6 };
	matrix_system.filter<iris_component_matrix_t>(&arr[0], &arr[0] + 2, [](iris_component_matrix_t& matrix) {
		matrix.values[1][1] = 2;
	});

	iris_system_t<entity_t, block_allocator_t, std::allocator, float, uint8_t> other_system;
	for (size_t k = 0; k < 5; k++) {
		other_system.insert(iris::iris_verify_cast<entity_t>(k), 0.1f, (uint8_t)k);
	}

	iris_systems_t<entity_t, block_allocator_t, std::allocator> systems(alloc, std::allocator<entity_t>());
	systems.attach(other_system);
	systems.attach(matrix_system);
	int counter = 0;
	systems.iterate<uint8_t>([&counter](uint8_t& i) {
		i = counter++;
	});

	systems.filter<uint8_t>(1, [](uint8_t& v) {
		printf("Entity 1 = %u\n", v);
	});

	entity_t qe[2] = { 1, 2 };
	systems.filter<uint8_t>(qe, qe + 2, [](uint8_t& v) {
		printf("Entity arr = %u\n", v);
	});

	systems.iterate_batch<iris_component_matrix_t>(4, [](size_t count, iris_queue_list_t<iris_component_matrix_t, block_allocator_t>::iterator it) {
		while (count-- != 0) {
			IRIS_ASSERT(it->values[3][3] == 2);
			it++;
		}
	});

	systems.detach(other_system);
	systems.iterate<entity_t, uint8_t>([&counter](entity_t e, uint8_t& i) {
		counter--;
	});
	IRIS_ASSERT(counter == 5);
	systems.attach(other_system);

	systems.iterate<float>([&counter](float& i) {
		counter--;
	});

	IRIS_ASSERT(counter == 0);
	
	iris_system_t<entity_t, block_allocator_t, std::allocator, uint8_t> re_system;
	systems.attach(re_system);
	re_system.insert(0, 1u);
	systems.remove(0);
	systems.compress();
	re_system.iterate<entity_t>([](entity_t entity) {
		IRIS_ASSERT(false); // already removed
	});

	re_system.clear();
	systems.clear();
	systems.detach(re_system);

	matrix_system.remove(&arr[0], &arr[0] + 2);
	matrix_system.filter<iris_component_matrix_t>(&arr[0], &arr[0] + 2, [](iris_component_matrix_t& matrix) {
		assert(false);
	});

	int union_set[10];
	iris_union_set_init(union_set, (int)0u, (int)(sizeof(union_set) / sizeof(union_set[0])));
	iris_union_set_join(union_set, 3u, 6u);
	iris_union_set_join(union_set, 6u, 9u);
	iris_union_set_join(union_set, 2u, 4u);
	iris_union_set_join(union_set, 8u, 4u);
	iris_union_set_join(union_set, 7u, 5u);
	iris_union_set_join(union_set, 1u, 5u);

	IRIS_ASSERT(iris_union_set_find(union_set, 1u) == iris_union_set_find(union_set, 7u));
	IRIS_ASSERT(iris_union_set_find(union_set, 4u) != iris_union_set_find(union_set, 6u));
	IRIS_ASSERT(iris_union_set_find(union_set, 2u) == iris_union_set_find(union_set, 8u));
	IRIS_ASSERT(iris_union_set_find(union_set, 5u) != iris_union_set_find(union_set, 9u));
	IRIS_ASSERT(iris_union_set_find(union_set, 0u) != iris_union_set_find(union_set, 3u));

	iris_cache_t<uint8_t> cache;
	iris_cache_allocator_t<double, uint8_t> cache_allocator(&cache);

	// todo: more tests
	std::vector<double, iris_cache_allocator_t<double, uint8_t>> vec(cache_allocator);
	vec.push_back(1234.0f);
	vec.resize(777);

	std::vector<double> dbl_vec;
	iris::iris_binary_insert(dbl_vec, 1234.0f);
	auto it = iris::iris_binary_find(dbl_vec.begin(), dbl_vec.end(), 1234.0f);
	IRIS_ASSERT(it != dbl_vec.end());
	iris::iris_binary_erase(dbl_vec, 1234.0f);

	std::vector<iris::iris_key_value_t<int, const char*>> str_vec;
	iris::iris_binary_insert(str_vec, iris::iris_make_key_value(1234, "asdf"));
	iris::iris_binary_insert(str_vec, iris::iris_make_key_value(2345, "defa"));
	auto it2 = iris::iris_binary_find(str_vec.begin(), str_vec.end(), 1234);
	IRIS_ASSERT(it2 != str_vec.end());
	IRIS_ASSERT(iris::iris_binary_find(str_vec.begin(), str_vec.end(), 1236) == str_vec.end());
	iris::iris_binary_erase(str_vec, 1234);
	iris::iris_binary_erase(str_vec, iris::iris_make_key_value(1234, ""));

	return 0;
}

