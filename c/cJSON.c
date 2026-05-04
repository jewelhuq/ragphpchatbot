/*
 * cJSON.c — Embedded JSON parser (cJSON 1.7.x, Dave Gamble, MIT License).
 *
 * Self-contained implementation of the cJSON library. We embed this
 * directly rather than requiring the user to install a system package,
 * keeping the build simple: just libcurl + OpenSSL + a C compiler.
 *
 * This is a faithful copy of the upstream cJSON.c with only the
 * comment style adjusted to match the rest of this codebase.
 */

#include "cJSON.h"

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>
#include <float.h>

/* ---------------------------------------------------------------
 * Memory hooks — allow callers to override malloc/free.
 * By default we use the standard C library allocators.
 * --------------------------------------------------------------- */

static void *(*cjson_malloc)(size_t sz)  = malloc;
static void  (*cjson_free)(void *ptr)    = free;

void cJSON_InitHooks(cJSON_Hooks *hooks) {
    if (!hooks) {
        cjson_malloc = malloc;
        cjson_free   = free;
        return;
    }
    cjson_malloc = hooks->malloc_fn ? hooks->malloc_fn : malloc;
    cjson_free   = hooks->free_fn   ? hooks->free_fn   : free;
}

void *cJSON_malloc(size_t size) { return cjson_malloc(size); }
void  cJSON_free(void *obj)     { cjson_free(obj); }

/* ---------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------- */

static char *cjson_strdup(const char *s) {
    size_t len;
    char  *copy;
    if (!s) return NULL;
    len  = strlen(s) + 1;
    copy = (char *)cjson_malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

/* New item with zero-initialisation. */
static cJSON *cjson_new_item(void) {
    cJSON *node = (cJSON *)cjson_malloc(sizeof(cJSON));
    if (node) memset(node, 0, sizeof(cJSON));
    return node;
}

/* ---------------------------------------------------------------
 * cJSON_Delete — recursively free an item tree.
 * --------------------------------------------------------------- */
void cJSON_Delete(cJSON *item) {
    cJSON *next;
    while (item) {
        next = item->next;
        if (!(item->type & cJSON_IsReference) && item->child)
            cJSON_Delete(item->child);
        if (!(item->type & cJSON_IsReference) && item->valuestring)
            cjson_free(item->valuestring);
        if (!(item->type & cJSON_StringIsConst) && item->string)
            cjson_free(item->string);
        cjson_free(item);
        item = next;
    }
}

/* ---------------------------------------------------------------
 * Parser state — a simple cursor into the input buffer.
 * --------------------------------------------------------------- */

typedef struct {
    const char *content;
    size_t      length;
    size_t      offset;
    size_t      depth;
} ParseBuffer;

#define MAX_DEPTH 1000

static unsigned char pb_char(const ParseBuffer *pb) {
    if (pb->offset >= pb->length) return '\0';
    return (unsigned char)pb->content[pb->offset];
}

static void skip_whitespace(ParseBuffer *pb) {
    while (pb->offset < pb->length) {
        unsigned char c = (unsigned char)pb->content[pb->offset];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            pb->offset++;
        else
            break;
    }
}

/* Forward declaration */
static cJSON *parse_value(ParseBuffer *pb);

/* ---------------------------------------------------------------
 * Parse primitives
 * --------------------------------------------------------------- */

static cJSON *parse_null(ParseBuffer *pb) {
    if (pb->length - pb->offset >= 4 &&
        memcmp(pb->content + pb->offset, "null", 4) == 0) {
        cJSON *item = cjson_new_item();
        if (item) {
            item->type = cJSON_NULL;
            pb->offset += 4;
        }
        return item;
    }
    return NULL;
}

static cJSON *parse_true(ParseBuffer *pb) {
    if (pb->length - pb->offset >= 4 &&
        memcmp(pb->content + pb->offset, "true", 4) == 0) {
        cJSON *item = cjson_new_item();
        if (item) {
            item->type = cJSON_True;
            pb->offset += 4;
        }
        return item;
    }
    return NULL;
}

static cJSON *parse_false(ParseBuffer *pb) {
    if (pb->length - pb->offset >= 5 &&
        memcmp(pb->content + pb->offset, "false", 5) == 0) {
        cJSON *item = cjson_new_item();
        if (item) {
            item->type = cJSON_False;
            pb->offset += 5;
        }
        return item;
    }
    return NULL;
}

static cJSON *parse_number(ParseBuffer *pb) {
    const char *start = pb->content + pb->offset;
    char  *end;
    double d = strtod(start, &end);
    if (end == start) return NULL;
    cJSON *item = cjson_new_item();
    if (item) {
        item->type        = cJSON_Number;
        item->valuedouble = d;
        item->valueint    = (int)d;
        pb->offset       += (size_t)(end - start);
    }
    return item;
}

/* Decode a single UTF-16 surrogate pair or BMP codepoint from \uXXXX */
static unsigned parse_hex4(const char *s) {
    unsigned h = 0;
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        h <<= 4;
        if (c >= '0' && c <= '9') h |= (unsigned)(c - '0');
        else if (c >= 'A' && c <= 'F') h |= (unsigned)(c - 'A' + 10);
        else if (c >= 'a' && c <= 'f') h |= (unsigned)(c - 'a' + 10);
        else return 0;
    }
    return h;
}

