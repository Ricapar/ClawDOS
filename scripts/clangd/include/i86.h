/* clangd-only shadow of Watcom's <i86.h> (see scripts/clangd/watcom.h).
   Watcom builds far pointers with its ':>' based-pointer operator, which
   clang cannot parse; rebuild MK_FP with plain arithmetic instead. */
#include_next <i86.h>

#undef _MK_FP
#define _MK_FP(__s, __o) \
    ((void __far *)(((unsigned long)(unsigned short)(__s) << 16) | (unsigned short)(__o)))
