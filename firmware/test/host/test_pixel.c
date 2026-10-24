#include <string.h>

#include "oh_pixel.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static oh_pix_cfg_t sacn(uint16_t count, uint16_t universe, uint16_t address, const char *order)
{
    oh_pix_cfg_t c;
    oh_pix_cfg_default(&c);
    c.mode = OH_PIX_MODE_SACN;
    c.count = count;
    c.universe = universe;
    c.address = address;
    strcpy(c.order, order);
    return c;
}

static void a_long_strip_spills_whole_pixels_into_the_next_universe(void)
{
    /* Doc 14's own example: 300 RGB pixels at 5/1 read 5 (170) and 6 (130). */
    oh_pix_cfg_t c = sacn(300, 5, 1, "GRB");
    oh_pix_map_t m;
    TEST_ASSERT_TRUE(oh_pix_map_build(&c, &m));
    TEST_ASSERT_EQUAL(2, m.n);
    TEST_ASSERT_EQUAL(5, m.universe[0]);
    TEST_ASSERT_EQUAL(170, m.pixels[0]);
    TEST_ASSERT_EQUAL(6, m.universe[1]);
    TEST_ASSERT_EQUAL(170, m.first_pixel[1]);
    TEST_ASSERT_EQUAL(130, m.pixels[1]);
    TEST_ASSERT_EQUAL(0, m.first_slot[1]);
}

static void an_address_shortens_only_the_first_universe(void)
{
    oh_pix_cfg_t c = sacn(200, 1, 100, "RGB");
    oh_pix_map_t m;
    TEST_ASSERT_TRUE(oh_pix_map_build(&c, &m));
    TEST_ASSERT_EQUAL(137, m.pixels[0]); /* (513 - 100) / 3 */
    TEST_ASSERT_EQUAL(99, m.first_slot[0]);
    TEST_ASSERT_EQUAL(63, m.pixels[1]);
    /* RGBW carries 128 a universe after the first. */
    oh_pix_cfg_t w = sacn(300, 1, 1, "GRBW");
    TEST_ASSERT_TRUE(oh_pix_map_build(&w, &m));
    TEST_ASSERT_EQUAL(3, m.n);
    TEST_ASSERT_EQUAL(128, m.pixels[0]);
    TEST_ASSERT_EQUAL(44, m.pixels[2]);
}

static void slots_land_on_their_pixels_and_a_short_frame_leaves_the_rest(void)
{
    oh_pix_cfg_t c = sacn(4, 1, 4, "GRB");
    oh_pix_map_t m;
    TEST_ASSERT_TRUE(oh_pix_map_build(&c, &m));
    uint8_t slots[512] = {0};
    for (int i = 0; i < 12; i++) {
        slots[3 + i] = (uint8_t)(10 + i);
    }
    uint8_t rgbw[16];
    memset(rgbw, 0xEE, sizeof(rgbw));
    oh_pix_map_apply(&m, 0, slots, 9, rgbw); /* reaches pixel 0 and 1 only */
    TEST_ASSERT_EQUAL(10, rgbw[0]); /* channels are R G B whatever the strip's order */
    TEST_ASSERT_EQUAL(11, rgbw[1]);
    TEST_ASSERT_EQUAL(12, rgbw[2]);
    TEST_ASSERT_EQUAL(0, rgbw[3]);
    TEST_ASSERT_EQUAL(13, rgbw[4]);
    TEST_ASSERT_EQUAL_HEX8(0xEE, rgbw[8]);
}

static void too_many_universes_and_the_top_of_the_range_are_refused(void)
{
    oh_pix_cfg_t c = sacn(1024, 63998, 1, "RGB");
    oh_pix_map_t m;
    TEST_ASSERT_FALSE(oh_pix_map_build(&c, &m));
    c = sacn(1024, 1, 1, "RGB");
    TEST_ASSERT_TRUE(oh_pix_map_build(&c, &m));
    TEST_ASSERT_EQUAL(7, m.n);
}