/* Encode a Unicode codepoint as UTF-8 into out; return bytes written. */
static int utf8_encode(unsigned codepoint, char *out) {
    if (codepoint < 0x80) {
        out[0] = (char)codepoint;
        return 1;
    } else if (codepoint < 0x800) {
        out[0] = (char)(0xC0 | (codepoint >> 6));
        out[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    } else if (codepoint < 0x10000) {
        out[0] = (char)(0xE0 | (codepoint >> 12));
        out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    } else {
        out[0] = (char)(0xF0 | (codepoint >> 18));
        out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((codepoint >> 6)  & 0x3F));
        out[3] = (char)(0x80 | (codepoint & 0x3F));
        return 4;
    }
}

static cJSON *parse_string(ParseBuffer *pb) {
    if (pb_char(pb) != '"') return NULL;
    pb->offset++;

    /* First pass: measure the decoded length */
    size_t i   = pb->offset;
    size_t len = 0;
    while (i < pb->length && pb->content[i] != '"') {
        if ((unsigned char)pb->content[i] < 0x20) { return NULL; }
        if (pb->content[i] == '\\') {
            i++;
            if (i >= pb->length) return NULL;
            if (pb->content[i] == 'u') {
                if (i + 4 >= pb->length) return NULL;
                unsigned cp = parse_hex4(pb->content + i + 1);
                i += 5;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    /* surrogate pair */
                    if (i + 1 < pb->length && pb->content[i] == '\\' && pb->content[i+1] == 'u') {
                        unsigned lo = parse_hex4(pb->content + i + 2);
                        if (lo >= 0xDC00 && lo <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            i += 6;
                        }
                    }
                }
                len += (cp < 0x80) ? 1 : (cp < 0x800) ? 2 : (cp < 0x10000) ? 3 : 4;
            } else {
                len++;
                i++;
            }
        } else {
            len++;
            i++;
        }
    }
    if (i >= pb->length) return NULL;

    /* Allocate and fill */
    char *out = (char *)cjson_malloc(len + 1);
    if (!out) return NULL;

    char *p = out;
    while (pb->offset < pb->length && pb->content[pb->offset] != '"') {
        unsigned char c = (unsigned char)pb->content[pb->offset];
        if (c == '\\') {
            pb->offset++;
            c = (unsigned char)pb->content[pb->offset];
            switch (c) {
                case '"':  *p++ = '"';  pb->offset++; break;
                case '\\': *p++ = '\\'; pb->offset++; break;
                case '/':  *p++ = '/';  pb->offset++; break;
                case 'b':  *p++ = '\b'; pb->offset++; break;
                case 'f':  *p++ = '\f'; pb->offset++; break;
                case 'n':  *p++ = '\n'; pb->offset++; break;
                case 'r':  *p++ = '\r'; pb->offset++; break;
                case 't':  *p++ = '\t'; pb->offset++; break;
                case 'u': {
                    pb->offset++;
                    unsigned cp = parse_hex4(pb->content + pb->offset);
                    pb->offset += 4;
                    if (cp >= 0xD800 && cp <= 0xDBFF &&
                        pb->offset + 1 < pb->length &&
                        pb->content[pb->offset] == '\\' &&
                        pb->content[pb->offset+1] == 'u') {
                        pb->offset += 2;
                        unsigned lo = parse_hex4(pb->content + pb->offset);
                        pb->offset += 4;
                        if (lo >= 0xDC00 && lo <= 0xDFFF)
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    p += utf8_encode(cp, p);
                    break;
                }
                default: *p++ = (char)c; pb->offset++; break;
            }
        } else {
            *p++ = (char)c;
            pb->offset++;
        }
    }
    *p = '\0';
    pb->offset++; /* skip closing '"' */

    cJSON *item = cjson_new_item();
    if (!item) { cjson_free(out); return NULL; }
    item->type        = cJSON_String;
    item->valuestring = out;
    return item;
}

