/* The DHCP lease details DHCP.EXE leaves in the mTCP configuration file (the
 * one the MTCPCFG environment variable names).
 *
 * mTCP refuses to start once the lease has run out, judged by the TIMESTAMP
 * line DHCP.EXE wrote (time() when it got the lease) against the clock now.
 * A PC without a clock boots in 1980 and gets a 1980 timestamp; when CLAWDOS
 * then sets the real date, the lease looks decades old to every mTCP program
 * started afterwards, CLAWDOS -resume included. Moving the timestamp by the
 * same amount as the clock keeps the lease arithmetic honest.
 */
#ifndef MTCPCFG_H
#define MTCPCFG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Add `delta` seconds to the TIMESTAMP line of the config file at `path`,
   rewriting the file. 0 ok; -1 no path, no such file or no TIMESTAMP line
   (the file is left alone); -2 the rewrite failed. */
int mtcpcfg_shift_timestamp(const char *path, long delta);

#ifdef __cplusplus
}
#endif

#endif
