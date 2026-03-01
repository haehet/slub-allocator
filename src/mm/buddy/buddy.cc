#include "mm/buddy/buddy.h"

namespace mm::buddy{

uint64_t Buddy::page_to_pfn(const mm::types::page* page) const {
  if (!page || !mem_map) throw std::runtime_error("page_to_pfn: null");
  if (page < mem_map || page >= mem_map + nr_pages)
    throw std::runtime_error("page_to_pfn: out of range");
  return static_cast<uint64_t>(page - mem_map);
}

mm::types::page* Buddy::pfn_to_page(uint64_t pfn) const {
  if (!mem_map) throw std::runtime_error("pfn_to_page: null mem_map");
  if (pfn >= nr_pages) throw std::runtime_error("pfn_to_page: out of range");
  return mem_map + pfn;
}

void* Buddy::page_to_virt(mm::types::page* page) const {
  uint64_t pfn = page_to_pfn(page);
  return base + (pfn * PAGE_SIZE);
}

mm::types::page* Buddy::virt_to_page(void* addr) const {
  if (!addr || !base || !mem_map) throw std::runtime_error("virt_to_page: null");
  auto a = reinterpret_cast<uint8_t*>(addr);
  if (a < base) throw std::runtime_error("virt_to_page: below base");

  uint64_t off = static_cast<uint64_t>(a - base);
  uint64_t pfn = off / PAGE_SIZE;
  if (pfn >= nr_pages) throw std::runtime_error("virt_to_page: out of range");
  return mem_map + pfn;
}


static inline unsigned long find_buddy_pfn(unsigned long page_pfn, unsigned int order){
  return page_pfn ^ (1UL << order);
}


static inline bool page_is_buddy(types::page *page, types::page *buddy, unsigned int order){
  if((buddy->flag & types::PG_BUDDY) == 0){
    return false;
  }
  if (buddy->order != order){
    return false;
  }
  return true;
}

static inline void add_to_free_list(types::page *page, zone::zone *zone, unsigned int order, int migratetype){
  zone::free_area *area = &zone->free_area[order];
  types::list_add(&page->lru, &area->free_list[migratetype]);
  area->nr_free++;
}

static inline void del_page_from_free_list(types::page *page, zone::zone *zone, unsigned int order){
  types::list_del(&page->lru);
  page->flag &= ~types::PG_BUDDY;
  page->order = 0;
  zone->free_area[order].nr_free --;
}

static inline void set_buddy_order(types::page *page, unsigned int order){
  page->order = order;
  page->flag |= types::PG_BUDDY;
}


static inline types::page *get_page_from_free_area(zone::free_area *area, int migratetype){
  return types::_list_first_entry_or_null<types::page>(
            &area->free_list[migratetype], 
            offsetof(types::page, lru));
}

static inline void expand(zone::zone *zone, types::page *page, int low, int high, int migratetype){
  unsigned long size = 1UL << high;

  while (high > low){
    high --;
    size >>=1;
    add_to_free_list(&page[size], zone, high, migratetype);
    set_buddy_order(&page[size], high);
  }
  
}

static inline void prep_compound_page(types::page* head, unsigned int order) {
    if (order == 0) return;
    uint64_t num_pages = 1ull << order;
    head->flag |= types::PG_HEAD;
    for (uint64_t i = 1; i < num_pages; ++i) {
        types::page* tail_page = head + i; 
        tail_page->flag |= types::PG_TAIL;
        tail_page->tail.compound_head = head;
    }
}

static inline void destroy_compound_page(types::page* head, unsigned int order) {
    if (order == 0) return;
    uint64_t num_pages = 1ull << order;
    head->flag &= ~types::PG_HEAD;
    for (uint64_t i = 1; i < num_pages; ++i) {
        types::page* tail_page = head + i; 
        tail_page->flag &= ~types::PG_TAIL;
        tail_page->tail.compound_head = nullptr;
    }
}


types::page* Buddy::alloc_pages(unsigned int order, int migratetype){
  util::BUG_ON(order >= MAX_ORDER, "Allocation order out of bounds");

  unsigned int current_order;
  zone::free_area *area;
  types::page* page;
  for (current_order = order; current_order<MAX_ORDER; ++current_order){
    area = &this->zone->free_area[current_order];
    page = get_page_from_free_area(area, migratetype);
    if (!page)
      continue;
    
    del_page_from_free_list(page, zone, current_order);
    expand(zone, page, order, current_order, migratetype);
    prep_compound_page(page, order);
    return page;
  }
  return NULL;

}

void Buddy::free_one_page(types::page *page, unsigned long pfn, unsigned int order, int migratetype){
  unsigned long buddy_pfn;
  unsigned long combined_pfn;
  unsigned int max_order;
  types::page *buddy;

  max_order = MAX_ORDER - 1;

  util::BUG_ON(!this->zone->initialized, "Zone uninitialized");
  util::BUG_ON(page->flag&PAGE_FLAGS_CHECK_AT_PREP, "Corrupted page flag");
  util::BUG_ON(pfn & ((1UL<<order)-1), "Unaligned buddy pfn");
  util::BUG_ON(pfn >= this->nr_pages, "bad_range: PFN out of bounds!");
  util::BUG_ON(page != &this->mem_map[pfn], "bad_range: Page pointer mismatch");

continue_merging:
  while (order < max_order){
    buddy_pfn = find_buddy_pfn(pfn, order);
    if (buddy_pfn >= this->nr_pages) break;
    buddy = &this->mem_map[buddy_pfn];

    if (!page_is_buddy(page, buddy, order))
      goto done_merging;


    del_page_from_free_list(buddy, zone, order);
    
    /* buddies differ by exactly one bit. The left buddy always has a 0 at that bit */
    combined_pfn = buddy_pfn & pfn;
    if (DEBUG_BUDDY) {std::cout << "[Buddy] Merge: O" << order << " [" << pfn << "+" << buddy_pfn << "] -> PFN:" << combined_pfn << " (O" << order + 1 << ")" << std::endl;}
    pfn = combined_pfn;
    page = &this->mem_map[pfn]; 
    order++;
  }

done_merging:
  set_buddy_order(page, order);
  add_to_free_list(page, this->zone, order, migratetype);
}

void Buddy::free_pages(types::page *page, unsigned int order){
  util::BUG_ON(!page, "free_pages: Attempted to free a NULL");
  util::BUG_ON(order >= MAX_ORDER, "free_pages: Order out of bounds");
  destroy_compound_page(page, order);
  
  unsigned long pfn = page - this->mem_map;
  util::BUG_ON(pfn >= this->nr_pages, "free_pages: Calculated PFN is out of range");
  this->free_one_page(page, pfn, order, MIGRATE_UNMOVABLE);
}


void Buddy::init() {
  base = phys.get_base_ptr();
  nr_pages = phys.get_num_pages();
  mem_map = phys.get_mem_map();
  zone::free_area_init_core(zone, 0, nr_pages);


  unsigned long pfn = 0;
  while (pfn <nr_pages){
    unsigned long remaining = nr_pages - pfn;
    unsigned int order = MAX_ORDER -1;
    
    while (order >0){
      if((1UL << order) <= remaining && (pfn & ((1UL << order) - 1)) == 0){break;}
      order--;
    }
    unsigned long pages_per_block = (1UL << order);
    for(unsigned long i=0; i<pages_per_block; i++){
      mem_map[pfn + i].flag &= ~mm::types::PG_RESERVED;
    }
    this->free_one_page(&mem_map[pfn], pfn, order, MIGRATE_UNMOVABLE);
    pfn += pages_per_block;
  }
}


Buddy::Buddy(uint64_t pages)
  : phys(pages)   
{
  this->zone = new zone::zone();
  init();
}

}