static void orders_say_where_each_byte_goes(void)
{
    oh_pix_order_t o;
    TEST_ASSERT_TRUE(oh_pix_order_parse("GRB", &o));
    TEST_ASSERT_EQUAL(1, o.pos[0]);
    TEST_ASSERT_EQUAL(0, o.pos[1]);
    TEST_ASSERT_EQUAL(3, o.components);
    TEST_ASSERT_TRUE(oh_pix_order_parse("GRBW", &o));
    TEST_ASSERT_EQUAL(3, o.pos[3]);
    TEST_ASSERT_EQUAL(4, o.components);
    TEST_ASSERT_FALSE(oh_pix_order_parse("RGW", &o));
    TEST_ASSERT_FALSE(oh_pix_order_parse("RRB", &o));
    TEST_ASSERT_FALSE(oh_pix_order_parse("rgb", &o));
}

static void settings_merge_and_refuse_what_is_out_of_range(void)
{
    oh_pix_cfg_t c;
    oh_pix_cfg_default(&c);
    char err[OH_ERR_MAX];
    cJSON *b = cJSON_Parse("{\"mode\":\"sacn\"}");
    TEST_ASSERT_FALSE(oh_pix_cfg_apply_json(&c, b, err)); /* no universe */
    TEST_ASSERT_EQUAL(OH_PIX_MODE_EFFECT, c.mode);       /* and nothing changed */
    cJSON_Delete(b);
    b = cJSON_Parse("{\"mode\":\"sacn\",\"universe\":5,\"count\":300}");
    TEST_ASSERT_TRUE_MESSAGE(oh_pix_cfg_apply_json(&c, b, err), err);
    cJSON_Delete(b);
    TEST_ASSERT_EQUAL(OH_PIX_MODE_SACN, c.mode);
    TEST_ASSERT_EQUAL(300, c.count);
    TEST_ASSERT_EQUAL_STRING("GRB", c.order);
    b = cJSON_Parse("{\"count\":2000}");
    TEST_ASSERT_FALSE(oh_pix_cfg_apply_json(&c, b, err));
    cJSON_Delete(b);
    b = cJSON_Parse("{\"order\":\"BGRX\"}");
    TEST_ASSERT_FALSE(oh_pix_cfg_apply_json(&c, b, err));
    cJSON_Delete(b);

    cJSON *o = oh_pix_cfg_to_json(&c, 1, 2);
    const cJSON *u = cJSON_GetObjectItem(o, "universes");
    TEST_ASSERT_EQUAL(2, cJSON_GetArraySize(u));
    TEST_ASSERT_EQUAL(6, cJSON_GetArrayItem(u, 1)->valueint);
    TEST_ASSERT_EQUAL(2, cJSON_GetObjectItem(o, "head")->valueint);
    cJSON_Delete(o);
}

static void the_rate_follows_the_length(void)
{
    TEST_ASSERT_EQUAL(40, oh_pix_max_fps(60));
    TEST_ASSERT_EQUAL(32, oh_pix_max_fps(1024));
}

static void dma_goes_to_the_longer_strip(void)
{
    TEST_ASSERT_EQUAL(-1, oh_pix_dma_choice(60, 64));
    TEST_ASSERT_EQUAL(1, oh_pix_dma_choice(60, 300));
    TEST_ASSERT_EQUAL(0, oh_pix_dma_choice(300, 300));
    TEST_ASSERT_EQUAL(1, oh_pix_dma_choice(0, 100));
}

static void a_strip_nobody_drives_holds_then_fades(void)
{
    TEST_ASSERT_EQUAL_FLOAT(1.0f, oh_pix_loss_level(9999, 10000));
    TEST_ASSERT_EQUAL_FLOAT(0.5f, oh_pix_loss_level(10500, 10000));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, oh_pix_loss_level(11000, 10000));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, oh_pix_loss_level(99999999, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(a_long_strip_spills_whole_pixels_into_the_next_universe);
    RUN_TEST(an_address_shortens_only_the_first_universe);
    RUN_TEST(slots_land_on_their_pixels_and_a_short_frame_leaves_the_rest);
    RUN_TEST(too_many_universes_and_the_top_of_the_range_are_refused);
    RUN_TEST(orders_say_where_each_byte_goes);
    RUN_TEST(settings_merge_and_refuse_what_is_out_of_range);
    RUN_TEST(the_rate_follows_the_length);
    RUN_TEST(dma_goes_to_the_longer_strip);
    RUN_TEST(a_strip_nobody_drives_holds_then_fades);
    return UNITY_END();
}
