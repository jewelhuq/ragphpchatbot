/*
 * cJSON — Embedded JSON parser for the C RAG system.
 *
 * This is a self-contained copy of cJSON 1.7.x (Dave Gamble, MIT License).
 * We embed it directly so the build has no external dependency beyond
 * libcurl and OpenSSL — a single "make" produces everything.
 *
 * MIT License
 * Copyright (c) 2009-2017 Dave Gamble and cJSON contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 */

#ifndef cJSON__h
#define cJSON__h

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/* cJSON_bool type — must be defined before it is used in declarations below */
#ifndef cJSON_bool
#ifdef __cplusplus
#define cJSON_bool bool
#else
#define cJSON_bool int
#endif
#endif

/* cJSON Types */
#define cJSON_Invalid  (0)
#define cJSON_False    (1 << 0)
#define cJSON_True     (1 << 1)
#define cJSON_NULL     (1 << 2)
#define cJSON_Number   (1 << 3)
#define cJSON_String   (1 << 4)
#define cJSON_Array    (1 << 5)
#define cJSON_Object   (1 << 6)
#define cJSON_Raw      (1 << 7)

#define cJSON_IsReference    256
#define cJSON_StringIsConst  512

typedef struct cJSON {
    struct cJSON *next;
    struct cJSON *prev;
    struct cJSON *child;
    int    type;
    char  *valuestring;
    int    valueint;       /* deprecated, use valuedouble */
    double valuedouble;
    char  *string;
} cJSON;

typedef struct cJSON_Hooks {
    void *(*malloc_fn)(size_t sz);
    void  (*free_fn)(void *ptr);
} cJSON_Hooks;

/* Supply malloc/free replacements (optional) */
extern void cJSON_InitHooks(cJSON_Hooks *hooks);

/* Parse a JSON string. Returns NULL on failure. Caller frees with cJSON_Delete. */
extern cJSON *cJSON_Parse(const char *value);
extern cJSON *cJSON_ParseWithLength(const char *value, size_t buffer_length);

/* Render a cJSON entity to text. Caller frees with cJSON_free / free. */
extern char  *cJSON_Print(const cJSON *item);
extern char  *cJSON_PrintUnformatted(const cJSON *item);
extern char  *cJSON_PrintBuffered(const cJSON *item, int prebuffer, cJSON_bool fmt);

/* Delete a cJSON entity and all sub-entities. */
extern void   cJSON_Delete(cJSON *item);

/* Returns the number of items in an array (or object). */
extern int    cJSON_GetArraySize(const cJSON *array);

/* Retrieve item number "index" from array "array". Returns NULL if unsuccessful. */
extern cJSON *cJSON_GetArrayItem(const cJSON *array, int index);

/* Get item "string" from object. Case insensitive. */
extern cJSON *cJSON_GetObjectItem(const cJSON * const object, const char * const string);
extern cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON * const object, const char * const string);
extern int    cJSON_HasObjectItem(const cJSON *object, const char *string);

/* For analysing failed parses. */
extern const char *cJSON_GetErrorPtr(void);

/* Check type */
extern cJSON_bool cJSON_IsBool(const cJSON * const item);
extern cJSON_bool cJSON_IsNull(const cJSON * const item);
extern cJSON_bool cJSON_IsNumber(const cJSON * const item);
extern cJSON_bool cJSON_IsString(const cJSON * const item);
extern cJSON_bool cJSON_IsArray(const cJSON * const item);
extern cJSON_bool cJSON_IsObject(const cJSON * const item);
extern cJSON_bool cJSON_IsRaw(const cJSON * const item);

/* Create basic types */
extern cJSON *cJSON_CreateNull(void);
extern cJSON *cJSON_CreateTrue(void);
extern cJSON *cJSON_CreateFalse(void);
extern cJSON *cJSON_CreateBool(cJSON_bool boolean);
extern cJSON *cJSON_CreateNumber(double num);
extern cJSON *cJSON_CreateString(const char *string);
extern cJSON *cJSON_CreateRaw(const char *raw);
extern cJSON *cJSON_CreateArray(void);
extern cJSON *cJSON_CreateObject(void);

