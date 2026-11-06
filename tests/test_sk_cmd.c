#include <string.h>
#include "unity.h"
#include "sk_cmd.h"

void setUp(void) {
  sk_cmd_init();
  sk_cmd_set_connected(true);
}
void tearDown(void) {}

static sk_cmd_t post(const char *path) {
  sk_cmd_t c;
  memset(&c, 0, sizeof c);
  snprintf(c.path, sizeof c.path, "%s", path);
  return c;
}
static sk_cmd_t put(const char *path, double v) {
  sk_cmd_t c = post(path);
  c.put_number = true;
  c.value = v;
  return c;
}
static sk_cmd_handle_t submit(sk_cmd_t c, bool replaces, int64_t t) { return sk_cmd_submit(&c, replaces, t); }

static void test_refused_while_disconnected_and_not_queued_later(void) {
  sk_cmd_set_connected(false);
  TEST_ASSERT_EQUAL_UINT16(0, submit(post("/p/drop"), false, 0));
  sk_cmd_set_connected(true);
  sk_cmd_t out;
  sk_cmd_handle_t h;
  TEST_ASSERT_FALSE(sk_cmd_next(&out, &h, 10));
}

static void test_disconnect_fails_queued_commands_and_they_never_go_out(void) {
  sk_cmd_handle_t h1 = submit(post("/p/drop"), false, 0);
  TEST_ASSERT_NOT_EQUAL(0, h1);
  sk_cmd_set_connected(false);
  sk_cmd_set_connected(true);
  sk_cmd_t out;
  sk_cmd_handle_t h;
  TEST_ASSERT_FALSE(sk_cmd_next(&out, &h, 10));
  TEST_ASSERT_EQUAL(SKC_FAILED, sk_cmd_result(h1).state);
  TEST_ASSERT_EQUAL(SKC_ERR_OFFLINE, sk_cmd_result(h1).err);
}

static void test_expires_after_5s(void) {
  sk_cmd_handle_t h1 = submit(post("/p/drop"), false, 1000), h2 = submit(post("/p/raise"), false, 1000);
  sk_cmd_t out;
  sk_cmd_handle_t h;
  TEST_ASSERT_TRUE(sk_cmd_next(&out, &h, 5999));   // 4999 ms old: still good
  TEST_ASSERT_EQUAL_UINT16(h1, h);
  TEST_ASSERT_FALSE(sk_cmd_next(&out, &h, 6001));  // 5001 ms old: dropped
  TEST_ASSERT_EQUAL(SKC_FAILED, sk_cmd_result(h2).state);
  TEST_ASSERT_EQUAL(SKC_ERR_OFFLINE, sk_cmd_result(h2).err);
}

static void test_short_deadline_expires_at_3s(void) {
  sk_cmd_t c = post("/p/mob");
  c.expire_ms = 3000;
  sk_cmd_handle_t h1 = submit(c, false, 1000);
  sk_cmd_t out;
  sk_cmd_handle_t h;
  TEST_ASSERT_FALSE(sk_cmd_next(&out, &h, 4001));   // 3001 ms old
  TEST_ASSERT_EQUAL(SKC_FAILED, sk_cmd_result(h1).state);
  TEST_ASSERT_EQUAL(SKC_ERR_OFFLINE, sk_cmd_result(h1).err);
}

static void test_short_deadline_still_good_just_inside_it(void) {
  sk_cmd_t c = post("/p/mob");
  c.expire_ms = 3000;
  sk_cmd_handle_t h1 = submit(c, false, 1000);
  sk_cmd_t out;
  sk_cmd_handle_t h;
  TEST_ASSERT_TRUE(sk_cmd_next(&out, &h, 3999));   // 2999 ms old
  TEST_ASSERT_EQUAL_UINT16(h1, h);
}

static void test_zero_deadline_is_the_default_5s(void) {
  sk_cmd_handle_t h1 = submit(post("/p/drop"), false, 1000);
  sk_cmd_t out;
  sk_cmd_handle_t h;
  TEST_ASSERT_TRUE(sk_cmd_next(&out, &h, 5999));
  TEST_ASSERT_EQUAL_UINT16(h1, h);
}

static void test_long_body_passes_through(void) {
  sk_cmd_t c = post("/p/mob");
  memset(c.body, 'x', 200);
  c.body[200] = 0;
  submit(c, false, 0);
  sk_cmd_t out;
  sk_cmd_handle_t h;
  TEST_ASSERT_TRUE(sk_cmd_next(&out, &h, 10));
  TEST_ASSERT_EQUAL(200, strlen(out.body));
  TEST_ASSERT_EQUAL_STRING(c.body, out.body);
}

