#include "slab.h"
#include <climits>
#include <new>

namespace mm::slab{

struct kmem_cache *kmem_cache;
struct kmem_cache *kmem_cache_node;
static types::list_head slab_caches;
static bool slab_caches_initialized = false;
static bool slab_allocator_initialized = false;

struct kmem_cache *kmalloc_caches[12];
static uint8_t size_index[64];

static constexpr uint32_t kKmallocCacheSizes[] = {
    8, 16, 32, 64, 96, 128, 192, 256, 512, 1024, 2048, 4096,
};
static constexpr const char *kKmallocCacheNames[] = {
    "kmalloc-8",   "kmalloc-16",  "kmalloc-32",  "kmalloc-64",
    "kmalloc-96",  "kmalloc-128", "kmalloc-192", "kmalloc-256",
    "kmalloc-512", "kmalloc-1k",  "kmalloc-2k",  "kmalloc-4k",
};
static constexpr unsigned long kMinPartialLowerBound = 5;
static constexpr unsigned long kMinPartialUpperBound = 10;
static constexpr size_t kNumKmallocCaches =
    sizeof(kKmallocCacheSizes) / sizeof(kKmallocCacheSizes[0]);
static_assert(kNumKmallocCaches == 12, "kmalloc_caches size mismatch");

#ifdef CONFIG_SLAB_FREELIST_RANDOM
static int init_cache_random_seq(struct kmem_cache *s) {
  const unsigned int count = s->oo.x;
  if (!count) {
    return -1;
  }

  if (s->random_seq) {
    return 0;
  }

  unsigned int *seq = new (std::nothrow) unsigned int[count];
  if (!seq) {
    return -1;
  }

  for (unsigned int i = 0; i < count; ++i) {
    seq[i] = i;
  }

  for (unsigned int i = count - 1; i > 0; --i) {
    const uint64_t r = util::get_urandom();
    const unsigned int j = static_cast<unsigned int>(r % (static_cast<uint64_t>(i) + 1ULL));
    const unsigned int tmp = seq[i];
    seq[i] = seq[j];
    seq[j] = tmp;
  }

  s->random_seq = seq;
  return 0;
}

static void init_freelist_randomization(void) {
  if (!slab_caches_initialized) {
    return;
  }

  types::list_head *head = &slab_caches;
  for (types::list_head *pos = head->next; pos != head; pos = pos->next) {
    struct kmem_cache *s = reinterpret_cast<struct kmem_cache *>(
        reinterpret_cast<char *>(pos) - offsetof(struct kmem_cache, list));
    util::BUG_ON(init_cache_random_seq(s) != 0,
                 "init_freelist_randomization: random_seq init failed");
  }
}
#else
static inline int init_cache_random_seq(struct kmem_cache *s) {
  (void)s;
  return 0;
}

static inline void init_freelist_randomization(void) {}
#endif

static void init_kmalloc_size_index_table() {
  for (uint32_t slot = 0; slot < 64; ++slot) {
    const uint32_t req_size = (slot + 1) * 8;
    uint8_t idx = static_cast<uint8_t>(kNumKmallocCaches - 1);
    for (uint8_t i = 0; i < kNumKmallocCaches; ++i) {
      if (kKmallocCacheSizes[i] >= req_size) {
        idx = i;
        break;
      }
    }
    size_index[slot] = idx;
  }
}

static void init_boot_cache_runtime(struct kmem_cache *cache,
                                    struct kmem_cache_cpu *cpu,
                                    struct kmem_cache_node *node) {
  cache->cpu_slab = cpu;
  cache->node[0] = node;

  cpu->page = nullptr;
  cpu->freelist = nullptr;
#ifdef CONFIG_SLUB_CPU_PARTIAL
  cpu->partial = nullptr;
#endif

