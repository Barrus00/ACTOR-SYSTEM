#include <pthread.h>
#include <semaphore.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "generic_queue.h"
#include "err.h"

#include "cacti.h"

#define INIT_SYSTEM_ERROR (-3)

#define NO_ACTIVE_SYSTEM (-4)

struct thread_pool;

typedef struct thread_pool tpool_t;

size_t how_many_commands(actor_id_t actor_id);

size_t how_many_messages(actor_id_t actor_id);

void execute_commands(actor_id_t actor_id, size_t how_many);

void try_to_add_actor(actor_id_t actor_id, tpool_t *tp);

void act_lock_mutex(actor_id_t actor_id);

void act_unlock_mutex(actor_id_t actor_id);

void act_lock_mutex_unsafe(actor_id_t actor_id);

void act_unlock_mutex_unsafe(actor_id_t actor_id);

void destroy_actor_system();

static __thread actor_id_t self_actor_id;
bool is_system_alive;
pthread_cond_t system_join = PTHREAD_COND_INITIALIZER;
pthread_mutex_t system_mutex = PTHREAD_MUTEX_INITIALIZER;
bool are_pthread_set = false;
bool waiting_join = false;
bool signaled = false;

void *safe_malloc(size_t size) {
    void *space = malloc(size);

    if (space == NULL) {
        fatal("Malloc failed!\n");
    }

    return space;
}

typedef struct actor_state {
    actor_id_t id;
    role_t     *role;
    generic_queue *q;
    pthread_mutex_t mutex;
    bool is_dead;
    bool is_already_on_queue;
    void *stateptr;
} actor_state_t;

void safe_destroy_actor(actor_state_t *actor) {
    if (actor != NULL) {
        if (actor->q != NULL) {
            free_queue(actor->q);
        }

        free(actor);
    }
}

actor_state_t* create_actor(actor_id_t id, role_t *role) {
    actor_state_t *new_actor = safe_malloc(sizeof (actor_state_t));

    new_actor->id = id;
    new_actor->role = role;
    new_actor->q = create_queue();
    new_actor->is_dead = false;
    new_actor->stateptr = NULL;
    new_actor->is_already_on_queue = false;
    pthread_mutex_init(&new_actor->mutex, NULL);

    return new_actor;
}

// ---------------- VECTOR IMPLEMENTATION -----------------
typedef struct vector {
    actor_state_t   **elements;
    size_t     max_size;
    size_t     curr_size;
    size_t     how_many_dead;
    pthread_mutex_t vec_mutex;
} vector;

vector* create_vector() {
    vector *new_vec;
    int res;

    new_vec = safe_malloc(sizeof (vector));

    new_vec->max_size = 1024;
    new_vec->curr_size = 0;
    new_vec->how_many_dead = 0;
    new_vec->elements = safe_malloc(sizeof(actor_state_t *) * new_vec->max_size);

    if ((res = pthread_mutex_init(&(new_vec->vec_mutex), NULL)) != 0) {
        syserr(res, "Mutex init failed!");
    }

    return new_vec;
}

void destroy_vector(vector *vec) {
    if (vec != NULL) {
        if (vec->elements != NULL) {
            for (size_t i = 0; i < vec->curr_size; i++) {
                safe_destroy_actor(vec->elements[i]);
            }

            free(vec->elements);
        }

        free(vec);
    }
}

void v_size_up(vector *vec) {
    printf("REALLOC MAN!\n");
    void *tmp_elements;

    vec->max_size *= 2;

    if (vec->max_size <= vec->curr_size) {
        fatal("Something went wrong with expanding vector!\n");
    }

    tmp_elements = realloc(vec->elements, vec->max_size * sizeof (actor_state_t *));

    if (tmp_elements == NULL) {
        fatal("Realloc failed\n");
    }

    vec->elements = tmp_elements;
}

