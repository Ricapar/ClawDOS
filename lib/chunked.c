/* Chunked decoding, see chunked.h. */
#include "chunked.h"

enum { SIZE, EXT, SIZE_LF, DATA, DATA_CR, DATA_LF, TRAILER, TRAILER_LF, DONE, BAD };

void chunked_init(Chunked *c)
{
    c->state = SIZE;
    c->left = 0;
    c->done = 0;
}

static int hexVal(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

int chunked_feed(Chunked *c, const char *in, unsigned n, ByteFn out, void *ctx)
{
    unsigned i = 0;
    while (i < n) {
        char ch = in[i];
        switch (c->state) {
        case SIZE: {
            int h = hexVal(ch);
            if (h >= 0) { c->left = (c->left << 4) | h; i++; }
            else if (ch == ';') { c->state = EXT; i++; }
            else if (ch == '\r') { c->state = SIZE_LF; i++; }
            else if (ch == '\n') { c->state = c->left ? DATA : TRAILER; i++; }
            else { c->state = BAD; return -1; }
            break;
        }
        case EXT:
            if (ch == '\r') c->state = SIZE_LF;
            else if (ch == '\n') c->state = c->left ? DATA : TRAILER;
            i++;
            break;
        case SIZE_LF:
            if (ch != '\n') { c->state = BAD; return -1; }
            c->state = c->left ? DATA : TRAILER;
            i++;
            break;
        case DATA: {
            unsigned take = n - i;
            if (take > c->left) take = (unsigned)c->left;
            if (out(ctx, in + i, take)) { c->state = BAD; return -1; }
            i += take;
            c->left -= take;
            if (c->left == 0) c->state = DATA_CR;
            break;
        }
        case DATA_CR:
            if (ch == '\r') c->state = DATA_LF;
            else if (ch == '\n') c->state = SIZE;
            else { c->state = BAD; return -1; }
            i++;
            break;
        case DATA_LF:
            if (ch != '\n') { c->state = BAD; return -1; }
            c->state = SIZE;
            i++;
            break;
        case TRAILER:                      /* trailer lines until an empty one */
            if (ch == '\r') c->state = TRAILER_LF;
            else if (ch == '\n') { c->state = DONE; c->done = 1; return 1; }
            else c->state = TRAILER + 100;  /* inside a trailer line */
            i++;
            break;
        case TRAILER + 100:
            if (ch == '\n') c->state = TRAILER;
            i++;
            break;
        case TRAILER_LF:
            if (ch != '\n') { c->state = BAD; return -1; }
            c->state = DONE;
            c->done = 1;
            return 1;
        case DONE:
            return 1;
        default:
            return -1;
        }
    }
    return c->done ? 1 : 0;
}