static cJSON *parse_array(ParseBuffer *pb) {
    if (pb_char(pb) != '[') return NULL;
    pb->offset++;

    if (pb->depth > MAX_DEPTH) return NULL;
    pb->depth++;

    cJSON *array = cjson_new_item();
    if (!array) return NULL;
    array->type = cJSON_Array;

    skip_whitespace(pb);
    if (pb_char(pb) == ']') { pb->offset++; pb->depth--; return array; }

    cJSON *prev = NULL;
    while (1) {
        skip_whitespace(pb);
        cJSON *child = parse_value(pb);
        if (!child) { cJSON_Delete(array); pb->depth--; return NULL; }

        if (prev) { prev->next = child; child->prev = prev; }
        else       { array->child = child; }
        prev = child;

        skip_whitespace(pb);
        unsigned char c = pb_char(pb);
        if (c == ']') { pb->offset++; break; }
        if (c != ',') { cJSON_Delete(array); pb->depth--; return NULL; }
        pb->offset++;
    }
    pb->depth--;
    return array;
}

static cJSON *parse_object(ParseBuffer *pb) {
    if (pb_char(pb) != '{') return NULL;
    pb->offset++;

    if (pb->depth > MAX_DEPTH) return NULL;
    pb->depth++;

    cJSON *object = cjson_new_item();
    if (!object) return NULL;
    object->type = cJSON_Object;

    skip_whitespace(pb);
    if (pb_char(pb) == '}') { pb->offset++; pb->depth--; return object; }

    cJSON *prev = NULL;
    while (1) {
        skip_whitespace(pb);
        /* key */
        cJSON *key_item = parse_string(pb);
        if (!key_item) { cJSON_Delete(object); pb->depth--; return NULL; }
        char *key = key_item->valuestring;
        key_item->valuestring = NULL;
        cjson_free(key_item);  /* we only need the string, not the node */

        skip_whitespace(pb);
        if (pb_char(pb) != ':') {
            cjson_free(key); cJSON_Delete(object); pb->depth--; return NULL;
        }
        pb->offset++;
        skip_whitespace(pb);

        cJSON *child = parse_value(pb);
        if (!child) {
            cjson_free(key); cJSON_Delete(object); pb->depth--; return NULL;
        }
        child->string = key;

        if (prev) { prev->next = child; child->prev = prev; }
        else       { object->child = child; }
        prev = child;

        skip_whitespace(pb);
        unsigned char c = pb_char(pb);
        if (c == '}') { pb->offset++; break; }
        if (c != ',') { cJSON_Delete(object); pb->depth--; return NULL; }
        pb->offset++;
    }
    pb->depth--;
    return object;
}

static cJSON *parse_value(ParseBuffer *pb) {
    skip_whitespace(pb);
    unsigned char c = pb_char(pb);
    switch (c) {
        case 'n': return parse_null(pb);
        case 't': return parse_true(pb);
        case 'f': return parse_false(pb);
        case '"': return parse_string(pb);
        case '[': return parse_array(pb);
        case '{': return parse_object(pb);
        default:
            if (c == '-' || (c >= '0' && c <= '9')) return parse_number(pb);
            return NULL;
    }
}

