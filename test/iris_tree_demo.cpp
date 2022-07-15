#include "../src/iris_tree.h"
#include "../src/iris_common.inl"
#include <utility>
#include <vector>
#include <ctime>
#include <cstdio>
using namespace iris;

struct float3 {
	using type = float;
	static constexpr size_t size = 3;

	explicit float3(float xx = 0.0f, float yy = 0.0f, float zz = 0.0f) noexcept : x(xx), y(yy), z(zz) {}
	float x, y, z;
};

using box = std::pair<float3, float3>;

static bool overlap(const box& lhs, const box& rhs) noexcept {
	if (rhs.second.x < lhs.first.x || lhs.second.x < rhs.first.x)
		return false;

	if (rhs.second.y < lhs.first.y || lhs.second.y < rhs.first.y)
		return false;

	if (rhs.second.z < lhs.first.z || lhs.second.z < rhs.first.z)
		return false;

	return true;
}

static box build_box(const float3& first, const float3& second) noexcept {
	box b;
	b.first.x = std::min(first.x, second.x);
	b.first.y = std::min(first.y, second.y);
	b.first.z = std::min(first.z, second.z);
	b.second.x = std::max(first.x, second.x);
	b.second.y = std::max(first.y, second.y);
	b.second.z = std::max(first.z, second.z);

	return b;
}

struct sample_tree : iris_tree_t<box> {
	using base = iris_tree_t<box>;
	sample_tree() noexcept : iris_tree_t<box>(box(float3(0, 0, 0), float3(0, 0, 0)), 0) {}
	sample_tree(const box& b, uint8_t k) noexcept : iris_tree_t<box>(b, k) {}
};

box build_box_randomly() noexcept {
	return build_box(float3((float)rand(), (float)rand(), (float)rand()), float3((float)rand(), (float)rand(), (float)rand()));
}

struct queryer {
	size_t count;
	box bounding;
	bool operator () (const sample_tree::base& tree) noexcept {
		if (overlap(bounding, tree.get_key()))
			count++;
		return true;
	}
};

size_t fast_query(sample_tree*& root, const box& box) noexcept {
	queryer q;
	q.bounding = box;
	q.count = 0;
	assert(root->get_parent() == nullptr);
	root->query<true>(box, q);

	return q.count;
}

struct random_select {
	bool operator () (sample_tree::base* left, sample_tree::base* right) noexcept {
		return rand() & 1;
	}
};

size_t linear_search(const sample_tree* root, std::vector<sample_tree>& nodes, const box& box) noexcept {
	size_t count = 0;
	for (size_t i = 0; i < nodes.size(); i++) {
		if (nodes[i].get_parent() != nullptr || &nodes[i] == root)
			count += overlap(nodes[i].get_key(), box);
	}

	return count;
}

int main(void) {
	static constexpr size_t length = 10;
	std::vector<sample_tree> nodes(length * 4096);
	srand(0);

	// initialize data
	for (size_t i = 0; i < nodes.size(); i++) {
		nodes[i] = sample_tree(build_box_randomly(), rand() % 6);
	}

	// link data
	// select root
	sample_tree* root = &nodes[rand() % nodes.size()];

	for (size_t j = 0; j < nodes.size(); j++) {
		if (root != &nodes[j]) {
			root->attach(&nodes[j]);
		}
	}

	// random detach data
	random_select random_selector;
	for (size_t k = 0; k < nodes.size() / 8; k++) {
		size_t index = rand() % nodes.size();
		sample_tree* to_detach = &nodes[index];
		sample_tree* new_root = static_cast<sample_tree*>(to_detach->detach(random_selector));
		if (new_root != nullptr) {
			root = new_root;
			if (k & 1) {
				root->attach(to_detach);
			}
		}
	}

	for (size_t j = 0; j < 2; j++) {
		// random select
		for (size_t n = 0; n < size_t(10 * length); n++) {
			box b = build_box_randomly();
			size_t search_count = linear_search(root, nodes, b);
			size_t query_count = fast_query(root, b);

			if (query_count != search_count) {
				printf("unmatched result, %d got, %d expected.\n", (int)query_count, (int)search_count);
				return -1;
			}
		}

		root->query<true>(build_box_randomly(), [](const sample_tree::base& tree) { return true; }, [](const box& key) { return true; });
		root = static_cast<sample_tree*>(root->optimize());
	}

	return 0;
}

