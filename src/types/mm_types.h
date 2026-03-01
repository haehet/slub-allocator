#pragma once
#include <cstdint>
#include "list.h"
#define PAGE_SIZE 4096

namespace mm::slab { struct kmem_cache; } 

namespace mm::types{

enum : unsigned long {
    PG_BUDDY    = 1ull << 0,
    PG_SLAB     = 1ull << 1,
    PG_RESERVED = 1ull << 2,
    PG_HEAD     = 1ull << 3, 
    PG_TAIL     = 1ull << 4, 
};


struct page
{
  unsigned long flag;

  union {
    struct {
      list_head lru;
      unsigned int order; // we don't recycle the "private" field XD
    }; 

    struct {
      union {
        list_head slab_list;
        struct{
          struct page *next;
          int pages;
          int pobjects;
        };
      };

      struct slab::kmem_cache *slab_cache;
      void *freelist;
      uint32_t inuse:16;
      uint32_t objects:15;
      // uint32_t froezen:1;      
    };
  };  
  struct {
      struct page *compound_head; 
    } tail;
};

}