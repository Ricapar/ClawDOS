/* Force-included by clangd (see scripts/compiledb.py) so clang can parse our
   Open Watcom 16-bit sources well enough for navigation and diagnostics.
   Nothing here is seen by the real build. */
#ifndef CLANGD_WATCOM_H
#define CLANGD_WATCOM_H

/* Pretend to be Watcom targeting 16-bit DOS so its headers pick the right
   declarations (union REGS with 16-bit members, int86, MK_FP, ...). */
#define __WATCOMC__ 1300
#define __I86__
#define __DOS__
#define M_I86
#define _M_I86

/* Pointer-size and calling-convention keywords clang has no notion of. */
#define __far
#define __near
#define __huge
#define __interrupt
#define __loadds
#define __saveregs
#define __based(x)
#define __segment unsigned
#define _Packed
#define __watcall
#define __pascal
#define __fortran
#define __syscall
#define __export
/* The bare spellings Watcom also accepts; mTCP's headers use them. */
#define far
#define near
#define huge
#define interrupt
#define cdecl

/* clang predeclares these as builtins with other prototypes than stdlib.h. */
#define _lrotl __wc_lrotl
#define _lrotr __wc_lrotr
#define _rotl __wc_rotl
#define _rotr __wc_rotr

#endif
