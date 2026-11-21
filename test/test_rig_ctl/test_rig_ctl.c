#include <unity.h>
#include <string.h>
#include "../../src/rig_cmd.c"
#include "../../src/rig_record.c"
#include "../../src/rig_ctl.c"
#include "fake_rig_ports.h"

void setUp(void) {}
void tearDown(void) {}

#define HARD_CAP 400000

static rig_t R;
static const rig_config_t CFG = {
    .start_us        = 500,
    .accel_steps     = 800,
    .min_interval_us = 40,
    .max_interval_us = 1000,
    .hard_cap        = HARD_CAP,
};

static void boot(void)
{
    fake_reset();
    rig_init(&R, &FAKE_PORTS, &CFG);
    F.n = 0;
}

static void cmd_move(int32_t steps, uint32_t interval_us)
{
    rig_event_t e = { .type = RIG_EVT_CMD,
                      .cmd = { .type = RIG_CMD_MOVE, .steps = steps,
                               .interval_us = interval_us } };
    rig_handle(&R, &e);
}

static void cmd(rig_cmd_type_t t)
{
    rig_event_t e = { .type = RIG_EVT_CMD, .cmd = { .type = t } };
    rig_handle(&R, &e);
}

static void bad_cmd(const char *msg)
{
    rig_event_t e = { .type = RIG_EVT_BAD_CMD };
    strncpy(e.error, msg, sizeof e.error - 1);
    rig_handle(&R, &e);
}

static void done(int32_t steps, bool completed)
{
    rig_event_t e = { .type = RIG_EVT_MOTION_DONE,
                      .done = { .steps = steps, .completed = completed } };
    rig_handle(&R, &e);
}

static void assert_rejected(void)
{
    const frec_t *r = f_last_topic(RIG_TOPIC_REPLY);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_FALSE(r->rec.reply.ok);
    TEST_ASSERT_TRUE(strlen(r->rec.reply.text) > 0);
}

/* ======================= the command parser ======================= */

static rig_cmd_t P;
static char ERR[RIG_MSG_LEN];

static rig_parse_result_t parse(const char *line)
{
    memset(&P, 0xA5, sizeof P);
    memset(ERR, 0, sizeof ERR);
    return rig_cmd_parse(line, &P, ERR);
}

static void test_parse_move(void)
{
    TEST_ASSERT_EQUAL(RIG_PARSE_OK, parse("move 1000 150"));
    TEST_ASSERT_EQUAL(RIG_CMD_MOVE, P.type);
    TEST_ASSERT_EQUAL_INT32(1000, P.steps);
    TEST_ASSERT_EQUAL_UINT32(150, P.interval_us);
}

static void test_parse_move_negative_steps(void)
{
    TEST_ASSERT_EQUAL(RIG_PARSE_OK, parse("move -2400 300"));
    TEST_ASSERT_EQUAL_INT32(-2400, P.steps);
    TEST_ASSERT_EQUAL_UINT32(300, P.interval_us);
}

static void test_parse_ignores_extra_spaces_and_line_ends(void)
{
    TEST_ASSERT_EQUAL(RIG_PARSE_OK, parse("  move\t 24000   200 \r\n"));
    TEST_ASSERT_EQUAL_INT32(24000, P.steps);
    TEST_ASSERT_EQUAL_UINT32(200, P.interval_us);
}

static void test_parse_status_and_stop(void)
{
    TEST_ASSERT_EQUAL(RIG_PARSE_OK, parse("status"));
    TEST_ASSERT_EQUAL(RIG_CMD_STATUS, P.type);
    TEST_ASSERT_EQUAL(RIG_PARSE_OK, parse("stop\r"));
    TEST_ASSERT_EQUAL(RIG_CMD_STOP, P.type);
}

static void test_parse_empty_line(void)
{
    TEST_ASSERT_EQUAL(RIG_PARSE_EMPTY, parse(""));
    TEST_ASSERT_EQUAL(RIG_PARSE_EMPTY, parse("   \t\r\n"));
}

static void test_parse_unknown_command(void)
{
    TEST_ASSERT_EQUAL(RIG_PARSE_ERROR, parse("jump 3"));
    TEST_ASSERT_NOT_NULL(strstr(ERR, "unknown command"));
    TEST_ASSERT_NOT_NULL(strstr(ERR, "jump"));
}