  node->nr_partial = 0;
  types::INIT_LIST_HEAD(&node->partial);
}

static inline void list_del_init(types::list_head *entry) {
  types::list_del(entry);
  types::INIT_LIST_HEAD(entry);
}

static inline bool page_on_partial_list(types::page *page) {
  return page->slab_list.next != &page->slab_list;
}

static inline struct kmem_cache *kmalloc_slab_for_size(size_t size) {
  if (size == 0 || size > kKmallocCacheSizes[kNumKmallocCaches - 1]) {
    return nullptr;
  }

  if (size <= 512) {
    const size_t slot = (size - 1) >> 3;
    return kmalloc_caches[size_index[slot]];
  }

  for (size_t i = 0; i < kNumKmallocCaches; ++i) {
    if (kKmallocCacheSizes[i] >= size) {
      return kmalloc_caches[i];
    }
  }
  return nullptr;
}

static inline unsigned int get_order_for_bytes(size_t size) {
  size_t span = PAGE_SIZE;
  unsigned int order = 0;
  while (span < size && order + 1 < MAX_ORDER) {
    span <<= 1;
    ++order;
  }
  return order;
}

static inline unsigned int align_down_u32(unsigned int value,
                                          unsigned int align) {
  return value & ~(align - 1);
}

static inline unsigned int ilog2_floor_u32(unsigned int v) {
  unsigned int l = 0;
  while (v > 1) {
    v >>= 1;
    ++l;
  }
  return l;
}

static inline void set_min_partial(struct kmem_cache *s) {
  unsigned long min = ilog2_floor_u32(s->size) / 2;
  if (min < kMinPartialLowerBound) {
    min = kMinPartialLowerBound;
  } else if (min > kMinPartialUpperBound) {
    min = kMinPartialUpperBound;
  }
  s->min_partial = min;
}

#ifdef CONFIG_SLUB_CPU_PARTIAL
static inline void set_cpu_partial(struct kmem_cache *s) {
  if (s->size >= PAGE_SIZE) {
    s->cpu_partial = 2;
  } else if (s->size >= 1024) {
    s->cpu_partial = 6;
  } else if (s->size >= 256) {
    s->cpu_partial = 13;
  } else {
    s->cpu_partial = 30;
  }
}

static inline unsigned int cpu_partial_page_count(types::page *head) {
  unsigned int cnt = 0;
  for (types::page *p = head; p; p = p->next) {
    ++cnt;
  }
  return cnt;
}
#else
static inline void set_cpu_partial(struct kmem_cache *s) { (void)s; }
#endif

static inline unsigned long freelist_addr_mix(unsigned long ptr_addr) {
#if ULONG_MAX == 0xffffffffUL
  return static_cast<unsigned long>(
      __builtin_bswap32(static_cast<uint32_t>(ptr_addr)));
#else
  return static_cast<unsigned long>(
      __builtin_bswap64(static_cast<uint64_t>(ptr_addr)));
#endif
}


static inline void* freelist_ptr(struct kmem_cache *s, void *ptr, unsigned long ptr_addr) {
#ifdef CONFIG_SLAB_FREELIST_HARDENED
    return (void *)((unsigned long)ptr ^ s->random ^ freelist_addr_mix(ptr_addr));
#else
    return ptr; 
#endif
}

static inline void* get_freepointer(struct kmem_cache *s, void *object) {
    unsigned long ptr_addr = (unsigned long)object + s->offset;
    void *p = *(void **)ptr_addr; 
    return freelist_ptr(s, p, ptr_addr); 
}

static inline void set_freepointer(struct kmem_cache *s, void *object, void *next) {
    unsigned long ptr_addr = (unsigned long)object + s->offset;
    void **p = (void **)ptr_addr;
    *p = freelist_ptr(s, next, ptr_addr);
}

void* get_freelist(struct kmem_cache *s, types::page *page) {
    (void)s;
    void *freelist = page->freelist; 
  
    page->freelist = nullptr;       
    return freelist;              
}

static void* get_partial(struct kmem_cache *s, struct kmem_cache_cpu *c) {
  struct kmem_cache_node* n = s->node[0];
  size_t off = offsetof(types::page, slab_list);

  types::page *p = types::_list_first_entry_or_null<types::page>(&n->partial, off);
  if (!p) return nullptr;

  list_del_init(&p->slab_list);
  n->nr_partial--;

  c->page = p;

  void *freelist = p->freelist;
  p->freelist = nullptr;  
  return freelist;
}

types::page* Slab::new_slab_page(struct kmem_cache* s){
   types::page* page = Buddy.alloc_pages(s->oo.order, MIGRATE_UNMOVABLE);
  if (!page) util::BUG_ON(true, "OOM");

  page->slab_cache = s;
  page->inuse = 0;
  const size_t bytes = (size_t)PAGE_SIZE << s->oo.order;
  const size_t objsz = s->size;
  const size_t nobj  = bytes / objsz;
  page->objects = (uint32_t)nobj;

  uint8_t* base = (uint8_t*)Buddy.page_to_virt(page); 
  page->freelist = nullptr;
  types::INIT_LIST_HEAD(&page->slab_list);

  bool randomized = false;
#ifdef CONFIG_SLAB_FREELIST_RANDOM
  if (s->random_seq && nobj > 1 && s->oo.x >= nobj) {
    randomized = true;
    for (size_t i = 0; i < nobj; ++i) {
      const unsigned int idx = s->random_seq[i];
      void *obj = base + static_cast<size_t>(idx) * objsz;
      set_freepointer(s, obj, page->freelist);
      page->freelist = obj;
    }
  }
#endif

  if (!randomized) {
    for (size_t i = 0; i < nobj; ++i) {
      void* obj = base + i * objsz;
      set_freepointer(s, obj, page->freelist);
      page->freelist = obj;
    }
  }
  return page;
}


void Slab::deactivate_slab(struct kmem_cache *s,
                           mm::types::page *page,
                           void *cpu_freelist)
{
    struct kmem_cache_node *n = s->node[0]; // only one node for this allocator
    if (cpu_freelist) {
        void *prior = page->freelist;     
        void *iter = cpu_freelist;
        while (true) {
            void *nx = get_freepointer(s, iter); 
            if (!nx) break;
            iter = nx;
        }
        set_freepointer(s, iter, prior);  
        page->freelist = cpu_freelist;
    }
    if (page->inuse == 0 && n->nr_partial >= s->min_partial) {
        page->slab_cache = nullptr;
        Buddy.free_pages(page, s->oo.order);
        return;
    }

    if (page->freelist) {
        mm::types::list_add(&page->slab_list, &n->partial);
        n->nr_partial++;
        return;
    }
}


