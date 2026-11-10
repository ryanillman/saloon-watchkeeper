// Signal K device access request, against a scripted fake HTTP transport.
#include <stdlib.h>
#include <string.h>
#include "sk_access.h"
#include "sk_transport.h"
#include "unity.h"

// ---- fake transport: each call takes the next scripted reply ----
typedef struct { bool ok; int status; const char *body; } reply_t;
static reply_t script[8];
static int n_script, calls;
static char last_method[8], last_url[256], last_body[512];

bool sk_http_request(const char *method, const char *url, const char *token, const char *body,
                     int *status, char **resp, size_t *resp_len) {
  (void)token;
  snprintf(last_method, sizeof last_method, "%s", method);
  snprintf(last_url, sizeof last_url, "%s", url);
  snprintf(last_body, sizeof last_body, "%s", body ? body : "");
  reply_t r = calls < n_script ? script[calls] : (reply_t){false, 0, NULL};
  calls++;
  *status = r.status;
  *resp = r.body ? strdup(r.body) : NULL;
  *resp_len = r.body ? strlen(r.body) : 0;
  return r.ok;
}
// Unused by sk_access; the core library links them.
bool sk_http_get(const char *u, const char *t, char **b, size_t *l) { (void)u; (void)t; (void)b; (void)l; return false; }
bool sk_http_post(const char *u, const char *t, const char *b, int *s) { (void)u; (void)t; (void)b; (void)s; return false; }
bool sk_ws_connect(const char *u, const char *t, sk_msg_cb c, void *x) { (void)u; (void)t; (void)c; (void)x; return false; }
bool sk_ws_send(const char *t) { (void)t; return false; }
bool sk_ws_poll(int ms) { (void)ms; return false; }
bool sk_ws_take_overflow(void) { return false; }
void sk_ws_close(void) {}
bool sk_resolve_auto(char *h, size_t n, unsigned *p) { (void)h; (void)n; (void)p; return false; }

static char token_seen[128], href_seen[128];
static int href_calls;
static void on_token(const char *t) { snprintf(token_seen, sizeof token_seen, "%s", t); }
static void on_href(const char *h) {
  snprintf(href_seen, sizeof href_seen, "%s", h);
  href_calls++;
}

static const char *BASE = "http://boat:3000";
static const char *PENDING =
  "{\"state\":\"PENDING\",\"requestId\":\"r1\",\"statusCode\":202,\"href\":\"/signalk/v1/requests/r1\"}";

void setUp(void) {
  sk_access_reset();
  sk_access_set_identity("c0ffee00-0000-4000-8000-000000000001", "Saloon Watchkeeper lcd7b 2a:10");
  sk_access_on_token(on_token);
  sk_access_on_href(on_href);
  n_script = calls = href_calls = 0;
  token_seen[0] = href_seen[0] = 0;
}
void tearDown(void) {}

static void test_request_then_approved_saves_token(void) {
  script[0] = (reply_t){true, 202, PENDING};
  script[1] = (reply_t){true, 200, PENDING};
  script[2] = (reply_t){true, 200,
                        "{\"state\":\"COMPLETED\",\"statusCode\":200,"
                        "\"accessRequest\":{\"permission\":\"APPROVED\",\"token\":\"tok123\"}}"};
  n_script = 3;
  sk_access_start();
  sk_access_poll(BASE, 0);
  TEST_ASSERT_EQUAL_STRING("POST", last_method);
  TEST_ASSERT_EQUAL_STRING("http://boat:3000/signalk/v1/access/requests", last_url);
  TEST_ASSERT_NOT_NULL(strstr(last_body, "\"permissions\":\"readwrite\""));
  TEST_ASSERT_NOT_NULL(strstr(last_body, "Saloon Watchkeeper lcd7b 2a:10"));
  TEST_ASSERT_EQUAL(SKA_PENDING, sk_access_state(NULL));
  TEST_ASSERT_EQUAL_STRING("/signalk/v1/requests/r1", href_seen);   // stored, to survive a restart
  sk_access_poll(BASE, 1000);
  TEST_ASSERT_EQUAL_INT(1, calls);   // not due yet: polls every 5 s
  sk_access_poll(BASE, 5000);
  TEST_ASSERT_EQUAL_STRING("GET", last_method);
  TEST_ASSERT_EQUAL_STRING("http://boat:3000/signalk/v1/requests/r1", last_url);
  TEST_ASSERT_EQUAL(SKA_PENDING, sk_access_state(NULL));
  sk_access_poll(BASE, 10000);
  TEST_ASSERT_EQUAL(SKA_APPROVED, sk_access_state(NULL));
  TEST_ASSERT_EQUAL_STRING("tok123", token_seen);
  TEST_ASSERT_EQUAL_STRING("", href_seen);   // finished: nothing to resume
}