static void test_parse_is_case_sensitive(void)
{
    TEST_ASSERT_EQUAL(RIG_PARSE_ERROR, parse("MOVE 1 200"));
    TEST_ASSERT_NOT_NULL(strstr(ERR, "unknown command"));
}

static void test_parse_move_wrong_argument_count(void)
{
    TEST_ASSERT_EQUAL(RIG_PARSE_ERROR, parse("move 100"));
    TEST_ASSERT_EQUAL_STRING("usage: move <steps> <interval_us>", ERR);
    TEST_ASSERT_EQUAL(RIG_PARSE_ERROR, parse("move"));
    TEST_ASSERT_EQUAL_STRING("usage: move <steps> <interval_us>", ERR);
    TEST_ASSERT_EQUAL(RIG_PARSE_ERROR, parse("move 100 150 7"));
    TEST_ASSERT_EQUAL_STRING("usage: move <steps> <interval_us>", ERR);
}

static void test_parse_move_steps_not_an_integer(void)
{
    TEST_ASSERT_EQUAL(RIG_PARSE_ERROR, parse("move abc 150"));
    TEST_ASSERT_NOT_NULL(strstr(ERR, "steps"));
    TEST_ASSERT_EQUAL(RIG_PARSE_ERROR, parse("move 12x 150"));
    TEST_ASSERT_NOT_NULL(strstr(ERR, "steps"));
}

static void test_parse_move_steps_out_of_int32(void)
{
    TEST_ASSERT_EQUAL(RIG_PARSE_ERROR, parse("move 99999999999 150"));
    TEST_ASSERT_NOT_NULL(strstr(ERR, "steps"));
    TEST_ASSERT_EQUAL(RIG_PARSE_ERROR, parse("move -99999999999 150"));
    TEST_ASSERT_NOT_NULL(strstr(ERR, "steps"));
}

static void test_parse_move_interval_not_a_positive_integer(void)
{
    TEST_ASSERT_EQUAL(RIG_PARSE_ERROR, parse("move 100 -5"));
    TEST_ASSERT_NOT_NULL(strstr(ERR, "interval_us"));
    TEST_ASSERT_EQUAL(RIG_PARSE_ERROR, parse("move 100 1.5"));
    TEST_ASSERT_NOT_NULL(strstr(ERR, "interval_us"));
    TEST_ASSERT_EQUAL(RIG_PARSE_ERROR, parse("move 100 0"));
    TEST_ASSERT_NOT_NULL(strstr(ERR, "interval_us"));
    TEST_ASSERT_EQUAL(RIG_PARSE_ERROR, parse("move 100 99999999999"));
    TEST_ASSERT_NOT_NULL(strstr(ERR, "interval_us"));
}

static void test_parse_status_and_stop_take_no_arguments(void)
{
    TEST_ASSERT_EQUAL(RIG_PARSE_ERROR, parse("status now"));
    TEST_ASSERT_EQUAL_STRING("usage: status", ERR);
    TEST_ASSERT_EQUAL(RIG_PARSE_ERROR, parse("stop 1"));
    TEST_ASSERT_EQUAL_STRING("usage: stop", ERR);
}

static void test_parse_long_line_is_safe(void)
{
    char line[300];
    memset(line, 'x', sizeof line - 1);
    line[sizeof line - 1] = '\0';
    TEST_ASSERT_EQUAL(RIG_PARSE_ERROR, parse(line));
    TEST_ASSERT_TRUE(strlen(ERR) < RIG_MSG_LEN);
}

/* A valid command, with spaces after it to a length past the buffer,
 * must not parse as the command at the start of the line. */
static void test_parse_rejects_a_line_that_is_too_long(void)
{
    char line[200];
    memset(line, ' ', sizeof line - 1);
    line[sizeof line - 1] = '\0';
    memcpy(line, "move 100 150", 12);
    memcpy(line + sizeof line - 6, "extra", 5);
    TEST_ASSERT_EQUAL(RIG_PARSE_ERROR, parse(line));
    TEST_ASSERT_EQUAL_STRING("the command is too long", ERR);
}

/* ======================= console line assembly ======================= */

static rig_line_t L;

