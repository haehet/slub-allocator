# slub-allocator

A small **SLUB-like slab allocator** implementation built on top of a buddy allocator.  
This project is a learning / research-oriented memory allocator that mimics the core flow of Linux SLUB in a simplified **single-CPU / single-node** model.

## Features

- Buddy allocator (`alloc_pages` / `free_pages`) as the page provider
- SLUB-style slab allocator
  - per-cpu cache (`kmem_cache_cpu`) fast path
  - slow path: cpu page refill → cpu partial → node partial → new slab from buddy
  - node partial list management (`kmem_cache_node`)
- `kmalloc` caches: 8 ~ 4096 bytes
- Large allocations fallback to buddy (order-based)
- Optional hardening (compile-time)
  - freelist pointer obfuscation (`CONFIG_SLAB_FREELIST_HARDENED`)
  - freelist randomization (`CONFIG_SLAB_FREELIST_RANDOM`)
  - cpu partial (`CONFIG_SLUB_CPU_PARTIAL`)
- Optional workload noise generator (`KERNEL_NOISE`)

## Project Layout


src/
main/
main.cc
mm/
phys/ # flat physical memory mapping (mmap)
buddy/ # buddy allocator + zone
slab/ # SLUB-like slab allocator (kmem_cache/kmalloc)
types/ # list, mm types
util/ # BUG_ON, random, helpers


## Wargame

You can solve a wargame challenge built with this allocator on Dreamhack:  
https://dreamhack.io/wargame/challenges/2744