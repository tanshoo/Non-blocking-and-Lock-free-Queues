#include <malloc.h>
#include <pthread.h>
#include <stdatomic.h>

#include "SimpleQueue.h"

struct SimpleQueueNode;
typedef struct SimpleQueueNode SimpleQueueNode;

struct SimpleQueueNode {
    _Atomic(SimpleQueueNode*) next;
    Value item;
};

SimpleQueueNode* SimpleQueueNode_new(Value item)
{
    SimpleQueueNode* node = (SimpleQueueNode*)malloc(sizeof(SimpleQueueNode));
    atomic_init(&node->next, NULL);
    node->item = item;
    return node;
}

struct SimpleQueue {
    SimpleQueueNode* head;
    SimpleQueueNode* tail;
    pthread_mutex_t head_mtx;
    pthread_mutex_t tail_mtx;
};

SimpleQueue* SimpleQueue_new(void)
{
    SimpleQueue* queue = (SimpleQueue*)malloc(sizeof(SimpleQueue));
 
    pthread_mutex_init(&queue->head_mtx, NULL);
    pthread_mutex_init(&queue->tail_mtx, NULL);

    SimpleQueueNode *node = SimpleQueueNode_new(EMPTY_VALUE);
    queue->head = node;
    queue->tail = node;

    return queue;
}

void SimpleQueue_delete(SimpleQueue* queue)
{
    pthread_mutex_destroy(&queue->head_mtx);
    pthread_mutex_destroy(&queue->tail_mtx);

    SimpleQueueNode *node = queue->head;
    while (node != NULL) {
        SimpleQueueNode *next = atomic_load(&node->next);
        free(node);
        node = next;
    };
    free(queue);
}

void SimpleQueue_push(SimpleQueue* queue, Value item)
{
    SimpleQueueNode *new_node = SimpleQueueNode_new(item);
    pthread_mutex_lock(&queue->tail_mtx);
    atomic_store(&queue->tail->next, new_node);
    queue->tail = new_node;
    pthread_mutex_unlock(&queue->tail_mtx);
}

Value SimpleQueue_pop(SimpleQueue* queue)
{
    pthread_mutex_lock(&queue->head_mtx);
    SimpleQueueNode *begin = atomic_load(&queue->head->next);

    if (begin == NULL) {
        pthread_mutex_unlock(&queue->head_mtx);
        return EMPTY_VALUE;
    }
    
    Value val = begin->item;
    free(queue->head);
    queue->head = begin;
    pthread_mutex_unlock(&queue->head_mtx);
    return val;
}

bool SimpleQueue_is_empty(SimpleQueue* queue)
{ 
    pthread_mutex_lock(&queue->head_mtx);
    SimpleQueueNode *next = atomic_load(&queue->head->next);
    pthread_mutex_unlock(&queue->head_mtx);
    return (next == NULL);
}
