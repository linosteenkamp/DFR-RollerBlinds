#ifndef RIG_RECORD_H
#define RIG_RECORD_H

/* The records that rig_ctl publishes, and their text form (pure). rig_ctl
 * fills a rig_record_t. The publish adapter makes the topic and the JSON
 * with the functions below. The serial console prints them now. The MQTT
 * adapter (ticket #36) will send them. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "rig_event.h"

typedef enum {
    RIG_STATE_IDLE,
    RIG_STATE_MOVING,
} rig_state_t;

/* One topic for each record type. The unit goes into the topic when the
 * firmware knows the unit (ticket #33). */
typedef enum {
    RIG_TOPIC_MOVE,    /* rig/move: the end of each move */
    RIG_TOPIC_STATUS,  /* rig/status: each change of state, and `status` */
    RIG_TOPIC_REPLY,   /* rig/reply: the answer to a command */
} rig_topic_t;

typedef struct {
    rig_topic_t topic;
    union {
        struct {
            int32_t  from;         /* the absolute start position */
            int32_t  target;       /* the absolute target position */
            int32_t  end;          /* the absolute final position */
            uint32_t interval_us;  /* the step interval at cruise */
            bool     completed;    /* false = stopped before the target */
        } move;
        struct {
            rig_state_t state;
            int32_t     position;  /* the absolute position, in microsteps */
        } status;
        struct {
            bool ok;               /* false = the command was rejected */
            char text[RIG_MSG_LEN];
        } reply;
    };
} rig_record_t;

/* The topic text, for example "rig/status". */
const char *rig_record_topic(rig_topic_t topic);

/* The state text, for example "idle". */
const char *rig_state_name(rig_state_t state);

/* Write the record as one JSON object into buf (NUL-terminated). Returns
 * the length, or -1 when buf is too small. */
int rig_record_json(const rig_record_t *rec, char *buf, size_t len);

#endif /* RIG_RECORD_H */
