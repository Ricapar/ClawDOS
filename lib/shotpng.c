/* ClawDOS screenshots, the picture: see shotpng.h. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <dos.h>
#include <i86.h>
#include "shotpng.h"

#define ROWS      200
#define ROW_BYTES 80                  /* both graphics modes */
#define BANK2     0x2000              /* the odd rows' bank */
#define OUT_BUF   1024
#define RAW_LEN   (ROWS * (ROW_BYTES + 1))   /* the image data: a filter byte, then the row */
#define ADLER_MOD 65521UL

/* One encoding's state. The CRC table (1 KB) and the buffers are a DOS
   block for that encoding only, given back with _dos_freemem: the C heap
   would keep it, and a program run later would have that much less. A byte
   table costs one lookup a byte where a 16-entry table costs two, which is
   most of the time spent on the ~16 KB a picture checksums. */
static unsigned long *crcTab;
static unsigned char *out, *row;
static unsigned outLen;
static int outFd, outBad;
static unsigned long crc;

static void flushOut(void)
{
    if (outLen && write(outFd, out, outLen) != (int)outLen)
        outBad = 1;
    outLen = 0;
}

/* n bytes to the file, and into the chunk's CRC if sum. */
static void emit(const unsigned char *p, unsigned n, int sum)
{
    unsigned long c = crc;
    while (n--) {
        unsigned char b = *p++;
        if (sum)
            c = crcTab[(unsigned char)c ^ b] ^ (c >> 8);
        if (outLen == OUT_BUF)
            flushOut();
        out[outLen++] = b;
    }
    crc = c;
}

static void emit4(unsigned long v, int sum)       /* big-endian, as PNG and zlib want */
{
    unsigned char b[4];
    b[0] = (unsigned char)(v >> 24);
    b[1] = (unsigned char)(v >> 16);
    b[2] = (unsigned char)(v >> 8);
    b[3] = (unsigned char)v;
    emit(b, 4, sum);
}

static void chunkStart(const char *type, unsigned long len)
{
    emit4(len, 0);
    crc = 0xFFFFFFFFUL;
    emit((const unsigned char *)type, 4, 1);
}

static void chunkEnd(void)
{
    emit4(crc ^ 0xFFFFFFFFUL, 0);
}

int png_write_cga(const char *path, const unsigned char __far *frame, int bpp,
                  const unsigned char *rgb, unsigned colors)
{
    static const unsigned char sig[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
    unsigned char ihdr[13], z[7];
    unsigned long s1 = 1, s2 = 0, c;
    unsigned i, y, k, seg;

    if (_dos_allocmem((256 * sizeof(unsigned long) + OUT_BUF + ROW_BYTES + 1 + 15) / 16, &seg))
        return -1;
    crcTab = (unsigned long *)MK_FP(seg, 0);
    out = (unsigned char *)(crcTab + 256);
    row = out + OUT_BUF;
    for (i = 0; i < 256; i++) {
        c = i;
        for (k = 0; k < 8; k++)
            c = c & 1 ? 0xEDB88320UL ^ (c >> 1) : c >> 1;
        crcTab[i] = c;
    }
    outFd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, S_IREAD | S_IWRITE);
    if (outFd < 0) {
        _dos_freemem(seg);
        return -1;
    }
    outLen = 0;
    outBad = 0;

    emit(sig, sizeof sig, 0);
    memset(ihdr, 0, sizeof ihdr);
    ihdr[2] = (unsigned char)((640 / bpp) >> 8);      /* width, then height: 4 bytes each */
    ihdr[3] = (unsigned char)(640 / bpp);
    ihdr[7] = ROWS;
    ihdr[8] = (unsigned char)bpp;                     /* bit depth */
    ihdr[9] = 3;                                      /* indexed colour; no compression, filter or interlace options */
    chunkStart("IHDR", sizeof ihdr);
    emit(ihdr, sizeof ihdr, 1);
    chunkEnd();
    chunkStart("PLTE", 3UL * colors);
    emit(rgb, 3 * colors, 1);
    chunkEnd();

    /* zlib: header (deflate, 32 KB window, check bits right), one final stored block */
    chunkStart("IDAT", 2 + 5 + RAW_LEN + 4UL);
    z[0] = 0x78;
    z[1] = 0x01;
    z[2] = 1;                                         /* BFINAL, BTYPE 00 */
    z[3] = (unsigned char)RAW_LEN;
    z[4] = (unsigned char)(RAW_LEN >> 8);
    z[5] = (unsigned char)~RAW_LEN;
    z[6] = (unsigned char)(~RAW_LEN >> 8);
    emit(z, sizeof z, 1);
    for (y = 0; y < ROWS; y++) {
        const unsigned char __far *src = frame + (y & 1 ? BANK2 : 0) + (y >> 1) * ROW_BYTES;
        row[0] = 0;                                   /* filter: none */
        for (i = 0; i < ROW_BYTES; i++)
            row[i + 1] = src[i];
        for (i = 0; i <= ROW_BYTES; i++) {            /* a row cannot overflow the sums: reduce once a row */
            s1 += row[i];
            s2 += s1;
        }
        s1 %= ADLER_MOD;
        s2 %= ADLER_MOD;
        emit(row, ROW_BYTES + 1, 1);
    }
    emit4(s2 << 16 | s1, 1);
    chunkEnd();
    chunkStart("IEND", 0);
    chunkEnd();

    flushOut();
    if (close(outFd))
        outBad = 1;
    _dos_freemem(seg);
    crcTab = NULL;
    if (outBad) {
        remove(path);
        return -1;
    }
    return 0;
}

