#pragma once

#ifndef MAX_NUMNODES 
#define MAX_NUMNODES  1
#endif


#ifndef PAGES
#define PAGES 0x4000
#endif

#ifndef MAX_ORDER
#define MAX_ORDER 10
#endif

#ifndef DEBUG_BUDDY 
#define DEBUG_BUDDY 0
#endif 

#ifndef CONFIG_SLUB_CPU_PARTIAL
#define CONFIG_SLUB_CPU_PARTIAL 1
#endif


#ifndef CONFIG_SLAB_FREELIST_HARDENED
#define CONFIG_SLAB_FREELIST_HARDENED 1
#endif


#ifndef CONFIG_SLAB_FREELIST_RANDOM
#define CONFIG_SLAB_FREELIST_RANDOM 1
#endif

#ifndef KERNEL_NOISE
#define KERNEL_NOISE 1
#endif