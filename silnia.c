#include "cacti.h"
#include <stdio.h>

typedef struct factorial_counter {
    unsigned long long n;
    unsigned long long res;
    unsigned long long next;
} fac_t;


void first_hello(void **stateptr, size_t nbytes, void *data);

void hello(void **stateptr, size_t nbytes, void *data);

void calc(void **stateptr, size_t nbytes, void *data);

void fun(void **stateptr, size_t nbytes, void *data);

role_t *first_role_create() {
    static role_t role = {.nprompts = 2, .prompts = NULL};

    static void (*prompts[2])(void**, size_t, void*);

    prompts[0] = &first_hello;
    prompts[1] = &fun;

    role.prompts = prompts;

    return &role;
}

role_t *default_role_create() {
    static role_t role = {.nprompts = 3, .prompts = NULL};

    static void (*prompts[3])(void**, size_t, void*);

    prompts[0] = &hello;
    prompts[1] = &fun;
    prompts[2] = &calc;

    role.prompts = prompts;

    return &role;
}

void first_hello(void **stateptr, size_t nbytes, void *data) {
    (void) nbytes;

    if (data != NULL) {
        fac_t *fac = (fac_t *) data;

        if (fac->n == 0 || fac->n == 1) {
            printf("%ull", 1);
            message_t die = {.message_type = MSG_GODIE};
            send_message(actor_id_self(), die);
        }
        else {
            fac->next = 2;
            message_t msg = {.message_type = MSG_SPAWN, .data = default_role_create()};
            *stateptr = fac;
            send_message(actor_id_self(), msg);
        }
    }
}

void hello(void **stateptr, size_t nbytes, void *data) {
    (void) stateptr;
    (void) nbytes;

    actor_id_t father = (actor_id_t) data;

    message_t msg = {.message_type = 1,
                     .data = (void *) actor_id_self()};

    send_message(father, msg);
}

void calc(void **stateptr, size_t nbytes, void *data) {
    (void) nbytes;

    fac_t *fac = (fac_t *) data;

    fac->res *= fac->next;

    if (fac->next == fac->n) {
        printf("%llu\n", fac->res);
        message_t die = {.message_type = MSG_GODIE};
        send_message(actor_id_self(), die);
    }
    else {
        fac->next++;
        *stateptr = fac;

        message_t msg = {.message_type = MSG_SPAWN, .data = default_role_create()};

        send_message(actor_id_self(), msg);
    }
}

void fun(void **stateptr, size_t nbytes, void *data) {
    (void) nbytes;

    actor_id_t child = (actor_id_t) data;

    message_t msg = {.message_type = 2,
                     .data = *stateptr};

    send_message(child, msg);
    message_t die = {.message_type = MSG_GODIE};
    send_message(actor_id_self(), die);
}

int main(){
	int n;
	actor_id_t first;
	scanf("%d", &n);

    fac_t fac = {.n = n, .res = 1, .next = 0};

    actor_system_create(&first, first_role_create());
    message_t hello_start = {.message_type = MSG_HELLO, .data = (void *) &fac};
    send_message(first, hello_start);
    actor_system_join(0);
}