/* Feed the bytes of s. Returns the number of completed lines. */
static int feed(const char *s)
{
    int done = 0;
    for (; *s; s++) if (rig_line_add(&L, *s)) done++;
    return done;
}

static void test_line_ends_at_cr_or_lf(void)
{
    rig_line_reset(&L);
    TEST_ASSERT_EQUAL_INT(1, feed("move 1 2\r"));
    TEST_ASSERT_EQUAL_STRING("move 1 2", L.buf);
    rig_line_reset(&L);
    TEST_ASSERT_EQUAL_INT(1, feed("status\n"));
    TEST_ASSERT_EQUAL_STRING("status", L.buf);
}

static void test_line_backspace_and_del_remove_the_last_byte(void)
{
    rig_line_reset(&L);
    TEST_ASSERT_EQUAL_INT(1, feed("move 24000 1\x7f" "100\n"));
    TEST_ASSERT_EQUAL_STRING("move 24000 100", L.buf);
    rig_line_reset(&L);
    TEST_ASSERT_EQUAL_INT(1, feed("mvo\b\bove 1 2\n"));
    TEST_ASSERT_EQUAL_STRING("move 1 2", L.buf);
}

static void test_line_backspace_on_an_empty_line_does_nothing(void)
{
    rig_line_reset(&L);
    TEST_ASSERT_EQUAL_INT(1, feed("\b\x7fstop\n"));
    TEST_ASSERT_EQUAL_STRING("stop", L.buf);
}

static void test_line_starts_again_after_each_end(void)
{
    rig_line_reset(&L);
    TEST_ASSERT_EQUAL_INT(1, feed("stop\n"));
    TEST_ASSERT_EQUAL_INT(1, feed("status\n"));
    TEST_ASSERT_EQUAL_STRING("status", L.buf);
}

/* A full line keeps its first bytes and drops the rest, so that the
 * parser rejects it as too long. Backspace still works. */
static void test_line_that_is_too_long_stays_too_long(void)
{
    rig_line_reset(&L);
    for (int i = 0; i < 300; i++) rig_line_add(&L, 'x');
    TEST_ASSERT_TRUE(rig_line_add(&L, '\n'));
    TEST_ASSERT_EQUAL_size_t(RIG_LINE_LEN - 1, strlen(L.buf));
    TEST_ASSERT_EQUAL(RIG_PARSE_ERROR, parse(L.buf));
    TEST_ASSERT_EQUAL_STRING("the command is too long", ERR);
}

/* ======================= rig_ctl ======================= */

static void test_boot_is_idle_at_zero(void)
{
    boot();
    TEST_ASSERT_EQUAL(RIG_STATE_IDLE, R.state);
    cmd(RIG_CMD_STATUS);
    const frec_t *s = f_last_topic(RIG_TOPIC_STATUS);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL(RIG_STATE_IDLE, s->rec.status.state);
    TEST_ASSERT_EQUAL_INT32(0, s->rec.status.position);
}

static void test_move_starts_motion_with_the_blind_ramp(void)
{
    boot();
    cmd_move(1000, 150);
    const frec_t *m = f_last(F_MOTION_START);
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_EQUAL_INT32(0, m->from);
    TEST_ASSERT_EQUAL_INT32(1000, m->to);
    TEST_ASSERT_EQUAL_UINT32(150, m->prof.cruise_us);
    TEST_ASSERT_EQUAL_UINT32(500, m->prof.start_us);
    TEST_ASSERT_EQUAL_INT32(800, m->prof.accel_steps);
    TEST_ASSERT_EQUAL_INT32(HARD_CAP, m->hard_cap);
    TEST_ASSERT_EQUAL(RIG_STATE_MOVING, R.state);
}

static void test_move_publishes_moving_status_retained(void)
{
    boot();
    cmd_move(-500, 300);
    const frec_t *s = f_last_topic(RIG_TOPIC_STATUS);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL(RIG_STATE_MOVING, s->rec.status.state);
    TEST_ASSERT_EQUAL_UINT8(1, s->qos);
    TEST_ASSERT_TRUE(s->retain);
    TEST_ASSERT_EQUAL_INT32(-500, f_last(F_MOTION_START)->to);
}

