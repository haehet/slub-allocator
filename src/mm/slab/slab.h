#include <cstdint>
#include <cstddef>
#include <string.h>
#include "CONFIG.h"
#include "types/mm_types.h"
#include "buddy.h"
#include "util/utils.h"

namespace mm::slab{
typedef uint32_t slab_flags_t;


struct kmem_cache_order_objects {
  unsigned int x;     
  unsigned int order; 
};

struct kmem_cache_cpu{
  void **freelist;
  types::page *page; 

#ifdef CONFIG_SLUB_CPU_PARTIAL
  types::page *partial; 
#endif
};

struct kmem_cache_node {
  unsigned long nr_partial;
  types::list_head partial;
};


struct  kmem_cache
{
  struct kmem_cache_cpu *cpu_slab;
  slab_flags_t flags;
  unsigned long min_partial;
  unsigned int size;  
  unsigned int object_size; 
  unsigned int offset;	

#ifdef CONFIG_SLUB_CPU_PARTIAL
  unsigned int cpu_partial;
#endif

  struct kmem_cache_order_objects oo;
  // int refcount;	    do we need cache destroy?

  unsigned int inuse; 
  unsigned int align;		
  const char *name;
  struct types::list_head list;	

#ifdef CONFIG_SLAB_FREELIST_HARDENED  
  unsigned long random;
#endif

#ifdef CONFIG_SLAB_FREELIST_RANDOM
  unsigned int *random_seq;
#endif
  struct kmem_cache_node *node[MAX_NUMNODES];
};


class Slab{
  public:
  explicit Slab(uint64_t size_memory); // the size of memory that we want to use for slab allocator
  void *kmalloc(size_t size);
  void kfree(void *ptr);
  void kernel_noise(uint32_t rounds = 128);


  private:
  buddy::Buddy Buddy;


  void deactivate_slab(struct kmem_cache *s, mm::types::page *page, void *freelist);
  void *slow_path(struct kmem_cache *s, struct kmem_cache_cpu *c);
  inline void *slab_alloc(struct kmem_cache *s);
  inline void slab_free(struct kmem_cache *s, void *object);
  types::page* new_slab_page(struct kmem_cache* s);
  struct kmem_cache* bootstrap(struct kmem_cache *static_cache);
  void kmem_cache_init(void);
};

  
}