static void test_denied(void) {
  script[0] = (reply_t){true, 202, PENDING};
  script[1] = (reply_t){true, 200, "{\"state\":\"COMPLETED\",\"accessRequest\":{\"permission\":\"DENIED\"}}"};
  n_script = 2;
  sk_access_start();
  sk_access_poll(BASE, 0);
  sk_access_poll(BASE, 5000);
  TEST_ASSERT_EQUAL(SKA_DENIED, sk_access_state(NULL));
  TEST_ASSERT_EQUAL_STRING("", token_seen);
  TEST_ASSERT_EQUAL_STRING("", href_seen);
}

static void test_expired_after_30_min(void) {
  script[0] = (reply_t){true, 202, PENDING};
  for (int i = 1; i < 8; i++) script[i] = (reply_t){true, 200, PENDING};
  n_script = 8;
  sk_access_start();
  sk_access_poll(BASE, 0);
  sk_access_poll(BASE, 29 * 60 * 1000);
  TEST_ASSERT_EQUAL(SKA_PENDING, sk_access_state(NULL));
  int before = calls;
  sk_access_poll(BASE, 30 * 60 * 1000);
  TEST_ASSERT_EQUAL(SKA_EXPIRED, sk_access_state(NULL));
  TEST_ASSERT_EQUAL_INT(before, calls);   // no more requests
  TEST_ASSERT_EQUAL_STRING("", href_seen);
}

// Review Focus 3: the server refuses or is unreachable: a clear state, no retry loop.
static void test_http_error_stops(void) {
  script[0] = (reply_t){true, 500, "oops"};
  n_script = 1;
  sk_access_start();
  sk_access_poll(BASE, 0);
  int code = 0;
  TEST_ASSERT_EQUAL(SKA_ERROR, sk_access_state(&code));
  TEST_ASSERT_EQUAL_INT(500, code);
  sk_access_poll(BASE, 60000);
  TEST_ASSERT_EQUAL_INT(1, calls);
}

static void test_unreachable_is_an_error(void) {
  n_script = 0;   // every call fails to connect
  sk_access_start();
  sk_access_poll(BASE, 0);
  int code = -1;
  TEST_ASSERT_EQUAL(SKA_ERROR, sk_access_state(&code));
  TEST_ASSERT_EQUAL_INT(0, code);
  TEST_ASSERT_NOT_NULL(strstr(sk_access_message(), "unreachable"));
}

static void test_already_requested_explains(void) {
  script[0] = (reply_t){true, 400,
                        "{\"state\":\"COMPLETED\",\"statusCode\":400,"
                        "\"message\":\"A device with clientId 'x' has already requested access\"}"};
  n_script = 1;
  sk_access_start();
  sk_access_poll(BASE, 0);
  int code = 0;
  TEST_ASSERT_EQUAL(SKA_ERROR, sk_access_state(&code));
  TEST_ASSERT_EQUAL_INT(400, code);
  TEST_ASSERT_NOT_NULL(strstr(sk_access_message(), "already requested"));
}

static void test_resume_polls_stored_href(void) {
  script[0] = (reply_t){true, 200, PENDING};
  n_script = 1;
  sk_access_resume("/signalk/v1/requests/r1", 0);
  TEST_ASSERT_EQUAL(SKA_PENDING, sk_access_state(NULL));
  sk_access_poll(BASE, 5000);
  TEST_ASSERT_EQUAL_STRING("GET", last_method);   // no new POST
  TEST_ASSERT_EQUAL_STRING("http://boat:3000/signalk/v1/requests/r1", last_url);
}

static void test_client_id_stable_for_mac(void) {
  const uint8_t m1[6] = {0xa4, 0xcb, 0x8f, 0xc7, 0x2a, 0x10}, m2[6] = {0xa4, 0xcb, 0x8f, 0xc7, 0x2a, 0x11};
  char a[37], b[37], c[37];
  sk_access_client_id_from_mac(m1, a);
  sk_access_client_id_from_mac(m1, b);
  sk_access_client_id_from_mac(m2, c);
  TEST_ASSERT_EQUAL_STRING(a, b);
  TEST_ASSERT_TRUE(strcmp(a, c) != 0);
  TEST_ASSERT_EQUAL_UINT(36, strlen(a));
  TEST_ASSERT_EQUAL_CHAR('-', a[8]);
  TEST_ASSERT_EQUAL_CHAR('4', a[14]);   // version 4 shape
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_request_then_approved_saves_token);
  RUN_TEST(test_denied);
  RUN_TEST(test_expired_after_30_min);
  RUN_TEST(test_http_error_stops);
  RUN_TEST(test_unreachable_is_an_error);
  RUN_TEST(test_already_requested_explains);
  RUN_TEST(test_resume_polls_stored_href);
  RUN_TEST(test_client_id_stable_for_mac);
  return UNITY_END();
}
