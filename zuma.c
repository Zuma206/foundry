#include "zuma.h"
#include <memory.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void zu_panic(char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  exit(1);
}

static inline void out_of_memory() {
  panic("fatal allocation error: out of memory\n");
}

static void *heap_allocate(void *, size_t size) {
  void *ptr = malloc(size);
  if (ptr == nullptr)
    out_of_memory();
  return ptr;
}

static void *heap_reallocate(void *, void *prev, size_t size) {
  void *ptr = realloc(prev, size);
  if (ptr == nullptr)
    out_of_memory();
  return ptr;
}

static void heap_deallocate(void *, void *ptr) { free(ptr); }

static allocator_vtable_t heap_vtable = {
    .deallocate_impl = heap_deallocate,
    .reallocate_impl = heap_reallocate,
    .allocate_impl = heap_allocate,
};

allocator_t zu_heap = {
    .vtable = &heap_vtable,
    .data = nullptr,
};

static void *arena_allocate(void *data, size_t size) {
  arena_t *arena = data;
  if (size > arena->page_size)
    panic("fatal allocation error: %ld bytes requested from arena with %ld "
          "page size\n",
          size, arena->page_size);
  if (arena->page == nullptr || size > arena->page_size - arena->used) {
    zu_page_t *page = allocate(arena->page_allocator, zu_page_t);
    page->prev = arena->page;
    arena->page = page;
    arena->used = 0;
  }
  void *allocation = arena->page->buffer + arena->used;
  arena->used += size;
  return allocation;
}

static void arena_deallocate(void *, void *) {}

static void *arena_reallocate(void *data, void *, size_t size) {
  return arena_allocate(data, size);
}

static allocator_vtable_t arena_vtable = {
    .deallocate_impl = arena_deallocate,
    .reallocate_impl = arena_reallocate,
    .allocate_impl = arena_allocate,
};

zu_arena_t *zu_new_arena_page_size(zu_allocator_t allocator, size_t page_size) {
  arena_t *arena = allocate(allocator, arena_t);
  arena->page_allocator = allocator;
  arena->page_size = page_size;
  arena->page = nullptr;
  arena->used = 0;
  arena->allocator = (allocator_t){
      .vtable = &arena_vtable,
      .data = arena,
  };
  return arena;
}

void zu_destroy_arena(arena_t *arena) {
  while (arena->page != nullptr) {
    zu_page_t *page = arena->page;
    arena->page = page->prev;
    deallocate(arena->page_allocator, page);
  }
  deallocate(arena->page_allocator, arena);
}

static void *block_allocate(void *data, size_t size) {
  block_t *block = data;
  if (block->size - block->used < size)
    panic("fatal allocation error: block allocator out of space\n");
  void *ptr = block->buffer + block->used;
  block->used += size;
  return ptr;
}

static void block_deallocate(void *, void *) {}

static void *block_reallocate(void *data, void *, size_t size) {
  return block_allocate(data, size);
}

static allocator_vtable_t block_vtable = {
    .reallocate_impl = block_reallocate,
    .deallocate_impl = block_deallocate,
    .allocate_impl = block_allocate,
};

block_t zu_make_block(void *buffer, size_t size) {
  return (block_t){
      .buffer = buffer,
      .size = size,
      .used = 0,
  };
}

allocator_t zu_to_allocator_arena(arena_t *arena) {
  return (allocator_t){
      .vtable = &arena_vtable,
      .data = arena,
  };
}

allocator_t zu_to_allocator_block(block_t *block) {
  return (allocator_t){
      .vtable = &block_vtable,
      .data = block,
  };
}

static void *tracker_allocate(void *data, size_t size) {
  zu_tracker_t *tracker = data;
  zu_tracker_allocation_t *allocation =
      allocate(tracker->backing_allocator, zu_tracker_allocation_t, +size);
  allocation->prev = tracker->prev;
  allocation->next = nullptr;
  tracker->prev = allocation;
  return allocation->buffer;
}

static inline zu_tracker_allocation_t *get_tracker_allocation(void *ptr) {
  return ptr - offsetof(zu_tracker_allocation_t, buffer);
}

