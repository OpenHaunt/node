#include <string.h>

#include "oh_hostname.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void check(const char *name, const char *want)
{
    char out[33];
    size_t n = oh_hostname_from_name(name, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING(want, out);
    TEST_ASSERT_EQUAL_size_t(strlen(want), n);
}

static void words_become_one_label(void)
{
    check("Coffin Room DMX", "coffin-room-dmx");
    check("  Crypt / Fog #2  ", "crypt-fog-2");
    check("relay_3", "relay-3");
}

static void umlauts_are_spelled_out(void)
{
    check("Gruft Türöffner", "gruft-tueroeffner");
    check("Äußere Straße", "aeussere-strasse");
}

static void other_characters_are_one_separator(void)
{
    /* é is two bytes and 🎃 four: each is one gap, not several. */
    check("Café 🎃 Ost", "caf-ost");
    check("🎃", "");
    check("", "");
}

static void a_cut_never_ends_in_a_dash(void)
{
    char out[8];
    TEST_ASSERT_EQUAL_size_t(6, oh_hostname_from_name("abcdef ghij", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("abcdef", out);
    /* Fits exactly. */
    TEST_ASSERT_EQUAL_size_t(7, oh_hostname_from_name("abc def", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("abc-def", out);
}

static void the_longest_name_fits_esp_netif(void)
{
    /* A 31-byte name of umlauts doubles in length; esp-netif takes 32. */
    char out[33];
    oh_hostname_from_name("üüüüüüüüüüüüüüü", out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(30, strlen(out));
    oh_hostname_from_name("Ein sehr langer Name für den Sarg im Keller", out, sizeof(out));
    TEST_ASSERT_TRUE(strlen(out) <= 32);
    TEST_ASSERT_NOT_EQUAL('-', out[strlen(out) - 1]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(words_become_one_label);
    RUN_TEST(umlauts_are_spelled_out);
    RUN_TEST(other_characters_are_one_separator);
    RUN_TEST(a_cut_never_ends_in_a_dash);
    RUN_TEST(the_longest_name_fits_esp_netif);
    return UNITY_END();
}
