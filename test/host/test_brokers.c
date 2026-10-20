#include <string.h>

#include "oh_brokers.h"
#include "oh_port.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static oh_brokers_t list_of(const char *joined)
{
    oh_brokers_t b;
    oh_brokers_split(joined, &b);
    return b;
}

static void a_node_walks_its_list_and_backs_off_after_a_whole_pass(void)
{
    oh_brokers_t b = {0};
    oh_brokers_t told = list_of("10.0.0.1:1883,10.0.0.2:1883");
    TEST_ASSERT_TRUE(oh_brokers_set(&b, &told, NULL));
    TEST_ASSERT_EQUAL_STRING("10.0.0.1:1883", oh_brokers_current(&b));

    TEST_ASSERT_EQUAL_UINT32(OH_BROKERS_BETWEEN_MS, oh_brokers_failed(&b));
    TEST_ASSERT_EQUAL_STRING("10.0.0.2:1883", oh_brokers_current(&b));
    TEST_ASSERT_EQUAL_UINT32(OH_BROKERS_FIRST_BACKOFF_MS, oh_brokers_failed(&b));
    TEST_ASSERT_EQUAL_STRING("10.0.0.1:1883", oh_brokers_current(&b)); /* from the top */
    oh_brokers_failed(&b);
    TEST_ASSERT_EQUAL_UINT32(2 * OH_BROKERS_FIRST_BACKOFF_MS, oh_brokers_failed(&b));
    for (int i = 0; i < 10; i++) {
        oh_brokers_failed(&b);
    }
    TEST_ASSERT_EQUAL_UINT32(OH_BROKERS_BETWEEN_MS, oh_brokers_failed(&b));
    TEST_ASSERT_EQUAL_UINT32(OH_BROKERS_MAX_BACKOFF_MS, oh_brokers_failed(&b));

    oh_brokers_connected(&b);
    oh_brokers_failed(&b);
    TEST_ASSERT_EQUAL_UINT32(OH_BROKERS_FIRST_BACKOFF_MS, oh_brokers_failed(&b));
}

static void a_new_leader_first_is_a_move_and_the_same_one_is_not(void)
{
    oh_brokers_t b = {0};
    oh_brokers_t ab = list_of("a:1,b:1"), abc = list_of("a:1,b:1,c:1"), ba = list_of("b:1,a:1");
    oh_brokers_set(&b, &ab, NULL);
    TEST_ASSERT_FALSE(oh_brokers_set(&b, &abc, "a:1")); /* a longer tail moves nobody */
    TEST_ASSERT_EQUAL(3, b.n);
    TEST_ASSERT_TRUE(oh_brokers_set(&b, &ba, "a:1")); /* a different leader does */
    TEST_ASSERT_EQUAL_STRING("b:1", oh_brokers_current(&b));
    /* Walked to the next entry, and that station now leads: already where it should be. */
    oh_brokers_failed(&b);
    TEST_ASSERT_FALSE(oh_brokers_set(&b, &ab, "a:1"));
    TEST_ASSERT_EQUAL_STRING("a:1", oh_brokers_current(&b));
}

static void an_empty_list_forgets_nothing(void)
{
    oh_brokers_t b = list_of("a:1"), none = {0};
    TEST_ASSERT_FALSE(oh_brokers_set(&b, &none, "a:1"));
    TEST_ASSERT_EQUAL(1, b.n);
}

static void a_console_from_before_lists_is_a_list_of_one(void)
{
    char err[OH_ERR_MAX];
    bool present = false;
    oh_brokers_t b;
    cJSON *old = cJSON_Parse("{\"broker\":\"10.0.0.5:1883\"}");
    TEST_ASSERT_TRUE(oh_brokers_parse(old, &b, &present, err));
    TEST_ASSERT_TRUE(present);
    TEST_ASSERT_EQUAL(1, b.n);
    TEST_ASSERT_EQUAL_STRING("10.0.0.5:1883", b.entry[0]);
    cJSON_Delete(old);

    cJSON *both = cJSON_Parse("{\"broker\":\"a:1\",\"brokers\":[\"b:1\",\"a:1\",\"b:1\"]}");
    TEST_ASSERT_TRUE(oh_brokers_parse(both, &b, &present, err));
    TEST_ASSERT_EQUAL(2, b.n); /* the list wins, and one door is listed once */
    TEST_ASSERT_EQUAL_STRING("b:1", b.entry[0]);
    cJSON_Delete(both);

    cJSON *neither = cJSON_Parse("{\"name\":\"x\"}");
    TEST_ASSERT_TRUE(oh_brokers_parse(neither, &b, &present, err));
    TEST_ASSERT_FALSE(present);
    cJSON_Delete(neither);
}

static void a_list_that_is_wrong_is_refused_whole(void)
{
    char err[OH_ERR_MAX];
    bool present = false;
    oh_brokers_t b;
    const char *bad[] = {
        "{\"brokers\":\"a:1\"}",
        "{\"brokers\":[\"a:1\",\"no-port\"]}",
        "{\"brokers\":[\"a:0\"]}",
        "{\"brokers\":[\"a:1,b:1\"]}",
        "{\"brokers\":[3]}",
        "{\"broker\":\"a:99999\"}",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        cJSON *j = cJSON_Parse(bad[i]);
        TEST_ASSERT_FALSE_MESSAGE(oh_brokers_parse(j, &b, &present, err), bad[i]);
        TEST_ASSERT_TRUE(err[0] != '\0');
        cJSON_Delete(j);
    }
}

static void a_list_is_capped_and_survives_flash(void)
{
    oh_brokers_t b = list_of("a:1,b:1,c:1,d:1,e:1");
    TEST_ASSERT_EQUAL(OH_BROKERS_MAX, b.n);
    char joined[OH_BROKERS_JOINED_MAX];
    oh_brokers_join(&b, joined, sizeof(joined));
    TEST_ASSERT_EQUAL_STRING("a:1,b:1,c:1,d:1", joined);
    oh_brokers_t back = list_of(joined);
    TEST_ASSERT_TRUE(oh_brokers_equal(&b, &back));

    oh_brokers_t empty = list_of("");
    TEST_ASSERT_EQUAL(0, empty.n);
    TEST_ASSERT_EQUAL_STRING("", oh_brokers_current(&empty));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(a_node_walks_its_list_and_backs_off_after_a_whole_pass);
    RUN_TEST(a_new_leader_first_is_a_move_and_the_same_one_is_not);
    RUN_TEST(an_empty_list_forgets_nothing);
    RUN_TEST(a_console_from_before_lists_is_a_list_of_one);
    RUN_TEST(a_list_that_is_wrong_is_refused_whole);
    RUN_TEST(a_list_is_capped_and_survives_flash);
    return UNITY_END();
}