actor_id_t add_act(vector *vec, role_t *role) {
    int res;
    actor_id_t act_id;
    if ((res = pthread_mutex_lock(&vec->vec_mutex)) != 0) {
        syserr(res, "Locking mutex failed! (Add_act)\n");
    }

    if (vec->curr_size == CAST_LIMIT) {
        fatal("CAST ACTOR LIMIT EXCEEDED!\n");
    }
    if (vec->curr_size == vec->max_size) {
        v_size_up(vec);
    }
    act_id = vec->curr_size;
    vec->elements[act_id] = create_actor(act_id, role);
    vec->curr_size++;

    if ((res = pthread_mutex_unlock(&vec->vec_mutex)) != 0) {
        syserr(res, "Unlocking mutex failed! (Add_act)\n");
    }

    return act_id;
}

actor_state_t *vector_get(vector *vec, size_t id) {
    int res;
    actor_state_t *tmp;

    if ((res = pthread_mutex_lock(&vec->vec_mutex)) != 0) {
        syserr(res, "Locking mutex failed! (Add_act)\n");
    }

    if (id <= vec->curr_size) {
        tmp = vec->elements[id];
    }
    else {
        tmp = NULL;
    }

    if ((res = pthread_mutex_unlock(&vec->vec_mutex)) != 0) {
        syserr(res, "Locking mutex failed! (Add_act)\n");
    }

    return tmp;
}

actor_state_t *vector_get_no_mutex(vector *vec, size_t id) {
    if (id <= vec->curr_size) {
        return vec->elements[id];
    }
    else {
        return NULL;
    }
}


void actor_turn_dead(vector *vec, actor_id_t act_id) {
    int res;

    act_lock_mutex_unsafe(act_id);

    if ((res = pthread_mutex_lock(&vec->vec_mutex)) != 0) {
        syserr(res, "Locking mutex failed! (Add_act)\n");
    }

    actor_state_t *actor_state = vector_get_no_mutex(vec, act_id);

    actor_state->is_dead = true;

    vec->how_many_dead++;
 //   printf("Actor %d, goes dead! \n", actor_id_self());
    if(vec->how_many_dead == vec->curr_size) {
        is_system_alive = false;
    }

    if ((res = pthread_mutex_unlock(&vec->vec_mutex)) != 0) {
        syserr(res, "Unlocking mutex failed! (Add_act)\n");
    }

    act_unlock_mutex_unsafe(act_id);
}

//---------------- END OF VECTOR IMPLEMENTATION ------------------------

//----------------- THREAD POOL IMPLEMENTATION --------------------------
struct thread_pool {
    pthread_mutex_t mutex;
    pthread_cond_t work_cond;
    size_t active_threads_num;
    size_t threads_num;
    bool still_running;
    generic_queue *work_q;
    pthread_t *threads;
};

void *tpool_worker(void *arg) {
    tpool_t *tp = arg;
    pthread_mutex_lock(&system_mutex);
    pthread_mutex_unlock(&system_mutex);

    while (1) {
        pthread_mutex_lock(&tp->mutex);

        while (is_empty(tp->work_q) && tp->still_running && is_system_alive && !signaled)
            pthread_cond_wait(&tp->work_cond, &tp->mutex);

        if ((!is_system_alive || signaled) && is_empty(tp->work_q)) {
            tp->still_running = false;
            pthread_cond_broadcast(&tp->work_cond);
            break;
        }

        actor_id_t act_id = (actor_id_t) queue_pop(tp->work_q);

        int nprompts = how_many_messages(act_id);

        self_actor_id = act_id;
        pthread_mutex_unlock(&tp->mutex);

        execute_commands(act_id, nprompts);
        try_to_add_actor(act_id, tp);
    }

    tp->active_threads_num--;

    if (tp->active_threads_num == 0) {
        pthread_mutex_unlock(&tp->mutex);
        destroy_actor_system();
    }
    else {
        pthread_mutex_unlock(&tp->mutex);
    }

    return NULL;
}

