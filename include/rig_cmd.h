#ifndef RIG_CMD_H
#define RIG_CMD_H

/* The command parser of the test rig (pure). It changes one line of text
 * from the serial console (and later MQTT) into a rig_cmd_t. It checks
 * only the syntax. rig_ctl checks the ranges and makes all decisions. */

#include <stdbool.h>
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

/* Line assembly for a serial console (pure). The console gives each
 * received byte to rig_line_add(). Backspace (0x08) and DEL (0x7F) remove
 * the last byte, because a terminal sends them when the operator corrects
 * a typing error. CR or LF ends the line. When the line is full, the
 * remaining bytes are ignored, and the parser rejects the line as too long. */
#define RIG_LINE_LEN 128   /* longer than the parser limit, with the NUL */

typedef struct {
    char   buf[RIG_LINE_LEN];  /* always NUL-terminated */
    size_t n;
    bool   done;               /* the last byte ended the line */
} rig_line_t;

void rig_line_reset(rig_line_t *l);

/* Add one byte. Returns true when the byte ends the line: l->buf then has
 * the full line. The next byte starts a new line. */
bool rig_line_add(rig_line_t *l, char c);

#endif /* RIG_CMD_H */
