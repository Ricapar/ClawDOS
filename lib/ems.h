/* ClawDOS EMS: two 16 KB pages of LIM expanded memory (PicoMEM's PMEMM.EXE
 * provides it on the DOS PC), for parking data outside conventional memory while
 * a program the user or the model runs needs every byte of it: page 0 holds
 * the chat screen, page 1 the launcher's screenshot while the resumed harness
 * makes its PNG (tools.cpp). Both are written and read only while no other
 * program runs: an interrupt handler never writes into the frame (lib/shot.h).
 */
#ifndef CLAWDOS_EMS_H
#define CLAWDOS_EMS_H

#ifdef __cplusplus
extern "C" {
#endif

#define EMS_PAGE_BYTES 16384u
#define EMS_SHOT_PAGE  1

/* Our page n (0 or 1), mapped into physical page n of the page frame: call
   again after anything else may have used EMS (a program run meanwhile),
   since that remaps the frame. The pages are allocated on first use and
   freed at exit. NULL without EMS (or, for page 1, when only one page could
   be had). */
void __far *ems_map(unsigned n);
#define ems_page() ems_map(0)

#ifdef __cplusplus
}
#endif

#endif
