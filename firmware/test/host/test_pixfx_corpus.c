/*
 * The pixel effects still draw what doc 14's corpus says they draw.
 *
 * `docs/14-pixel-effects.json` is the effects as bytes, and the-pult draws a strip in
 * effect mode from the same file. A change to oh_pixfx.c that moves a byte fails here,
 * which is the moment to decide whether it was meant — and, if it was, to regenerate
 * the corpus with `pixfx_corpus` so the console follows.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "oh_pixfx.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = malloc((size_t)n + 1);
    if (text && fread(text, 1, (size_t)n, f) != (size_t)n) {
        free(text);
        text = NULL;
    }
    if (text) {
        text[n] = '\0';
    }
    fclose(f);
    return text;
}

static oh_color_t color_of(const cJSON *a)
{
    return (oh_color_t){(uint8_t)cJSON_GetArrayItem(a, 0)->valueint, (uint8_t)cJSON_GetArrayItem(a, 1)->valueint,
                        (uint8_t)cJSON_GetArrayItem(a, 2)->valueint};
}

static void every_case_draws_the_bytes_the_corpus_says(void)
{
    char *text = slurp(PIXFX_CORPUS);
    TEST_ASSERT_NOT_NULL_MESSAGE(text, "the corpus is where CMake says it is");
    cJSON *root = cJSON_Parse(text);
    TEST_ASSERT_NOT_NULL(root);
    const cJSON *cases = cJSON_GetObjectItem(root, "cases");
    TEST_ASSERT_TRUE(cJSON_GetArraySize(cases) > 100);

    int wrong = 0;
    const cJSON *c;
    cJSON_ArrayForEach(c, cases)
    {
        oh_pixfx_params_t p;
        oh_pixfx_defaults(&p);
        TEST_ASSERT_TRUE(oh_pixfx_parse(cJSON_GetObjectItem(c, "effect")->valuestring, &p.kind));
        p.color = color_of(cJSON_GetObjectItem(c, "color"));
        p.color2 = color_of(cJSON_GetObjectItem(c, "color2"));
        p.brightness = (float)cJSON_GetObjectItem(c, "brightness")->valuedouble;
        p.speed = (float)cJSON_GetObjectItem(c, "speed")->valuedouble;
        p.size = (float)cJSON_GetObjectItem(c, "size")->valuedouble;
        p.reverse = cJSON_IsTrue(cJSON_GetObjectItem(c, "reverse"));
        p.mirror = cJSON_IsTrue(cJSON_GetObjectItem(c, "mirror"));
        p.segments = cJSON_GetObjectItem(c, "segments")->valueint;
        const int64_t now = (int64_t)cJSON_GetObjectItem(c, "now_ms")->valuedouble;
        const uint16_t count = (uint16_t)cJSON_GetObjectItem(c, "count")->valueint;
        const float phase = oh_pixfx_phase_at(p.speed, now);

        uint8_t rgb[3 * 64];
        TEST_ASSERT_TRUE(count <= 64);
        oh_pixfx_render(&p, phase, now, count, rgb);
        char hex[6 * 64 + 1];
        for (int i = 0; i < 3 * count; i++) {
            snprintf(hex + 2 * i, 3, "%02x", rgb[i]);
        }
        if (strcmp(hex, cJSON_GetObjectItem(c, "rgb")->valuestring) != 0) {
            printf("differs: %s\n", cJSON_GetObjectItem(c, "name")->valuestring);
            wrong++;
        }
    }
    cJSON_Delete(root);
    free(text);
    TEST_ASSERT_EQUAL_MESSAGE(0, wrong, "cases that no longer draw what the corpus says");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(every_case_draws_the_bytes_the_corpus_says);
    return UNITY_END();
}
