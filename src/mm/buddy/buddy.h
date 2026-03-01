#include <iostream>
#include "mm/phys/phys.h"
#include "mm/buddy/zone.h"
#include "util/utils.h"



namespace mm::buddy
{
constexpr unsigned long PAGE_FLAGS_CHECK_AT_PREP = 
    types::PG_BUDDY | types::PG_SLAB | types::PG_RESERVED;


  class Buddy{
    public:
    explicit Buddy(uint64_t pages);
    void free_pages(types::page *page, unsigned int order);
    types::page *alloc_pages(unsigned int order, int migratetype);
    uint64_t page_to_pfn(const mm::types::page* page) const;
    mm::types::page* pfn_to_page(uint64_t pfn) const;

    void* page_to_virt(mm::types::page* page) const;
    mm::types::page* virt_to_page(void* addr) const;
    
    private:
    void init();
    void free_one_page(types::page *page, unsigned long pfn, unsigned int order, int migratetype);

    phys::FLATMEM phys;
    zone::zone *zone; // C++ develop skill issue lol 

    uint8_t *base = nullptr;
    uint64_t nr_pages = 0;
    types::page* mem_map = nullptr;

  };
};
