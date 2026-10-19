/*
 * The clip store: a wear-levelled FAT partition, one file per clip, named by the
 * sha256 of its bytes. OpenHaunt/node doc 13, "The store".
 */
#include "oh_audio.h"
#include "oh_audio_priv.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/sha256.h"
#include "oh_wav.h"
#include "sdkconfig.h"

#if CONFIG_OH_AUDIO

static const char *TAG = "oh_clips";

/* How much of the head of a file is kept for reading the WAV header. */
#define HEAD_BYTES 512
#define TMP_PATH OH_CLIPS_BASE "/upload.tmp"

static struct {
    bool mounted;
    wl_handle_t wl;
    SemaphoreHandle_t lock; /* one upload at a time */
    bool uploading;
    FILE *f;
    mbedtls_sha256_context sha;
    char want[OH_CLIP_ID_MAX];
    uint8_t head[HEAD_BYTES];
    size_t head_n;
    size_t written;
    size_t expected;
} s;

static void clip_path(char *out, size_t cap, const char *sha)
{
    snprintf(out, cap, OH_CLIPS_BASE "/%s.wav", sha);
}

esp_err_t oh_clips_mount(void)
{
    s.lock = xSemaphoreCreateMutex();
    const esp_vfs_fat_mount_config_t cfg = {
        .format_if_mount_failed = true,
        .max_files = OH_MIX_SLOTS + 2,
        .allocation_unit_size = 16 * 1024,
    };
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(OH_CLIPS_BASE, CONFIG_OH_AUDIO_CLIPS_PARTITION, &cfg, &s.wl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "clip store: %s (is there a \"%s\" FAT partition?)", esp_err_to_name(err),
                 CONFIG_OH_AUDIO_CLIPS_PARTITION);
        return err;
    }
    s.mounted = true;
    /* A reboot in the middle of an upload leaves its temporary file behind. */
    unlink(TMP_PATH);
    uint32_t total, free_bytes;
    oh_clips_budget(&total, &free_bytes);
    ESP_LOGI(TAG, "clip store: %lu of %lu bytes free", (unsigned long)free_bytes, (unsigned long)total);
    return ESP_OK;
}

bool oh_clips_mounted(void)
{
    return s.mounted;
}

void oh_clips_budget(uint32_t *total, uint32_t *free_bytes)
{
    uint64_t t = 0, f = 0;
    if (s.mounted) {
        esp_vfs_fat_info(OH_CLIPS_BASE, &t, &f);
    }
    *total = (uint32_t)t;
    *free_bytes = (uint32_t)f;
}

/* ── Playing ── */

typedef struct {
    FILE *f;
    oh_wav_reader_t r;
} clip_ctx_t;

static size_t io_read(void *ctx, void *buf, size_t n)
{
    return fread(buf, 1, n, ((clip_ctx_t *)ctx)->f);
}

static bool io_seek(void *ctx, uint32_t offset)
{
    return fseek(((clip_ctx_t *)ctx)->f, (long)offset, SEEK_SET) == 0;
}

static size_t src_pull(void *ctx, int16_t *frames, size_t n)
{
    return oh_wav_reader_read(&((clip_ctx_t *)ctx)->r, frames, n);
}

static bool src_seek(void *ctx, uint32_t frame)
{
    return oh_wav_reader_seek(&((clip_ctx_t *)ctx)->r, frame);
}

static void src_release(void *ctx)
{
    clip_ctx_t *c = ctx;
    fclose(c->f);
    heap_caps_free(c);
}

/* Read a WAV header from an open file. */
static bool read_info(FILE *f, oh_wav_info_t *info, char err[OH_ERR_MAX])
{
    uint8_t head[HEAD_BYTES];
    struct stat st;
    if (fstat(fileno(f), &st) != 0) {
        snprintf(err, OH_ERR_MAX, "cannot stat");
        return false;
    }
    fseek(f, 0, SEEK_SET);
    const size_t n = fread(head, 1, sizeof(head), f);
    return oh_wav_parse(head, n, (uint32_t)st.st_size, info, err);
}