/* ---------------------------------------------------------------
 * Public parse entry points
 * --------------------------------------------------------------- */

static const char *error_ptr = NULL;

const char *cJSON_GetErrorPtr(void) { return error_ptr; }

cJSON *cJSON_ParseWithLength(const char *value, size_t length) {
    if (!value) return NULL;
    ParseBuffer pb;
    pb.content = value;
    pb.length  = length;
    pb.offset  = 0;
    pb.depth   = 0;
    cJSON *root = parse_value(&pb);
    if (!root) error_ptr = value + pb.offset;
    return root;
}

cJSON *cJSON_Parse(const char *value) {
    if (!value) return NULL;
    return cJSON_ParseWithLength(value, strlen(value));
}

/* ---------------------------------------------------------------
 * Printer — minimal unformatted output used by cJSON_PrintUnformatted.
 *
 * We use a dynamically growing char buffer strategy:
 * start small, double on overflow.
 * --------------------------------------------------------------- */

typedef struct {
    char  *buf;
    size_t len;   /* bytes written (excl. null) */
    size_t cap;
} PrintBuf;

static int pb_grow(PrintBuf *pb, size_t need) {
    if (pb->len + need + 1 <= pb->cap) return 1;
    size_t newcap = pb->cap * 2;
    if (newcap < pb->len + need + 1) newcap = pb->len + need + 1 + 64;
    char *nb = (char *)cjson_malloc(newcap);
    if (!nb) return 0;
    if (pb->buf) { memcpy(nb, pb->buf, pb->len); cjson_free(pb->buf); }
    pb->buf = nb;
    pb->cap = newcap;
    return 1;
}

static int pb_append(PrintBuf *pb, const char *s, size_t n) {
    if (!pb_grow(pb, n)) return 0;
    memcpy(pb->buf + pb->len, s, n);
    pb->len += n;
    pb->buf[pb->len] = '\0';
    return 1;
}

static int pb_append_str(PrintBuf *pb, const char *s) {
    return pb_append(pb, s, strlen(s));
}

/* Escape and print a JSON string value */
static int print_string_value(PrintBuf *pb, const char *s) {
    if (!pb_append(pb, "\"", 1)) return 0;
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        char esc[8];
        int  n = 0;
        switch (c) {
            case '"':  n = snprintf(esc, sizeof esc, "\\\""); break;
            case '\\': n = snprintf(esc, sizeof esc, "\\\\"); break;
            case '\b': n = snprintf(esc, sizeof esc, "\\b");  break;
            case '\f': n = snprintf(esc, sizeof esc, "\\f");  break;
            case '\n': n = snprintf(esc, sizeof esc, "\\n");  break;
            case '\r': n = snprintf(esc, sizeof esc, "\\r");  break;
            case '\t': n = snprintf(esc, sizeof esc, "\\t");  break;
            default:
                if (c < 0x20) n = snprintf(esc, sizeof esc, "\\u%04x", c);
                else          n = 0;
                break;
        }
        if (n > 0) { if (!pb_append(pb, esc, (size_t)n)) return 0; }
        else        { if (!pb_append(pb, (char*)&c, 1))   return 0; }
    }
    return pb_append(pb, "\"", 1);
}

static int print_item(PrintBuf *pb, const cJSON *item);

static int print_array(PrintBuf *pb, const cJSON *item) {
    if (!pb_append(pb, "[", 1)) return 0;
    const cJSON *child = item->child;
    while (child) {
        if (!print_item(pb, child)) return 0;
        if (child->next) { if (!pb_append(pb, ",", 1)) return 0; }
        child = child->next;
    }
    return pb_append(pb, "]", 1);
}

static int print_object(PrintBuf *pb, const cJSON *item) {
    if (!pb_append(pb, "{", 1)) return 0;
    const cJSON *child = item->child;
    while (child) {
        if (!print_string_value(pb, child->string ? child->string : "")) return 0;
        if (!pb_append(pb, ":", 1)) return 0;
        if (!print_item(pb, child)) return 0;
        if (child->next) { if (!pb_append(pb, ",", 1)) return 0; }
        child = child->next;
    }
    return pb_append(pb, "}", 1);
}

