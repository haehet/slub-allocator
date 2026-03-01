#include <sys/mman.h>
#include <cstdint>
#include <stdexcept>
#include <limits>
#include <stdlib.h>
#include "phys.h"

namespace mm::phys{

FLATMEM::FLATMEM(uint64_t pages) : num_pages(pages) {
    size_t alloc_size = PAGE_SIZE * pages;

    phys_base = (uint8_t*)mmap(
        (void*)((util::get_urandom() & 0xFFFFFFFF) << 12), 
        alloc_size, 
        PROT_READ | PROT_WRITE, 
        MAP_PRIVATE | MAP_ANONYMOUS, 
        -1, 
        0
    );

    if (phys_base == MAP_FAILED) {
        throw std::runtime_error("Failed to mmap phys base");
    }
    mem_map = static_cast<mm::types::page*>(std::calloc(static_cast<size_t>(num_pages), sizeof(mm::types::page)));
    if (!mem_map){
      munmap(phys_base, alloc_size);
      throw std::runtime_error("Failed to calloc page array");
    }  
}

FLATMEM::~FLATMEM() {
    if (phys_base != MAP_FAILED) {
        munmap(phys_base, num_pages * PAGE_SIZE);
        std::free(mem_map);
    }
}

}