bool oh_clips_open(const char *sha, oh_mix_source_t *src)
{
    if (!s.mounted || !oh_audio_clip_id_valid(sha)) {
        return false;
    }
    char path[96];
    clip_path(path, sizeof(path), sha);
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "clip %.12s… is not stored", sha);
        return false;
    }
    oh_wav_info_t info;
    char err[OH_ERR_MAX] = "";
    if (!read_info(f, &info, err)) {
        ESP_LOGW(TAG, "clip %.12s…: %s", sha, err);
        fclose(f);
        return false;
    }
    /* The reader holds a decoded ADPCM block: PSRAM, not the internal heap. */
    clip_ctx_t *c = heap_caps_malloc(sizeof(*c), MALLOC_CAP_SPIRAM);
    if (!c) {
        c = heap_caps_malloc(sizeof(*c), MALLOC_CAP_DEFAULT);
    }
    if (!c) {
        fclose(f);
        return false;
    }
    c->f = f;
    const oh_io_t io = {io_read, io_seek, c};
    oh_wav_reader_init(&c->r, &info, &io);
    *src = (oh_mix_source_t){src_pull, src_seek, src_release, c, info.channels, info.rate, info.frames};
    return true;
}

/* ── Uploading ── */

int oh_clips_upload_begin(const char *sha, size_t len, const char **msg)
{
    if (!s.mounted) {
        *msg = "no clip store";
        return 503;
    }
    if (!oh_audio_clip_id_valid(sha)) {
        *msg = "a clip is named by the lowercase sha256 of its bytes";
        return 400;
    }
    char path[96];
    clip_path(path, sizeof(path), sha);
    struct stat st;
    if (stat(path, &st) == 0) {
        *msg = "already stored";
        return 200;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    if (s.uploading) {
        xSemaphoreGive(s.lock);
        *msg = "another upload is running";
        return 409;
    }
    uint32_t total, free_bytes;
    oh_clips_budget(&total, &free_bytes);
    /* A cluster of slack, so a clip that only just fits does not fail at the last write. */
    if (len == 0 || len + 16 * 1024 > free_bytes) {
        xSemaphoreGive(s.lock);
        *msg = len == 0 ? "empty body" : "does not fit in the clip store";
        return len == 0 ? 400 : 413;
    }
    s.f = fopen(TMP_PATH, "wb");
    if (!s.f) {
        xSemaphoreGive(s.lock);
        *msg = "cannot write to the clip store";
        return 500;
    }
    s.uploading = true;
    strcpy(s.want, sha);
    s.head_n = 0;
    s.written = 0;
    s.expected = len;
    mbedtls_sha256_init(&s.sha);
    mbedtls_sha256_starts(&s.sha, 0);
    xSemaphoreGive(s.lock);
    return 0;
}

esp_err_t oh_clips_upload_write(const void *buf, size_t n)
{
    if (!s.uploading) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s.head_n < HEAD_BYTES) {
        size_t take = HEAD_BYTES - s.head_n;
        if (take > n) {
            take = n;
        }
        memcpy(s.head + s.head_n, buf, take);
        s.head_n += take;
    }
    mbedtls_sha256_update(&s.sha, buf, n);
    if (fwrite(buf, 1, n, s.f) != n) {
        return ESP_FAIL;
    }
    s.written += n;
    return ESP_OK;
}

void oh_clips_upload_abort(void)
{
    if (!s.uploading) {
        return;
    }
    fclose(s.f);
    s.f = NULL;
    unlink(TMP_PATH);
    mbedtls_sha256_free(&s.sha);
    s.uploading = false;
}

