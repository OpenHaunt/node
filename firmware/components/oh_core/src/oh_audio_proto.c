#include "oh_audio_proto.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

void oh_json_audio_caps(cJSON *info, const oh_audio_caps_t *caps)
{
    cJSON *a = cJSON_AddObjectToObject(info, "audio");
    cJSON_AddNumberToObject(a, "rate", caps->rate);
    cJSON_AddNumberToObject(a, "voices", caps->voices);
    cJSON *outs = cJSON_AddArrayToObject(a, "outputs");
    for (uint8_t i = 0; i < caps->n_outputs; i++) {
        const oh_audio_output_t *o = &caps->outputs[i];
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "id", o->id);
        cJSON_AddStringToObject(j, "name", o->name);
        cJSON_AddStringToObject(j, "kind", o->kind);
        cJSON_AddNumberToObject(j, "channels", o->channels);
        cJSON_AddItemToArray(outs, j);
    }
    cJSON *clips = cJSON_AddObjectToObject(a, "clips");
    cJSON_AddStringToObject(clips, "container", "wav");
    cJSON *formats = cJSON_AddArrayToObject(clips, "formats");
    cJSON_AddItemToArray(formats, cJSON_CreateString("pcm16"));
    cJSON_AddItemToArray(formats, cJSON_CreateString("ima-adpcm"));
    cJSON_AddNumberToObject(clips, "bytes_total", caps->bytes_total);
    cJSON_AddNumberToObject(clips, "bytes_free", caps->bytes_free);
    cJSON *stream = cJSON_AddObjectToObject(a, "stream");
    cJSON_AddStringToObject(stream, "payload", "L16");
    cJSON_AddNumberToObject(stream, "max_buffer_ms", caps->max_buffer_ms);
}

static bool fail(char err[OH_ERR_MAX], const char *why)
{
    if (err) {
        snprintf(err, OH_ERR_MAX, "%s", why);
    }
    return false;
}

bool oh_audio_clip_id_valid(const char *id)
{
    if (!id || strlen(id) != 64) {
        return false;
    }
    for (int i = 0; i < 64; i++) {
        if (!isxdigit((unsigned char)id[i]) || isupper((unsigned char)id[i])) {
            return false;
        }
    }
    return true;
}

/* A non-negative whole number of milliseconds, or the default when absent. */
static bool get_ms(const cJSON *json, const char *key, uint32_t *out, char err[OH_ERR_MAX])
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(json, key);
    if (!v) {
        return true;
    }
    if (!cJSON_IsNumber(v) || v->valuedouble < 0 || v->valuedouble > 86400000.0) {
        char why[OH_ERR_MAX];
        snprintf(why, sizeof(why), "%s must be milliseconds", key);
        return fail(err, why);
    }
    *out = (uint32_t)v->valuedouble;
    return true;
}

static bool get_gain(const cJSON *json, float *out, char err[OH_ERR_MAX])
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(json, "gain");
    if (!v) {
        return true;
    }
    if (!cJSON_IsNumber(v) || v->valuedouble < 0 || v->valuedouble > 1) {
        return fail(err, "gain must be 0..1");
    }
    *out = (float)v->valuedouble;
    return true;
}

static bool get_at(const cJSON *json, oh_audio_cmd_t *out, char err[OH_ERR_MAX])
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(json, "at");
    if (!v || cJSON_IsNull(v)) {
        return true;
    }
    if (!cJSON_IsNumber(v)) {
        return fail(err, "at must be show-time milliseconds");
    }
    out->has_at = true;
    out->at_ms = (int64_t)v->valuedouble;
    return true;
}

static bool get_name(const cJSON *json, const char *key, char *dst, size_t cap, bool required, char err[OH_ERR_MAX])
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(json, key);
    if (!v || cJSON_IsNull(v)) {
        if (required) {
            char why[OH_ERR_MAX];
            snprintf(why, sizeof(why), "%s is required", key);
            return fail(err, why);
        }
        return true;
    }
    if (!cJSON_IsString(v) || strlen(v->valuestring) >= cap) {
        char why[OH_ERR_MAX];
        snprintf(why, sizeof(why), "%s must be a string under %u characters", key, (unsigned)cap);
        return fail(err, why);
    }
    strcpy(dst, v->valuestring);
    return true;
}

