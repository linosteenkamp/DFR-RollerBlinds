#ifndef RIG_CMD_H
#define RIG_CMD_H

/* The command parser of the test rig (pure). It changes one line of text
 * from the serial console (and later MQTT) into a rig_cmd_t. It checks
 * only the syntax. rig_ctl checks the ranges and makes all decisions. */

#include <stddef.h>
#include "rig_event.h"

typedef enum {
    RIG_PARSE_OK,     /* *out has the command */
    RIG_PARSE_EMPTY,  /* the line has only spaces: ignore it */
    RIG_PARSE_ERROR,  /* err has a message for the operator */
} rig_parse_result_t;

/* Parse one line. Spaces, tabs, CR and LF separate the words. The words
 * are case-sensitive. err must have RIG_MSG_LEN bytes or more. */
rig_parse_result_t rig_cmd_parse(const char *line, rig_cmd_t *out,
                                 char err[RIG_MSG_LEN]);

#endif /* RIG_CMD_H */