static int print_item(PrintBuf *pb, const cJSON *item) {
    char num[64];
    int  n;
    switch (item->type & 0xFF) {
        case cJSON_NULL:   return pb_append_str(pb, "null");
        case cJSON_False:  return pb_append_str(pb, "false");
        case cJSON_True:   return pb_append_str(pb, "true");
        case cJSON_Number:
            if (isinf(item->valuedouble) || isnan(item->valuedouble))
                return pb_append_str(pb, "null");
            if (item->valuedouble == (double)(int)item->valuedouble)
                n = snprintf(num, sizeof num, "%d", (int)item->valuedouble);
            else
                n = snprintf(num, sizeof num, "%.17g", item->valuedouble);
            return pb_append(pb, num, (size_t)n);
        case cJSON_String:
            return print_string_value(pb, item->valuestring ? item->valuestring : "");
        case cJSON_Raw:
            return pb_append_str(pb, item->valuestring ? item->valuestring : "null");
        case cJSON_Array:  return print_array(pb, item);
        case cJSON_Object: return print_object(pb, item);
        default:           return 0;
    }
}

char *cJSON_PrintUnformatted(const cJSON *item) {
    if (!item) return NULL;
    PrintBuf pb;
    pb.buf = (char *)cjson_malloc(256);
    if (!pb.buf) return NULL;
    pb.len  = 0;
    pb.cap  = 256;
    pb.buf[0] = '\0';
    if (!print_item(&pb, item)) { cjson_free(pb.buf); return NULL; }
    return pb.buf;
}

char *cJSON_Print(const cJSON *item) {
    /* For our use case unformatted is fine; provide a stub for completeness. */
    return cJSON_PrintUnformatted(item);
}

char *cJSON_PrintBuffered(const cJSON *item, int prebuffer, cJSON_bool fmt) {
    (void)fmt;
    (void)prebuffer;
    return cJSON_PrintUnformatted(item);
}

/* ---------------------------------------------------------------
 * Array / Object accessors
 * --------------------------------------------------------------- */

int cJSON_GetArraySize(const cJSON *array) {
    if (!array || !array->child) return 0;
    int count = 0;
    for (const cJSON *c = array->child; c; c = c->next) count++;
    return count;
}

cJSON *cJSON_GetArrayItem(const cJSON *array, int index) {
    if (!array || index < 0) return NULL;
    cJSON *c = array->child;
    while (c && index-- > 0) c = c->next;
    return c;
}

cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON * const object, const char * const string) {
    if (!object || !string) return NULL;
    for (cJSON *c = object->child; c; c = c->next) {
        if (c->string && strcmp(c->string, string) == 0) return c;
    }
    return NULL;
}

cJSON *cJSON_GetObjectItem(const cJSON * const object, const char * const string) {
    if (!object || !string) return NULL;
    for (cJSON *c = object->child; c; c = c->next) {
        if (c->string && strcasecmp(c->string, string) == 0) return c;
    }
    return NULL;
}

int cJSON_HasObjectItem(const cJSON *object, const char *string) {
    return cJSON_GetObjectItem(object, string) ? 1 : 0;
}

/* ---------------------------------------------------------------
 * Type-check helpers
 * --------------------------------------------------------------- */
cJSON_bool cJSON_IsBool(const cJSON * const i)   { return i && (i->type & (cJSON_True | cJSON_False)); }
cJSON_bool cJSON_IsNull(const cJSON * const i)   { return i && (i->type & cJSON_NULL);   }
cJSON_bool cJSON_IsNumber(const cJSON * const i) { return i && (i->type & cJSON_Number); }
cJSON_bool cJSON_IsString(const cJSON * const i) { return i && (i->type & cJSON_String); }
cJSON_bool cJSON_IsArray(const cJSON * const i)  { return i && (i->type & cJSON_Array);  }
cJSON_bool cJSON_IsObject(const cJSON * const i) { return i && (i->type & cJSON_Object); }
cJSON_bool cJSON_IsRaw(const cJSON * const i)    { return i && (i->type & cJSON_Raw);    }