 /* ___slab_alloc */
 void* Slab::slow_path(struct kmem_cache *s, struct kmem_cache_cpu *c){
  void *freelist;
  types::page *page;

reread_page:
  page = c->page;
  if (!page){
    goto new_slab;
  }

redo:
  freelist = c->freelist;
  if (freelist)
    goto load_freelist;
  
  // slow path -1 (get percpu → page → freelist)
  freelist = get_freelist(s, page);
  if (!freelist){
    goto deactivate_current;
  }

load_freelist:
  page = c->page;
  page->inuse++;                 
  c->freelist = (void **)get_freepointer(s, freelist);
  return freelist;


deactivate_current:
  // This matches the kernel intent: before switching slabs, flush current cpu slab state.
  if (page != c->page){
    goto reread_page;
  }

  freelist = c->freelist;
  c->page = NULL;
  c->freelist = NULL;
  deactivate_slab(s, page, freelist);
  goto new_slab;

new_slab: 
  // slow path -2 (get percpu → partial to prcpu page)
  if(c->partial){
    page = c->partial;
    c->partial = (types::page *)page->next;
    c->page = page;
    c->freelist = NULL;
    goto redo;
  }
  else{
    goto new_objects;
  }

new_objects:
  // slow path - 3 (get node → partial to percpu page)
  freelist = get_partial(s, c);
  if (freelist){
    c->freelist = NULL;
    goto load_freelist;
  }

  // slow path - 4 (get new page by buddy)
  page = new_slab_page(s);
  c->page = page;
  c->freelist = NULL;
  freelist = get_freelist(s, page);
  if (!freelist){
    goto deactivate_current;
  }
  goto load_freelist;

} 


inline void *Slab::slab_alloc(struct kmem_cache *s){
  void *object; 
  struct kmem_cache_cpu *c;
  mm::types::page *page;

  c = s->cpu_slab;

  object = c->freelist;
  page = c->page;
  if (object ==  nullptr || page == nullptr){
    return slow_path(s, c);
  }
  
  // fast path - if c->freelist available 
  void *next_object = get_freepointer(s, object);
  page->inuse++;
  c->freelist = (void **)next_object;
  return object;
}

inline void Slab::slab_free(struct kmem_cache *s, void *object) {
  util::BUG_ON(!s || !object, "slab_free: invalid input");

  types::page *page = Buddy.virt_to_page(object);
  util::BUG_ON(page->slab_cache != s, "slab_free: cache mismatch");
  util::BUG_ON(page->inuse == 0, "slab_free: double free detected");

  struct kmem_cache_cpu *c = s->cpu_slab;

  if (page == c->page) {
    set_freepointer(s, object, c->freelist);
    c->freelist = (void **)object;
    page->inuse--;
    return;
  }

  struct kmem_cache_node *n = s->node[0];
  void *prior = page->freelist;
  set_freepointer(s, object, prior);
  page->freelist = object;
  page->inuse--;

  if (!prior) {
#ifdef CONFIG_SLUB_CPU_PARTIAL
    page->next = c->partial;
    c->partial = page;

    if (cpu_partial_page_count(c->partial) > s->cpu_partial) {
      types::page *drain = c->partial;
      c->partial = nullptr;

      while (drain) {
        types::page *next = drain->next;
        drain->next = nullptr;

        if (drain->inuse == 0 && n->nr_partial >= s->min_partial) {
          drain->slab_cache = nullptr;
          Buddy.free_pages(drain, s->oo.order);
        } else if (drain->freelist) {
          types::list_add(&drain->slab_list, &n->partial);
          n->nr_partial++;
        }
        drain = next;
      }
    }
    return;
#else
    types::list_add(&page->slab_list, &n->partial);
    n->nr_partial++;
#endif
  }

#ifndef CONFIG_SLUB_CPU_PARTIAL
  if (page->inuse == 0) {
    if (page_on_partial_list(page)) {
      if (n->nr_partial >= s->min_partial) {
        list_del_init(&page->slab_list);
        util::BUG_ON(n->nr_partial == 0, "slab_free: partial underflow");
        n->nr_partial--;
        page->slab_cache = nullptr;
        Buddy.free_pages(page, s->oo.order);
      }
    }
  }
#endif
}



struct kmem_cache* Slab::bootstrap(struct kmem_cache *static_cache) {
  util::BUG_ON(!static_cache, "bootstrap: static_cache is null");
  util::BUG_ON(!kmem_cache, "bootstrap: kmem_cache is null");
  util::BUG_ON(kmem_cache->object_size != sizeof(struct kmem_cache),
               "bootstrap: kmem_cache object size mismatch");

