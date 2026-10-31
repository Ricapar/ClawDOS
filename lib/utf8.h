/* ClawDOS text encoding: the API speaks UTF-8, the screen and DOS files are
 * code page 437. Characters CP437 lacks are approximated the way the bridge's
 * cp437.py did (quotes, dashes, arrows, ticks, accented letters -> bare letter,
 * anything else -> '?'), so Claude's text always lands on the CGA screen.
 */
#ifndef CLAWDOS_UTF8_H
#define CLAWDOS_UTF8_H

#ifdef __cplusplus
extern "C" {
#endif

/* UTF-8 (n bytes, need not be NUL-terminated) -> CP437 bytes in out (no NUL).
   Returns the bytes written; stops when out is full. Invalid or truncated
   sequences become '?'. */
unsigned utf8_to_cp437(const char *in, unsigned n, char *out, unsigned outmax);

/* One CP437 byte -> 1-3 bytes of UTF-8 in out; returns how many. Bytes 0x01-0x1F
   are the CP437 glyphs (0x07 is a bullet) except tab, LF and CR, which stay. */
unsigned cp437_to_utf8(unsigned char c, char *out);

#ifdef __cplusplus
}
#endif

#endif
