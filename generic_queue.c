#include <stddef.h>
#include <stdlib.h>
#include <argp.h>
#include <pthread.h>
#include "err.h"

#include "generic_queue.h"

struct queue {
     size_t max_size;   /* Liczba dostępnych do zajęcia komórek
                         * w pamięci (Maksymalny rozmiar 'elements') */
     size_t curr_size;  // Aktualna liczba zajętych komórek w kolejce
     size_t curr_index; //
     size_t first_index;
     size_t limit;
     pthread_mutex_t q_mutex;
     void **elements;
};

static void *safe_malloc(size_t size) {
    void *space = malloc(size);

    if (space == NULL) {
        fatal("safe_malloc failed!\n");
    }

    return space;
}

void free_queue(generic_queue* q) {
    int res;
    if (q) {
        if (q->elements) {
            for (size_t i = 0; i < q->max_size; i++) {
                if (q->elements[i]) {
                    free(q->elements[i]);
                }
            }

            free(q->elements);
        }

        if ((res = pthread_mutex_destroy(&q->q_mutex)) != 0) {
            syserr(res, "Destroying mutex failed!\n");
        }

        free(q);
    }
}

void swap(void **ptr1, void **ptr2) {
    void *tmp = *ptr1;
    *ptr1 = *ptr2;
    *ptr2 = tmp;
}

void reverese(generic_queue *q, size_t l, size_t r) {
    if (l >= r) {
        return;
    }

    size_t mid = (l + r) / 2;

    for (size_t i = l; i <= mid; i++) {
        swap(&q->elements[i], &q->elements[r - i]);
    }
}

void cyclic_shift(generic_queue *q, size_t k) {
    reverese(q, 0, k - 1);
    reverese(q, k, q->max_size - 1);
    reverese(q, 0, q->max_size - 1);
}

void size_up(generic_queue *q) {
    if (q->first_index > 0) {
        cyclic_shift(q, q->first_index);
    }

    q->max_size *= 2;

    void* tmp = realloc(q->elements, q->max_size * (sizeof (void *)));

    if (!tmp) {
        free_queue(q);
        exit(1);
    }
    else {
        q->elements = tmp;
        q->curr_index = q->curr_size;
        q->first_index = 0;

        for (size_t i = q->curr_index; i < q->max_size; i++) {
            q->elements[i] = NULL;
        }
    }
}

void queue_lock_mutex(generic_queue *q) {
    int res;

    if ((res = pthread_mutex_lock(&q->q_mutex)) != 0) {
        syserr(res, "Locking queue mutex failed!\n");
    }
}


void queue_unlock_mutex(generic_queue *q) {
    int res;

    if ((res = pthread_mutex_unlock(&q->q_mutex)) != 0) {
        syserr(res, "Unlocking queue mutex failed!\n");
    }
}

generic_queue* create_queue(void *limit) {
    int res;
    generic_queue *new_queue;

    new_queue = safe_malloc(sizeof (struct queue));

    if (!new_queue) {
        return NULL;
    }

    new_queue->max_size = 1024;
    new_queue->curr_size = 0;
    new_queue->first_index = 0;
    new_queue->curr_index = 0;
    new_queue->limit = limit == NULL ? 0 : (size_t) limit;
    new_queue->elements = safe_malloc((sizeof (void*)) * new_queue->max_size);

    for (size_t i = new_queue->curr_index; i < new_queue->max_size; i++) {
        new_queue->elements[i] = NULL;
    }

    if ((res = pthread_mutex_init(&new_queue->q_mutex, NULL)) != 0) {
        syserr(res, "Mutex initialization failed!\n");
    }

    return new_queue;
}

int queue_add(generic_queue *q, void *arg) {
    queue_lock_mutex(q);

    if (q->limit != 0 && q->curr_size == q->limit) {
        queue_unlock_mutex(q);
        return -1;
    }

    if (q->curr_size == q->max_size) {
        size_up(q);
    }

    q->curr_size++;
    q->elements[q->curr_index] = arg;
    q->curr_index = (q->curr_index + 1) % q->max_size;

    queue_unlock_mutex(q);
    return 0;
}

int is_empty(generic_queue *q) {
    return q->curr_size == 0;
}

void *queue_pop(generic_queue *q) {
    queue_lock_mutex(q);

    if (!is_empty(q)) {
        void *out = q->elements[q->first_index];

        q->curr_size--;
        q->elements[q->first_index] = NULL;
        q->first_index = (q->first_index + 1) % q->max_size;
        queue_unlock_mutex(q);

        return out;
    }
    else {
        queue_unlock_mutex(q);
        return NULL;
    }
}

void *queue_peek(generic_queue *q) {
    if (!is_empty(q)) {
        return q->elements[q->first_index];
    }
    else {
        return NULL;
    }
}

size_t queue_size(generic_queue *q) {
    return q->curr_size;
}
