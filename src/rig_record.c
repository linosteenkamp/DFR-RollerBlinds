/**
 * @file rig_record.c
 * @brief The topic and the JSON text of a rig record (pure). The publish
 *        adapters use it. rig_ctl does not.
 */
#include "rig_record.h"

#include <stdio.h>

const char *rig_record_topic(rig_topic_t topic)
{
    switch (topic) {
    case RIG_TOPIC_MOVE:   return "rig/move";
    case RIG_TOPIC_STATUS: return "rig/status";
    case RIG_TOPIC_REPLY:  return "rig/reply";
    }
    return "rig/unknown";
}

const char *rig_state_name(rig_state_t state)
{
    switch (state) {
    case RIG_STATE_IDLE:   return "idle";
    case RIG_STATE_MOVING: return "moving";
    }
    return "unknown";
}

/* Copy s into out as the body of a JSON string. Returns false when out is
 * too small. */
static bool json_escape(const char *s, char *out, size_t len)
{
    size_t o = 0;
    for (; *s != '\0'; s++) {
        unsigned char c = (unsigned char)*s;
        char tmp[7];
        int n;
        if (c == '"' || c == '\\') n = snprintf(tmp, sizeof tmp, "\\%c", c);
        else if (c < 0x20)         n = snprintf(tmp, sizeof tmp, "\\u%04x", c);
        else                       n = snprintf(tmp, sizeof tmp, "%c", c);
        if (o + (size_t)n >= len) return false;
        for (int i = 0; i < n; i++) out[o++] = tmp[i];
    }
    out[o] = '\0';
    return true;
}

int rig_record_json(const rig_record_t *rec, char *buf, size_t len)
{
    int n = -1;
    switch (rec->topic) {
    case RIG_TOPIC_MOVE:
        n = snprintf(buf, len,
                     "{\"from\":%ld,\"target\":%ld,\"end\":%ld,"
                     "\"interval_us\":%lu,\"completed\":%s}",
                     (long)rec->move.from, (long)rec->move.target,
                     (long)rec->move.end, (unsigned long)rec->move.interval_us,
                     rec->move.completed ? "true" : "false");
        break;
    case RIG_TOPIC_STATUS:
        n = snprintf(buf, len, "{\"state\":\"%s\",\"position\":%ld}",
                     rig_state_name(rec->status.state),
                     (long)rec->status.position);
        break;
    case RIG_TOPIC_REPLY: {
        char text[RIG_MSG_LEN * 6];   /* each byte can become \u00XX */
        if (!json_escape(rec->reply.text, text, sizeof text)) break;
        n = snprintf(buf, len, "{\"ok\":%s,\"text\":\"%s\"}",
                     rec->reply.ok ? "true" : "false", text);
        break;
    }
    }
    if (n < 0 || (size_t)n >= len) {
        if (len > 0) buf[0] = '\0';
        return -1;
    }
    return n;
}
