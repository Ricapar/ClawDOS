/* C:\CLAWDOS\CLAWDOS.CFG: settings and the AI providers, "key value" lines
 * like MTCP.CFG with [name] sections for providers.
 */
#ifndef CLAWDOS_CFG_H
#define CLAWDOS_CFG_H

#ifdef __cplusplus
extern "C" {
#endif

#define CFG_PROVIDERS 8
#define CFG_MAX_TOKENS 64000L /* thinking counts toward it, so it is a ceiling, not a target */

typedef struct Provider {
    char id[16];          /* [section] */
    char type[12];        /* "anthropic" */
    char tunnel[48];      /* host:port of the TLS tunnel */
    char host[64];        /* Host header */
    char path[48];        /* default "/v1/messages" */
    char auth[12];        /* "x-api-key" (default) or "bearer" */
    char key[128];
    char keyfile[64];     /* read into key by cfg_load when key is empty */
    char model[48];
    char effort[8];       /* "" = omit output_config */
    char betas[64];       /* "" = no anthropic-beta header */
    char name[24];        /* assistant name; "" = the global one */
    int cache;            /* default 1: send cache_control */
    int cacheTtl1h;       /* cachettl 1h: the markers last an hour; 0 (5m, absent): five minutes */
    int progress;         /* progress 1: thinking display "updates", the notes shown (beta) */
    int dropThinking;     /* dropthinking 1: thinking block_binding drop_block (beta) */
    int threads;          /* threads 1: message threads, later requests send only what is new (beta) */
    long maxTokens;       /* max_tokens; default CFG_MAX_TOKENS */
} Provider;

typedef struct Cfg {
    char provider[16];    /* startup section */
    char name[24];        /* default "Claude" */
    int nproviders;
    Provider providers[CFG_PROVIDERS];
} Cfg;

/* Cfg is over 4 KB: allocate it with malloc, never as a static. */
void cfg_defaults(Cfg *c);
/* Parse text (LF or CRLF; '#' starts a comment except in key/keyfile values;
   "key value" separated by spaces or tabs, trailing blanks trimmed). Returns 0,
   or the 1-based number of the first bad line (unknown key, a bad maxtokens or
   cachettl value, too many sections). */
int cfg_parse(Cfg *c, const char *text);
int cfg_load(Cfg *c, const char *path);          /* file -> cfg_parse, then keyfile -> key; -1 unreadable, else as cfg_parse */
Provider *cfg_provider(Cfg *c, const char *id);  /* NULL if unknown; case-insensitive */
/* What a provider section lacks to be usable ("tunnel", "host", "key", "model"), or NULL. */
const char *cfg_missing(const Provider *p);

#ifdef __cplusplus
}
#endif

#endif
