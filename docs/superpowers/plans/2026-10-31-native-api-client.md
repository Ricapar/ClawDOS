# Native API Client Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** CLAWDOS.EXE talks the Anthropic Messages API itself over a dumb TLS tunnel; the Python bridge and its protocol disappear.

**Architecture:** Small C modules in lib/ do the wire work (UTF-8/CP437, JSON, chunked HTTP, SSE, config, on-disk history, request writer, stream-to-history), each unit-tested in DOSUNIT.EXE. A C++ connection class over mTCP carries bytes. clawdos.cpp's state machine drives the agent loop; tools.cpp runs one call from a JSON input. Python shrinks to `tunnel/tunnel.py` (byte copier) and `tunnel/mockapi.py` (fake API for boot tests), stdlib only.

**Tech Stack:** Open Watcom C/C++ (`-0`, large model), mTCP, NASM; Python 3 stdlib; DOSBox-X boot tests.

**Spec:** docs/superpowers/specs/2026-10-31-native-api-client-design.md

## Global Constraints

- 8086 instruction set only (`wcc/wpp -0`); Watcom large model; no quoted `-D` values; wlink lists in .lnk files.
- New C modules are C89-style C (compile with wcc; `extern "C"` headers) so tests/unit/dosunit.c can link them. Only code touching mTCP is C++.
- Data files on the PC: `C:\CLAWDOS\CLAWDOS.CFG`, `PROMPT.TXT`, `TOOLS.JSN`, `CHATS\*.JSN`, `CLAWDOS.LOG`; 8.3 names.
- Buffers: SSE data line 16 KB, socket read 2 KB, tool arguments 16 KB, all in far heap (`malloc` in the large model).
- Request fields and values exactly as the spec's "The request": `max_tokens` 16000, `anthropic-version: 2023-06-01`, `tool_choice {"type":"auto","disable_parallel_tool_use":true}`, `{"type":"none"}` on round 26, `cache_control` on `system[0]` and top level unless `cache 0`, no `thinking`, no `fallbacks`.
- Retries: 2 s then 8 s on 429/529/5xx or a TCP error before any body byte; `retry-after` wins. One re-ask per turn after a mid-stream drop. 25 tool rounds, then the "Tool limit reached; answer now without tools." note with `tool_choice none`.
- Tool result texts for the model: ok → the output; error → `Error: ` + output; denied → `The user denied this tool call.`; stopped → `Interrupted by the user.`; `is_error` true unless ok.
- Status notes stay as today where the spec keeps them: `[reply cut off: too long]`, "The model declined to answer that.", `Interrupted by user`, and the new `Clock set from the API`, `[link lost, asking again]`, `connection #n`.
- The old bridge path keeps building until Task 15's tools.bat passes; Task 17 deletes it in one commit.
- Commit after every task (checkpoints on `native-api`; never merge).

## Review Focus

Inputs the spec implies but that a casual implementation would get wrong; each has a test in the owning task:

1. A `text_delta` whose JSON string ends in a `\uD83D` high surrogate with the low half in the next delta: show `?` once, never crash (Task 2 json_unescape lone-surrogate test; Task 8 split-surrogate event test).
2. A chunked body where a chunk boundary falls inside the `\r\n` of a chunk-size line or inside an SSE `data:` prefix (Task 3 boundary tests, every split position).
3. A history file left with a dangling `,\r\n` or a half-written message after a power cut, reopened next run: `/resume`-style reopening is out of scope, but `hist_open` on an existing file must still produce a valid array when copied (Task 6 reopen-after-rollback test).
4. CLAWDOS.CFG with CRLF line ends, trailing spaces, a `key` line containing `#`, and a provider section missing `tunnel` (Task 5 tests).
5. An HTTP response with `Content-Length` and no chunking (the mock's error responses, a proxy that de-chunks), and a `Connection: close` server that ends the body by closing (Task 4 head tests; Task 11 read-to-close).

---

### Task 1: UTF-8 ↔ CP437

**Files:**
- Create: `lib/utf8.h`, `lib/utf8.c`
- Modify: `tests/unit/dosunit.c` (new `testUtf8`, call from `main`), `scripts/build.py:55-57` (add `lib/utf8.c` to `dosunit.exe` sources)

**Interfaces:**
- Produces:
  - `unsigned utf8_to_cp437(const char *in, unsigned n, char *out, unsigned outmax);` → bytes written (no NUL). Decodes UTF-8; code points CP437 has map to their byte; the fallbacks of bridge/cp437.py `CP437_FALLBACKS` (quotes → `'`/`"`, `–` → `-`, `—` → `--`, `…` → `...`, `→` → `->`, `•` → `\xF9`, `×` → `x`, `≤` → `<=`, `✓` → `v`, `©` → `(c)`, `€` → `EUR`, zero-width chars → nothing, tab → 4 spaces, the rest of that table); Latin letters with accents CP437 lacks → the bare letter (a fixed table for U+00C0–U+017F); anything else, and invalid sequences, → `?`. Stops when `out` is full.
  - `unsigned cp437_to_utf8(unsigned char c, char out[4]);` → 1-3 bytes written. Bytes 0x80-0xFF through the standard CP437 table; 0x01-0x1F through the CP437 glyph table (0x07 → U+2022 `•`, 0x10 → U+25BA, ...) except `\t`, `\n`, `\r`, which stay as they are.

- [ ] **Step 1: Write the failing tests** in dosunit.c:

```c
static void testUtf8(void)
{
    char out[64]; unsigned n;
    n = utf8_to_cp437("caf\xC3\xA9 \xE2\x80\x94 \xE2\x80\x9Cok\xE2\x80\x9D", 17, out, sizeof out); out[n] = 0;
    CHECK_STR("utf8 accents and quotes", out, "caf\x82 -- \"ok\"");
    n = utf8_to_cp437("\xE2\x94\x80\xE2\x96\x88", 6, out, sizeof out); out[n] = 0;
    CHECK_STR("utf8 box drawing", out, "\xC4\xDB");
    n = utf8_to_cp437("\xC5\x81\xC3\xB8 \xF0\x9F\x98\x80", 9, out, sizeof out); out[n] = 0;
    CHECK_STR("utf8 fallbacks", out, "Lo ?");
    n = utf8_to_cp437("a\xE2\x80\x8Bb\tc", 7, out, sizeof out); out[n] = 0;
    CHECK_STR("utf8 zero width and tab", out, "ab    c");
    n = utf8_to_cp437("\xC3", 1, out, sizeof out); out[n] = 0;
    CHECK_STR("utf8 truncated sequence", out, "?");
    n = utf8_to_cp437("abcdef", 6, out, 3);
    CHECK("utf8 respects outmax", n == 3);
    n = cp437_to_utf8(0x82, out); out[n] = 0;
    CHECK_STR("cp437 e-acute", out, "\xC3\xA9");
    n = cp437_to_utf8(0xC4, out); out[n] = 0;
    CHECK_STR("cp437 box", out, "\xE2\x94\x80");
    n = cp437_to_utf8('A', out); out[n] = 0;
    CHECK_STR("cp437 ascii", out, "A");
    n = cp437_to_utf8(0x07, out); out[n] = 0;
    CHECK_STR("cp437 bullet glyph", out, "\xE2\x80\xA2");
    n = cp437_to_utf8('\n', out);
    CHECK("cp437 newline stays", n == 1 && out[0] == '\n');
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `python3 scripts/build.py && python3 scripts/boottest.py image/tests/units.bat --expect UNITS-OK`
Expected: build error (utf8.h missing).

- [ ] **Step 3: Implement lib/utf8.c** with a 128-entry `unsigned short cp437_high[128]` table (0x80-0xFF → Unicode), a 32-entry glyph table for 0x00-0x1F, and the fallback table as `{code point, const char *}` pairs; the reverse lookup for `utf8_to_cp437` scans the 128-entry table (256 compares per character is fine at these sizes).

- [ ] **Step 4: Run the unit boot test; expect `UNITS-OK`.**

- [ ] **Step 5: Commit** `git commit -m "Add UTF-8 <-> CP437 conversion with CP437 fallbacks"`

---

### Task 2: JSON tokenizer, escapes, object iteration

**Files:**
- Create: `lib/sink.h`, `lib/sink.c`, `lib/json.h`, `lib/json.c`
- Modify: `tests/unit/dosunit.c`, `scripts/build.py` (dosunit sources + `lib/sink.c`, `lib/json.c`, `lib/utf8.c`)

**Interfaces:**
- Produces (lib/sink.h):
  ```c
  typedef struct Sink {
      int (*put)(struct Sink *s, const char *p, unsigned n);  /* NULL: count only */
      void *ctx;
      unsigned long count;                                     /* bytes offered so far */
  } Sink;
  int sink_put(Sink *s, const char *p, unsigned n);   /* adds to count, calls put; 0 ok, -1 put failed */
  int sink_puts(Sink *s, const char *z);
  void sink_count(Sink *s);                           /* init as a counting sink */
  void sink_buffer(Sink *s, char *buf, unsigned max); /* init as a buffer sink; stops at max, NUL-terminates */
  ```
- Produces (lib/json.h):
  ```c
  enum { JSON_NONE = 0, JSON_STRING, JSON_NUMBER, JSON_OBJECT, JSON_ARRAY, JSON_TRUE, JSON_FALSE, JSON_NULL };
  /* Value at a dotted key path in an object ("delta.text"). For a string, *val
     points inside the quotes and *len is the raw (still escaped) length. */
  int json_find(const char *doc, unsigned doclen, const char *path, const char **val, unsigned *len);
  /* Decode a raw JSON string body to UTF-8 (\uXXXX, surrogate pairs; a lone
     surrogate becomes '?'). Returns bytes written. */
  unsigned json_unescape(const char *raw, unsigned n, char *out, unsigned outmax);
  /* CP437 text -> UTF-8 -> JSON-escaped, without the quotes, into a sink. */
  int json_escape_cp437(Sink *s, const char *text, unsigned n);
  typedef int (*JsonEachFn)(void *ctx, const char *key, unsigned klen, const char *val, unsigned vlen, int type);
  /* Call fn for each member of the object `obj` (top level only). Stops at the first non-zero return. */
  int json_each(const char *obj, unsigned len, JsonEachFn fn, void *ctx);
  ```

- [ ] **Step 1: Write the failing tests** (dosunit.c `testJson`):

```c
static const char EV[] = "{\"type\":\"content_block_delta\",\"index\":0,"
    "\"delta\":{\"type\":\"text_delta\",\"text\":\"Hi \\\"there\\\" \\u00e9\\n\"},\"n\":[1,{\"x\":2}],\"ok\":true}";
static int eachCount; static char eachKeys[64];
static int eachFn(void *c, const char *k, unsigned kl, const char *v, unsigned vl, int t)
{ (void)c; (void)v; (void)vl; eachCount++; sprintf(eachKeys + strlen(eachKeys), "%.*s=%d ", kl, k, t); return 0; }

static void testJson(void)
{
    const char *v; unsigned n; char out[64]; Sink s;
    CHECK("json find string", json_find(EV, sizeof EV - 1, "delta.text", &v, &n) == JSON_STRING
          && n == 26 && !memcmp(v, "Hi \\\"there\\\" \\u00e9\\n", 26));
    CHECK("json find number", json_find(EV, sizeof EV - 1, "index", &v, &n) == JSON_NUMBER && n == 1 && *v == '0');
    CHECK("json find true", json_find(EV, sizeof EV - 1, "ok", &v, &n) == JSON_TRUE);
    CHECK("json find array", json_find(EV, sizeof EV - 1, "n", &v, &n) == JSON_ARRAY && n == 9);
    CHECK("json find missing", json_find(EV, sizeof EV - 1, "delta.nope", &v, &n) == JSON_NONE);
    CHECK("json find skips nested", json_find(EV, sizeof EV - 1, "x", &v, &n) == JSON_NONE);
    n = json_unescape("Hi \\\"there\\\" \\u00e9\\n", 26, out, sizeof out); out[n] = 0;
    CHECK_STR("json unescape", out, "Hi \"there\" \xC3\xA9\n");
    n = json_unescape("\\ud83d\\ude00!", 13, out, sizeof out); out[n] = 0;
    CHECK_STR("json unescape surrogate pair", out, "\xF0\x9F\x98\x80!");
    n = json_unescape("a\\ud83d", 7, out, sizeof out); out[n] = 0;
    CHECK_STR("json unescape lone surrogate", out, "a?");
    n = json_unescape("\\/\\\\\\t", 6, out, sizeof out); out[n] = 0;
    CHECK_STR("json unescape slash backslash tab", out, "/\\\t");
    sink_buffer(&s, out, sizeof out);
    json_escape_cp437(&s, "say \"hi\"\n\x82\xC4\x01", 13);
    CHECK_STR("json escape cp437", out, "say \\\"hi\\\"\\n\xC3\xA9\xE2\x94\x80\xE2\x98\xBA");
    CHECK("json escape count", s.count == 25);
    eachCount = 0; eachKeys[0] = 0;
    json_each(EV, sizeof EV - 1, eachFn, NULL);
    CHECK_STR("json each keys", eachKeys, "type=1 index=2 delta=3 n=4 ok=5 ");
    CHECK("json find in truncated doc", json_find(EV, 20, "delta.text", &v, &n) == JSON_NONE);
}
```

- [ ] **Step 2: Run the units boot test; expect a build failure.**
- [ ] **Step 3: Implement** sink.c and json.c. `json_find`: one pass with a depth counter; at depth 1 compare keys against the current path segment, descend into the matching object, repeat; `json_skip_value` (static) skips strings with escapes, nested `{}`/`[]`, literals. Control characters 0x00-0x1F in `json_escape_cp437` go out as `\u00XX`; `"` and `\` escaped; `\n`, `\r`, `\t` as the short forms.
- [ ] **Step 4: Run the units boot test; expect `UNITS-OK`.**
- [ ] **Step 5: Commit** `git commit -m "Add a small JSON tokenizer, escapes and a byte sink"`

---

### Task 3: Chunked transfer decoding and SSE event splitting

**Files:**
- Create: `lib/chunked.h`, `lib/chunked.c`, `lib/sse.h`, `lib/sse.c`
- Modify: `tests/unit/dosunit.c`, `scripts/build.py`

**Interfaces:**
- Produces (lib/chunked.h):
  ```c
  typedef struct Chunked { int state; unsigned long left; int done; } Chunked;
  typedef int (*ByteFn)(void *ctx, const char *p, unsigned n);   /* 0 ok */
  void chunked_init(Chunked *c);
  /* Feed raw body bytes; decoded payload goes to out. Returns 0, 1 once the
     terminating chunk (and its trailer CRLF) is consumed, -1 on a malformed stream. */
  int chunked_feed(Chunked *c, const char *in, unsigned n, ByteFn out, void *ctx);
  ```
- Produces (lib/sse.h):
  ```c
  typedef void (*SseEventFn)(void *ctx, const char *event, const char *data, unsigned datalen);
  typedef struct Sse { char *buf; unsigned max, len; char event[32]; int overflow; } Sse;
  void sse_init(Sse *s, char *buf, unsigned max);
  /* Feed decoded body bytes. Lines: "event: x", "data: y" (a second data line
     appends with '\n'), ":" comments, blank line = dispatch. Returns 0, or -1
     once a data payload exceeds max (the event is dropped, s->overflow set). */
  int sse_feed(Sse *s, const char *in, unsigned n, SseEventFn fn, void *ctx);
  ```

- [ ] **Step 1: Write the failing tests** (`testChunked`, `testSse`): feed the body `"5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n"` whole and then split at every byte position (loop over split points, reinitialising), asserting the collected output is `"hello world"` and the return is 1 at the end; a chunk size with an extension `"5;ext=1\r\n"` works; `"zz\r\n"` returns -1. SSE: feed `"event: a\ndata: {\"x\":1}\n\n: ping\n\ndata: l1\ndata: l2\n\n"` whole and byte by byte, asserting events `a:{"x":1}` and `:l1\nl2` (event name empty), also with `\r\n` line ends; a data line of max+1 bytes returns -1 with `overflow` set and the following short event still dispatches.
- [ ] **Step 2: Run; expect a build failure.**
- [ ] **Step 3: Implement** both as byte-at-a-time state machines (sizes are small; the 8088 cost is negligible next to the network).
- [ ] **Step 4: Run; expect `UNITS-OK`.**
- [ ] **Step 5: Commit** `git commit -m "Add chunked transfer decoding and SSE event splitting"`

---

### Task 4: HTTP head parsing and the Date header

**Files:**
- Create: `lib/httpp.h`, `lib/httpp.c` (parsing only; the connection class is Task 11)
- Modify: `tests/unit/dosunit.c`, `scripts/build.py`

**Interfaces:**
- Produces:
  ```c
  typedef struct HttpHead {
      int status;                 /* 200, 429, ... ; 0 if the status line is bad */
      long contentLength;         /* -1 if absent */
      int chunked;                /* Transfer-Encoding: chunked */
      int close;                  /* Connection: close */
      int retryAfter;             /* seconds, 0 if absent/not a number */
      char date[32];              /* raw Date value */
  } HttpHead;
  /* Parse "HTTP/1.1 200 OK\r\nName: value\r\n...\r\n\r\n" (head only, NUL-terminated). 0 ok, -1 malformed. */
  int http_parse_head(const char *head, HttpHead *h);
  /* "Fri, 31 Oct 2026 14:03:07 GMT" -> fields; 0 ok, -1 bad. */
  int http_parse_date(const char *date, int *y, int *mo, int *d, int *h, int *mi, int *s);
  /* Shift a date/time by `hours` (may be negative), handling day/month/year carries. */
  void http_shift_time(int *y, int *mo, int *d, int *h, int hours);
  ```

- [ ] **Step 1: Write the failing tests**: a 200 chunked head with `Date`, `transfer-encoding: Chunked` in mixed case and `Connection: keep-alive`; a 529 head with `Content-Length: 87` and `retry-after: 12`; a `Connection: close` head; a head without a blank line → -1; `http_parse_date("Fri, 31 Oct 2026 23:30:00 GMT", ...)` then `http_shift_time(..., -5)` → 31 Oct 18:30 and `http_shift_time(..., +2)` → 1 Nov 01:30; 28 Feb 2028 23:00 +2 → 29 Feb.
- [ ] **Step 2: Run; expect a build failure.**
- [ ] **Step 3: Implement** with `strnicmp` header matching, line by line.
- [ ] **Step 4: Run; expect `UNITS-OK`.**
- [ ] **Step 5: Commit** `git commit -m "Add HTTP response head and Date parsing"`

---

### Task 5: CLAWDOS.CFG parser

**Files:**
- Create: `lib/cfg.h`, `lib/cfg.c`
- Modify: `tests/unit/dosunit.c`, `scripts/build.py`

**Interfaces:**
- Produces:
  ```c
  #define CFG_PROVIDERS 8
  typedef struct Provider {
      char id[16];          /* [section] */
      char type[12];        /* "anthropic" */
      char tunnel[48];      /* host:port of the TLS tunnel */
      char host[64];        /* Host header */
      char path[48];        /* default "/v1/messages" */
      char auth[12];        /* "x-api-key" (default) or "bearer" */
      char key[128];
      char keyfile[64];     /* read into key by cfg_load if key is empty */
      char model[48];
      char effort[8];       /* "" = omit output_config */
      char betas[64];       /* "" = no anthropic-beta header */
      char name[24];        /* assistant name; "" = the global one */
      int cache;            /* default 1 */
  } Provider;
  typedef struct Cfg {
      char provider[16];    /* startup section */
      char theme[16];
      int timezone;         /* hours from UTC */
      char name[24];        /* default "Claude" */
      int nproviders;
      Provider providers[CFG_PROVIDERS];
  } Cfg;
  void cfg_defaults(Cfg *c);
  /* Parse text (LF or CRLF; '#' starts a comment unless inside a key/keyfile value;
     "key value" with one or more spaces/tabs). Returns 0, or the 1-based line of the first error. */
  int cfg_parse(Cfg *c, const char *text);
  int cfg_load(Cfg *c, const char *path);         /* file -> cfg_parse, then keyfile -> key; -1 if unreadable */
  Provider *cfg_provider(Cfg *c, const char *id);  /* NULL if unknown; case-insensitive */
  /* What a provider section lacks to be usable ("tunnel", "host", "key", "model"), or NULL. */
  const char *cfg_missing(const Provider *p);
  ```

- [ ] **Step 1: Write the failing tests** with the spec's example config as a CRLF string (`key sk-ant-abc#def` keeps the `#`; `theme claude   ` trailing spaces; a comment line; `[openrouter]` with `auth bearer`); assert `nproviders == 2`, `timezone == -5`, `cfg_provider(c, "ANTHROPIC")->path` is `/v1/messages` by default, `->cache == 1`, `->auth` is `x-api-key`, openrouter's `auth` is `bearer`; a section with no `tunnel` → `cfg_missing` returns `"tunnel"`; an unknown global key → returns its line number; a ninth section → the line number.
- [ ] **Step 2: Run; expect a build failure.**
- [ ] **Step 3: Implement** (the `Cfg` struct is 4+ KB: callers allocate it with `malloc`, never as a static in DGROUP).
- [ ] **Step 4: Run; expect `UNITS-OK`.**
- [ ] **Step 5: Commit** `git commit -m "Add the CLAWDOS.CFG parser"`

---

### Task 6: The history file

**Files:**
- Create: `lib/history.h`, `lib/history.c`
- Modify: `tests/unit/dosunit.c`, `scripts/build.py`

**Interfaces:**
- Produces:
  ```c
  /* Open (create or append). Scans an existing file so a dangling separator or
     a half message after the last complete one is truncated away. 0 ok, -1 error. */
  int  hist_open(const char *path);
  void hist_close(void);
  long hist_size(void);                 /* bytes a hist_copy would produce */
  /* Append raw bytes of the current message. The first write after a boundary
     (and a non-empty file) first emits the ",\r\n" separator. */
  int  hist_write(const char *p, unsigned n);
  int  hist_puts(const char *z);
  int  hist_message_done(void);         /* the message is complete: new boundary */
  void hist_turn_begin(void);           /* remember where this turn started */
  int  hist_rollback_message(void);     /* truncate to the last boundary */
  int  hist_rollback_turn(void);        /* truncate to the turn start */
  int  hist_turn_messages(void);        /* complete messages since turn_begin */
  int  hist_copy(Sink *s);              /* file contents (no brackets) into the sink */
  ```
  Implementation uses `open`/`read`/`write`/`lseek`/`chsize` (Watcom io.h) with a 512-byte copy buffer in far heap.

- [ ] **Step 1: Write the failing tests** on `C:\HISTTEST.JSN` (deleted first): open, write `{"role":"user","content":"a"}`, done, write `{"role":"assistant"...}` partially, `hist_rollback_message` → copy gives exactly the first message, no separator; write a second message, done → copy gives `m1,\r\nm2`; `hist_turn_begin`, write two complete messages, `hist_turn_messages() == 2`, `hist_rollback_turn` → back to `m1,\r\nm2`; close, append raw bytes `,\r\n{"role":"us` by hand, `hist_open` again → copy still gives `m1,\r\nm2` (Review Focus 3); `hist_size` matches the copy's byte count.
- [ ] **Step 2: Run; expect a build failure.**
- [ ] **Step 3: Implement.** The reopen scan reads the file backwards from the end looking for the last `}` that is followed only by an optional `,\r\n` and partial bytes; simpler and adequate: remember the boundary offset in a sidecar is *not* done; scan forward with a depth counter over the whole file instead (files are at most a few hundred KB; this happens once per run).
- [ ] **Step 4: Run; expect `UNITS-OK`.**
- [ ] **Step 5: Commit** `git commit -m "Add the on-disk conversation history"`

---

### Task 7: Request body writer

**Files:**
- Create: `lib/apireq.h`, `lib/apireq.c`
- Modify: `tests/unit/dosunit.c`, `scripts/build.py`

**Interfaces:**
- Consumes: `Sink`, `json_escape_cp437`, `hist_copy`, `Provider`.
- Produces:
  ```c
  typedef struct ApiReq {
      const Provider *p;
      const char *promptPath;      /* PROMPT.TXT: CP437 text with {name} and {memories} */
      const char *toolsPath;       /* TOOLS.JSN, copied verbatim */
      const char *name;            /* for {name} */
      const char *memories;        /* for {memories}; "(none)" if empty */
      int noTools;                 /* round 26: tool_choice none */
  } ApiReq;
  /* Write the JSON body to the sink (count or send). 0 ok, -1 a file could not be read, -2 the sink failed. */
  int api_body(Sink *s, const ApiReq *r);
  /* Write the request head ("POST path HTTP/1.1\r\n" + headers + "\r\n") for a body of `length` bytes. */
  int api_head(Sink *s, const Provider *p, unsigned long length);
  ```

- [ ] **Step 1: Write the failing tests**: with `C:\PTEST.TXT` = `Hi {name}.\r\nMem:\r\n{memories}` and `C:\TTEST.JSN` = `[{"name":"t"}]`, a history of one user message, provider effort `low`, cache 1: the counting sink's `count` equals the buffer sink's length; the buffer starts with `{"model":"claude-opus-5-5","max_tokens":16000,"stream":true,"output_config":{"effort":"low"},"cache_control":{"type":"ephemeral"},"tool_choice":{"type":"auto","disable_parallel_tool_use":true},"system":[{"type":"text","text":"Hi Claude.\nMem:\n(none)","cache_control":{"type":"ephemeral"}}],"tools":[{"name":"t"}],"messages":[`; ends with `]}`; with `effort ""` there is no `output_config`; with `cache 0` no `cache_control` anywhere; `noTools` gives `"tool_choice":{"type":"none"}`; `api_head` for auth `x-api-key` contains `x-api-key: sk-ant-abc\r\n`, `anthropic-version: 2023-06-01\r\n`, `Content-Length: 123\r\n`, `Host: api.anthropic.com\r\n`, `Accept: text/event-stream\r\n`, `Connection: keep-alive\r\n`, no `anthropic-beta`; auth `bearer` gives `Authorization: Bearer sk-or-x\r\n`; `betas` set gives `anthropic-beta: <value>\r\n`.
- [ ] **Step 2: Run; expect a build failure.**
- [ ] **Step 3: Implement.** CRLF in PROMPT.TXT becomes `\n`; the prompt is read in 512-byte pieces and placeholders are matched across piece boundaries by buffering from a `{` until the matching `}` or 12 bytes.
- [ ] **Step 4: Run; expect `UNITS-OK`.**
- [ ] **Step 5: Commit** `git commit -m "Add the Messages API request writer"`

---

### Task 8: Stream events → screen text, history, tool call

**Files:**
- Create: `lib/apistream.h`, `lib/apistream.c`
- Modify: `tests/unit/dosunit.c`, `scripts/build.py`

**Interfaces:**
- Consumes: `json_find`, `json_unescape`, `utf8_to_cp437`, `hist_write`/`hist_puts` (through a callback so the test can capture).
- Produces:
  ```c
  typedef struct ApiStream {
      /* callbacks */
      void (*text)(void *ctx, const char *cp437, unsigned n);  /* visible reply text */
      void (*thinking)(void *ctx);                             /* a thinking delta arrived */
      int  (*hist)(void *ctx, const char *p, unsigned n);      /* bytes of the assistant message */
      void *ctx;
      /* results */
      char stopReason[16];       /* end_turn, tool_use, max_tokens, refusal, ... */
      char model[48];
      long inTok, outTok, cacheRead, cacheWrite;
      int  hasTool;
      char toolId[64], toolName[24];
      char *toolInput; unsigned toolInputLen, toolInputMax; int toolInputOverflow;
      char error[160];           /* set when apistream_event returns -1 */
      /* private state */
      int blocks, inBlock, blockType, sigOpen;
  } ApiStream;
  void apistream_init(ApiStream *a, char *toolInputBuf, unsigned max);
  /* One SSE event. 0 continue, 1 message_stop seen, -1 error (a->error says why). */
  int apistream_event(ApiStream *a, const char *event, const char *data, unsigned n);
  ```
  History bytes emitted: on `message_start` → `{"role":"assistant","content":[`; per block `{"type":"text","text":"` + raw deltas + `"}`; `{"type":"thinking","thinking":"` + raw deltas + `","signature":"` + sig + `"}`; `{"type":"redacted_thinking","data":"<data>"}`; `{"type":"tool_use","id":"<id>","name":"<name>","input":` + raw partial_json (or `{}` if none) + `}`; commas between blocks; on `message_stop` → `]}`. Unknown block type → -1 with error `unknown block type X`. `error` event → -1 with `error.message` unescaped into `error`. `message_delta` fills `stopReason` and `outTok`; `message_start` fills `model`, `inTok`, `cacheRead`, `cacheWrite`.

- [ ] **Step 1: Write the failing tests** feeding a canned sequence: message_start (usage input 100, cache_read 50), a thinking block (`thinking_delta` ×2, `signature_delta` `abc`), a text block with deltas `"Hi \u00e9"` and `"!"`, a tool_use block (`id` `toolu_1`, `name` `read`, partial_json `{"path":` and `"C:\\A.TXT"}`), message_delta (`stop_reason` `tool_use`, output 7), message_stop. Assert: text callback received `Hi \x82!`, thinking callback hit twice, the captured history is exactly the concatenation above, `hasTool`, `toolId`, `toolName`, `toolInput` equals `{"path":"C:\\A.TXT"}`, stopReason `tool_use`, usage numbers, return 1 at message_stop. Second test: a split surrogate across two text deltas (`"\ud83d"` then `"\ude00"`) yields `??` on screen but the history keeps both raw strings (Review Focus 1). Third: `event: error` with `{"type":"error","error":{"type":"overloaded_error","message":"Overloaded"}}` → -1, error `Overloaded`. Fourth: a block of type `fallback` → -1. Fifth: partial_json totalling more than `max` → `toolInputOverflow`, history still complete.
- [ ] **Step 2: Run; expect a build failure.**
- [ ] **Step 3: Implement.** Text deltas: `json_unescape` into a 512-byte far scratch, then `utf8_to_cp437` into another, then `text()`; the raw escaped delta goes to `hist`.
- [ ] **Step 4: Run; expect `UNITS-OK`.**
- [ ] **Step 5: Commit** `git commit -m "Add the stream handler: events to screen text, history and tool calls"`

---

### Task 9: The mock API server (Python)

**Files:**
- Create: `tunnel/mockapi.py`, `tunnel/test_mockapi.py`
- Reuse: `bridge/mockchat.py` scenario generators (copy `MockChat` and `tour_steps` into `tunnel/mockchat.py`; the bridge's copy is deleted in Task 17)

**Interfaces:**
- Produces: `python3 tunnel/mockapi.py [--port 18023] [--speed 1.0] [--tools image/clawdos/TOOLS.JSN]`: an `http.server`-based (ThreadingHTTPServer) fake of `POST /v1/messages` with `stream: true`. Scenarios as in the spec (`!help`, `!scroll`, `!burst`, `!long`, `!wide`, `!cp437`, `!markdown`, `!slow`, `!stall`, `!error`, `!refuse`, `!cutoff`, `!drop`, `!history`, `!run`, `!tool`, `!tooltour`, `!play`, plus `!retry`, `!chunks`, `!bigevent`). Event framing: `message_start` (model `mock`, usage), one `thinking` block with an empty `thinking` and a 1500-byte base64 `signature` on every reply (so the client always exercises that path), `text` blocks as `text_delta`s, `tool_use` with `input_json_delta` split in two, `message_delta`, `message_stop`; chunked transfer encoding with chunk boundaries at natural write points except `!chunks`, which splits every 7 bytes. Tool results arrive in the next request's last user message (`tool_result` blocks); the mock keeps one pending scenario generator and resumes it with the result. `!tooltour` logs `tool tour: PASS` or `tool tour: FAIL ...` to stdout. `!error` sends an `error` event after a few words; `!retry` answers the first request of a conversation with HTTP 529 (`{"type":"error","error":{"type":"overloaded_error","message":"Overloaded"}}`, `Content-Length`, `retry-after: 1`) and the next normally with the text `second attempt`; `!drop` closes the socket after a few words (once per conversation, then answers `after the drop`); `!bigevent` sends a 20 KB `text_delta`; `!cutoff` stops with `max_tokens`; `!refuse` stops with `refusal`. Every request is validated: a parse failure or a `messages` array whose roles don't alternate returns 400 with the reason in `error.message`, and the mock logs `request: N messages, last role R`.
- `tunnel/test_mockapi.py` (unittest, stdlib): starts the server on an ephemeral port, posts with `http.client`, checks each scenario's events parse as JSON, the thinking signature is present, tool_use input reassembles, `!retry` returns 529 then 200, `!chunks` output equals the unsplit output, a bad body gives 400.

- [ ] **Step 1: Write test_mockapi.py (failing: no module).**
- [ ] **Step 2: Run `python3 tunnel/test_mockapi.py`; expect ImportError.**
- [ ] **Step 3: Implement mockapi.py** (async generator scenarios run under a private event loop per request thread, or convert MockChat's `async def` generators to plain generators with `time.sleep`; choose the latter and drop `asyncio`).
- [ ] **Step 4: Run the tests; expect OK.**
- [ ] **Step 5: Commit** `git commit -m "Add the mock Messages API server for boot tests"`

---

### Task 10: The TLS tunnel (Python)

**Files:**
- Create: `tunnel/tunnel.py`
- Modify: `Makefile` (`tunnel` target replaces `bridge`; `mock` runs mockapi; `test` runs test_mockapi with `$(PYTHON)`; drop the venv targets once Task 17 removes requirements.txt)

**Interfaces:**
- Produces: `python3 tunnel/tunnel.py [--map PORT=HOST:443]...` (default `8443=api.anthropic.com:443`); asyncio; per client connection an `ssl.create_default_context()` connection with SNI; two copy coroutines; logs `open`/`close` with peer and byte counts only; `--verbose` adds nothing about content. Also document in its docstring the equivalents: `socat TCP-LISTEN:8443,fork,reuseaddr OPENSSL:api.anthropic.com:443` and an stunnel stanza.

- [ ] **Step 1: Write tunnel.py.**
- [ ] **Step 2: Verify by hand:** `python3 tunnel/tunnel.py & printf 'GET / HTTP/1.1\r\nHost: api.anthropic.com\r\nConnection: close\r\n\r\n' | nc localhost 8443 | head -1` prints an HTTP status line.
- [ ] **Step 3: Commit** `git commit -m "Add the TLS tunnel: plain TCP in, HTTPS out, no logic"`

---

### Task 11: Network connection and HTTP client (C++)

**Files:**
- Create: `lib/net.h`, `lib/net.cpp` (from lib/dosbridge.cpp: stack, config, counters, DNS, socket; minus the line protocol), `lib/http.h`, `lib/http.cpp`
- Keep `lib/dosbridge.cpp` building until Task 17.

**Interfaces:**
- Produces (lib/net.h), the static half of today's `DOSBridge` unchanged in name except the class: `Net::startStack`, `Net::stopStack`, `Net::service`, `Net::localAddress`, `Net::counters`, `Net::configValue`, `Net::breakPressed`; and
  ```cpp
  class NetConn {
    public:
      int8_t connect(const char *host, uint16_t port);   /* "a.b.c.d" or name; 0 ok */
      void   close();
      int    isConnected();
      int    send(const char *p, unsigned n);            /* all or -1 */
      int    recv(char *buf, unsigned max);              /* bytes, 0 none yet, -1 gone */
      const char *errorText();
  };
  ```
- Produces (lib/http.h):
  ```cpp
  class Http {
    public:
      int8_t open(const char *hostport);                 /* "host:port"; 0 ok */
      void   close();
      int    isOpen();
      Sink  *sink();                                     /* writes go to the socket */
      int8_t sendHead(const Provider *p, unsigned long length);   /* api_head into the socket */
      /* Read status and headers (<= 2 KB) within `secs`. 0 ok, -1 link lost, -2 timeout, -3 malformed. */
      int8_t readHead(HttpHead *h, int secs);
      /* Decoded body bytes (chunked or Content-Length or to-close). >0 bytes, 0 body finished, -1 link lost, -2 timeout. */
      int    read(char *buf, unsigned max, int secs);
      void   service();                                  /* Net::service */
      const char *errorText();
  };
  ```
  Timeouts use lib/ticks. `read` keeps the connection open after a finished body unless `h->close`; a server that closes mid-body returns -1.

- [ ] **Step 1: Build:** add `lib/net.cpp`, `lib/http.cpp` and the Task 1-8 C files to `clawdos.exe` sources in scripts/build.py. No DOS unit test (needs the stack); the boot test in Task 14 covers it.
- [ ] **Step 2: Run `python3 scripts/build.py`; expect a clean build with no warnings.**
- [ ] **Step 3: Commit** `git commit -m "Add the mTCP connection and HTTP client classes"`

---

### Task 12: Data files and the image build

**Files:**
- Create: `image/clawdos/CLAWDOS.CFG` (the spec's example minus the `key` lines, with `tunnel 10.0.2.2:8443`), `image/clawdos/PROMPT.TXT`, `image/clawdos/TOOLS.JSN`
- Modify: `image/manifest.txt` (`CLAWDOS/ repo:image/clawdos/*`), `scripts/build.py` (`--tunnel` replaces `--bridge` in `DEFAULT_SETTINGS`/argparse; `build_image` rewrites `CLAWDOS/CLAWDOS.CFG`: the `tunnel` line of `[anthropic]` from settings and a `key` line from `.env`'s `ANTHROPIC_API_KEY` if present; MTCP.CFG no longer gets `CLAWDOS_BRIDGE`), `Makefile` (`TUNNEL=` variable)

**Interfaces:**
- `TOOLS.JSN` is produced once by `python3 -c "import sys; sys.path.insert(0,'bridge'); import json, tools; print(json.dumps(tools.anthropic_tools(), indent=1))"` and committed.
- `PROMPT.TXT` = bridge/prompts/default.txt + blank line + `TOOLS_PROMPT` + blank line + `MEMORY_PROMPT` with `{index}` renamed `{memories}` and "connected to you over a network bridge" changed to "connected to you over the network". CRLF line ends.

- [ ] **Step 1: Create the three files; update manifest and build.py.**
- [ ] **Step 2: Verify:** `python3 scripts/build.py --tunnel 10.0.2.2:8443 && python3 scripts/fatimg.py get build/CLAWDOS.IMG CLAWDOS/CLAWDOS.CFG -` shows the tunnel line and no key (no `.env`); with a temporary `.env` holding `ANTHROPIC_API_KEY=sk-test`, the key line appears; `fatimg.py ls build/CLAWDOS.IMG CLAWDOS` lists the three files.
- [ ] **Step 3: Commit** `git commit -m "Ship the prompt, tool schemas and config on the image"`

---

### Task 13: tools.cpp runs a call from JSON

**Files:**
- Modify: `src/clawdos/tools.h`, `src/clawdos/tools.cpp`, `lib/toolapi.h`, `lib/toolapi.c` (`store` → 16 KB far buffer allocated in `targ_init`; `targ_set(key, utf8, n)` converts UTF-8 → CP437 and stores; `targ_append` and the `\n` unescaping go)
- Keep `tools_line` compiling until Task 17 (it calls `targ_set` with pre-unescaped text).

**Interfaces:**
- Produces:
  ```cpp
  /* Run one tool call: `input` is the tool_use input object (UTF-8 JSON). Shows the
     header, asks in safe mode, runs the executor. `t` holds the CP437 output; returns TS_*. */
  int tools_call(const char *name, const char *input, unsigned len, ToolCtx *t);
  const char *tools_result_text(int status, const ToolCtx *t);  /* the spec's four texts; output for ok */
  ```
  `tools_call` fills arguments with `json_each` → `targ_set` (strings unescaped, numbers raw, `true`/`false` → `1`/`0`, `null` skipped); an argument set that overflows → `tool_fail(t, "arguments too long")`.

- [ ] **Step 1: Add to dosunit.c `testToolArgs`:** `targ_set` takes decoded UTF-8: test that `targ_set("t", "\xC3\xA9", 2)` then `targ("t")` is `"\x82"`, that `targ_set("path", "C:\\A.TXT", 8)` round-trips unchanged, and that filling past 16 KB returns -1.
- [ ] **Step 2: Run units; expect a build failure.**
- [ ] **Step 3: Implement** toolapi changes and `tools_call` (refactor `runCall`'s body into it; `runCall` becomes a thin wrapper for the old path).
- [ ] **Step 4: Build and run units; expect `UNITS-OK`; run `image/tests/tools.bat --bridge --expect "tool tour: PASS"` to confirm the old path still works.**
- [ ] **Step 5: Commit** `git commit -m "Tools: run a call from a JSON input; 16 KB far argument store"`

---

### Task 14: CLAWDOS speaks the API (chat without tools)

**Files:**
- Create: `src/clawdos/chat.h`, `src/clawdos/chat.cpp` (the agent loop: connection, request, stream, history, retries; no screen code except through callbacks)
- Modify: `src/clawdos/clawdos.cpp` (command line, config, states, `handleLine` → `Chat` callbacks; `connectBridge`/`reconnect`/`checkLink`/`setClock` rewritten; `welcome()` shows provider:model from the config)

**Interfaces:**
- Produces (src/clawdos/chat.h):
  ```cpp
  struct ChatUi {        /* what the loop needs from the screen */
      void (*text)(const char *cp437, unsigned n);
      void (*thinking)(void);
      void (*note)(const char *text, int error);
      int  (*tool)(const char *name, const char *input, unsigned len, ToolCtx *t);  /* tools_call */
      int  (*escPressed)(void);
      void (*log)(const char *line);                     /* CLAWDOS.LOG */
  };
  class Chat {
    public:
      int8_t init(Cfg *cfg, const char *providerId, const char *hostOverride, ChatUi *ui);
      int8_t newConversation();          /* new CHATS file; memories re-read */
      /* Send the user's text and run the whole turn (all tool rounds). Returns
         0 done, 1 stopped by Esc, -1 failed (note already shown). */
      int8_t turn(const char *cp437Text);
      int8_t switchProvider(const char *id, const char *model);   /* /model */
      const char *describe();            /* "anthropic: claude-opus-5-5" */
      int8_t clockFromDate(const char *date, int tz, char *report);  /* sets DOS clock; 1 if it did */
  };
  ```
  Conversation file name: `C:\CLAWDOS\CHATS\MMDDnnn.JSN`, `nnn` the first free number that day. CLAWDOS.LOG line per round: `YYYY-MM-DD HH:MM:SS api <provider> <model> ttft=<ms> total=<ms> stop=<reason> in=<n> out=<n> cr=<n> cw=<n>`; notes as `YYYY-MM-DD HH:MM:SS note <text>`.

- [ ] **Step 1: Write a smoke boot test** `image/tests/chat.bat`: `CLAWDOS.EXE 10.0.2.2:18023 -q -m "hello"` then `TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1`; add `--mock` to scripts/boottest.py (runs `tunnel/mockapi.py --port 18023 --speed 0`, keeps `--bridge` for now) and expect `stop=end_turn`.
- [ ] **Step 2: Run it; expect failure (no --mock / no reply).**
- [ ] **Step 3: Implement Chat::turn for the no-tool case:** write the user message to history, loop {count body, open connection if needed, send head+body, readHead, on 429/529/5xx wait and retry, stream events through `Sse` → `apistream_event`, on message_stop `hist_message_done`}, log the round. Esc during the stream: `close()`, `hist_rollback_turn`, return 1. Mid-stream -1 from `read`: rollback the message, note `[link lost, asking again]`, log `connection #2`, once.
- [ ] **Step 4: Wire clawdos.cpp:** `CLAWDOS [host:port] [-p provider] ...`; load `C:\CLAWDOS\CLAWDOS.CFG` (fatal with the file name if missing); `submit()` calls `chat.turn`; `/new` → `newConversation`; the clock from the first response's `Date`; welcome panel shows `describe()`.
- [ ] **Step 5: Run `image/tests/chat.bat --mock --expect "stop=end_turn"` → passes; also `clock.bat` with its new expectation `Clock set from the API` after switching it to `--mock` and adding the `TYPE` line.**
- [ ] **Step 6: Commit** `git commit -m "CLAWDOS talks the Messages API: plain chat over the tunnel"`

---

### Task 15: The tool loop, limits and the tool tour

**Files:**
- Modify: `src/clawdos/chat.cpp` (rounds), `image/tests/tools.bat`, `image/tests/memory.bat` (`--mock`, `TYPE` line)

- [ ] **Step 1: Switch tools.bat to `--mock`; run it; expect FAIL (no tool loop yet).**
- [ ] **Step 2: Implement rounds in Chat::turn:** on `stop_reason tool_use`: `ui->tool(...)`, write the tool_result user message (`{"role":"user","content":[{"type":"tool_result","tool_use_id":"..","content":"<escaped>","is_error":<bool>}]}`), `hist_message_done`, next round; TS_STOPPED → write the stopped result, end the turn as stopped; round 26 → `noTools` + append `{"type":"text","text":"Tool limit reached; answer now without tools."}` to that results message; a following user message after tool results joins the pending user message as a text block (spec: roles alternate); `max_tokens` → note `[reply cut off: too long]`; `refusal` → note and `hist_rollback_turn`; unknown block/error event → note, rollback message, settle (`hist_turn_messages() == 0` → rollback turn, else write stopped results for an open tool_use).
- [ ] **Step 3: Run `tools.bat --mock --expect "tool tour: PASS"`, `memory.bat --mock --expect "memory_write -> ok"`; expect PASS.**
- [ ] **Step 4: Commit** `git commit -m "The agent loop: tool rounds, limits and settling the history"`

---

### Task 16: Lost links, retries, big events, /model; boot tests complete

**Files:**
- Modify: `src/clawdos/chat.cpp`, `src/clawdos/clawdos.cpp` (`/model`), `image/tests/drop.bat` (`--mock`, `--expect "connection #2"` + `TYPE`), `scripts/boottest.py` (remove `--bridge`), `Makefile` (`boottests`)
- Create: `image/tests/retry.bat` (`-m "!retry"`, expect `second attempt`), `image/tests/chunks.bat` (`-m "!chunks"`, expect the scenario's closing words), `image/tests/bigevent.bat` (`-m "!bigevent"`, expect `event too long` and `stop=` absent, then a second `-m "hello"` run in the same batch expecting `stop=end_turn`)

- [ ] **Step 1: Write the three batch files; update drop.bat; run each with `--mock`; expect FAIL where the behaviour is missing.**
- [ ] **Step 2: Implement:** retries with `retry-after`; drop re-ask; `Sse` overflow → error `event too long`; `/model` listing (`id  model  [current] [no key]` lines printed like a reply) and switching via `Chat::switchProvider` (closes the connection, keeps the history file).
- [ ] **Step 3: Run `make boottests` (updated list: smoke, units, net, chat, tools, drop, memory, clock, retry, chunks, bigevent); expect all PASS.**
- [ ] **Step 4: Commit** `git commit -m "Retries, lost-link re-ask, oversize events and /model; boot tests on the mock API"`

---

### Task 17: Remove the bridge; docs

**Files:**
- Delete: `bridge/` (all), `lib/dosbridge.cpp`, `lib/dosbridge.h`, `docs/PROTOCOL.md`, `tools_line`/`runCall`/`sendResult` and the `ui_send` entry in `src/clawdos/ui.h`, `.venv` lines in Makefile/.gitignore, `requirements.txt` references in CLAUDE.md
- Create: `docs/API.md` (what CLAWDOS sends and expects: the spec's request/stream/history sections kept current, CLAWDOS.CFG, the tunnel and the socat/stunnel one-liners)
- Modify: `README.md` (architecture section and mermaid diagram: tunnel instead of bridge; "What you need"; the key-with-a-spending-limit advice), `docs/ROADMAP.md` ("Why a bridge" → why a tunnel; M3 history; the 8088 timings placeholder), `CLAUDE.md` (Workflow bullets for tunnel/mock/tests), `.gitignore` (`/tunnel/*.log`)

- [ ] **Step 1: Delete, then `python3 scripts/build.py`; fix every reference the compiler finds.**
- [ ] **Step 2: `make check` (test_mockapi + all boot tests); expect PASS.**
- [ ] **Step 3: `grep -rn "bridge" --include=*.md --include=*.py --include=*.c --include=*.cpp --include=*.h --include=*.bat --include=Makefile .` and fix what still means the old program (the tunnel is "the tunnel"; "the bridge" may remain only in history notes).**
- [ ] **Step 4: Commit** `git commit -m "Remove the bridge: CLAWDOS is the harness, Python is a tunnel and a mock"`

---

### Task 18: Live run and 8088 timings

**Files:**
- Modify: `docs/ROADMAP.md` (the two measurements)

- [ ] **Step 1: Needs an API key:** put `ANTHROPIC_API_KEY=...` in `.env`, `make build TUNNEL=10.0.2.2:8443`, `make tunnel &`, `make dosbox`; chat, use a tool, `/new`; confirm `usage` cache reads are non-zero on the second round (CLAWDOS.LOG `cr=`).
- [ ] **Step 2: `make run` (86Box): time Enter → first token on a fresh conversation and on one after ~100 KB of history (`dir C:\CLAWDOS\CHATS`); record both in the ROADMAP.**
- [ ] **Step 3: Commit** `git commit -m "Record the 8088 timings of the native client"`