  struct kmem_cache *s =
      static_cast<struct kmem_cache *>(slab_alloc(kmem_cache));
  util::BUG_ON(!s, "bootstrap: OOM allocating kmem_cache from slab");
  memcpy(s, static_cache, kmem_cache->object_size);

  /*
   * Single CPU model: flush current per-cpu slab state into node partial/free
   * before replacing the cache pointer.
   */
  if (s->cpu_slab && s->cpu_slab->page) {
    types::page *cpu_page = s->cpu_slab->page;
    void *cpu_freelist = s->cpu_slab->freelist;
    s->cpu_slab->page = nullptr;
    s->cpu_slab->freelist = nullptr;
    deactivate_slab(s, cpu_page, cpu_freelist);
  }

  for (int node = 0; node < MAX_NUMNODES; ++node) {
    struct kmem_cache_node *n = s->node[node];
    if (!n) {
      continue;
    }

    types::list_head *head = &n->partial;
    for (types::list_head *pos = head->next; pos != head; pos = pos->next) {
      types::page *p = reinterpret_cast<types::page *>(
          reinterpret_cast<char *>(pos) - offsetof(types::page, slab_list));
      p->slab_cache = s;
    }
  }

  if (!slab_caches_initialized) {
    types::INIT_LIST_HEAD(&slab_caches);
    slab_caches_initialized = true;
  }
  types::INIT_LIST_HEAD(&s->list);
  types::list_add(&s->list, &slab_caches);
  return s;
}


static void create_boot_cache(struct kmem_cache *s, const char *name,
                              uint32_t size) {
    memset(s, 0, sizeof(*s));

    s->name = name;
    s->object_size = size;

    const uint32_t align = 8;
    s->size = (size + align - 1) & ~(align - 1);
    s->inuse = s->size;
    s->align = align;
    s->offset = align_down_u32(s->object_size / 2, sizeof(void *));

#ifdef CONFIG_SLAB_FREELIST_HARDENED
    s->random = util::get_urandom();
#endif
#ifdef CONFIG_SLAB_FREELIST_RANDOM
    s->random_seq = nullptr;
#endif

    s->oo.order = 0;
    s->oo.x = PAGE_SIZE / s->size;
    set_min_partial(s);
    set_cpu_partial(s);

    s->flags = 0;
    types::INIT_LIST_HEAD(&s->list);
}


void Slab::kmem_cache_init(void){
  if (slab_allocator_initialized) {
    return;
  }
  slab_allocator_initialized = true;

  static struct kmem_cache boot_kmem_cache, boot_kmem_cache_node;
  static struct kmem_cache_cpu boot_cpu_kmem_cache, boot_cpu_kmem_cache_node;
  static struct kmem_cache_node boot_node_kmem_cache, boot_node_kmem_cache_node;
  static struct kmem_cache boot_kmalloc_caches[12];
  static struct kmem_cache_cpu boot_kmalloc_cpus[12];
  static struct kmem_cache_node boot_kmalloc_nodes[12];

  kmem_cache_node = &boot_kmem_cache_node;
  kmem_cache = &boot_kmem_cache;

  create_boot_cache(kmem_cache_node, "kmem_cache_node", sizeof(struct kmem_cache_node));
  create_boot_cache(kmem_cache, "kmem_cache", sizeof(struct kmem_cache));

  // Bring up a minimal boot state so slab_alloc(kmem_cache) can run.
  init_boot_cache_runtime(&boot_kmem_cache, &boot_cpu_kmem_cache,
                          &boot_node_kmem_cache);
  init_boot_cache_runtime(&boot_kmem_cache_node, &boot_cpu_kmem_cache_node,
                          &boot_node_kmem_cache_node);

  // Prime one slab page per boot cache to seed per-cpu freelists.
  {
    types::page *p = new_slab_page(kmem_cache);
    kmem_cache->cpu_slab->page = p;
    kmem_cache->cpu_slab->freelist = (void **)get_freelist(kmem_cache, p);
    util::BUG_ON(!kmem_cache->cpu_slab->freelist,
                 "kmem_cache_init: failed to seed kmem_cache freelist");
  }
  {
    types::page *p = new_slab_page(kmem_cache_node);
    kmem_cache_node->cpu_slab->page = p;
    kmem_cache_node->cpu_slab->freelist = (void **)get_freelist(kmem_cache_node, p);
    util::BUG_ON(!kmem_cache_node->cpu_slab->freelist,
                 "kmem_cache_init: failed to seed kmem_cache_node freelist");
  }

  kmem_cache = bootstrap(&boot_kmem_cache);
  kmem_cache_node = bootstrap(&boot_kmem_cache_node);

  for (size_t i = 0; i < kNumKmallocCaches; ++i) {
    kmalloc_caches[i] = nullptr;
  }
  init_kmalloc_size_index_table();

  for (size_t i = 0; i < kNumKmallocCaches; ++i) {
    struct kmem_cache *boot_cache = &boot_kmalloc_caches[i];
    create_boot_cache(boot_cache, kKmallocCacheNames[i], kKmallocCacheSizes[i]);
    init_boot_cache_runtime(boot_cache, &boot_kmalloc_cpus[i], &boot_kmalloc_nodes[i]);

    types::page *p = new_slab_page(boot_cache);
    boot_cache->cpu_slab->page = p;
    boot_cache->cpu_slab->freelist = (void **)get_freelist(boot_cache, p);
    util::BUG_ON(!boot_cache->cpu_slab->freelist,
                 "kmem_cache_init: failed to seed kmalloc freelist");

    kmalloc_caches[i] = bootstrap(boot_cache);
  }

  init_freelist_randomization();
}

void *Slab::kmalloc(size_t size) {
  if (size == 0) {
    return nullptr;
  }

  struct kmem_cache *s = kmalloc_slab_for_size(size);
  if (s) {
    return slab_alloc(s);
  }

  const unsigned int order = get_order_for_bytes(size);
  const size_t span = (static_cast<size_t>(PAGE_SIZE) << order);
  if (span < size || order >= MAX_ORDER) {
    return nullptr;
  }

  types::page *page = Buddy.alloc_pages(order, MIGRATE_UNMOVABLE);
  if (!page) {
    return nullptr;
  }

  page->slab_cache = nullptr;
  page->freelist = nullptr;
  page->inuse = static_cast<uint16_t>(order);
  page->objects = 0;
  return Buddy.page_to_virt(page);
}

void Slab::kfree(void *ptr) {
  if (!ptr) {
    return;
  }

  types::page *page = Buddy.virt_to_page(ptr);
  if (page->slab_cache) {
    slab_free(page->slab_cache, ptr);
    return;
  }

  const unsigned int order = page->inuse;
  util::BUG_ON(order >= MAX_ORDER, "kfree: invalid large allocation order");
  Buddy.free_pages(page, order);
}

void Slab::kernel_noise(uint32_t rounds) {
#if KERNEL_NOISE
  static constexpr size_t kNoiseSlots = 256;
  static constexpr size_t kNoiseSizes[] = {
      8, 16, 32, 64, 96, 128, 192, 256, 512, 1024, 2048, 4096, 5000, 8192};
  static constexpr unsigned int kNoiseOrders[] = {0, 1, 2, 3};

  void *live[kNoiseSlots];
  size_t live_cnt = 0;
  memset(live, 0, sizeof(live));

  for (uint32_t i = 0; i < rounds; ++i) {
    const uint64_t r = util::get_urandom();
    const uint32_t op = static_cast<uint32_t>(r & 0x3);

    if (op <= 1 && live_cnt < kNoiseSlots) {
      const size_t sz =
          kNoiseSizes[(r >> 8) % (sizeof(kNoiseSizes) / sizeof(kNoiseSizes[0]))];
      void *p = kmalloc(sz);
      if (p) {
        live[live_cnt++] = p;
      }
      continue;
    }

    if (op == 2 && live_cnt > 0) {
      const size_t idx = (r >> 16) % live_cnt;
      void *victim = live[idx];
      kfree(victim);
      live[idx] = live[live_cnt - 1];
      live[live_cnt - 1] = nullptr;
      --live_cnt;
      continue;
    }

    const unsigned int order =
        kNoiseOrders[(r >> 24) % (sizeof(kNoiseOrders) / sizeof(kNoiseOrders[0]))];
    types::page *p = Buddy.alloc_pages(order, MIGRATE_UNMOVABLE);
    if (p) {
      if (((r >> 32) & 1ULL) != 0ULL) {
        void *v = Buddy.page_to_virt(p);
        const size_t span = static_cast<size_t>(PAGE_SIZE) << order;
        memset(v, static_cast<int>(r & 0xff), util::min_t<size_t>(span, 128));
      }
      Buddy.free_pages(p, order);
    }
  }

  while (live_cnt > 0) {
    kfree(live[--live_cnt]);
  }
#else
  (void)rounds;
#endif
}

Slab::Slab(uint64_t size_memory)
  : Buddy(size_memory >>12)
{
  kmem_cache_init();
}

  
}
