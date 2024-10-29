#include <malloc.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>

#include "HazardPointer.h"

thread_local int _thread_id = -1;
int _num_threads = -1;
thread_local int end = 0;

void HazardPointer_register(int thread_id, int num_threads)
{
    _thread_id = thread_id;
    _num_threads = num_threads;
}

void HazardPointer_initialize(HazardPointer* hp)
{
    for (int i = 0; i < MAX_THREADS; ++i) {
        atomic_init(&hp->pointer[i], NULL);
        for (int j = 0; j <= RETIRED_THRESHOLD; ++j) {
            hp->retired[i][j] = NULL;
        }
    }
}

void HazardPointer_finalize(HazardPointer* hp)
{
    for (int i = 0; i < _num_threads; ++i) {
        atomic_store(&hp->pointer[i], NULL);
        for (int j = 0; j < RETIRED_THRESHOLD; ++j) {
            free(hp->retired[i][j]);
        }
        end = 0;
    }
}

void* HazardPointer_protect(HazardPointer* hp, const _Atomic(void*)* atom)
{
    void *node;
    do {
        node = atomic_load(atom);
        atomic_store(&hp->pointer[_thread_id], node);
    } while (node != atomic_load(atom));
    return node;
}

void HazardPointer_clear(HazardPointer* hp)
{
    atomic_store(&hp->pointer[_thread_id], NULL);
}

void HazardPointer_retire(HazardPointer* hp, void* ptr)
{    
    hp->retired[_thread_id][end] = ptr;
    ++end;
    if (end == RETIRED_THRESHOLD+1) {
        end = 0;
        for (int i = 0; i <= RETIRED_THRESHOLD; ++i) {
            bool can_be_freed = 1;
            for (int j = 0; j < _num_threads; ++j) {
                if (hp->retired[_thread_id][i] == atomic_load(&hp->pointer[j])) {
                    can_be_freed = 0;
                    break;
                }
            }

            if (can_be_freed) {
                free(hp->retired[_thread_id][i]);
                hp->retired[_thread_id][i] = NULL;
            } else {
                if (end != i) {
                    hp->retired[_thread_id][end] = hp->retired[_thread_id][i];
                    hp->retired[_thread_id][i] = NULL;
                }
                ++end;
            }
        }
    }
}
