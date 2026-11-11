/* ClawDOS screenshots, the capture: what the `run` tool's screenshot takes
 * of the CGA screen, in whichever program ran the command (HARNESS.EXE, or
 * CLAWDOS.EXE for a full_memory run). Both programs link this file; the PNG
 * is made by the harness (lib/shotpng.h), so the launcher stays small.
 *
 * At exit the picture is still in video memory until the chat screen comes
 * back, so it needs no copy. A timed shot (shot_at) is copied from the timer
 * interrupt into a 16 KB buffer that exists only for that run: in the harness
 * an EMS page when there is EMS, else (and always in the launcher, built
 * with SHOT_NO_EMS to stay small) a DOS block given back afterwards.
 */
#ifndef CLAWDOS_SHOT_H
#define CLAWDOS_SHOT_H

#ifdef __cplusplus
extern "C" {
#endif

#define SHOT_BYTES 16384u                   /* the CGA's video memory */
#define SHOT_RAW   "C:\\CLAWDOS\\SHOT.RAW"   /* the launcher's capture: a ShotInfo, then the 16 KB */
#define SHOT_PNG   "C:\\CLAWDOS\\SHOT.PNG"   /* the picture the tool_result carries */

/* shot_mode: what the frame is taken to be. */
enum { SHOT_AUTO, SHOT_320, SHOT_640, SHOT_TEXT };
/* When the picture was taken. */
enum { SHOT_AT_EXIT, SHOT_TIMED, SHOT_LATE, SHOT_NOBUF };
#define SHOT_NO_COLOR 0xFF
#define SHOT_BURST_OFF 0x40         /* shot_color's bit 6: the mode register's colour burst is off */

typedef struct ShotInfo {
    unsigned char mode;       /* the BIOS's video mode (0040:0049) when it was taken */
    unsigned char modeReg;    /* its shadows of the mode (3D8h) and colour (3D9h) registers */
    unsigned char colorReg;
    unsigned char how;        /* SHOT_AT_EXIT; SHOT_TIMED; SHOT_LATE: shot_at was never reached;
                                 SHOT_NOBUF: no memory for the timed copy (both then taken at exit) */
    unsigned char force;      /* SHOT_AUTO, or the mode shot_mode named */
    unsigned char color;      /* shot_color (the colour register, SHOT_BURST_OFF too), or SHOT_NO_COLOR */
} ShotInfo;

/* Before the run: take the picture `ticks` timer ticks after the first
   program starts (shell_run calls the hook in, through shell_before_run).
   -1 when there is no memory for the copy (the picture is then taken at exit). */
int  shot_arm(unsigned ticks);
/* After the run: the timer hook comes out (also at exit, by atexit). */
void shot_disarm(void);
/* The picture to use, with *info filled in (force and color left alone): the
   timed copy if it was taken, else video memory as the program left it. */
const unsigned char __far *shot_frame(ShotInfo *info);
void shot_release(void);                   /* the timed copy's memory goes back */

#ifdef __cplusplus
}
#endif

#endif
