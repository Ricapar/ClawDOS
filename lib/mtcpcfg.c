/* See mtcpcfg.h. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mtcpcfg.h"

#define CFG_MAX 4096                  /* MTCP.CFG is a few hundred bytes */

/* The start of the TIMESTAMP line, or NULL. mTCP matches the keyword without
   regard to case. */
static char *findLine(char *text)
{
    char *line = text;

    for (;;) {
        if (strnicmp(line, "TIMESTAMP", 9) == 0 && (line[9] == ' ' || line[9] == '\t'))
            return line;
        line = strchr(line, '\n');
        if (line == NULL)
            return NULL;
        line++;
    }
}

int mtcpcfg_shift_timestamp(const char *path, long delta)
{
    FILE *f;
    char *text, *line, *rest;
    unsigned long stamp;
    time_t when;
    int n, rc;

    if (path == NULL || (f = fopen(path, "r")) == NULL)
        return -1;
    text = (char *)malloc(CFG_MAX + 1);
    if (text == NULL) {
        fclose(f);
        return -2;
    }
    n = fread(text, 1, CFG_MAX, f);
    fclose(f);
    if (n <= 0 || n >= CFG_MAX) {     /* empty, or too big to be DHCP's: leave it */
        free(text);
        return -1;
    }
    text[n] = 0;
    line = findLine(text);
    if (line == NULL || sscanf(line + 9, " ( %lu )", &stamp) != 1) {
        free(text);
        return -1;
    }
    rest = strchr(line, '\n');
    rest = rest ? rest + 1 : line + strlen(line);
    stamp += (unsigned long)delta;
    when = (time_t)stamp;

    rc = -2;                          /* the file is rewritten whole: it is one sector */
    f = fopen(path, "w");
    if (f != NULL) {
        if (fwrite(text, 1, line - text, f) == (size_t)(line - text)
            && fprintf(f, "TIMESTAMP ( %lu ) %s", stamp, ctime(&when)) > 0
            && fwrite(rest, 1, strlen(rest), f) == strlen(rest))
            rc = 0;
        if (fclose(f))
            rc = -2;
    }
    free(text);
    return rc;
}
