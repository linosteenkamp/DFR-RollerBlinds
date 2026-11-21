#ifndef RIG_EVENT_H
#define RIG_EVENT_H

/* The queue item of the test rig firmware. The serial console (and later
 * MQTT) and the dispatcher share it. Only the dispatcher task gives it to
 * rig_handle(). Pure C, with no ESP-IDF headers, for the host tests. */

#include <stdbool.h>
#include <stdint.h>

/* The length of a message text, with the terminating NUL. */
#define RIG_MSG_LEN 64

typedef enum {
    RIG_CMD_MOVE,     /* move <steps> <interval_us> */
    RIG_CMD_STOP,     /* stop */
    RIG_CMD_STATUS,   /* status */
} rig_cmd_type_t;

/* A command that the parser accepted. The parser checks only the syntax.
 * rig_ctl checks the ranges. */
typedef struct {
    rig_cmd_type_t type;
    int32_t  steps;        /* MOVE: signed relative distance in microsteps */
    uint32_t interval_us;  /* MOVE: the step interval at cruise */
} rig_cmd_t;

typedef enum {
    RIG_EVT_CMD,          /* .cmd: a command that the parser accepted */
    RIG_EVT_BAD_CMD,      /* .error: the parser rejected a command */
    RIG_EVT_MOTION_DONE,  /* .done: motion stopped */
} rig_event_type_t;

typedef struct {
    rig_event_type_t type;
    union {
        rig_cmd_t cmd;
        char      error[RIG_MSG_LEN];
        struct {
            int32_t steps;      /* the final absolute position */
            bool    completed;  /* false = stopped before the target */
        } done;
    };
} rig_event_t;

#endif /* RIG_EVENT_H */
