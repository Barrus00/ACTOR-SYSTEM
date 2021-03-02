#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include "cacti.h"

#define MSG_NOTIFY (1)
#define MSG_SEND_INFO (2)
#define MSG_CALC (3)
#define MSG_NOTIFY_FATHER (4)
#define MSG_END (5)
#define MILISECOND (1000)

void first_hello (void **stateptr, size_t nbytes, void *data);
void hello (void **stateptr, size_t nbytes, void *data);
void get_notify (void **stateptr, size_t nbytes, void *data);
void get_info (void **stateptr, size_t nbytes, void *data);
void calculate (void **stateptr, size_t nbytes, void *data);
void notify_father (void **stateptr, size_t nbytes, void *data);
void end (void **stateptr, size_t nbytes, void *data);

static act_t first_act[6] = {&first_hello, &get_notify, &get_info, &calculate, &notify_father, &end};

static act_t default_act[6] = {&hello, &get_notify, &get_info, &calculate, &notify_father, &end};

static role_t first_role = {.nprompts = 6, .prompts = first_act};

static role_t default_role = {.nprompts = 6, .prompts = default_act};

typedef struct matrix_val {
    int val;
    int time;
} matrix_val_t;

typedef struct matrix {
    matrix_val_t **matrix;
    int columns;
    int rows;
} matrix_t;

typedef struct actor_struct {
    matrix_t *matrix;
    int my_row;
    unsigned long long sum;
    actor_id_t my_father;
    actor_id_t my_child;
} actor_t;

matrix_t *create_matrix(int columns, int rows) {
    matrix_t *new_matrix = malloc(sizeof (matrix_t));
    new_matrix->columns = columns;
    new_matrix->rows = rows;
    new_matrix->matrix = malloc(sizeof (struct matrix_val *) * rows);

    for (int i = 0; i < rows; i++) {
        new_matrix->matrix[i] = malloc(sizeof (matrix_val_t) * columns);
    }

    return new_matrix;
}

void first_hello (void **stateptr, size_t nbytes, void *data) {
    (void) nbytes;

    if (data != NULL) {
        matrix_t *matrix = data;
        actor_t *actor_info = malloc(sizeof (struct actor_struct));

        actor_info->matrix = matrix;
        actor_info->my_row = 0;
        actor_info->sum = 0;

        *stateptr = actor_info;

        if (actor_info->my_row + 1 == actor_info->matrix->rows) {
            message_t msg = {.message_type = MSG_END};
            send_message(actor_id_self(), (message_t){.message_type = MSG_CALC});
            send_message(actor_id_self(), msg);
        }
        else {
            message_t msg = {.message_type = MSG_SPAWN, .data = (void *) &default_role};
            send_message(actor_id_self(), msg);
        }
    }
}

void hello (void **stateptr, size_t nbytes, void *data) {
    (void) stateptr; (void) nbytes;

    actor_id_t father_id = (actor_id_t) data;

    message_t msg = {.message_type = MSG_NOTIFY, .data = (void *) actor_id_self()};

    send_message(father_id, msg);
}

void get_notify(void **stateptr, size_t nbytes, void *data) {
    (void) nbytes;

    actor_t *actor_info = malloc(sizeof (struct actor_struct));
    actor_id_t child = (actor_id_t) data;
    actor_t *my_info = (actor_t *) *stateptr;

    my_info->my_child = child;

    actor_info->matrix = my_info->matrix;
    actor_info->my_row = my_info->my_row + 1;
    actor_info->sum = 0;
    actor_info->my_father = actor_id_self();

    message_t msg = {.message_type = MSG_SEND_INFO,
                     .data = actor_info};

    send_message(child, msg);

    message_t calc_msg = {.message_type = MSG_CALC};

    send_message(actor_id_self(), calc_msg);
}

void get_info(void **stateptr, size_t nbytes, void *data) {
    (void) nbytes;

    actor_t *actor_info = (actor_t *)data;
    *stateptr = actor_info;

    if (actor_info->my_row + 1 < actor_info->matrix->rows) {
        message_t msg = {.message_type = MSG_SPAWN, .data = (void *) &default_role};

        send_message(actor_id_self(), msg);
    }
    else {
        message_t calc_msg = {.message_type = MSG_CALC};

        send_message(actor_id_self(), calc_msg);
    }
}

void calculate(void **stateptr, size_t nbytes, void *data) {
    (void) nbytes; (void) data;

    actor_t *my_state = *stateptr;

    for (int i = 0; i < my_state->matrix->columns; i++) {
        usleep(MILISECOND * my_state->matrix->matrix[my_state->my_row][i].time);
        my_state->sum += my_state->matrix->matrix[my_state->my_row][i].val;
    }

    if (my_state->my_row + 1 == my_state->matrix->rows && my_state->matrix->rows != 1) {
        message_t msg = {.message_type = MSG_NOTIFY_FATHER};

        send_message(actor_id_self(), msg);
    }
}

void notify_father(void **stateptr, size_t nbytes, void *data) {
    (void) nbytes; (void) data;

    actor_t *my_state = *stateptr;

    if (my_state->my_row == 1) {
        message_t msg = {.message_type = MSG_END};

        send_message(my_state->my_father, msg);
    }
    else {
        message_t msg = {.message_type = MSG_NOTIFY_FATHER};

        send_message(my_state->my_father, msg);
    }
}

void end(void **stateptr, size_t nbytes, void *data) {
    (void) nbytes; (void) data;

    actor_t *my_state = *stateptr;

    message_t msg = {.message_type = MSG_END};

    printf("%llu\n", my_state->sum);

    if (my_state->my_row + 1 < my_state->matrix->rows)
        send_message(my_state->my_child, msg);

    free(*stateptr);

    message_t die = {.message_type = MSG_GODIE};

    send_message(actor_id_self(), die);
}

int main(){
    int NO_column, NO_row;
    actor_id_t first;

    scanf("%d", &NO_row);
    scanf("%d", &NO_column);

    if (NO_column == 0 || NO_row == 0) {
        printf("%d\n",0);
        return 0;
    }

    matrix_t *mat = create_matrix(NO_column, NO_row);

    for (int col = 0; col < NO_row; col++) {
        for (int row = 0; row < NO_column; row++ ) {
            scanf("%d", &mat->matrix[col][row].val);
            scanf("%d", &mat->matrix[col][row].time);
        }
    }

    actor_system_create(&first, &first_role);
    message_t msg = {.message_type = MSG_HELLO, .data = (void *) mat};
    send_message(first, msg);

    actor_system_join(0);

    for (int i = 0; i < mat->rows; i++) {
        free(mat->matrix[i]);
    }

    free(mat->matrix);
    free(mat);

    return 0;
}
