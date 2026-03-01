#pragma once
#include "types/list.h"
#include "CONFIG.h"

enum migratetype{
  MIGRATE_UNMOVABLE,
  MIGRATE_TYPES
};


namespace mm::buddy::zone{

struct free_area
{
  mm::types::list_head free_list[MIGRATE_TYPES];
  unsigned long nr_free;
};

struct zone
{
  struct free_area free_area[MAX_ORDER];
  int initialized;
  unsigned long		zone_start_pfn;

  unsigned long managed_pages;
  unsigned long spanned_pages;
};

void free_area_init_core(struct zone *zone, unsigned long start_pfn, unsigned long total_pages);

}