int oh_clips_upload_finish(const char **msg)
{
    if (!s.uploading) {
        *msg = "no upload running";
        return 400;
    }
    uint8_t digest[32];
    mbedtls_sha256_finish(&s.sha, digest);
    char hex[OH_CLIP_ID_MAX];
    for (int i = 0; i < 32; i++) {
        snprintf(hex + 2 * i, 3, "%02x", digest[i]);
    }
    const size_t written = s.written;
    fclose(s.f);
    s.f = NULL;
    mbedtls_sha256_free(&s.sha);
    s.uploading = false;
    if (written != s.expected || strcmp(hex, s.want) != 0) {
        unlink(TMP_PATH);
        *msg = written != s.expected ? "body shorter than Content-Length" : "bytes do not hash to the sha in the URL";
        return 400;
    }
    oh_wav_info_t info;
    static char err[OH_ERR_MAX];
    if (!oh_wav_parse(s.head, s.head_n, (uint32_t)written, &info, err)) {
        unlink(TMP_PATH);
        *msg = err;
        return 400;
    }
    char path[96];
    clip_path(path, sizeof(path), s.want);
    if (rename(TMP_PATH, path) != 0) {
        unlink(TMP_PATH);
        *msg = "cannot store the clip";
        return 500;
    }
    ESP_LOGI(TAG, "stored %.12s…: %s %lu Hz %u ch, %lu ms", s.want, oh_wav_format_str(info.format),
             (unsigned long)info.rate, info.channels, (unsigned long)oh_wav_duration_ms(&info));
    *msg = "stored";
    return 201;
}

/* ── Listing and deleting ── */

cJSON *oh_clips_list(void)
{
    cJSON *o = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(o, "clips");
    DIR *d = s.mounted ? opendir(OH_CLIPS_BASE) : NULL;
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            const size_t len = strlen(e->d_name);
            if (len != 68 || strcmp(e->d_name + 64, ".wav") != 0) {
                continue;
            }
            char sha[OH_CLIP_ID_MAX];
            memcpy(sha, e->d_name, 64);
            sha[64] = 0;
            if (!oh_audio_clip_id_valid(sha)) {
                continue;
            }
            char path[96];
            clip_path(path, sizeof(path), sha);
            FILE *f = fopen(path, "rb");
            if (!f) {
                continue;
            }
            oh_wav_info_t info;
            char err[OH_ERR_MAX];
            struct stat st;
            const bool ok = read_info(f, &info, err) && fstat(fileno(f), &st) == 0;
            fclose(f);
            if (!ok) {
                continue;
            }
            cJSON *j = cJSON_CreateObject();
            cJSON_AddStringToObject(j, "sha", sha);
            cJSON_AddNumberToObject(j, "bytes", (double)st.st_size);
            cJSON_AddStringToObject(j, "format", oh_wav_format_str(info.format));
            cJSON_AddNumberToObject(j, "rate", info.rate);
            cJSON_AddNumberToObject(j, "channels", info.channels);
            cJSON_AddNumberToObject(j, "duration_ms", oh_wav_duration_ms(&info));
            cJSON_AddItemToArray(arr, j);
        }
        closedir(d);
    }
    uint32_t total, free_bytes;
    oh_clips_budget(&total, &free_bytes);
    cJSON_AddNumberToObject(o, "bytes_total", total);
    cJSON_AddNumberToObject(o, "bytes_free", free_bytes);
    return o;
}

int oh_clips_delete(const char *sha)
{
    if (!s.mounted || !oh_audio_clip_id_valid(sha)) {
        return 404;
    }
    if (oh_audio_clip_in_use(sha)) {
        return 409;
    }
    char path[96];
    clip_path(path, sizeof(path), sha);
    return unlink(path) == 0 ? 204 : 404;
}

#else /* !CONFIG_OH_AUDIO */

int oh_clips_upload_begin(const char *sha, size_t len, const char **msg)
{
    (void)sha;
    (void)len;
    *msg = "this node has no audio";
    return 404;
}

esp_err_t oh_clips_upload_write(const void *buf, size_t n)
{
    (void)buf;
    (void)n;
    return ESP_ERR_NOT_SUPPORTED;
}

int oh_clips_upload_finish(const char **msg)
{
    *msg = "this node has no audio";
    return 404;
}

void oh_clips_upload_abort(void) {}

cJSON *oh_clips_list(void)
{
    return NULL;
}

int oh_clips_delete(const char *sha)
{
    (void)sha;
    return 404;
}

#endif