static void test_motion_done_publishes_move_then_idle_status(void)
{
    boot();
    cmd_move(1000, 150);
    F.n = 0;
    done(1000, true);
    TEST_ASSERT_EQUAL(RIG_STATE_IDLE, R.state);
    TEST_ASSERT_EQUAL(2, f_count(F_PUBLISH));
    TEST_ASSERT_EQUAL(RIG_TOPIC_MOVE, F.rec[0].rec.topic);
    TEST_ASSERT_EQUAL_UINT8(0, F.rec[0].qos);
    TEST_ASSERT_FALSE(F.rec[0].retain);
    TEST_ASSERT_EQUAL_INT32(0, F.rec[0].rec.move.from);
    TEST_ASSERT_EQUAL_INT32(1000, F.rec[0].rec.move.target);
    TEST_ASSERT_EQUAL_INT32(1000, F.rec[0].rec.move.end);
    TEST_ASSERT_EQUAL_UINT32(150, F.rec[0].rec.move.interval_us);
    TEST_ASSERT_TRUE(F.rec[0].rec.move.completed);
    TEST_ASSERT_EQUAL(RIG_TOPIC_STATUS, F.rec[1].rec.topic);
    TEST_ASSERT_EQUAL(RIG_STATE_IDLE, F.rec[1].rec.status.state);
    TEST_ASSERT_EQUAL_INT32(1000, F.rec[1].rec.status.position);
}

static void test_next_move_starts_from_the_final_position(void)
{
    boot();
    cmd_move(1000, 150);
    done(1000, true);
    cmd_move(-300, 200);
    const frec_t *m = f_last(F_MOTION_START);
    TEST_ASSERT_EQUAL_INT32(1000, m->from);
    TEST_ASSERT_EQUAL_INT32(700, m->to);
}

static void test_move_while_moving_is_rejected(void)
{
    boot();
    cmd_move(1000, 150);
    F.n = 0;
    cmd_move(500, 150);
    TEST_ASSERT_EQUAL(0, f_count(F_MOTION_START));
    assert_rejected();
    TEST_ASSERT_EQUAL(RIG_STATE_MOVING, R.state);
}

static void test_move_interval_limits(void)
{
    boot();
    cmd_move(100, 39);
    TEST_ASSERT_EQUAL(0, f_count(F_MOTION_START));
    assert_rejected();
    cmd_move(100, 1001);
    TEST_ASSERT_EQUAL(0, f_count(F_MOTION_START));
    assert_rejected();

    cmd_move(100, 40);
    TEST_ASSERT_EQUAL(1, f_count(F_MOTION_START));
    done(100, true);
    cmd_move(100, 1000);
    TEST_ASSERT_EQUAL(2, f_count(F_MOTION_START));
}

static void test_move_of_zero_steps_is_rejected(void)
{
    boot();
    cmd_move(0, 150);
    TEST_ASSERT_EQUAL(0, f_count(F_MOTION_START));
    assert_rejected();
    TEST_ASSERT_EQUAL(RIG_STATE_IDLE, R.state);
}

static void test_move_longer_than_the_hard_cap_is_rejected(void)
{
    boot();
    cmd_move(HARD_CAP + 1, 150);
    cmd_move(-(HARD_CAP + 1), 150);
    TEST_ASSERT_EQUAL(0, f_count(F_MOTION_START));
    assert_rejected();
    cmd_move(-HARD_CAP, 150);
    TEST_ASSERT_EQUAL(1, f_count(F_MOTION_START));
}

static void test_move_start_failure_stays_idle(void)
{
    boot();
    F.start_err = 0x103;   /* ESP_ERR_INVALID_STATE */
    cmd_move(1000, 150);
    TEST_ASSERT_EQUAL(1, f_count(F_MOTION_START));
    TEST_ASSERT_EQUAL(RIG_STATE_IDLE, R.state);
    TEST_ASSERT_EQUAL(0, f_count_topic(RIG_TOPIC_STATUS));
    assert_rejected();

    F.start_err = RIG_OK;
    cmd_move(1000, 150);
    TEST_ASSERT_EQUAL(RIG_STATE_MOVING, R.state);
    TEST_ASSERT_EQUAL_INT32(0, f_last(F_MOTION_START)->from);
}

