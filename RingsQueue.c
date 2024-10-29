#include <malloc.h>
#include <pthread.h>
#include <stdatomic.h>

#include <stdio.h>
#include <stdlib.h>

#include "HazardPointer.h"
#include "RingsQueue.h"

struct RingsQueueNode;
typedef struct RingsQueueNode RingsQueueNode;

struct RingsQueueNode {
    _Atomic(RingsQueueNode*) next;
    Value buffer[RING_SIZE];
    _Atomic(int_fast64_t) push_idx, pop_idx;
};


RingsQueueNode* RingsQueueNode_new() {
    RingsQueueNode *node = malloc(sizeof(RingsQueueNode));
    atomic_init(&node->next, NULL);
    atomic_init(&node->push_idx, 0);
    atomic_init(&node->pop_idx, 0);
    return node;
}

struct RingsQueue {
    RingsQueueNode* head;
    RingsQueueNode* tail;
    pthread_mutex_t pop_mtx;
    pthread_mutex_t push_mtx;
};

RingsQueue* RingsQueue_new(void)
{   
    RingsQueue* queue = (RingsQueue*)malloc(sizeof(RingsQueue));
    RingsQueueNode *node = RingsQueueNode_new();
    queue->head = node;
    queue->tail = node;

    pthread_mutex_init(&queue->pop_mtx, NULL);
    pthread_mutex_init(&queue->push_mtx, NULL);

    return queue;
}


void RingsQueue_delete(RingsQueue* queue)
{
    pthread_mutex_destroy(&queue->pop_mtx);
    pthread_mutex_destroy(&queue->push_mtx);
    RingsQueueNode *node = queue->head;
    while (node != NULL) {
        RingsQueueNode *next = atomic_load(&node->next);
        free(node);
        node = next;
    }
    free(queue);
}


void RingsQueue_push(RingsQueue* queue, Value item)
{
    pthread_mutex_lock(&queue->push_mtx);

    if ((atomic_load(&queue->tail->push_idx) - atomic_load(&queue->tail->pop_idx)) < RING_SIZE) {
        queue->tail->buffer[atomic_load(&queue->tail->push_idx)%RING_SIZE] = item;
        atomic_fetch_add(&queue->tail->push_idx, 1);
    } else {
        RingsQueueNode *node = RingsQueueNode_new();
        
        node->buffer[0] = item;
        atomic_fetch_add(&node->push_idx, 1);

        atomic_store(&queue->tail->next, node);
        queue->tail = node;
    }
    pthread_mutex_unlock(&queue->push_mtx);
}


Value RingsQueue_pop(RingsQueue* queue)
{
    pthread_mutex_lock(&queue->pop_mtx);
    
    // pop_idx value won't change (pop mtx)
    int_fast64_t pop_idx = atomic_load(&queue->head->pop_idx);

    RingsQueueNode *node = atomic_load(&queue->head->next);
    if (node != NULL && pop_idx == atomic_load(&queue->head->push_idx)) {
        RingsQueueNode *prev_head = queue->head;
        queue->head = node;
        pop_idx = atomic_load(&queue->head->pop_idx);
        free(prev_head);
    }

    if (pop_idx == atomic_load(&queue->head->push_idx)) {
        pthread_mutex_unlock(&queue->pop_mtx);
        return EMPTY_VALUE;
    }
    Value val = queue->head->buffer[pop_idx%RING_SIZE];
    atomic_fetch_add(&queue->head->pop_idx, 1);
    pthread_mutex_unlock(&queue->pop_mtx);
    return val;
}


bool RingsQueue_is_empty(RingsQueue* queue)
{
    pthread_mutex_lock(&queue->pop_mtx);
    bool is_empty = 
    ((atomic_load(&queue->head->pop_idx) == atomic_load(&queue->head->push_idx))
    && (atomic_load(&queue->head->next) == NULL));
    pthread_mutex_unlock(&queue->pop_mtx);
    return is_empty;
}