bool oh_audio_cmd_parse(const char *verb, const cJSON *json, oh_audio_cmd_t *out, char err[OH_ERR_MAX])
{
    memset(out, 0, sizeof(*out));
    out->gain = 1.0f;
    if (!cJSON_IsObject(json)) {
        return fail(err, "payload must be an object");
    }
    if (strcmp(verb, "play") == 0) {
        out->verb = OH_AUDIO_PLAY;
        if (!get_name(json, "voice", out->voice, sizeof(out->voice), true, err) ||
            !get_name(json, "clip", out->clip, sizeof(out->clip), true, err)) {
            return false;
        }
        if (!out->voice[0] || strcmp(out->voice, OH_MIX_STREAM_VOICE) == 0) {
            return fail(err, "voice must be a name, and not \"stream\"");
        }
        if (!oh_audio_clip_id_valid(out->clip)) {
            return fail(err, "clip must be a lowercase sha256");
        }
        const cJSON *loop = cJSON_GetObjectItemCaseSensitive(json, "loop");
        if (loop && !cJSON_IsBool(loop)) {
            return fail(err, "loop must be true or false");
        }
        out->loop = cJSON_IsTrue(loop);
        return get_at(json, out, err) && get_gain(json, &out->gain, err) &&
               get_ms(json, "fade_in_ms", &out->fade_in_ms, err) && get_ms(json, "offset_ms", &out->offset_ms, err);
    }
    if (strcmp(verb, "stop") == 0) {
        out->verb = OH_AUDIO_STOP;
        return get_name(json, "voice", out->voice, sizeof(out->voice), false, err) && get_at(json, out, err) &&
               get_ms(json, "fade_ms", &out->fade_ms, err);
    }
    if (strcmp(verb, "gain") == 0) {
        out->verb = OH_AUDIO_GAIN;
        if (!cJSON_GetObjectItemCaseSensitive(json, "gain")) {
            return fail(err, "gain is required");
        }
        return get_name(json, "voice", out->voice, sizeof(out->voice), false, err) &&
               get_gain(json, &out->gain, err) && get_ms(json, "fade_ms", &out->fade_ms, err);
    }
    if (strcmp(verb, "output") == 0) {
        out->verb = OH_AUDIO_OUTPUT;
        if (!get_name(json, "output", out->output, sizeof(out->output), true, err)) {
            return false;
        }
        const cJSON *en = cJSON_GetObjectItemCaseSensitive(json, "enabled");
        if (!cJSON_IsBool(en)) {
            return fail(err, "enabled must be true or false");
        }
        out->enabled = cJSON_IsTrue(en);
        return true;
    }
    if (strcmp(verb, "stream") == 0) {
        const cJSON *op = cJSON_GetObjectItemCaseSensitive(json, "op");
        if (!cJSON_IsString(op)) {
            return fail(err, "op must be start or stop");
        }
        strcpy(out->voice, OH_MIX_STREAM_VOICE);
        if (strcmp(op->valuestring, "stop") == 0) {
            out->verb = OH_AUDIO_STREAM_STOP;
            return get_at(json, out, err) && get_ms(json, "fade_ms", &out->fade_ms, err);
        }
        if (strcmp(op->valuestring, "start") != 0) {
            return fail(err, "op must be start or stop");
        }
        out->verb = OH_AUDIO_STREAM_START;
        const cJSON *port = cJSON_GetObjectItemCaseSensitive(json, "port");
        const cJSON *rate = cJSON_GetObjectItemCaseSensitive(json, "rate");
        const cJSON *ch = cJSON_GetObjectItemCaseSensitive(json, "channels");
        const cJSON *ts = cJSON_GetObjectItemCaseSensitive(json, "ts");
        const cJSON *ssrc = cJSON_GetObjectItemCaseSensitive(json, "ssrc");
        if (!cJSON_IsNumber(port) || port->valuedouble < 1024 || port->valuedouble > 65535) {
            return fail(err, "port must be 1024..65535");
        }
        if (!cJSON_IsNumber(rate) || rate->valuedouble < 8000 || rate->valuedouble > 96000) {
            return fail(err, "rate must be 8000..96000");
        }
        if (ch && (!cJSON_IsNumber(ch) || (ch->valuedouble != 1 && ch->valuedouble != 2))) {
            return fail(err, "channels must be 1 or 2");
        }
        if (!cJSON_IsNumber(ts) || ts->valuedouble < 0 || ts->valuedouble > 4294967295.0) {
            return fail(err, "ts must be an RTP timestamp");
        }
        if (!cJSON_GetObjectItemCaseSensitive(json, "at")) {
            return fail(err, "at is required for a stream");
        }
        if (ssrc) {
            if (!cJSON_IsNumber(ssrc) || ssrc->valuedouble < 0 || ssrc->valuedouble > 4294967295.0) {
                return fail(err, "ssrc must be a 32-bit number");
            }
            out->has_ssrc = true;
            out->ssrc = (uint32_t)ssrc->valuedouble;
        }
        out->port = (uint16_t)port->valuedouble;
        out->rate = (uint32_t)rate->valuedouble;
        out->channels = ch ? (uint8_t)ch->valuedouble : 1;
        out->ts = (uint32_t)ts->valuedouble;
        return get_at(json, out, err) && get_gain(json, &out->gain, err);
    }
    return fail(err, "unknown audio verb");
}