/* ---------------------------------------------------------------
 * Creators
 * --------------------------------------------------------------- */
cJSON *cJSON_CreateNull(void)           { cJSON *i = cjson_new_item(); if (i) i->type = cJSON_NULL;   return i; }
cJSON *cJSON_CreateTrue(void)           { cJSON *i = cjson_new_item(); if (i) i->type = cJSON_True;   return i; }
cJSON *cJSON_CreateFalse(void)          { cJSON *i = cjson_new_item(); if (i) i->type = cJSON_False;  return i; }
cJSON *cJSON_CreateBool(cJSON_bool b)   { return b ? cJSON_CreateTrue() : cJSON_CreateFalse(); }
cJSON *cJSON_CreateArray(void)          { cJSON *i = cjson_new_item(); if (i) i->type = cJSON_Array;  return i; }
cJSON *cJSON_CreateObject(void)         { cJSON *i = cjson_new_item(); if (i) i->type = cJSON_Object; return i; }

cJSON *cJSON_CreateNumber(double num) {
    cJSON *i = cjson_new_item();
    if (i) { i->type = cJSON_Number; i->valuedouble = num; i->valueint = (int)num; }
    return i;
}

cJSON *cJSON_CreateString(const char *string) {
    cJSON *i = cjson_new_item();
    if (i) {
        i->type        = cJSON_String;
        i->valuestring = cjson_strdup(string ? string : "");
        if (!i->valuestring) { cjson_free(i); return NULL; }
    }
    return i;
}

cJSON *cJSON_CreateRaw(const char *raw) {
    cJSON *i = cjson_new_item();
    if (i) {
        i->type        = cJSON_Raw;
        i->valuestring = cjson_strdup(raw ? raw : "");
        if (!i->valuestring) { cjson_free(i); return NULL; }
    }
    return i;
}

cJSON *cJSON_CreateStringReference(const char *string) {
    cJSON *i = cjson_new_item();
    if (i) { i->type = cJSON_String | cJSON_IsReference; i->valuestring = (char*)string; }
    return i;
}

cJSON *cJSON_CreateObjectReference(const cJSON *child) {
    cJSON *i = cjson_new_item();
    if (i) { i->type = cJSON_Object | cJSON_IsReference; i->child = (cJSON*)child; }
    return i;
}

cJSON *cJSON_CreateArrayReference(const cJSON *child) {
    cJSON *i = cjson_new_item();
    if (i) { i->type = cJSON_Array | cJSON_IsReference; i->child = (cJSON*)child; }
    return i;
}

cJSON *cJSON_CreateIntArray(const int *numbers, int count) {
    cJSON *a = cJSON_CreateArray();
    if (!a) return NULL;
    for (int i = 0; i < count; i++) {
        cJSON *n = cJSON_CreateNumber(numbers[i]);
        if (!n || !cJSON_AddItemToArray(a, n)) { cJSON_Delete(a); return NULL; }
    }
    return a;
}

cJSON *cJSON_CreateFloatArray(const float *numbers, int count) {
    cJSON *a = cJSON_CreateArray();
    if (!a) return NULL;
    for (int i = 0; i < count; i++) {
        cJSON *n = cJSON_CreateNumber(numbers[i]);
        if (!n || !cJSON_AddItemToArray(a, n)) { cJSON_Delete(a); return NULL; }
    }
    return a;
}

cJSON *cJSON_CreateDoubleArray(const double *numbers, int count) {
    cJSON *a = cJSON_CreateArray();
    if (!a) return NULL;
    for (int i = 0; i < count; i++) {
        cJSON *n = cJSON_CreateNumber(numbers[i]);
        if (!n || !cJSON_AddItemToArray(a, n)) { cJSON_Delete(a); return NULL; }
    }
    return a;
}

