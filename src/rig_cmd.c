/**
 * @file rig_cmd.c
 * @brief The command parser of the test rig (pure): one line of text to a
 *        rig_cmd_t. It checks only the syntax. rig_ctl checks the ranges.
 */
#include "rig_cmd.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_WORDS 4    /* one more than the longest command, to find extras */
#define CMD_LINE_MAX 96   /* the longest line, with the NUL; longer lines are rejected */

static bool is_sep(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Split buf in place. Returns the number of words, up to MAX_WORDS. */
static int split(char *buf, char *words[MAX_WORDS])
{
    int n = 0;
    char *p = buf;
    while (*p != '\0' && n < MAX_WORDS) {
        while (is_sep(*p)) p++;
        if (*p == '\0') break;
        words[n++] = p;
        while (*p != '\0' && !is_sep(*p)) p++;
        if (*p != '\0') *p++ = '\0';
    }
    return n;
}

static bool parse_i32(const char *s, int32_t *out)
{
    char *end;
    errno = 0;
    long long v = strtoll(s, &end, 10);
    if (end == s || *end != '\0' || errno != 0) return false;
    if (v < INT32_MIN || v > INT32_MAX) return false;
    *out = (int32_t)v;
    return true;
}

/* A positive decimal integer, with no sign. */
static bool parse_u32_positive(const char *s, uint32_t *out)
{
    if (*s < '0' || *s > '9') return false;
    char *end;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 10);
    if (*end != '\0' || errno != 0) return false;
    if (v == 0 || v > UINT32_MAX) return false;
    *out = (uint32_t)v;
    return true;
}

static rig_parse_result_t fail(char err[RIG_MSG_LEN], const char *msg)
{
    snprintf(err, RIG_MSG_LEN, "%s", msg);
    return RIG_PARSE_ERROR;
}

rig_parse_result_t rig_cmd_parse(const char *line, rig_cmd_t *out,
                                 char err[RIG_MSG_LEN])
{
    err[0] = '\0';
    char buf[CMD_LINE_MAX];
    if (strlen(line) >= sizeof buf) return fail(err, "the command is too long");
    memcpy(buf, line, strlen(line) + 1);
    char *w[MAX_WORDS];
    int n = split(buf, w);

    if (n == 0) return RIG_PARSE_EMPTY;

    if (strcmp(w[0], "move") == 0) {
        if (n != 3) return fail(err, "usage: move <steps> <interval_us>");
        rig_cmd_t c = { .type = RIG_CMD_MOVE };
        if (!parse_i32(w[1], &c.steps))
            return fail(err, "move: steps must be an integer");
        if (!parse_u32_positive(w[2], &c.interval_us))
            return fail(err, "move: interval_us must be a positive integer");
        *out = c;
        return RIG_PARSE_OK;
    }
    if (strcmp(w[0], "stop") == 0) {
        if (n != 1) return fail(err, "usage: stop");
        *out = (rig_cmd_t){ .type = RIG_CMD_STOP };
        return RIG_PARSE_OK;
    }
    if (strcmp(w[0], "status") == 0) {
        if (n != 1) return fail(err, "usage: status");
        *out = (rig_cmd_t){ .type = RIG_CMD_STATUS };
        return RIG_PARSE_OK;
    }
    snprintf(err, RIG_MSG_LEN, "unknown command '%.16s' (move, stop, status)", w[0]);
    return RIG_PARSE_ERROR;
}
