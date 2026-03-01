#include "zone.h"
#define for_each_migratetype_order(order, type) \
	for (order = 0; order < MAX_ORDER; order++) \
		for (type = 0; type < MIGRATE_TYPES; type++)

namespace mm::buddy::zone{

static void  zone_init_free_lists(struct zone *zone)
{
	unsigned int order, t;
	for_each_migratetype_order(order, t) {
		types::INIT_LIST_HEAD(&zone->free_area[order].free_list[t]);
		zone->free_area[order].nr_free = 0;
	}
}

void free_area_init_core(struct zone *zone, unsigned long start_pfn, unsigned long total_pages){
  zone->zone_start_pfn = start_pfn;
  zone->spanned_pages = total_pages;
  zone->managed_pages = total_pages;

  zone_init_free_lists(zone);
  zone->initialized = 1;
}

}

  


  