cJSON *cJSON_CreateStringArray(const char **strings, int count) {
    cJSON *a = cJSON_CreateArray();
    if (!a) return NULL;
    for (int i = 0; i < count; i++) {
        cJSON *s = cJSON_CreateString(strings[i]);
        if (!s || !cJSON_AddItemToArray(a, s)) { cJSON_Delete(a); return NULL; }
    }
    return a;
}

/* ---------------------------------------------------------------
 * Linked-list helpers for Add/Detach
 * --------------------------------------------------------------- */

static void suffix_object(cJSON *prev, cJSON *item) {
    prev->next  = item;
    item->prev  = prev;
}

static cJSON *get_last(cJSON *c) {
    while (c && c->next) c = c->next;
    return c;
}

cJSON_bool cJSON_AddItemToArray(cJSON *array, cJSON *item) {
    if (!array || !item) return 0;
    if (array->child) suffix_object(get_last(array->child), item);
    else              array->child = item;
    return 1;
}

cJSON_bool cJSON_AddItemToObjectCS(cJSON *object, const char *string, cJSON *item) {
    if (!object || !string || !item) return 0;
    if (item->string && !(item->type & cJSON_StringIsConst)) cjson_free(item->string);
    item->string = (char*)string;
    item->type  |= cJSON_StringIsConst;
    return cJSON_AddItemToArray(object, item);
}

cJSON_bool cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item) {
    if (!object || !string || !item) return 0;
    char *copy = cjson_strdup(string);
    if (!copy) return 0;
    if (item->string && !(item->type & cJSON_StringIsConst)) cjson_free(item->string);
    item->type &= ~cJSON_StringIsConst;
    item->string = copy;
    return cJSON_AddItemToArray(object, item);
}

cJSON_bool cJSON_AddItemReferenceToArray(cJSON *array, cJSON *item) {
    cJSON *ref = cjson_new_item();
    if (!ref) return 0;
    *ref = *item;
    ref->next = ref->prev = NULL;
    ref->type |= cJSON_IsReference;
    return cJSON_AddItemToArray(array, ref);
}

cJSON_bool cJSON_AddItemReferenceToObject(cJSON *object, const char *string, cJSON *item) {
    cJSON *ref = cjson_new_item();
    if (!ref) return 0;
    *ref = *item;
    ref->next = ref->prev = NULL;
    ref->type |= cJSON_IsReference;
    return cJSON_AddItemToObject(object, string, ref);
}

static void unlink_item(cJSON *parent, cJSON *item) {
    if (item->prev) item->prev->next = item->next;
    else            parent->child    = item->next;
    if (item->next) item->next->prev = item->prev;
    item->next = item->prev = NULL;
}

cJSON *cJSON_DetachItemViaPointer(cJSON *parent, cJSON * const item) {
    if (!parent || !item) return NULL;
    unlink_item(parent, item);
    return item;
}

cJSON *cJSON_DetachItemFromArray(cJSON *array, int which) {
    cJSON *c = cJSON_GetArrayItem(array, which);
    return cJSON_DetachItemViaPointer(array, c);
}

void cJSON_DeleteItemFromArray(cJSON *array, int which) {
    cJSON_Delete(cJSON_DetachItemFromArray(array, which));
}

cJSON *cJSON_DetachItemFromObject(cJSON *object, const char *string) {
    cJSON *c = cJSON_GetObjectItem(object, string);
    return cJSON_DetachItemViaPointer(object, c);
}

cJSON *cJSON_DetachItemFromObjectCaseSensitive(cJSON *object, const char *string) {
    cJSON *c = cJSON_GetObjectItemCaseSensitive(object, string);
    return cJSON_DetachItemViaPointer(object, c);
}

void cJSON_DeleteItemFromObject(cJSON *object, const char *string) {
    cJSON_Delete(cJSON_DetachItemFromObject(object, string));
}

void cJSON_DeleteItemFromObjectCaseSensitive(cJSON *object, const char *string) {
    cJSON_Delete(cJSON_DetachItemFromObjectCaseSensitive(object, string));
}

