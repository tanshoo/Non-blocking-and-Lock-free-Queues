#include <malloc.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "BLQueue.h"
#include "HazardPointer.h"

struct BLNode;
typedef struct BLNode BLNode;
typedef _Atomic(BLNode*) AtomicBLNodePtr;

struct BLNode {
    AtomicBLNodePtr next;
    _Atomic(Value) buffer[BUFFER_SIZE];
    _Atomic(int_fast64_t) push_idx, pop_idx;
};


BLNode* BLNode_new() {
    BLNode *node = (BLNode*)malloc(sizeof(BLNode));
    for (int i = 0; i < BUFFER_SIZE; ++i) {
        atomic_init(&node->buffer[i], EMPTY_VALUE);
    }
    atomic_init(&node->next, NULL);
    atomic_init(&node->push_idx, 0);
    atomic_init(&node->pop_idx, 0);
    return node;
}


struct BLQueue {
    AtomicBLNodePtr head;
    AtomicBLNodePtr tail;
    HazardPointer hp;
};

BLQueue* BLQueue_new(void)
{
    BLQueue* queue = (BLQueue*)malloc(sizeof(BLQueue));
    BLNode *node = BLNode_new();
    atomic_init(&queue->head, node);
    atomic_init(&queue->tail, node);
    HazardPointer_initialize(&queue->hp);
    return queue;
}

void BLQueue_delete(BLQueue* queue)
{
    BLNode *node = atomic_load(&queue->head);
    while (node != NULL) {
        BLNode *next = atomic_load(&node->next);
        free(node);
        node = next;
    }
    HazardPointer_finalize(&queue->hp);
    free(queue);
}

void BLQueue_push(BLQueue* queue, Value item)
{
    while (1) {
        BLNode *tail = HazardPointer_protect(&queue->hp, &queue->tail);
        int_fast64_t push_idx = atomic_fetch_add(&tail->push_idx, 1);
        
        if (push_idx < BUFFER_SIZE) {
            Value empty = EMPTY_VALUE;
            if (atomic_compare_exchange_strong(&tail->buffer[push_idx], &empty, item)) {
                HazardPointer_clear(&queue->hp);
                return;
            }
        }
        else {
            if (atomic_load(&tail->next) != NULL) {
                continue;
            }
            BLNode *node = BLNode_new();
            atomic_store(&node->buffer[0], item);
            atomic_store(&node->push_idx, 1);
            BLNode *empty = NULL;
            if (atomic_compare_exchange_strong(&queue->tail, &tail, node)) {
                atomic_store(&tail->next, node);
                HazardPointer_clear(&queue->hp);
                return;
            }
            free(node);
        }
    }
}

Value BLQueue_pop(BLQueue* queue)
{
    while (1) {
        BLNode *head = HazardPointer_protect(&queue->hp, &queue->head);
        int_fast64_t pop_idx = atomic_fetch_add(&head->pop_idx, 1);

        if (pop_idx < BUFFER_SIZE) {
            Value val = atomic_exchange(&head->buffer[pop_idx], TAKEN_VALUE);
            if (val != EMPTY_VALUE) {
                HazardPointer_clear(&queue->hp);
                return val;
            }
        }
        else {
            BLNode *next = atomic_load(&head->next);
            if (next == NULL) {
                HazardPointer_clear(&queue->hp);
                return EMPTY_VALUE;
            }
            if (atomic_compare_exchange_strong(&queue->head, &head, next)) {
                HazardPointer_retire(&queue->hp, head);
            }
        }
    }
}

bool BLQueue_is_empty(BLQueue* queue)
{
    BLNode *head = HazardPointer_protect(&queue->hp, &queue->head);
    bool is_empty = 
    (atomic_load(&head->pop_idx) >= atomic_load(&head->push_idx)) && (atomic_load(&head->next) == NULL);
    return is_empty;
}
