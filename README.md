# Iris
Iris is an extensible asynchronous header-only framework written in pure modern C++, including a M:N task scheduler (with coroutine support for C++ 20 optionally) and an advanced DAG-based task dispatcher.

## Build

Iris is header-only. The only thing you need to do is to include the corresponding header files.

Most of Iris classes works with C++ 11 compatible compilers, except some optional features: 

* Lua Binding support requires C++ 17 if-constexpr feature. (Visual Studio 2017+, GCC 7+, Clang 3.9+)
* Coroutine support for thread pool scheduler requires C++ 20 standard coroutine feature. (Visual Studio 2019+, GCC 11+, Clang 14+)

All examples could be built by [CMake build system](https://cmake.org/), see CMakeLists.txt for more details.

## License

Iris is distributed under MIT License.

## Concepts

Iris provides a simple M:N task scheduler called **Warp System** which is inspired by [Boost](https://www.boost.org/) Strand System. Let's start illustrating it from basic concepts.

#### Task

A task is the **logical** executing unit in concept of application development . Usually it is represented by a function pointer. 

#### Thread

A thread is a **native** execution unit provided by operating system. **Tasks must be run in threads**. Different threads are considered to be possibly running at the same time.

**Multi-threading**, which aims to run several threads within an program, is an effective approach to making full use of CPUs in many-core system. Usually it's very hard to code and debug. Therefore, there are many data structures, programming patterns and frameworks to simplify coding process and make it easier for developers. This project is one of them.

#### Thread Pool

Threads are heavy. It is not efficient to run every task by invoking a brand-new thread. Thread pool is a type of multi-threading framework that could make it more efficient. Thread pool maintains a set of threads called "Worker Thread" **reused** for running tasks. When a new task are required to be run, the thread pool could schedule it to a proper worker thread if there were idled one, or queue it until any worker became idle.

#### Warp

Some tasks are going to read/write at the same objects, or visiting the same thread-unsafe interfaces, indicating that they are not able to run at the same time. See [RACE Condition](https://en.wikipedia.org/wiki/Race_condition) for details. Here we just call them **conflicting** tasks.

To make our programs run correctly, we must establish some techniques prevent unexpected conflicts. Here introduce a new concept: **Warp**.

A warp is a logical container of series conflicting tasks. Tasks belong to the **same warp** are granted to be **mutexed** automatically and thus **neither two of them** can be run at the same time, avoiding race-conditions prospectively. This feature is called **warp restriction**. To make coding easier, we could bind all tasks about a specified object into a specific warp. In this case, we call that this object is totally bound to a warp context.

Besides, tasks among **different** warps **can** be run at the same time respectively.

#### Warp System

The Warp System is a bridge between **warps** and **thread pool**. That is, programmers commit tasks labeled by warp to the system, then the latter schedule them to a thread pool. With some magic techniques applied internally, we finally construct a conflict-free task flow.

The thread count **M** of Warp System is **fixed** when it starts. But the warp count **N** can be dynamically adjusted by programmers freely. So the warp system is a type of flexible M:N task mapping system.

## Quick Start

Let's start with simple programs in [iris_dispatcher_demo.cpp](iris_dispatcher_demo.cpp) . 

#### Basic Example: simple explosion

The Warp System runs on a thread pool, and the first thing is to create it. There is a built-in thread pool written in C++ 11 std::thread in [iris_dispatcher.h](iris_dispatcher.h), you can replace it with your own platform-correlated implementation.

```C++
static const size_t thread_count = 4;
iris_async_worker_t<> worker(thread_count);
```

Then we initialize the warps. There is no "warp system class". Each warp is **individual**, just create a vector of them. We call them warp 0, warp 1, etc. 

Different from boost strands, the tasks in a warp are **NOT** ordered by default, which means the **final execution order** is not the same order of committing. You can still enable ordering as you like anyway (see declaration of "strand_t" as following code), which is not recommended because ordering may be slightly inefficient than default setting.

```C++
static const size_t warp_count = 8;
using warp_t = iris_warp_t<iris_async_worker_t<>>;
using strand_t = iris_warp_t<iris_async_worker_t<>, true>; // behaves like a strand

std::vector<warp_t> warps;
warps.reserve(warp_count);
for (size_t i = 0; i < warp_count; i++) {
	warps.emplace_back(worker); // calls iris_warp_t::iris_warp_t(iris_async_worker_t<>&)
}
```

Then we can schedule a task into the warp you want.

If you are already in one thread of thread pool, just use **queue_routine**. Otherwise **queue_routine_external** is required (a typical case is to schedule task from main thread).

```C++
warps[0].queue_routine_external([]() {/* operations on warps[0] */}); // outter of thread pool
warps[0].queue_routine([]() {/* operations on warps[0] */});          // inner of thread pool
```

That's all you need to do. Suppose you wrote the following two lines:

```C++
warps[0].queue_routine([]() { /* do operation A */});
warps[0].queue_routine([]() { /* do operation B */});
```

Then according to warp restrictions, operation A and operation B are **never executed at the same time**, since they are in the **same** warp.

For another case,

```C++
warps[0].queue_routine([]() { /* do operation C */});
warps[1].queue_routine([]() { /* do operation D */});
```

According to warp restrictions, operation A and operation B **could be executed at the same time**, since they are in **different warps**.

Now let's back to the "explosion" example, we code a function called "explosion", which randomly folks multiple recursions of writing operations on a integer array described here:

```C++
static int32_t warp_data[warp_count] = { 0 };
```

The restriction is that warp 0 can only write warp_data[0], warp 1 can only write warp_data[1]:

```C++
explosion = [&warps, &explosion, &worker]() {
	if (worker.is_terminated())
		return;

	warp_t& current_warp = *warp_t::get_current_warp();
	size_t warp_index = &current_warp - &warps[0];
	warp_data[warp_index]++;

	// simulate working
	std::this_thread::sleep_for(std::chrono::milliseconds(rand() % 40));
	warp_data[warp_index]++;

	if (rand() % terminate_factor == 0) {
		// randomly terminates
		worker.terminate();
	}

	warp_data[warp_index]++;
	// randomly dispatch to warp
	for (size_t i = 0; i < split_count; i++) {
		warps[rand() % warp_count].queue_routine(std::function<void()>(explosion));
	}

	warp_data[warp_index] -= 3;
};
```

Though there is no locks or atomics on operating warp_data, we can still assert that the final value of each warp_data must be 0. The execution of the same warp never overlap in time-line.

#### Advanced Example: garbage collection

There is a function named garbage_collection, which simulates a multi-threaded mark-sweep [garbage collection](http://en.wikipedia.org/wiki/Garbage_collection_(computer_science) ) process. 

Garbage collection is a technique for collecting unreferenced objects and deleting them. Mark-sweep is a basic approach for garbage collection. It contains three steps:

1. Scanning all objects and mark them **unvisited**.
2. Traverse from root objects through reference relationships, mark all objects that can be directly or indirectly referenced to **visited**.
3. Rescanning all objects, delete the objects with **unvisited** mark. Thus all objects that not linked with root objects (i.e. garbage) are deleted.

Now suppose we got the definition of basic object node as followed:

```C++
struct node_t {
	size_t warp_index = 0;
	size_t visit_count = 0; // we do not use std::atomic<> here.
	std::vector<size_t> references;
};
```

To apply garbage collection, we need to record every **references** from the current node, and traverse them from root object as collecting. We use **visit_count** to record whether the current node is **visited**.

If you are experienced in multi-threaded programming, you may figure out that **visit_count** should be of type std::atomic<size_t> because there may be several threads performing modification during collecting progress.

But we have decided to make things different.

We are splitting the node visiting operations into multiple warps (recorded by member **warp_index**). For example, node 1-10 are grouped into warp 0, node 11-20 are grouped into warp 1, or just randomly assigned. Any task operations on the nodes in the same warp will be protected by warp system. As a result, the variable **visit_count** is granted to **never** operated by multiple-threads and no atomic or locks are required.

In order to obey the warp restriction, all we need to do is to invoke a task with related node's warp when we are planning to do something on it:

```C++
warps[target_node.warp_index].queue_routine([]() {
	// operations on target_node
});
```

Since we have visited a new node, all **references** should be added into next collection process. To preserve the warp restriction, we schedule them into their own warps: (see the line commented with <------)

```C++
collector = [&warps, &collector, &worker, &graph, &collecting_count](size_t node_index) {
	warp_t& current_warp = *warp_t::get_current_warp();
	size_t warp_index = &current_warp - &warps[0];

	node_t& node = graph.nodes[node_index];
	assert(node.warp_index == warp_index);

	if (node.visit_count == 0) {
		node.visit_count++; // SAFE: only one thread can visit it

		for (size_t i = 0; i < node.references.size(); i++) {
			size_t next_node_index = node.references[i];
			size_t next_node_warp = graph.nodes[next_node_index].warp_index;
			collecting_count.fetch_add(1, std::memory_order_acquire);
			warps[next_node_warp].queue_routine(std::bind(collector, next_node_index)); // <------
		}
	}

	if (collecting_count.fetch_sub(1, std::memory_order_release) == 1) {
		// all work finished.
		// ...
	}
};
```

That's all, there would be **no** explicit locks and atomics. All dangerous multi-threaded works are done by Warp System. See the full source code of garbage_collection for more details.

#### Discussion

Now let's get back to the beginning, what's the meaning of warps? What if we just use atomics or locks?

The answer contains three aspects: 

1. Convenient: The only thing you must remember is the rule that **always schedule task according to warp**. There is no lock-order requirement, dead-locking, busy-waiting, memory order problem and atomic myths.
2. High performance: If we abuse locks and atomics everywhere, for example, allocating separate locks on each object, do locks or atomic operations as long as we need to visit objects, then the program will stuck on bus-locking, kernel-switching and thread-switching, which lead to low performance issues. The warp concept wraps a series of operations or a mount of objects into a logical "scheduling package", reducing switching cost and busy-wait cost, making them more friendly for multi-thread systems.
3. Flexible: you could easily adjust the object/task warping rules as you like. For example, allocating more warps and splitting objects with smaller granularity if you have more CPUs.  The system allows programmers to transport a object or a group of tasks from one warp to another dynamically, if they are working on some dynamic overload balancing features. 



## Step further

### In-Warp Parallel

In common case, there is only one thread running in a warp context. But what if we want to break the rule temporarily by local code and do some parallelized operations with warp restriction holding for other code? I know it's unsafe, but I just want to do it.

Open the [iris_dispatcher_demo.cpp](iris_dispatcher_demo.cpp) and you could find a piece of code in function "simple_explosion":

```C++
if (rand() % parallel_factor == 0) {
	// read-write lock example: multiple reading blocks writing
	std::shared_ptr<std::atomic<int32_t>> shared_value = std::make_shared<std::atomic<int32_t>>(-0x7fffffff);
	for (size_t i = 0; i < parallel_count; i++) {
		current_warp.queue_routine_parallel([shared_value, warp_index]() {
			// only read operations
			std::this_thread::sleep_for(std::chrono::milliseconds(rand() % 40));
			int32_t v = shared_value->exchange(warp_data[warp_index], std::memory_order_release);
			assert(v == warp_data[warp_index] || v == -0x7fffffff);
		}, 1);
	}
}
```

The function **queue_routine_parallel** invokes a special parallelized task on current_warp, which can be run at the same time. As one parallelized task running, other normal tasks on current_warp remains to be **blocked**. After all parallelized task finishes, the normal tasks then could to be scheduled.

**Parallelized tasks to normal tasks is what read locks to write locks**. It's an advanced feature and you must be careful when use them.

### Coroutines

In C++ 20, we can use coroutines to simplify asynchronous program development.

Warp system supports coroutines integration, you could find an example at [iris_coroutine_demo.cpp](iris_coroutine_demo.cpp):

To start with a coroutine, just write a function with return value type "iris_coroutine_t":

```C++
iris_coroutine_t<return_type> example(warp_t::async_worker_t& async_worker, warp_t* warp, int value) {}
```

In this coroutine function, you could await call **iris_switch** to switch to another warp context:

```C++
if (warp != nullptr) {
	warp_t* current = co_await iris_switch(warp);
	printf("Switch to warp %p\n", warp);
	co_await iris_switch((warp_t*)nullptr);
	printf("Detached\n");
	co_await iris_switch(warp);
	printf("Attached\n");
	co_await iris_switch(current);
	assert(current == warp_t::get_current_warp());
}
```

co_wait iris_switch returns previous warp. Notice that we can switch to a nullptr warp, which means that we are planning to detach from current warp. Switching from nullptr warp to a valid warp is also allowed respectively.

And we can create and wait a asynchronized task on target warp:

```C++
// Step 1: test single await
co_await iris_awaitable(warp, []() {});
```

It is equivalent to switching to warp and switching back. But **iris_awaitable** allows multiple awaiting:

```C++
iris_awaitable_multiple_t<iris_warp_t, std::function<void()>> multiple(async_worker, iris_awaitable(warp, std::move(v1)));
multiple += iris_awaitable(warp, std::move(v2));
multiple += iris_awaitable(warp, std::move(v3));
co_await multiple;
```

or just union multiple tasks in one line:

```C++
// Step 3: test multiple await by join-like construction
std::function<int()> v4 = [value]() { return value + 4; };
std::function<int()> v5 = [value]() { return value + 5; };
std::vector<int> rets = co_await iris_awaitable_union(async_worker, iris_awaitable(warp, std::move(v4)), iris_awaitable(warp, std::move(v5)));
```

It's much more clear than use callbacks.

iris_coroutine_t<return_type> is not only a coroutine but also an awaitable object. You could also co_await it to chain your coroutine pipeline.

### DAG-based Task Dispatcher

DAG-based Task Dispatcher, also well-known as Task Graph, is widely used task dispatching techniques for tasks with partial order dependency.

We also provide a DAG-based Task Dispatcher called iris_dispatcher_t (see function "graph_dispatch" at [iris_dispatcher_demo ](iris_dispatcher_demo.cpp) ):

You can create a dispatcher with:

```C++
iris_dispatcher_t<warp_t> dispatcher(worker, [](iris_dispatcher_t<warp_t>&) { /* on all task finish */});
```

The second parameter is an optional function, called after all tasks in dispatcher graph finished.

To add a task to dispatcher, call **allocate**. 

```C++
auto d = dispatcher.allocate(&warps[2], []() { std::cout << "Warp 2 task [4]" << std::endl; });
auto a = dispatcher.allocate(&warps[0], []() { std::cout << "Warp 0 task [1]" << std::endl; });
auto b = dispatcher.allocate(&warps[1], []() { std::cout << "Warp 1 task [2]" << std::endl; });
```

Notice that there is a return value with internal type routine_t*. You could call **order** function to order them later. 

```C++
dispatcher.order(a, b);
// dispatcher.order(b, a); // will trigger validate assertion

auto c = dispatcher.allocate(nullptr, []() { std::cout << "Warp nil task [3]" << std::endl; });
dispatcher.order(b, c);
// dispatcher.order(c, a); // will trigger validate assertion
dispatcher.order(b, d);
```

Then call **dispatch** to run them. 

```C++
dispatcher.dispatch(a);
dispatcher.dispatch(b);
dispatcher.dispatch(c);
dispatcher.dispatch(d);
```

To dispatch more flexible, you can **defer/dispatch** a task dynamically. Notice that **defer** must be called during dispatcher running and **BEFORE** target task actually runs.

```c++
auto b = dispatcher.allocate(&warps[1], [&dispatcher, d]() {
	dispatcher.defer(d);
	std::cout << "Warp 1 task [2]" << std::endl;
	dispatcher.dispatch(d);
});
```

### Polling from external thread

It is a common case that a thread has to be blocked to wait for some signals arrive. For example, suppose you are spinning to wait an atomic variable to be expected value (spin lock, for example), and there are nothing to be done but to spin. In this case, we can try to "borrow" some tasks from thread pool and executing them if our atomic variable is not ready yet.

```C++
while (some_variable.load(std::memory_order_acquire) != expected_value) {
	// delay at most 20ms or poll tasks with priority 0 if possible 
	worker.poll_delay(0, 20);
}
```