static void rgbi(unsigned char *p, unsigned c)
{
    unsigned char lo = c & 8 ? 0x55 : 0;
    p[0] = (unsigned char)((c & 4 ? 0xAA : 0) + lo);
    p[1] = (unsigned char)(c == 6 ? 0x55 : (c & 2 ? 0xAA : 0) + lo);   /* the 5153 shows dark yellow as brown */
    p[2] = (unsigned char)((c & 1 ? 0xAA : 0) + lo);
}

unsigned shot_palette(const ShotInfo *info, int kind, unsigned char *rgb)
{
    unsigned reg = info->color != SHOT_NO_COLOR ? info->color : info->colorReg;
    /* 320x200: palette 0 is green, red, brown; 1 cyan, magenta, light grey;
       with the colour burst off (mode 5) cyan, red, light grey on an RGBI
       monitor. Bit 4 makes the three bright. The burst bit is the mode
       register's (bit 2, in a 320x200 graphics mode: bits 1, 2 and not 4),
       or shot_color's bit 6 when the program wrote the registers itself. */
    static const unsigned char fg[3][3] = { { 2, 4, 6 }, { 3, 5, 7 }, { 3, 4, 7 } };
    int burstOff = info->color != SHOT_NO_COLOR ? (info->color & SHOT_BURST_OFF) != 0
                                                : (info->modeReg & 0x16) == 0x06;
    unsigned i, set = burstOff ? 2 : (reg & 0x20 ? 1 : 0), hi = reg & 0x10 ? 8 : 0;

    if (kind == SHOT_640) {           /* black, and the colour register's colour */
        rgbi(rgb, 0);
        rgbi(rgb + 3, reg & 15);
        return 2;
    }
    rgbi(rgb, reg & 15);              /* the background */
    for (i = 0; i < 3; i++)
        rgbi(rgb + 3 * (i + 1), fg[set][i] + hi);
    return 4;
}

int shot_load_raw(ShotInfo *info, unsigned char __far *frame)
{
    int fd = open(SHOT_RAW, O_RDONLY | O_BINARY), rc = -1;

    if (fd < 0)
        return -1;
    if (read(fd, info, sizeof *info) == sizeof *info && read(fd, frame, SHOT_BYTES) == (int)SHOT_BYTES)
        rc = 0;
    close(fd);
    return rc;
}

int shot_seconds(const char *s, unsigned *tenths)
{
    unsigned long t = 0;
    const char *p = s;

    while (*p >= '0' && *p <= '9' && t <= SHOT_AT_MAX)
        t = t * 10 + (*p++ - '0');
    t *= 10;
    if (*p == '.') {
        p++;
        if (*p >= '0' && *p <= '9')
            t += *p++ - '0';
        while (*p >= '0' && *p <= '9')   /* hundredths and less: dropped */
            p++;
    }
    if (*p || p == s || *s == '.' || t == 0 || t > SHOT_AT_MAX)
        return -1;
    *tenths = (unsigned)t;
    return 0;
}