cJSON *oh_json_audio_status(const oh_mixer_t *m, const oh_audio_counters_t *c)
{
    cJSON *o = cJSON_CreateObject();
    cJSON *voices = cJSON_AddArrayToObject(o, "voices");
    for (int i = 0; i < OH_MIX_SLOTS; i++) {
        const oh_mix_voice_t *v = &m->slots[i];
        if (!v->active || v->ending || !v->name[0]) {
            continue;
        }
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "voice", v->name);
        cJSON_AddStringToObject(j, "clip", v->clip);
        const uint32_t pos = v->started ? v->pos : 0;
        cJSON_AddNumberToObject(j, "pos_ms", v->src.rate ? (double)(((uint64_t)pos * 1000u) / v->src.rate) : 0);
        cJSON_AddBoolToObject(j, "loop", v->loop);
        cJSON_AddBoolToObject(j, "started", v->started);
        cJSON_AddNumberToObject(j, "gain", v->level);
        cJSON_AddItemToArray(voices, j);
    }
    cJSON *s = cJSON_AddObjectToObject(o, "stream");
    cJSON_AddBoolToObject(s, "active", m->stream.active);
    if (m->stream.active) {
        cJSON_AddNumberToObject(s, "port", m->stream.port);
        cJSON_AddNumberToObject(s, "depth_ms",
                                m->stream.rate ? (double)(((uint64_t)c->stream_depth_frames * 1000u) / m->stream.rate)
                                               : 0);
        cJSON_AddNumberToObject(s, "late", c->stream_late);
        cJSON_AddNumberToObject(s, "missing", m->stream.missing);
        cJSON_AddNumberToObject(s, "resyncs", m->stream.resyncs);
        cJSON_AddNumberToObject(s, "packets", c->stream_packets);
        cJSON_AddNumberToObject(s, "seq_lost", c->stream_seq_lost);
        cJSON_AddNumberToObject(s, "reordered", c->stream_reordered);
        cJSON_AddNumberToObject(s, "duplicates", c->stream_duplicates);
        cJSON_AddNumberToObject(s, "foreign", c->stream_foreign);
        cJSON_AddNumberToObject(s, "rx_gap_ms", c->stream_rx_gap_ms);
        cJSON_AddNumberToObject(s, "rx_gap_max_ms", c->stream_rx_gap_max_ms);
    }
    cJSON_AddNumberToObject(o, "underruns", c->underruns);
    cJSON_AddNumberToObject(o, "master", m->master.target);
    cJSON_AddBoolToObject(o, "locked", c->locked);
    return o;
}
