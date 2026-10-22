/* Claude-XT PLAY: GW-BASIC's music macro language, parsed into tones.
 * No hardware here: the caller's PlayOps makes the sound (sound.h does it on
 * the PC speaker; the unit tests record it).
 *
 * Notes A-G (# or + sharp, - flat, then an optional length and dots), On
 * octave 0-6 (O3 starts at middle C; default O4), < and >, Nn note number
 * 0-84 (0 = rest), Ln length 1-64, Pn/Rn rest, Tn tempo 32-255, MN/ML/MS
 * normal/legato/staccato (MF and MB are accepted and ignored).
 */
#ifndef CXT_PLAY_H
#define CXT_PLAY_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PlayOps {
    void *ctx;
    void (*tone)(void *ctx, unsigned hz, unsigned ms);   /* hz 0 = silence */
    int  (*stop)(void *ctx);                             /* checked after each note */
} PlayOps;

/* 0 = played, 1 = stopped, -1 = error (err gets "col N: message"). */
int play_run(const char *music, const PlayOps *ops, unsigned long *total_ms,
             char *err, int errlen);

#ifdef __cplusplus
}
#endif

#endif
