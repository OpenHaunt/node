#include "oh_labels.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *find(const oh_labels_t *l, bool head, uint8_t number)
{
    for (int i = 0; i < l->n; i++) {
        if (l->entry[i].head == head && l->entry[i].number == number) {
            return l->entry[i].text;
        }
    }
    return NULL;
}

const char *oh_labels_port(const oh_labels_t *l, uint8_t port)
{
    return find(l, false, port);
}

const char *oh_labels_head(const oh_labels_t *l, uint8_t head)
{
    return find(l, true, head);
}

/* Well-formed UTF-8, no overlong forms or surrogates, and no control characters. */
static bool readable(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        unsigned c = *p;
        int more;
        unsigned cp;
        if (c < 0x80) {
            if (c < 0x20 || c == 0x7f) {
                return false;
            }
            p++;
            continue;
        } else if ((c & 0xe0) == 0xc0) {
            more = 1, cp = c & 0x1f;
        } else if ((c & 0xf0) == 0xe0) {
            more = 2, cp = c & 0x0f;
        } else if ((c & 0xf8) == 0xf0) {
            more = 3, cp = c & 0x07;
        } else {
            return false;
        }
        p++;
        for (int i = 0; i < more; i++, p++) {
            if ((*p & 0xc0) != 0x80) {
                return false;
            }
            cp = (cp << 6) | (*p & 0x3f);
        }
        static const unsigned least[] = {0, 0x80, 0x800, 0x10000};
        if (cp < least[more] || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) || (cp >= 0x80 && cp < 0xa0)) {
            return false;
        }
    }
    return true;
}

static void set(oh_labels_t *l, bool head, uint8_t number, const char *text)
{
    for (int i = 0; i < l->n; i++) {
        if (l->entry[i].head == head && l->entry[i].number == number) {
            if (text[0]) {
                strcpy(l->entry[i].text, text);
            } else {
                l->entry[i] = l->entry[--l->n];
            }
            return;
        }
    }
    if (text[0] && l->n < OH_LABELS_MAX) {
        oh_label_t *e = &l->entry[l->n++];
        e->head = head;
        e->number = number;
        strcpy(e->text, text);
    }
}

static bool merge_one(oh_labels_t *next, const cJSON *group, bool head, const oh_module_t *module, char *err)
{
    const char *what = head ? "heads" : "ports";
    if (!group) {
        return true;
    }
    if (!cJSON_IsObject(group)) {
        snprintf(err, OH_ERR_MAX, "labels.%s is an object of number to name", what);
        return false;
    }
    const cJSON *item;
    cJSON_ArrayForEach(item, group)
    {
        char *end = NULL;
        long n = strtol(item->string, &end, 10);
        if (!item->string[0] || *end || n < 0 || n > 255) {
            snprintf(err, OH_ERR_MAX, "labels.%s: \"%.16s\" is not a number", what, item->string);
            return false;
        }
        if (module && (head ? oh_module_find_head(module, (uint8_t)n) == NULL
                            : oh_module_find_port(module, (uint8_t)n) == NULL)) {
            snprintf(err, OH_ERR_MAX, "labels.%s: this node has no %s %ld", what, head ? "head" : "port", n);
            return false;
        }
        if (!cJSON_IsString(item)) {
            snprintf(err, OH_ERR_MAX, "labels.%s.%ld is a string", what, n);
            return false;
        }
        const char *s = item->valuestring;
        while (*s == ' ') {
            s++;
        }
        size_t len = strlen(s);
        while (len && s[len - 1] == ' ') {
            len--;
        }
        if (len >= OH_NAME_MAX) {
            snprintf(err, OH_ERR_MAX, "labels.%s.%ld is up to %d bytes", what, n, OH_NAME_MAX - 1);
            return false;
        }
        char text[OH_NAME_MAX];
        memcpy(text, s, len);
        text[len] = '\0';
        if (!readable(text)) {
            snprintf(err, OH_ERR_MAX, "labels.%s.%ld is not readable text", what, n);
            return false;
        }
        set(next, head, (uint8_t)n, text);
    }
    return true;
}

static bool same(const oh_labels_t *a, const oh_labels_t *b)
{
    if (a->n != b->n) {
        return false;
    }
    for (int i = 0; i < a->n; i++) {
        const char *other = find(b, a->entry[i].head, a->entry[i].number);
        if (!other || strcmp(other, a->entry[i].text)) {
            return false;
        }
    }
    return true;
}

bool oh_labels_merge(oh_labels_t *l, const cJSON *labels, const oh_module_t *module, char *err, bool *changed)
{
    err[0] = '\0';
    *changed = false;
    if (!cJSON_IsObject(labels)) {
        snprintf(err, OH_ERR_MAX, "labels is an object with ports and heads");
        return false;
    }
    oh_labels_t *next = malloc(sizeof(*next));
    if (!next) {
        snprintf(err, OH_ERR_MAX, "out of memory");
        return false;
    }
    *next = *l;
    bool ok = merge_one(next, cJSON_GetObjectItemCaseSensitive(labels, "ports"), false, module, err) &&
              merge_one(next, cJSON_GetObjectItemCaseSensitive(labels, "heads"), true, module, err);
    if (ok) {
        *changed = !same(l, next);
        *l = *next;
    }
    free(next);
    return ok;
}

cJSON *oh_labels_to_json(const oh_labels_t *l)
{
    cJSON *o = cJSON_CreateObject();
    cJSON *ports = cJSON_AddObjectToObject(o, "ports");
    cJSON *heads = cJSON_AddObjectToObject(o, "heads");
    for (int i = 0; i < l->n; i++) {
        char key[4];
        snprintf(key, sizeof(key), "%u", l->entry[i].number);
        cJSON_AddStringToObject(l->entry[i].head ? heads : ports, key, l->entry[i].text);
    }
    return o;
}

static void annotate(const oh_labels_t *l, cJSON *list, const char *key, bool head)
{
    cJSON *item;
    cJSON_ArrayForEach(item, list)
    {
        const cJSON *n = cJSON_GetObjectItemCaseSensitive(item, key);
        const char *label = cJSON_IsNumber(n) ? find(l, head, (uint8_t)n->valueint) : NULL;
        if (label) {
            cJSON_AddStringToObject(item, "label", label);
        }
    }
}

void oh_labels_annotate(const oh_labels_t *l, cJSON *info)
{
    if (!l || !info) {
        return;
    }
    annotate(l, cJSON_GetObjectItemCaseSensitive(info, "ports"), "port", false);
    annotate(l, cJSON_GetObjectItemCaseSensitive(info, "heads"), "head", true);
}
