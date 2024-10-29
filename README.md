# Non-blocking and Lock-free Queues with Hazard Pointer

C implementation of four types of non-blocking and lock-free queues with multiple readers and writers:

* [SimpleQueue](#simplequeue-a-singly-linked-list-based-queue-with-two-mutexes) (non-blocking, mutex-based)
* [RingsQueue](#ringsqueue-a-queue-based-on-a-singly-linked-list-of-cyclic-buffers) (non-blocking, mutex-based with circular buffer)
* [LLQueue](#llqueue-a-lock-free-queue-based-on-a-singly-linked-list) (lock-free, with compare-exchange)  
* [BLQueue](#blqueue-a-lock-free-queue-based-on-a-singly-linked-list-of-bounded-buffers) (lock-free, bounded buffer with compare-exchange)

As well as:
* [Hazard Pointer](#hazard-pointer) (used in lock-free queues)

Every queue supports:
* `<queue>* <queue>_new(void)` - allocates (malloc) and initializes a new queue.
* `void < queue>_delete(< queue>* queue)` - frees any memory allocated by queue methods.
* `void <queue>_push(<queue>* queue, Value value)` - adds a value to the end of the queue.
* `Value <queue>_pop(<queue>* queue)` - retrieves the value from the beginning of the queue or returns EMPTY_VALUE if the queue is empty.
* `bool <queue>_is_empty(<queue>* queue)` - checks if the queue is empty.


## Full task description

The task is to make several implementations of a non-blocking queue with multiple readers and writers: SimpleQueue, RingsQueue, LLQueue, BLQueue. Two implementations rely on the use of ordinary mutexes, and two more on the use of atomic operations, including the key compare_exchange.

In each case, the implementation consists of a `<queue>` structure and methods:
* `<queue>* <queue>_new(void)` - allocates (malloc) and initializes a new queue.
* `void < queue>_delete(< queue>* queue)` - frees any memory allocated by queue methods.
* `void <queue>_push(<queue>* queue, Value value)` - adds a value to the end of the queue.
* `Value <queue>_pop(<queue>* queue)` - retrieves the value from the beginning of the queue or returns EMPTY_VALUE if the queue is empty.
* `bool <queue>_is_empty(<queue>* queue)` - checks if the queue is empty.

(For example, the first implementation should define the `SimpleQueue` structure and `SimpleQueue* SimpleQueue_new(void)` methods, etc.).

The values in the queue have a `Value` type equal to `int64_t` (for convenient testing, normally we would rather keep `void*` there).\
Queue users do not use `EMPTY_VALUE=0` or `TAKEN_VALUE=-1` (they can be used as special).\
Queue users guarantee that they execute new/delete exactly once, respectively before/after all push/pop/is_empty operations from all threads.

All implementations should safely behave as if push, pop, is_empty operations are indivisible (looking at the values returned to queue users).\
Queue usage must not lead to memory leaks or deadlocks.\
Implementations do not have to fully guarantee fairness and no starvation of each individual thread. It is allowed, for example, for one thread performing a push to be starved, as long as other threads manage to complete their pushes.


Implementations of lock-free queues (LLQueue and BLQueue) should guarantee that in each parallel operation execution,\
at least one push operation will complete in a finite number of steps (bounded by a constant that depends on the number of threads and buffer sizes in BLQueue),\
at least one pop operation will terminate in a finite number of steps, and\
at least one is_empty operation will terminate in a finite number of steps if such operations have started and have not been stopped.

This guarantee must occur even if other threads are randomly suspended for a longer time (e.g., by preemption).

Solutions will also be checked for performance (but deviations of 10% from the benchmark solution will not affect the evaluation).

### SimpleQueue: a singly linked list-based queue with two mutexes

This is one of the simpler queue implementations. A separate mutex for producers and for consumers enables better parallelization of operations.

The SimpleQueue structure consists of:
* a singly linked list of nodes, where each node contains: 
    * an atomic `next` pointer to the next node in the list,
    * a value of type `Value`;
* a `head` pointer to the first node in the list, along with a mutex to control access to it;
* a `tail` pointer to the last node in the list, along with a mutex to control access to it.

### RingsQueue: a queue based on a singly linked list of cyclic buffers

This is a combination of SimpleQueue and a queue implemented on a cyclic buffer, combining the unlimited size of the former with the efficiency of the latter (singly linked lists are relatively slow due to continuous memory allocations).

The RingsQueue structure consists of:
* a singly linked list of nodes, where each node contains: 
    * an atomic `next` pointer to the next node in the list,
    * a cyclic buffer in the form of a `RING_SIZE` table of values of type Value,
    * an atomic `push_idx` counter of the pushes performed on this node,
    * an atomic `pop_idx` counter of pops performed on this node.
* `head` pointer to the first node in the list;
* `tail` pointer to the last node in the list (`head` and `tail` can point to the same node);
* `pop_mtx` mutex to lock for the duration of the entire pop operation;
* `push_mtx` mutex to lock for the duration of the entire push operation.

It can be assumed that there will be at most `2^60` total push and pop operations. \
The `RING_SIZE` constant is defined in `RingsQueue.h` and is 1024, but can be changed in tests to a lower power of two greater than 2. \
Push and pop/is_empty operations should be able to run in parallel, i.e., suspending the thread performing push should not block the thread performing pop/is_empty, and suspending the thread performing pop/is_empty should not block the thread performing push.

Push should work as follows:

* if the node pointed to by `tail` is not full, it adds a new value to its cyclic buffer, incrementing `push_idx`.
* if it is full, it creates a new node and updates the corresponding pointers.

Pop should work as follows:

* if the node pointed to by `head` is not empty, it returns the value from its cyclic buffer, increasing `pop_idx` in the process.
* if the node is empty and has no successor: it returns `EMPTY_VALUE`.
* if the node is empty and has a successor: updates `head` to the next node and returns the value from its cyclic buffer.

Making the above descriptions more precise is part of the task, in particular, you should think about the order in which the checks are performed.

### LLQueue: a lock-free queue based on a singly linked list

This is one of the simplest implementations of a lock-free queue.

The LLQueue structure consists of:
* a singly linked list of nodes, where each node contains: 
    * an atomic `next` pointer to the next node in the list,
    * a value of type Value, equal to EMPTY_VALUE if the value from the node has already been retrieved;
* an atomic `head` pointer to the first node in the list;
* an atomic `tail` pointer to the last node in the list;
* a HazardPointer structure (see below).

Push should run in a loop attempting the following steps:
1. Read the pointer to the last node in the queue.
2. Replace its successor with a new node containing our element.\
3a. If successful, we update the pointer to the last node in the queue to our new node and exit the function.
3b. If it failed (another thread managed to extend the list), we try everything all over again, making sure the last node was updated.

Pop should run in a loop attempting the following steps:
1. Read the pointer to the first node in the queue.
2. Read the value from that node and replace it with EMPTY_VALUE.\
3a. If the value read was other than EMPTY_VALUE, we update the pointer to the first node (if needed) and return the result.\
3b. If the value read was EMPTY_VALUE, we check if the queue is empty.\
If so, we return EMPTY_VALUE, and if not, we try everything over again, making sure that the first node was updated.

Refinement of the above algorithms is part of the task (while the given order of numbered points is correct).\
The entire implementation of the `LLQueue_is_empty` operation is part of the task.


### BLQueue: a lock-free queue based on a singly linked list of bounded buffers

This is one of the simpler yet very efficient queue implementations. The idea behind this queue is to combine a singly linked list solution with a solution where the queue is a simple array with atomic indices for inserting and retrieving elements (but the number of operations would be limited by the length of the array). We combine the advantages of both by making a list of arrays; we only need to go to a new list node when the array is full. However, the array here is not a cyclic buffer; each field in it is filled at most once (the variant with cyclic buffers would be much more difficult).

The BLQueue structure consists of:
* a singly linked list of nodes, where each node contains: 
    * an atomic `next` pointer to the next node in the list,
    * a buffer of `BUFFER_SIZE` atomic values of type Value,
    * an atomic `push_idx` index of the next place in the buffer to be filled by the push (grows with each push attempt),
    * an atomic `pop_idx` index of the next space in the buffer to be emptied by pop (grows with each pop attempt);
* an atomic `head` pointer to the first node in the list;
* an atomic `tail` pointer to the last node in the list;
* HazardPointer structure (see below).

The queue initially contains one node with an empty buffer. The elements of the buffer initially have the value EMPTY_VALUE.\
Pop operations will convert fetched or empty values into a TAKEN_VALUE value (we allow that pop will sometimes waste array elements in this way).\
The `BUFFER_SIZE` constant is defined in `BLQueue.h` and is 1024, but can be changed in tests to a lower power of two greater than 2.

Push should run in a loop attempting the following steps:\
1\. Read a pointer to the last node in the queue.\
2\. Retrieve and increment from that node the index of the space in the buffer to be filled by the push (no one else will try to push into that space anymore).\
3a. If the index is smaller than the size of the buffer, we try to insert the element into that buffer space.
* If another thread has managed to change that space (another thread doing the pop may have changed it to TAKEN_VALUE), we try everything all over again.
* And if we succeeded in inserting the element, we exit the function.\

3b. And if the index is larger-equal to the size of the buffer, it means that the buffer is full and we will have to create or move to the next node. To do this, we first check if the next node has already been created.\
4a. If so, we make sure that the pointer to the last node in the queue has changed and we try all over again.\
4b. If not, we create a new node, right away with our one element in the buffer. We try to insert a pointer to the new node as a successor. 
* If it failed (another thread has already managed to extend the list), we delete our node and try everything all over again.
* If successful, we update the pointer to the last node in the queue to our new node.


Pop should run in a loop attempting the following steps:\
1\. Read the pointer to the first node in the queue.\
2\. Retrieve and increment from that node the index of the space in the buffer to be read by pop (no one else will try to pop from this space anymore).\
3a. If the index is smaller than the size of the buffer, we read the element from this buffer space and substitute TAKEN_VALUE.\ 
* If we retrieved EMPTY_VALUE, we try all over again.
* If we retrieved another element, we exit the function.

3b. If, on the other hand, the index is larger-equal to the size of the buffer, it means that the buffer is completely emptied and we will have to move on to the next node. To do this, we first check whether the next node has already been created.\
4a. If not, the queue is empty, we exit the function.\
4b. If yes, we make sure that the pointer to the first node in the queue has changed and we try all over again.

Refinement of the above algotithms is part of the task (while the given order of numbered points is correct).\
The whole implementation of the `BLQueue_is_empty` operation is part of the task.

### Hazard pointer
Hazard Pointer is a technique for dealing with the problem of safe memory freeing in data structures shared by multiple threads and the problem of ABA. The idea is that each thread can reserve one address per node (one per queue instance) that it needs to protect from deletion (or ABA substitution) for push/pop/is_empty operations. Wanting to release a node (`free()`), a thread instead adds its address to its set of “nodes to be freed later” and then periodically reviews this set, freeing addresses that are not reserved.

The HazardPointer structure should be used in implementations of push/pop/is_empty operations of LLQueue and BLQueue queues, protecting the address from which we will read values (in the described implementations, there is at most one such address per thread at any given time) and removing the protection before exiting the operation. Instead of freeing the node, it should be retired if another thread can still access it.

The HazardPointer structure should consist of:
* an array containing an atomic pointer for each thread - containing the "reserved" node address by that thread;
* an array of sets of pointers for each thread - "retired" addresses, i.e. "to be freed" later;

The implementation consists of the following methods:
* `void HazardPointer_register(int thread_id, int num_threads)` - registers the thread with the identifier `thread_id`.
* `void HazardPointer_initialize(HazardPointer* hp)` - initializes the (already allocated) structure: reserved addresses are all NULL.
* `void HazardPointer_finalize(HazardPointer* hp)` - clears any reservations, frees the memory allocated by the structure's methods, and frees all addresses from the retired array (does not free the HazardPointer structure itself).
* `void* HazardPointer_protect(HazardPointer* hp, const AtomicPtr* atom)` - writes to the array of reserved addresses the address read from `atom` at index `thread_id` and returns it (overwrites the existing reservation, if there was one for `thread_id`).
* `void HazardPointer_clear(HazardPointer* hp)` - removes the reservation, i.e. replaces the address at the `thread_id` index with NULL.
* `void HazardPointer_retire(HazardPointer* hp, void* ptr)` - adds `ptr` to the set of retired addresses, the release of which is the responsibility of the `thread_id`. Then, if the size of the set of retired exceeds the threshold defined by the `RETIRED_THRESHOLD` constant (equal to, for example, `MAX_THREADS`), it looks through all addresses in its set and releases (`free()`) those that are not reserved by any thread (removing them from the set as well).

Users of queues using HazardPointer guarantee that each thread calls `HazardPointer_register` with a unique `thread_id` (an integer in the range `[0, num_threads)`) before performing any `push/pop/is_empty` operation on the queue, with the same `num_threads` for all threads. The `<queue>_new` function (and therefore also `HazardPointer_initialize`) can be called by users before `HazardPointer_register`. The `HazardPointer_register` implementation should store the number of threads and thread number in the global variable `thread_local int _thread_id = -1`; and use it in HazardPointer methods.

Note: in the `HazardPointer_protect` implementation, the `atom` atomic address should be read multiple times to ensure that it has not been retired and freed in the time between reading and reserving. In doing so, it can be assumed that node addresses are only released by    `HazardPointer_retire`, and this always occurs after all atomic pointers (in particular, values in `atom`) are changed to another address.

It can be assumed that `num_threads <= MAX_THREADS`, where `MAX_THREADS = 128`. The structure when empty can occupy `O(MAX_THREAD^2)` memory, or it can allocate it statically for the worst case, i.e. `sizeof(HazardPointer)` can be equal to `10 * 128 * 128 * 64` bits of memory regardless of the given value of `num_threads`. On the other hand, the `HazardPointer_retire` implementation should use the true value of `num_threads` to avoid browsing unnecessarily many elements. 