static void test_status_while_moving_shows_the_live_position(void)
{
    boot();
    cmd_move(24000, 150);
    F.steps = 6123;
    F.n = 0;
    cmd(RIG_CMD_STATUS);
    TEST_ASSERT_EQUAL(1, f_count(F_PUBLISH));
    const frec_t *s = f_last_topic(RIG_TOPIC_STATUS);
    TEST_ASSERT_EQUAL(RIG_STATE_MOVING, s->rec.status.state);
    TEST_ASSERT_EQUAL_INT32(6123, s->rec.status.position);
}

static void test_stop_while_moving_requests_one_stop(void)
{
    boot();
    cmd_move(24000, 150);
    F.n = 0;
    cmd(RIG_CMD_STOP);
    cmd(RIG_CMD_STOP);
    TEST_ASSERT_EQUAL(1, f_count(F_MOTION_STOP));
    TEST_ASSERT_TRUE(f_last_topic(RIG_TOPIC_REPLY)->rec.reply.ok);
    TEST_ASSERT_EQUAL(RIG_STATE_MOVING, R.state);   /* until MOTION_DONE */

    done(9000, false);
    TEST_ASSERT_EQUAL(RIG_STATE_IDLE, R.state);
    const frec_t *m = f_last_topic(RIG_TOPIC_MOVE);
    TEST_ASSERT_FALSE(m->rec.move.completed);
    TEST_ASSERT_EQUAL_INT32(9000, m->rec.move.end);
    TEST_ASSERT_EQUAL_INT32(24000, m->rec.move.target);

    /* The stop latch is for one move only. */
    cmd_move(-1000, 150);
    cmd(RIG_CMD_STOP);
    TEST_ASSERT_EQUAL(2, f_count(F_MOTION_STOP));
}

static void test_stop_while_idle_does_nothing(void)
{
    boot();
    cmd(RIG_CMD_STOP);
    TEST_ASSERT_EQUAL(0, f_count(F_MOTION_STOP));
    TEST_ASSERT_TRUE(f_last_topic(RIG_TOPIC_REPLY)->rec.reply.ok);
}

static void test_bad_command_publishes_the_parser_message(void)
{
    boot();
    bad_cmd("usage: move <steps> <interval_us>");
    const frec_t *r = f_last_topic(RIG_TOPIC_REPLY);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_FALSE(r->rec.reply.ok);
    TEST_ASSERT_EQUAL_STRING("usage: move <steps> <interval_us>", r->rec.reply.text);
    TEST_ASSERT_EQUAL(0, f_count(F_MOTION_START));
}

static void test_publish_failure_does_not_change_a_decision(void)
{
    boot();
    F.publish_ok = false;
    cmd_move(1000, 150);
    TEST_ASSERT_EQUAL(RIG_STATE_MOVING, R.state);
    done(1000, true);
    TEST_ASSERT_EQUAL(RIG_STATE_IDLE, R.state);
    cmd_move(-1000, 150);
    TEST_ASSERT_EQUAL(2, f_count(F_MOTION_START));
    TEST_ASSERT_EQUAL_INT32(1000, f_last(F_MOTION_START)->from);
}

static void test_motion_done_when_idle_is_ignored(void)
{
    boot();
    done(5000, true);
    TEST_ASSERT_EQUAL(0, f_count(F_PUBLISH));
    cmd_move(100, 150);
    TEST_ASSERT_EQUAL_INT32(0, f_last(F_MOTION_START)->from);
}

static void test_move_that_overflows_the_position_is_rejected(void)
{
    boot();
    R.pos = INT32_MAX - 10;
    cmd_move(100, 150);
    TEST_ASSERT_EQUAL(0, f_count(F_MOTION_START));
    assert_rejected();
}

/* ======================= rig_record ======================= */

static char J[256];

static void test_record_topics(void)
{
    TEST_ASSERT_EQUAL_STRING("rig/move",   rig_record_topic(RIG_TOPIC_MOVE));
    TEST_ASSERT_EQUAL_STRING("rig/status", rig_record_topic(RIG_TOPIC_STATUS));
    TEST_ASSERT_EQUAL_STRING("rig/reply",  rig_record_topic(RIG_TOPIC_REPLY));
}

