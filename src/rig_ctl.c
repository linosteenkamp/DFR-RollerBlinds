/**
 * @file rig_ctl.c
 * @brief The dispatcher core of the test rig. All the decisions of the rig
 *        occur here. Refer to rig_ctl.h for the concurrency rule.
 */
#include "rig_ctl.h"

#include <stdio.h>
#include <string.h>

/* QoS and retain for each topic (refer to the MQTT topics in the spec). */
#define QOS_MOVE   0
#define QOS_STATUS 1
#define QOS_REPLY  1

static void reply(rig_t *r, bool ok, const char *text)
{
    rig_record_t rec = { .topic = RIG_TOPIC_REPLY, .reply = { .ok = ok } };
    snprintf(rec.reply.text, sizeof rec.reply.text, "%s", text);
    (void)r->io->publish(&rec, QOS_REPLY, false);
}

static void publish_status(rig_t *r)
{
    int32_t pos = (r->state == RIG_STATE_MOVING) ? r->io->motion_steps() : r->pos;
    rig_record_t rec = { .topic = RIG_TOPIC_STATUS,
                         .status = { .state = r->state, .position = pos } };
    (void)r->io->publish(&rec, QOS_STATUS, true);
}

static void handle_move(rig_t *r, const rig_cmd_t *c)
{
    char msg[RIG_MSG_LEN];
    if (r->state != RIG_STATE_IDLE) {
        reply(r, false, "move: busy, a move is not complete");
        return;
    }
    if (c->interval_us < r->cfg.min_interval_us ||
        c->interval_us > r->cfg.max_interval_us) {
        snprintf(msg, sizeof msg, "move: interval_us must be %lu..%lu",
                 (unsigned long)r->cfg.min_interval_us,
                 (unsigned long)r->cfg.max_interval_us);
        reply(r, false, msg);
        return;
    }
    int64_t dist = c->steps < 0 ? -(int64_t)c->steps : (int64_t)c->steps;
    if (dist == 0 || dist > r->cfg.hard_cap) {
        snprintf(msg, sizeof msg, "move: steps must be 1..%ld (+ or -)",
                 (long)r->cfg.hard_cap);
        reply(r, false, msg);
        return;
    }
    int64_t target = (int64_t)r->pos + c->steps;
    if (target < INT32_MIN || target > INT32_MAX) {
        reply(r, false, "move: the target is out of range");
        return;
    }

    motion_profile_t prof = { .cruise_us   = c->interval_us,
                              .start_us    = r->cfg.start_us,
                              .accel_steps = r->cfg.accel_steps };
    rig_err_t err = r->io->motion_start(r->pos, (int32_t)target, &prof,
                                        r->cfg.hard_cap);
    if (err != RIG_OK) {
        snprintf(msg, sizeof msg, "move: motion_start failed (0x%x)", (unsigned)err);
        reply(r, false, msg);
        return;
    }
    r->state            = RIG_STATE_MOVING;
    r->move_from        = r->pos;
    r->move_target      = (int32_t)target;
    r->move_interval_us = c->interval_us;
    r->stop_sent        = false;
    publish_status(r);
}

static void handle_stop(rig_t *r)
{
    if (r->state != RIG_STATE_MOVING) {
        reply(r, true, "stop: not moving");
        return;
    }
    if (!r->stop_sent) {
        r->io->motion_stop();
        r->stop_sent = true;
    }
    reply(r, true, "stop: the motor decelerates");
}

static void handle_motion_done(rig_t *r, int32_t steps, bool completed)
{
    /* Only a move that rig_ctl started can end. Ignore an item that
     * comes when no move is in progress. */
    if (r->state != RIG_STATE_MOVING) return;
    r->pos   = steps;
    r->state = RIG_STATE_IDLE;
    rig_record_t rec = { .topic = RIG_TOPIC_MOVE,
                         .move = { .from = r->move_from, .target = r->move_target,
                                   .end = steps, .interval_us = r->move_interval_us,
                                   .completed = completed } };
    (void)r->io->publish(&rec, QOS_MOVE, false);
    publish_status(r);
}

void rig_init(rig_t *r, const rig_ports_t *io, const rig_config_t *cfg)
{
    memset(r, 0, sizeof *r);
    r->io    = io;
    r->cfg   = *cfg;
    r->state = RIG_STATE_IDLE;
}

void rig_handle(rig_t *r, const rig_event_t *ev)
{
    switch (ev->type) {
    case RIG_EVT_CMD:
        switch (ev->cmd.type) {
        case RIG_CMD_MOVE:   handle_move(r, &ev->cmd); break;
        case RIG_CMD_STOP:   handle_stop(r);           break;
        case RIG_CMD_STATUS: publish_status(r);        break;
        }
        break;
    case RIG_EVT_BAD_CMD:
        reply(r, false, ev->error);
        break;
    case RIG_EVT_MOTION_DONE:
        handle_motion_done(r, ev->done.steps, ev->done.completed);
        break;
    }
}