tpool_t *tpool_create(size_t active_threads_num) {
    tpool_t *new_tp = safe_malloc(sizeof (tpool_t));
    int res;

    if (!new_tp) {
        exit(1);
    }

    new_tp->work_q = create_queue(sizeof (actor_id_t));

    if (!new_tp->work_q){
        fatal("Thread pool initialization failure!\n");
        exit(1);
    }

    new_tp->active_threads_num = active_threads_num;
    new_tp->threads_num = active_threads_num;
    new_tp->still_running = true;
    new_tp->threads = safe_malloc(sizeof(pthread_t) * active_threads_num);

    if ((res = pthread_mutex_init(&(new_tp->mutex), NULL)) != 0) {
        syserr(res, "Thread pool mutex initalization failure!\n");
    }

    if ((res = pthread_mutex_init(&(new_tp->mutex), NULL)) != 0) {
        syserr(res, "Thread pool mutex initalization failure!\n");
    }

    if ((res = pthread_cond_init(&(new_tp->work_cond), NULL)) != 0) {
        syserr(res, "Thread pool conditional initialization failure!\n");
    }

    for (size_t i = 0; i < active_threads_num; i++) {
        pthread_create(&new_tp->threads[i], NULL, tpool_worker, new_tp);
    }

    return new_tp;
}

void tpool_destroy(tpool_t *tp) {
    int res;

    if (tp != NULL) {
        if (tp->work_q != NULL) {
            free_queue(tp->work_q);
        }
        if (tp->threads != NULL) {
            for (size_t i = 0; i < tp->threads_num; i++) {
                pthread_join(tp->threads[i], NULL);
            }

            free(tp->threads);
        }

        if ((res = pthread_mutex_destroy(&tp->mutex)) != 0) {
            syserr(res, "Destroying thread pool mutex failed!\n");
        }

        if ((res = pthread_cond_destroy(&tp->work_cond)) != 0) {
            syserr(res, "Destroying thread pool cond failed!\n");
        }

        free(tp);
    }
}

//----------------- END OF THREAD POOL IMPLEMENTATION --------------------------

tpool_t *thread_pool = NULL;
vector *actors = NULL;

void catch_signal() {
    printf("STOPPED!\n");
    signaled = true;
    pthread_cond_broadcast(&thread_pool->work_cond);
}

void destroy_actor_system() {
    int res;
    if ((res = pthread_mutex_lock(&system_mutex)) != 0) {
        syserr(res, "Destroy system mutex failed!\n");
    }

    destroy_vector(actors);
    actors = NULL;
    pthread_cond_signal(&system_join);

    if ((res = pthread_mutex_unlock(&system_mutex)) != 0) {
        syserr(res, "Destroy system mutex failed!\n");
    }
}

void act_lock_mutex(actor_id_t actor_id) {
    int res;
    actor_state_t *actor_state = vector_get(actors, actor_id);

    if ((res = pthread_mutex_lock(&actor_state->mutex)) != 0) {
        syserr(res, "Actor mutex failed!\n");
    }
}

void act_unlock_mutex(actor_id_t actor_id) {
    int res;
    actor_state_t *actor_state = vector_get(actors, actor_id);

    if ((res = pthread_mutex_unlock(&actor_state->mutex)) != 0) {
        syserr(res, "Actor mutex failed!\n");
    }
}

void act_lock_mutex_unsafe(actor_id_t actor_id) {
    int res;
    actor_state_t *actor_state = vector_get_no_mutex(actors, actor_id);

    if ((res = pthread_mutex_lock(&actor_state->mutex)) != 0) {
        syserr(res, "Actor mutex failed!\n");
    }
}

void act_unlock_mutex_unsafe(actor_id_t actor_id) {
    int res;
    actor_state_t *actor_state = vector_get_no_mutex(actors, actor_id);

    if ((res = pthread_mutex_unlock(&actor_state->mutex)) != 0) {
        syserr(res, "Actor mutex failed!\n");
    }
}


void actor_end_work(actor_id_t actor_id) {
    actor_state_t *actor_state = vector_get(actors, actor_id);

    act_lock_mutex(actor_id);

    actor_state->is_already_on_queue = false;

    act_unlock_mutex(actor_id);
}