static void test_record_move_json(void)
{
    rig_record_t r = { .topic = RIG_TOPIC_MOVE,
                       .move = { .from = 0, .target = -1000, .end = -998,
                                 .interval_us = 150, .completed = false } };
    TEST_ASSERT_TRUE(rig_record_json(&r, J, sizeof J) > 0);
    TEST_ASSERT_EQUAL_STRING(
        "{\"from\":0,\"target\":-1000,\"end\":-998,\"interval_us\":150,\"completed\":false}", J);
}

static void test_record_status_json(void)
{
    rig_record_t r = { .topic = RIG_TOPIC_STATUS,
                       .status = { .state = RIG_STATE_MOVING, .position = 6123 } };
    TEST_ASSERT_TRUE(rig_record_json(&r, J, sizeof J) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"state\":\"moving\",\"position\":6123}", J);
}

static void test_record_reply_json_escapes_the_text(void)
{
    rig_record_t r = { .topic = RIG_TOPIC_REPLY, .reply = { .ok = false } };
    strcpy(r.reply.text, "unknown command 'a\"b\\c'");
    TEST_ASSERT_TRUE(rig_record_json(&r, J, sizeof J) > 0);
    TEST_ASSERT_EQUAL_STRING(
        "{\"ok\":false,\"text\":\"unknown command 'a\\\"b\\\\c'\"}", J);
}

static void test_record_json_buffer_too_small(void)
{
    rig_record_t r = { .topic = RIG_TOPIC_STATUS,
                       .status = { .state = RIG_STATE_IDLE, .position = 0 } };
    char small[10];
    TEST_ASSERT_EQUAL_INT(-1, rig_record_json(&r, small, sizeof small));
    TEST_ASSERT_EQUAL_CHAR('\0', small[0]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_move);
    RUN_TEST(test_parse_move_negative_steps);
    RUN_TEST(test_parse_ignores_extra_spaces_and_line_ends);
    RUN_TEST(test_parse_status_and_stop);
    RUN_TEST(test_parse_empty_line);
    RUN_TEST(test_parse_unknown_command);
    RUN_TEST(test_parse_is_case_sensitive);
    RUN_TEST(test_parse_move_wrong_argument_count);
    RUN_TEST(test_parse_move_steps_not_an_integer);
    RUN_TEST(test_parse_move_steps_out_of_int32);
    RUN_TEST(test_parse_move_interval_not_a_positive_integer);
    RUN_TEST(test_parse_status_and_stop_take_no_arguments);
    RUN_TEST(test_parse_long_line_is_safe);
    RUN_TEST(test_parse_rejects_a_line_that_is_too_long);
    RUN_TEST(test_line_ends_at_cr_or_lf);
    RUN_TEST(test_line_backspace_and_del_remove_the_last_byte);
    RUN_TEST(test_line_backspace_on_an_empty_line_does_nothing);
    RUN_TEST(test_line_starts_again_after_each_end);
    RUN_TEST(test_line_that_is_too_long_stays_too_long);

    RUN_TEST(test_boot_is_idle_at_zero);
    RUN_TEST(test_move_starts_motion_with_the_blind_ramp);
    RUN_TEST(test_move_publishes_moving_status_retained);
    RUN_TEST(test_motion_done_publishes_move_then_idle_status);
    RUN_TEST(test_next_move_starts_from_the_final_position);
    RUN_TEST(test_move_while_moving_is_rejected);
    RUN_TEST(test_move_interval_limits);
    RUN_TEST(test_move_of_zero_steps_is_rejected);
    RUN_TEST(test_move_longer_than_the_hard_cap_is_rejected);
    RUN_TEST(test_move_start_failure_stays_idle);
    RUN_TEST(test_status_while_moving_shows_the_live_position);
    RUN_TEST(test_stop_while_moving_requests_one_stop);
    RUN_TEST(test_stop_while_idle_does_nothing);
    RUN_TEST(test_bad_command_publishes_the_parser_message);
    RUN_TEST(test_publish_failure_does_not_change_a_decision);
    RUN_TEST(test_motion_done_when_idle_is_ignored);
    RUN_TEST(test_move_that_overflows_the_position_is_rejected);

    RUN_TEST(test_record_topics);
    RUN_TEST(test_record_move_json);
    RUN_TEST(test_record_status_json);
    RUN_TEST(test_record_reply_json_escapes_the_text);
    RUN_TEST(test_record_json_buffer_too_small);
    return UNITY_END();
}