/* Create a string where valuestring references a string (no copy, no free). */
extern cJSON *cJSON_CreateStringReference(const char *string);
/* Create an object/array that only references its elements (no free on delete). */
extern cJSON *cJSON_CreateObjectReference(const cJSON *child);
extern cJSON *cJSON_CreateArrayReference(const cJSON *child);

/* Create arrays of primitives. Handy shortcuts. */
extern cJSON *cJSON_CreateIntArray(const int *numbers, int count);
extern cJSON *cJSON_CreateFloatArray(const float *numbers, int count);
extern cJSON *cJSON_CreateDoubleArray(const double *numbers, int count);
extern cJSON *cJSON_CreateStringArray(const char **strings, int count);

/* Append item to array/object. */
extern cJSON_bool cJSON_AddItemToArray(cJSON *array, cJSON *item);
extern cJSON_bool cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item);
extern cJSON_bool cJSON_AddItemToObjectCS(cJSON *object, const char *string, cJSON *item);
extern cJSON_bool cJSON_AddItemReferenceToArray(cJSON *array, cJSON *item);
extern cJSON_bool cJSON_AddItemReferenceToObject(cJSON *object, const char *string, cJSON *item);

/* Remove/Detach items from Arrays/Objects. */
extern cJSON *cJSON_DetachItemViaPointer(cJSON *parent, cJSON * const item);
extern cJSON *cJSON_DetachItemFromArray(cJSON *array, int which);
extern void   cJSON_DeleteItemFromArray(cJSON *array, int which);
extern cJSON *cJSON_DetachItemFromObject(cJSON *object, const char *string);
extern cJSON *cJSON_DetachItemFromObjectCaseSensitive(cJSON *object, const char *string);
extern void   cJSON_DeleteItemFromObject(cJSON *object, const char *string);
extern void   cJSON_DeleteItemFromObjectCaseSensitive(cJSON *object, const char *string);

/* Update array items. */
extern cJSON_bool cJSON_InsertItemInArray(cJSON *array, int which, cJSON *newitem);
extern cJSON_bool cJSON_ReplaceItemViaPointer(cJSON * const parent, cJSON * const item, cJSON *replacement);
extern cJSON_bool cJSON_ReplaceItemInArray(cJSON *array, int which, cJSON *newitem);
extern cJSON_bool cJSON_ReplaceItemInObject(cJSON *object, const char *string, cJSON *newitem);
extern cJSON_bool cJSON_ReplaceItemInObjectCaseSensitive(cJSON *object, const char *string, cJSON *newitem);

/* Duplicate a cJSON item */
extern cJSON *cJSON_Duplicate(const cJSON *item, cJSON_bool recurse);
extern cJSON_bool cJSON_Compare(const cJSON * const a, const cJSON * const b, const cJSON_bool case_sensitive);

/* Minify a JSON string in-place. */
extern void cJSON_Minify(char *json);

/* Helper macros for adding to objects */
#define cJSON_AddNullToObject(object, name)        cJSON_AddItemToObject(object, name, cJSON_CreateNull())
#define cJSON_AddTrueToObject(object, name)        cJSON_AddItemToObject(object, name, cJSON_CreateTrue())
#define cJSON_AddFalseToObject(object, name)       cJSON_AddItemToObject(object, name, cJSON_CreateFalse())
#define cJSON_AddBoolToObject(object, name, b)     cJSON_AddItemToObject(object, name, cJSON_CreateBool(b))
#define cJSON_AddNumberToObject(object, name, n)   cJSON_AddItemToObject(object, name, cJSON_CreateNumber(n))
#define cJSON_AddStringToObject(object, name, s)   cJSON_AddItemToObject(object, name, cJSON_CreateString(s))
#define cJSON_AddRawToObject(object, name, s)      cJSON_AddItemToObject(object, name, cJSON_CreateRaw(s))

/* Iteration macro */
#define cJSON_ArrayForEach(element, array) \
    for (element = (array != NULL) ? (array)->child : NULL; element != NULL; element = element->next)

/* Memory management */
extern void *cJSON_malloc(size_t size);
extern void  cJSON_free(void *object);

#ifdef __cplusplus
}
#endif

#endif /* cJSON__h */