void try_to_add_actor(actor_id_t actor_id, tpool_t *tp) {
    actor_state_t *actor_state = vector_get(actors, actor_id);

    act_lock_mutex(actor_id);

    if (!is_empty(actor_state->q) && !actor_state->is_already_on_queue) {
        actor_state->is_already_on_queue = true;

        pthread_mutex_lock(&tp->mutex);

        queue_add(tp->work_q, (void *) actor_state->id);

        pthread_cond_signal(&tp->work_cond);

        pthread_mutex_unlock(&tp->mutex);
    }

    act_unlock_mutex(actor_id);
}

size_t how_many_messages(actor_id_t actor_id) {
    actor_state_t *actor_state = vector_get(actors, actor_id);

    return queue_size(actor_state->q);
}

void execute_command(actor_id_t actor_id) {
    actor_state_t *actorState = vector_get(actors, actor_id);

    message_t *msg = (message_t *)queue_pop(actorState->q);
    actor_id_t new_actor;

    switch (msg->message_type) {
        case MSG_SPAWN :
            if (!signaled) {
                new_actor = add_act(actors, (role_t *) msg->data);

                message_t hello_message = {.message_type = MSG_HELLO,
                        .nbytes = sizeof(actor_id_t),
                        .data = (void *) actorState->id};

                send_message(new_actor, hello_message);
            }
            break;
        case MSG_GODIE :
            actor_turn_dead(actors, actor_id);
            break;
        default:

            actorState->role->prompts[msg->message_type](&actorState->stateptr, msg->nbytes, msg->data);
            break;
    }

    free(msg);
}

void execute_commands(actor_id_t actor_id, size_t how_many) {

    for (size_t i = 0; i < how_many; i++){
        execute_command(actor_id);
    }

    actor_end_work(actor_id);
}

int send_message(actor_id_t actor, message_t message) {
    if (!is_system_alive) {
        return NO_ACTIVE_SYSTEM;
    }
    else if (actor >= actors->curr_size) {
        return -1;
    }
    else {
        actor_state_t *act = vector_get(actors, actor);
        if (act->is_dead || signaled) {
            return -2;
        }
        else {
            message_t *allocated_message = safe_malloc(sizeof (message_t));

            allocated_message->data = message.data;
            allocated_message->nbytes = message.nbytes;
            allocated_message->message_type = message.message_type;

            queue_add(act->q, (void *) allocated_message);
            try_to_add_actor(actor, thread_pool);

            return 0;
        }
    }
}

void proc_mask(int type) {
    static struct sigaction newhandler, old_handler;
    newhandler.sa_handler = &catch_signal;
    sigemptyset(&(newhandler.sa_mask));
    newhandler.sa_flags = 0;

    if (type == 0) {
        if (sigaction(SIGINT, &newhandler, &old_handler) != -1) {
            printf("New handler set!\n");
        }
    }
    else {
        if (sigaction(SIGINT, &old_handler, NULL) != -1) {
            printf("Old handler set!\n");
        }
    }

}

int actor_system_create(actor_id_t *actor, role_t *const role) {
      pthread_mutex_lock(&system_mutex);
      if (actors != NULL) {
          return INIT_SYSTEM_ERROR;
      }
      is_system_alive = true;

      thread_pool = tpool_create(POOL_SIZE);
      actors = create_vector();
      actor_id_t new_actor = add_act(actors, role);
      signaled = false;
      message_t hello = {.message_type = MSG_HELLO,
                         .nbytes = 0,
                         .data = NULL};

      send_message(new_actor, hello);

      proc_mask(0);

      pthread_mutex_unlock(&system_mutex);

      *actor = new_actor;

      return 0;
}

void actor_system_join(actor_id_t actor) {
    int res;

    if ((res = pthread_mutex_lock(&system_mutex))) {
        syserr(res, "System mutex failed!\n");
    }

    printf("WAITIN FOR JOIN!\n");
    while (actors != NULL) {

        pthread_cond_wait(&system_join, &system_mutex);
    }

    printf("WAITIN IS NO MORE!\n");

    if (thread_pool != NULL) {
        tpool_destroy(thread_pool);
        thread_pool = NULL;
        proc_mask(1);
        signaled = false;
    }

    if ((res = pthread_mutex_unlock(&system_mutex))) {
        syserr(res, "System mutex failed!\n");
    }
}

actor_id_t actor_id_self() {
    return self_actor_id;
