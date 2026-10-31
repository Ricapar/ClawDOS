/* A small JSON toolkit for the Messages API: find a value by key path in an
 * event (no allocation, no tree), decode and encode strings, walk an object's
 * members. Events are a few hundred bytes to a few KB, so single passes are fine.
 */
#ifndef CLAWDOS_JSON_H
#define CLAWDOS_JSON_H

#include "sink.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { JSON_NONE = 0, JSON_STRING, JSON_NUMBER, JSON_OBJECT, JSON_ARRAY, JSON_TRUE, JSON_FALSE, JSON_NULL };

/* The value at a dotted key path in an object ("delta.text"); returns its type
   (JSON_NONE if absent or the document is malformed/truncated). For a string
   *val points inside the quotes and *len is the raw, still escaped, length. */
int json_find(const char *doc, unsigned doclen, const char *path, const char **val, unsigned *len);

/* Decode a raw JSON string body (no quotes) to UTF-8: \n \t \" \\ \/ \uXXXX,
   surrogate pairs; a lone surrogate becomes '?'. Returns bytes written. */
unsigned json_unescape(const char *raw, unsigned n, char *out, unsigned outmax);

/* CP437 text -> UTF-8 -> JSON-escaped (no quotes) into a sink. 0 ok, -1 sink failed. */
int json_escape_cp437(Sink *s, const char *text, unsigned n);

typedef int (*JsonEachFn)(void *ctx, const char *key, unsigned klen, const char *val, unsigned vlen, int type);

/* Call fn for each member of the object obj (top level only; key is the raw
   string, val/vlen/type as for json_find). Stops at the first non-zero return
   and returns it; 0 at the end; -1 if obj is not a well-formed object. */
int json_each(const char *obj, unsigned len, JsonEachFn fn, void *ctx);

#ifdef __cplusplus
}
#endif

#endif
