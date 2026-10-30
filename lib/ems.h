/* ClawDOS EMS: one 16 KB page of LIM expanded memory (PicoMEM's PMEMM.EXE
 * provides it on the XT), for parking data outside conventional memory while
 * a program the user or the model runs needs every byte of it.
 */
#ifndef CXT_EMS_H
#define CXT_EMS_H

#ifdef __cplusplus
extern "C" {
#endif

#define EMS_PAGE_BYTES 16384u

/* Our page, mapped into the page frame: call again after anything else may
   have used EMS (a program run meanwhile), since that remaps the frame. The
   page is allocated on first use and freed at exit. NULL without EMS. */
void __far *ems_page(void);

#ifdef __cplusplus
}
#endif

#endif
