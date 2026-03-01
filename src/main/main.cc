#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <unistd.h>

#include "CONFIG.h"
#include "mm/slab/slab.h"

namespace {

constexpr size_t kMaxNote = 0x1000;
constexpr size_t kMinDataSize = 64;     
constexpr size_t kMaxDataSize = 1024;

typedef struct Note {
  uint64_t size;
  uint8_t *data;
} Note;

Note *notes[kMaxNote];
mm::slab::Slab *Slab = nullptr;

static void menu() {
  std::cout << "Average Heap Note challenge" << std::endl;
  std::cout << "1. create note" << std::endl;
  std::cout << "2. delete note" << std::endl;
  std::cout << "3. edit note" << std::endl;
  std::cout << "4. read note" << std::endl;
  std::cout << "5. exit" << std::endl;
}

static Note *get_note(uint64_t idx) {
  if (idx >= kMaxNote) {
    return nullptr;
  }
  return notes[idx];
}

static void create_note() {
  uint64_t idx = 0;
  uint64_t req_size = 0;
  std::cout << "idx > " << std::flush;
  if (!(std::cin >> idx)) {
    return;
  }
  if (idx >= kMaxNote) {
    std::cout << "invalid idx" << std::endl;
    return;
  }
  std::cout << "size > " << std::flush;
  if (!(std::cin >> req_size)) {
    return;
  }

  Note *note = notes[idx];
  if (!note) {
    note = static_cast<Note *>(Slab->kmalloc(sizeof(Note)));
    if (!note) {
      std::cout << "alloc note failed" << std::endl;
      return;
    }
    note->size = 0;
    note->data = nullptr;
    notes[idx] = note;
  }

  if (note->data) {
    Slab->kfree(note->data);
    note->data = nullptr;
    note->size = 0;
  }

  size_t alloc_size = static_cast<size_t>(req_size);
  if (alloc_size < kMinDataSize) {
    alloc_size = kMinDataSize;
  }
  if (alloc_size > kMaxDataSize) {
    std::cout << "size too large" << std::endl;
    return;
  }

  void *buf = Slab->kmalloc(alloc_size);
  if (!buf) {
    std::cout << "alloc data failed" << std::endl;
    return;
  }

  note->size = alloc_size;
  note->data = static_cast<uint8_t *>(buf);
  std::memset(note->data, 0, note->size);
}

static void delete_note() {
  uint64_t idx = 0;
  std::cout << "idx > " << std::flush;
  if (!(std::cin >> idx)) {
    return;
  }

  Note *note = get_note(idx);
  if (!note || !note->data) {
    std::cout << "no note" << std::endl;
    return;
  }

  Slab->kfree(note->data);
  std::cout << "deleted" << std::endl;
}

static void edit_note() {
  uint64_t idx = 0;
  std::cout << "idx > " << std::flush;
  if (!(std::cin >> idx)) {
    return;
  }

  Note *note = get_note(idx);
  if (!note || !note->data) {
    std::cout << "no note" << std::endl;
    return;
  }

  std::cout << "data > " << std::flush;
  const ssize_t n = ::read(0, note->data, note->size);
}

static void read_note() {
  uint64_t idx = 0;
  std::cout << "idx > " << std::flush;
  if (!(std::cin >> idx)) {
    return;
  }

  Note *note = get_note(idx);
  if (!note || !note->data) {
    std::cout << "no note" << std::endl;
    return;
  }

  std::cout << "data: " << std::flush;
  const ssize_t n = ::write(1, note->data, note->size);
  if (n <= 0) {
    std::cout << std::endl << "read failed" << std::endl;
    return;
  }
  std::cout << std::endl;
}
} 

int main() {
  setvbuf(stdin, nullptr, _IONBF, 0);
  setvbuf(stdout, nullptr, _IONBF, 0);
  setvbuf(stderr, nullptr, _IONBF, 0);

  mm::slab::Slab slab(static_cast<uint64_t>(PAGES) * PAGE_SIZE);
  Slab = &slab;
  std::memset(notes, 0, sizeof(notes));
  Slab->kernel_noise(256);

  menu();
  while (true) {
    uint64_t cmd = 0;
    std::cout << "cmd > " << std::flush;
    if (!(std::cin >> cmd)) {
      return 0;
    }

    switch (cmd) {
    case 1:
      create_note();
      break;
    case 2:
      delete_note();
      break;
    case 3:
      edit_note();
      break;
    case 4:
      read_note();
      break;
    case 5:
      return 0;
    default:
      std::cout << "unknown" << std::endl;
      break;
    }
  }
}