static void *tracker_reallocate(void *data, void *ptr, size_t size) {
  zu_tracker_t *tracker = data;
  zu_tracker_allocation_t *allocation = get_tracker_allocation(ptr);
  allocation = reallocate(tracker->backing_allocator, allocation,
                          zu_tracker_allocation_t, +size);
  if (allocation->prev != nullptr)
    allocation->prev->next = allocation;
  if (allocation->next != nullptr)
    allocation->next->prev = allocation;
  else
    tracker->prev = allocation;
  return allocation->buffer;
}

static void tracker_deallocate(void *data, void *ptr) {
  zu_tracker_t *tracker = data;
  zu_tracker_allocation_t *allocation = get_tracker_allocation(ptr);
  if (allocation->prev != nullptr)
    allocation->prev->next = allocation->next;
  if (allocation->next != nullptr)
    allocation->next->prev = allocation->prev;
  else
    tracker->prev = allocation->prev;
  deallocate(tracker->backing_allocator, allocation);
}

static zu_allocator_vtable_t tacker_vtable = {
    .deallocate_impl = tracker_deallocate,
    .reallocate_impl = tracker_reallocate,
    .allocate_impl = tracker_allocate,
};

zu_tracker_t *zu_new_tracker(zu_allocator_t backing_allocator) {
  zu_tracker_t *tracker = allocate(backing_allocator, zu_tracker_t);
  tracker->backing_allocator = backing_allocator;
  tracker->prev = nullptr;
  return tracker;
}

allocator_t zu_to_allocator_tracker(zu_tracker_t *tracker) {
  return (allocator_t){
      .vtable = &tacker_vtable,
      .data = tracker,
  };
}

void zu_destroy_tracker(zu_tracker_t *tracker) {
  while (tracker->prev != nullptr) {
    zu_tracker_allocation_t *allocation = tracker->prev;
    tracker->prev = allocation->prev;
    deallocate(tracker->backing_allocator, allocation);
  }
  deallocate(tracker->backing_allocator, tracker);
}

void *zu_new_vec_items(zu_allocator_t allocator, size_t item_size, vec_t *vec,
                       size_t items_length, void *items) {
  size_t items_size = item_size * items_length;
  if (items_length > 0)
    vec->buffer = allocate_buffer(allocator, items_size);
  else
    vec->buffer = nullptr;
  vec->capacity = items_length;
  vec->item_size = items_size;
  vec->length = items_length;
  vec->allocator = allocator;
  return memcpy(vec->buffer, items, items_size);
}

static inline size_t vec_next_capacity(size_t capacity, size_t desired_length) {
  size_t next_capacity = capacity > 0 ? capacity : 1;
  while (next_capacity < desired_length)
    next_capacity *= 2;
  return next_capacity;
}

static inline bool vec_full(vec_t *vec) { return vec->length >= vec->capacity; }

static inline void *vec_grow(vec_t *vec) {
  vec->capacity = vec_next_capacity(vec->capacity, vec->length + 1);
  return vec->buffer = reallocate_buffer(vec->allocator, vec->buffer,
                                         vec->capacity * vec->item_size);
}

void zu_pre_append(void **buffer, vec_t *vec) {
  if (*buffer != vec->buffer)
    panic("vector paired with incorrect buffer");
  if (vec_full(vec))
    *buffer = vec_grow(vec);
  vec->length++;
}

zu_string_t zu_substring_start_length(string_t s, size_t start, size_t length) {
  return (string_t){.characters = s.characters + start, .length = length};
}

zu_string_t zu_to_string(char *cstr) {
  return (string_t){.characters = cstr, .length = strlen(cstr)};
}

bool zu_equals_string(string_t a, string_t b) {
  return strncmp(a.characters, b.characters, min(len(a), len(b))) == 0;
}

#define fvn_offset_basis 0xcbf29ce484222325
#define fvn_prime 0x100000001b3

uint64_t zu_hash(string_t s) {
  uint64_t result = fvn_offset_basis;
  for (size_t i = 0; i < len(s); i++) {
    result ^= s.characters[i];
    result *= fvn_prime;
  }
  return result;
}
