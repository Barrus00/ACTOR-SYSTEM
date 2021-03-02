#ifndef CACTI_GENERIC_QUEUE_H
#define CACTI_GENERIC_QUEUE_H

/* Implementacja współbieżnej kolejki generycznej,
 * operacje krytyczne które modyfikują zawartosć kolejki są opatrzone dostępem do mutexa */

struct queue;

typedef struct queue generic_queue;

void queue_lock_mutex(generic_queue *q);

void queue_unlock_mutex(generic_queue *q);

// Tworzy kolejkę generyczną, z ustalonym limitem danych, jeżeli limi = NULL, to brak limitu.
generic_queue* create_queue(void *limit);

/* Dodaje element do kolejki, uważając na limit kolejki. Zwraca 0
 * jeżeli poprawnie dodano element, w.p.p -1. */
int queue_add(generic_queue *q, void *arg);

void* queue_peek(generic_queue *q);

void* queue_pop(generic_queue *q);

size_t queue_size(generic_queue *q);

int is_empty(generic_queue *q);

void free_queue(generic_queue *q);

#endif //CACTI_GENERIC_QUEUE_H
