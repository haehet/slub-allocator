#pragma once
#include <cstdint>
#include "types/mm_types.h"
#include "util/utils.h"


namespace mm::phys{
class FLATMEM{
  private:
  uint8_t *phys_base;
  uint64_t num_pages;
  mm::types::page* mem_map;

  public:
  FLATMEM(uint64_t pages);
  ~FLATMEM();

  uint8_t* get_base_ptr() const { return phys_base; }
  uint64_t get_num_pages() const { return num_pages; }
  mm::types::page* get_mem_map() const { return mem_map; }
};

};

  

