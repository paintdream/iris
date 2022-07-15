#include "../src/iris_buffer.h"
#include "../src/iris_common.inl"
#include <vector>
using namespace iris;

int main(void) {
	iris_bytes_t bytes;
	iris_buffer_t<uint8_t> buffer;
	iris_cache_t<uint8_t> cache;
	iris_cache_allocator_t<double, uint8_t> allocator(&cache);

	char var[] = "12345";
	bytes = iris_bytes_t::make_view(reinterpret_cast<const uint8_t*>(var), 5);
	bytes.test(15);
	bytes.set(16); // breaks const rule ...
	buffer = iris_bytes_t::make_view(reinterpret_cast<const uint8_t*>("1234568901234567890"), 20);
	cache.link(bytes, buffer);
	iris_bytes_t combined;
	combined.resize(bytes.get_view_size());
	combined.copy(0, bytes);

	// todo: more tests
	std::vector<double, iris_cache_allocator_t<double, uint8_t>> vec(allocator);
	vec.push_back(1234.0f);
	vec.resize(777);

	std::vector<double> dbl_vec;
	iris::iris_binary_insert(dbl_vec, 1234.0f);
	auto it = iris::iris_binary_find(dbl_vec.begin(), dbl_vec.end(), 1234.0f);
	assert(it != dbl_vec.end());
	iris::iris_binary_erase(dbl_vec, 1234.0f);

	std::vector<iris::iris_key_value_t<int, const char*>> str_vec;
	iris::iris_binary_insert(str_vec, iris::iris_make_key_value(1234, "asdf"));
	iris::iris_binary_insert(str_vec, iris::iris_make_key_value(2345, "defa"));
	auto it2 = iris::iris_binary_find(str_vec.begin(), str_vec.end(), 1234);
	assert(it2 != str_vec.end());
	assert(iris::iris_binary_find(str_vec.begin(), str_vec.end(), 1236) == str_vec.end());
	iris::iris_binary_erase(str_vec, 1234);
	iris::iris_binary_erase(str_vec, iris::iris_make_key_value(1234, ""));

	return 0;
}

