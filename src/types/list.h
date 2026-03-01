#pragma once
#include <cstddef>

namespace mm::types{
struct list_head
{
  struct list_head *next, *prev;
};


static inline void INIT_LIST_HEAD(struct list_head *list)
{
  list->next = list;
  list->prev = list;
}

static inline void list_del(struct list_head *entry){
  entry->next->prev = entry->prev;
  entry->prev->next = entry->next;
}

static inline void list_add(struct list_head *entry, struct list_head *head){
  head->next->prev = entry;
  entry->next = head->next;
  entry->prev = head;
  head->next = entry;
}

template <typename T>
static inline T* _list_first_entry_or_null(struct list_head *ptr, size_t offset) {
    if (ptr->next == ptr) { 
        return nullptr;
    }
    return reinterpret_cast<T*>(reinterpret_cast<char*>(ptr->next) - offset);
}




}