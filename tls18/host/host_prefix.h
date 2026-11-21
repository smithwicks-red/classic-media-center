/* host_prefix.h -- HOST ONLY. Pretend to be CW8 so the glue takes its
   classic (real OT) code paths, then use the same prefix as the Mac. */
#ifndef __MWERKS__
#define __MWERKS__ 0x3200
#endif
#include "../https_prefix.h"
/* sha2big.c's CW8-only path is correct only on 32-bit big-endian; the
   x86 build compiles that one file with -DHOST_STOCK_SHA2BIG. */
#ifdef HOST_STOCK_SHA2BIG
#undef __MWERKS__
#endif
