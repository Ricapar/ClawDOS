/* UTF-8 <-> CP437, see utf8.h. */
#include "utf8.h"

/* CP437 0x80-0xFF -> Unicode */
static const unsigned short cp437High[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0,
};

/* CP437 0x00-0x1F glyphs -> Unicode (0x09, 0x0A, 0x0D are kept as controls) */
static const unsigned short cp437Low[32] = {
    0x0000, 0x263A, 0x263B, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022,
    0x25D8, 0x25CB, 0x25D9, 0x2642, 0x2640, 0x266A, 0x266B, 0x263C,
    0x25BA, 0x25C4, 0x2195, 0x203C, 0x00B6, 0x00A7, 0x25AC, 0x21A8,
    0x2191, 0x2193, 0x2192, 0x2190, 0x221F, 0x2194, 0x25B2, 0x25BC,
};

/* U+00C0..U+017F -> bare ASCII letter (0: not a letter CP437 lacks, or unknown) */
static const char latinBase[192] = {
    65, 65, 65, 65, 0, 0, 0, 0, 69, 0, 69, 69, 73, 73, 73, 73,
    68, 0, 79, 79, 79, 79, 0, 0, 79, 85, 85, 85, 0, 89, 84, 0,
    0, 0, 0, 97, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    100, 0, 0, 0, 0, 111, 0, 0, 111, 0, 0, 0, 0, 121, 116, 0,
    65, 97, 65, 97, 65, 97, 67, 99, 67, 99, 67, 99, 67, 99, 68, 100,
    68, 100, 69, 101, 69, 101, 69, 101, 69, 101, 69, 101, 71, 103, 71, 103,
    71, 103, 71, 103, 72, 104, 72, 104, 73, 105, 73, 105, 73, 105, 73, 105,
    73, 105, 73, 105, 74, 106, 75, 107, 107, 76, 108, 76, 108, 76, 108, 0,
    0, 76, 108, 78, 110, 78, 110, 78, 110, 0, 78, 110, 79, 111, 79, 111,
    79, 111, 0, 0, 82, 114, 82, 114, 82, 114, 83, 115, 83, 115, 83, 115,
    83, 115, 84, 116, 84, 116, 84, 116, 85, 117, 85, 117, 85, 117, 85, 117,
    85, 117, 85, 117, 87, 119, 89, 121, 89, 90, 122, 90, 122, 90, 122, 115,
};

/* Code points outside CP437 with a textual stand-in. */
struct Fallback { unsigned short cp; const char *text; };
static const struct Fallback fallbacks[] = {
    { 0x2018, "'" }, { 0x2019, "'" }, { 0x201A, "'" }, { 0x201B, "'" },
    { 0x201C, "\"" }, { 0x201D, "\"" }, { 0x201E, "\"" }, { 0x2033, "\"" }, { 0x2032, "'" },
    { 0x2013, "-" }, { 0x2014, "--" }, { 0x2015, "--" }, { 0x2212, "-" }, { 0x2010, "-" }, { 0x2011, "-" },
    { 0x2026, "..." },
    { 0x200B, "" }, { 0x200D, "" }, { 0xFE0F, "" },
    { 0x2192, "->" }, { 0x2190, "<-" }, { 0x21D2, "=>" }, { 0x2194, "<->" },
    { 0x2022, "\xF9" }, { 0x25E6, "o" }, { 0x2023, ">" }, { 0x00D7, "x" },
    { 0x2260, "!=" },
    { 0x2713, "v" }, { 0x2714, "v" }, { 0x2717, "x" }, { 0x2718, "x" },
    { 0x00A9, "(c)" }, { 0x00AE, "(R)" }, { 0x2122, "(TM)" }, { 0x20AC, "EUR" },
    { 0x0152, "OE" }, { 0x0153, "oe" },
    { 0x0009, "    " },
};
#define N_FALLBACKS (sizeof(fallbacks) / sizeof(fallbacks[0]))

/* The CP437 byte for a code point, or -1. A tab is -1 unless kept (it
   then falls back to spaces). */
static int cp437Byte(unsigned long cp, int keepTabs)
{
    unsigned i;
    if (cp < 0x80 && (cp != 0x09 || keepTabs))
        return (int)cp;
    for (i = 0; i < 128; i++)
        if (cp437High[i] == cp)
            return 0x80 + i;
    return -1;
}

static unsigned put(char *out, unsigned outmax, unsigned len, const char *s)
{
    while (*s && len < outmax)
        out[len++] = *s++;
    return len;
}

/* Decode one UTF-8 sequence at *p (n bytes left); advances *p. 0xFFFFFFFF if bad. */
static unsigned long decode(const char **p, unsigned n)
{
    const unsigned char *s = (const unsigned char *)*p;
    unsigned long cp;
    unsigned need, i;

    if (s[0] < 0x80) { cp = s[0]; need = 0; }
    else if ((s[0] & 0xE0) == 0xC0) { cp = s[0] & 0x1F; need = 1; }
    else if ((s[0] & 0xF0) == 0xE0) { cp = s[0] & 0x0F; need = 2; }
    else if ((s[0] & 0xF8) == 0xF0) { cp = s[0] & 0x07; need = 3; }
    else { (*p)++; return 0xFFFFFFFFUL; }
    if (need + 1 > n) { *p += n; return 0xFFFFFFFFUL; }
    for (i = 1; i <= need; i++) {
        if ((s[i] & 0xC0) != 0x80) { *p += i; return 0xFFFFFFFFUL; }
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    *p += need + 1;
    return cp;
}

static unsigned convert(const char *in, unsigned n, char *out, unsigned outmax, int keepTabs)
{
    const char *end = in + n;
    unsigned len = 0, i;

    while (in < end && len < outmax) {
        unsigned left = (unsigned)(end - in);
        unsigned long cp = decode(&in, left);
        int b = cp == 0xFFFFFFFFUL ? -1 : cp437Byte(cp, keepTabs);
        if (b >= 0) {
            out[len++] = (char)b;
            continue;
        }
        for (i = 0; i < N_FALLBACKS; i++)
            if (fallbacks[i].cp == cp)
                break;
        if (i < N_FALLBACKS)
            len = put(out, outmax, len, fallbacks[i].text);
        else if (cp >= 0xC0 && cp < 0x180 && latinBase[cp - 0xC0])
            out[len++] = latinBase[cp - 0xC0];
        else
            out[len++] = '?';
    }
    return len;
}

unsigned utf8_to_cp437(const char *in, unsigned n, char *out, unsigned outmax)
{
    return convert(in, n, out, outmax, 0);
}

unsigned utf8_to_cp437_tabs(const char *in, unsigned n, char *out, unsigned outmax)
{
    return convert(in, n, out, outmax, 1);
}

unsigned cp437_to_utf8(unsigned char c, char *out)
{
    unsigned long cp;
    if (c < 0x20 && c != '\t' && c != '\n' && c != '\r')
        cp = cp437Low[c];
    else if (c >= 0x80)
        cp = cp437High[c - 0x80];
    else
        cp = c;
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
}
