#include <malloc.h>
#include <stdatomic.h>
#include <stdbool.h>

#include "HazardPointer.h"
#include "LLQueue.h"

struct LLNode;
typedef struct LLNode LLNode;
typedef _Atomic(LLNode*) AtomicLLNodePtr;


struct LLNode {
    AtomicLLNodePtr next;
    _Atomic(Value) item;
};

LLNode* LLNode_new(Value item)
{
    LLNode* node = (LLNode*)malloc(sizeof(LLNode));
    atomic_init(&node->next, NULL);
    atomic_init(&node->item, item);
    return node;
}

struct LLQueue {
    AtomicLLNodePtr head;
    AtomicLLNodePtr tail;
    HazardPointer hp;
};

LLQueue* LLQueue_new(void)
{
    LLQueue *queue = (LLQueue*)malloc(sizeof(LLQueue));
    LLNode *node = LLNode_new(1984);
    atomic_init(&queue->head, node);
    atomic_init(&queue->tail, node);
    HazardPointer_initialize(&queue->hp);
    return queue;
}

void LLQueue_delete(LLQueue* queue)
{   
    LLNode *node = atomic_load(&queue->head);
    while (node != NULL) {
        LLNode *next = atomic_load(&node->next);
        free(node);
        node = next;
    }
    HazardPointer_finalize(&queue->hp); 
    free(queue);
}

void LLQueue_push(LLQueue* queue, Value item)
{
    LLNode *node = LLNode_new(item), *null_node = NULL, *tail;
    while (1) {
        null_node = NULL;
        tail = HazardPointer_protect(&queue->hp, &queue->tail);
        if (atomic_compare_exchange_strong(&tail->next, &null_node, node)) {break;}
    }
    atomic_store(&queue->tail, node);
    HazardPointer_clear(&queue->hp);
}

Value LLQueue_pop(LLQueue* queue)
{
    while (1) {
        LLNode *head = HazardPointer_protect(&queue->hp, &queue->head);
        LLNode *next = atomic_load(&head->next);
        if (next == NULL) {
            HazardPointer_clear(&queue->hp);
            return EMPTY_VALUE;
        }
        Value val = atomic_exchange(&head->item, EMPTY_VALUE);
        if (val != EMPTY_VALUE) {
            val = atomic_load(&next->item);
            atomic_store(&queue->head, next);
            HazardPointer_clear(&queue->hp);
            HazardPointer_retire(&queue->hp, head);
            return val;
        }
    }
}

bool LLQueue_is_empty(LLQueue* queue)
{
    LLNode *head = HazardPointer_protect(&queue->hp, &queue->head);
    LLNode *next = atomic_load(&head->next);
    HazardPointer_clear(&queue->hp);
    return (next == NULL);
}