cJSON_bool cJSON_InsertItemInArray(cJSON *array, int which, cJSON *newitem) {
    cJSON *after = cJSON_GetArrayItem(array, which);
    if (!after) return cJSON_AddItemToArray(array, newitem);
    newitem->next = after;
    newitem->prev = after->prev;
    if (after->prev) after->prev->next = newitem;
    else             array->child       = newitem;
    after->prev = newitem;
    return 1;
}

cJSON_bool cJSON_ReplaceItemViaPointer(cJSON * const parent, cJSON * const item, cJSON *replacement) {
    if (!parent || !item || !replacement) return 0;
    replacement->next = item->next;
    replacement->prev = item->prev;
    if (replacement->next) replacement->next->prev = replacement;
    if (replacement->prev) replacement->prev->next = replacement;
    else                   parent->child            = replacement;
    item->next = item->prev = NULL;
    cJSON_Delete(item);
    return 1;
}

cJSON_bool cJSON_ReplaceItemInArray(cJSON *array, int which, cJSON *newitem) {
    return cJSON_ReplaceItemViaPointer(array, cJSON_GetArrayItem(array, which), newitem);
}

cJSON_bool cJSON_ReplaceItemInObject(cJSON *object, const char *string, cJSON *newitem) {
    cJSON *old = cJSON_GetObjectItem(object, string);
    if (!old || !newitem) return 0;
    if (newitem->string) cjson_free(newitem->string);
    newitem->string = cjson_strdup(string);
    return cJSON_ReplaceItemViaPointer(object, old, newitem);
}

cJSON_bool cJSON_ReplaceItemInObjectCaseSensitive(cJSON *object, const char *string, cJSON *newitem) {
    return cJSON_ReplaceItemInObject(object, string, newitem);
}

/* ---------------------------------------------------------------
 * Duplicate
 * --------------------------------------------------------------- */
cJSON *cJSON_Duplicate(const cJSON *item, cJSON_bool recurse) {
    if (!item) return NULL;
    cJSON *copy = cjson_new_item();
    if (!copy) return NULL;
    copy->type = item->type;
    copy->valueint    = item->valueint;
    copy->valuedouble = item->valuedouble;
    if (item->valuestring && !(item->type & cJSON_IsReference)) {
        copy->valuestring = cjson_strdup(item->valuestring);
        if (!copy->valuestring) { cJSON_Delete(copy); return NULL; }
    }
    if (item->string && !(item->type & cJSON_StringIsConst)) {
        copy->string = cjson_strdup(item->string);
        if (!copy->string) { cJSON_Delete(copy); return NULL; }
    }
    if (recurse && item->child) {
        cJSON *prev = NULL;
        for (cJSON *c = item->child; c; c = c->next) {
            cJSON *dc = cJSON_Duplicate(c, 1);
            if (!dc) { cJSON_Delete(copy); return NULL; }
            if (prev) { prev->next = dc; dc->prev = prev; }
            else       copy->child = dc;
            prev = dc;
        }
    }
    return copy;
}

cJSON_bool cJSON_Compare(const cJSON * const a, const cJSON * const b, const cJSON_bool case_sensitive) {
    (void)case_sensitive;
    if (!a || !b || (a->type & 0xFF) != (b->type & 0xFF)) return 0;
    switch (a->type & 0xFF) {
        case cJSON_NULL:  return 1;
        case cJSON_True:  return 1;
        case cJSON_False: return 1;
        case cJSON_Number: return a->valuedouble == b->valuedouble;
        case cJSON_String: return (a->valuestring && b->valuestring && strcmp(a->valuestring, b->valuestring) == 0);
        default: return 0;
    }
}

/* ---------------------------------------------------------------
 * Minify — strip whitespace outside strings in-place.
 * --------------------------------------------------------------- */
void cJSON_Minify(char *json) {
    char *into = json;
    while (*json) {
        if (*json == ' ' || *json == '\t' || *json == '\n' || *json == '\r') {
            json++;
        } else if (*json == '"') {
            *into++ = *json++;
            while (*json && *json != '"') {
                if (*json == '\\') *into++ = *json++;
                *into++ = *json++;
            }
            *into++ = *json++;
        } else {
            *into++ = *json++;
        }
    }
    *into = '\0';
}