static void test_queue_holds_4_evicts_oldest_radius_never_drop_or_raise(void) {
  sk_cmd_handle_t radius = submit(put("navigation.anchor.maxRadius", 30), true, 0);
  sk_cmd_handle_t drop = submit(post("/p/drop"), false, 1);
  sk_cmd_handle_t raise = submit(post("/p/raise"), false, 2);
  TEST_ASSERT_NOT_EQUAL(0, submit(post("/p/other"), false, 3));
  // Full. A new radius command (a different path) pushes out the oldest radius command.
  sk_cmd_handle_t r2 = submit(post("/p/setZone"), true, 4);
  TEST_ASSERT_NOT_EQUAL(0, r2);
  TEST_ASSERT_EQUAL(SKC_FAILED, sk_cmd_result(radius).state);
  TEST_ASSERT_EQUAL(SKC_PENDING, sk_cmd_result(drop).state);
  TEST_ASSERT_EQUAL(SKC_PENDING, sk_cmd_result(raise).state);
  // Full again, and the only radius entry is the newcomer's equal: a drop or raise is refused, never evicting.
  TEST_ASSERT_NOT_EQUAL(0, submit(post("/p/drop2"), false, 5));   // evicts r2 (the only radius entry)
  TEST_ASSERT_EQUAL(SKC_FAILED, sk_cmd_result(r2).state);
  TEST_ASSERT_EQUAL_UINT16(0, submit(post("/p/raise2"), false, 6));   // nothing left to evict
  TEST_ASSERT_EQUAL(SKC_PENDING, sk_cmd_result(drop).state);
  TEST_ASSERT_EQUAL(SKC_PENDING, sk_cmd_result(raise).state);
}

static void test_radius_replaces_pending_radius(void) {
  sk_cmd_handle_t a = submit(put("navigation.anchor.maxRadius", 30), true, 0);
  sk_cmd_handle_t b = submit(put("navigation.anchor.maxRadius", 40), true, 100);
  TEST_ASSERT_NOT_EQUAL(0, b);
  sk_cmd_t out;
  sk_cmd_handle_t h;
  TEST_ASSERT_TRUE(sk_cmd_next(&out, &h, 200));
  TEST_ASSERT_EQUAL_DOUBLE(40, out.value);
  TEST_ASSERT_TRUE(out.put_number);
  TEST_ASSERT_FALSE(sk_cmd_next(&out, &h, 200));   // only one went out
  (void)a;
}

static void test_duplicate_drop_ignored(void) {
  TEST_ASSERT_NOT_EQUAL(0, submit(post("/p/drop"), false, 0));
  TEST_ASSERT_EQUAL_UINT16(0, submit(post("/p/drop"), false, 10));
  sk_cmd_t out;
  sk_cmd_handle_t h;
  TEST_ASSERT_TRUE(sk_cmd_next(&out, &h, 20));
  TEST_ASSERT_FALSE(sk_cmd_next(&out, &h, 20));
}

static sk_cmd_result_t run_one(bool transport_ok, int status) {
  sk_cmd_handle_t h = submit(post("/p/x"), false, 0);
  sk_cmd_t out;
  sk_cmd_handle_t got;
  TEST_ASSERT_TRUE(sk_cmd_next(&out, &got, 1));
  TEST_ASSERT_EQUAL_UINT16(h, got);
  TEST_ASSERT_EQUAL(SKC_PENDING, sk_cmd_result(h).state);   // sent, no answer yet
  sk_cmd_finish(h, transport_ok, status);
  return sk_cmd_result(h);
}

static void test_status_mapping(void) {
  TEST_ASSERT_EQUAL(SKC_OK, run_one(true, 200).state);
  TEST_ASSERT_EQUAL(SKC_OK, run_one(true, 204).state);
  TEST_ASSERT_EQUAL(SKC_ERR_NO_ACCESS, run_one(true, 401).err);
  TEST_ASSERT_EQUAL(SKC_ERR_NO_ACCESS, run_one(true, 403).err);
  TEST_ASSERT_EQUAL(SKC_ERR_NO_PLUGIN, run_one(true, 404).err);
  TEST_ASSERT_EQUAL(SKC_ERR_REJECTED, run_one(true, 400).err);
  TEST_ASSERT_EQUAL(SKC_ERR_REJECTED, run_one(true, 500).err);
  TEST_ASSERT_EQUAL(SKC_ERR_OFFLINE, run_one(false, 0).err);
  TEST_ASSERT_EQUAL(SKC_FAILED, run_one(false, 0).state);
}

static void test_failed_command_is_not_offered_again(void) {
  run_one(true, 500);
  sk_cmd_t out;
  sk_cmd_handle_t h;
  TEST_ASSERT_FALSE(sk_cmd_next(&out, &h, 2));
}

static void test_unknown_handle_is_failed_none(void) {
  sk_cmd_result_t r = sk_cmd_result(4242);
  TEST_ASSERT_EQUAL(SKC_FAILED, r.state);
  TEST_ASSERT_EQUAL(SKC_ERR_NONE, r.err);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_refused_while_disconnected_and_not_queued_later);
  RUN_TEST(test_disconnect_fails_queued_commands_and_they_never_go_out);
  RUN_TEST(test_expires_after_5s);
  RUN_TEST(test_short_deadline_expires_at_3s);
  RUN_TEST(test_short_deadline_still_good_just_inside_it);
  RUN_TEST(test_zero_deadline_is_the_default_5s);
  RUN_TEST(test_long_body_passes_through);
  RUN_TEST(test_queue_holds_4_evicts_oldest_radius_never_drop_or_raise);
  RUN_TEST(test_radius_replaces_pending_radius);
  RUN_TEST(test_duplicate_drop_ignored);
  RUN_TEST(test_status_mapping);
  RUN_TEST(test_failed_command_is_not_offered_again);
  RUN_TEST(test_unknown_handle_is_failed_none);
  return UNITY_END();
}
