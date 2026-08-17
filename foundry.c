#define fy_force_prefix
#include "foundry.h"
#include <memory.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void fy_panic(char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  exit(1);
}

static inline void out_of_memory() {
  fy_panic("fatal allocation error: out of memory\n");
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

static fy_allocator_vtable_t heap_vtable = {
    .deallocate_impl = heap_deallocate,
    .reallocate_impl = heap_reallocate,
    .allocate_impl = heap_allocate,
};

fy_allocator_t zu_heap = {
    .vtable = &heap_vtable,
    .data = nullptr,
};

static void *arena_allocate(void *data, size_t size) {
  fy_arena_t *arena = data;
  if (size > arena->page_size)
    fy_panic("fatal allocation error: %ld bytes requested from arena with %ld "
             "page size\n",
             size, arena->page_size);
  if (arena->page == nullptr || size > arena->page_size - arena->used) {
    fy_page_t *page = fy_allocate(arena->page_allocator, fy_page_t);
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

static fy_allocator_vtable_t arena_vtable = {
    .deallocate_impl = arena_deallocate,
    .reallocate_impl = arena_reallocate,
    .allocate_impl = arena_allocate,
};

fy_arena_t *fy_new_arena_page_size(fy_allocator_t allocator, size_t page_size) {
  fy_arena_t *arena = fy_allocate(allocator, fy_arena_t);
  arena->page_allocator = allocator;
  arena->page_size = page_size;
  arena->page = nullptr;
  arena->used = 0;
  arena->allocator = (fy_allocator_t){
      .vtable = &arena_vtable,
      .data = arena,
  };
  return arena;
}

void zu_destroy_arena(fy_arena_t *arena) {
  while (arena->page != nullptr) {
    fy_page_t *page = arena->page;
    arena->page = page->prev;
    fy_deallocate(arena->page_allocator, page);
  }
  fy_deallocate(arena->page_allocator, arena);
}

static void *block_allocate(void *data, size_t size) {
  fy_block_t *block = data;
  if (block->size - block->used < size)
    fy_panic("fatal allocation error: block allocator out of space\n");
  void *ptr = block->buffer + block->used;
  block->used += size;
  return ptr;
}

static void block_deallocate(void *, void *) {}

static void *block_reallocate(void *data, void *, size_t size) {
  return block_allocate(data, size);
}

static fy_allocator_vtable_t block_vtable = {
    .reallocate_impl = block_reallocate,
    .deallocate_impl = block_deallocate,
    .allocate_impl = block_allocate,
};

fy_block_t fy_make_block(void *buffer, size_t size) {
  return (fy_block_t){
      .buffer = buffer,
      .size = size,
      .used = 0,
  };
}

fy_allocator_t fy_to_allocator_arena(fy_arena_t *arena) {
  return (fy_allocator_t){
      .vtable = &arena_vtable,
      .data = arena,
  };
}

fy_allocator_t fy_to_allocator_block(fy_block_t *block) {
  return (fy_allocator_t){
      .vtable = &block_vtable,
      .data = block,
  };
}

static void *tracker_allocate(void *data, size_t size) {
  fy_tracker_t *tracker = data;
  fy_tracker_allocation_t *allocation =
      fy_allocate(tracker->backing_allocator, fy_tracker_allocation_t, +size);
  allocation->prev = tracker->prev;
  allocation->next = nullptr;
  tracker->prev = allocation;
  return allocation->buffer;
}

static inline fy_tracker_allocation_t *get_tracker_allocation(void *ptr) {
  return ptr - offsetof(fy_tracker_allocation_t, buffer);
}

static void *tracker_reallocate(void *data, void *ptr, size_t size) {
  fy_tracker_t *tracker = data;
  fy_tracker_allocation_t *allocation = get_tracker_allocation(ptr);
  allocation = fy_reallocate(tracker->backing_allocator, allocation,
                             fy_tracker_allocation_t, +size);
  if (allocation->prev != nullptr)
    allocation->prev->next = allocation;
  if (allocation->next != nullptr)
    allocation->next->prev = allocation;
  else
    tracker->prev = allocation;
  return allocation->buffer;
}

static void tracker_deallocate(void *data, void *ptr) {
  if (ptr == nullptr)
    return;
  fy_tracker_t *tracker = data;
  fy_tracker_allocation_t *allocation = get_tracker_allocation(ptr);
  if (allocation->prev != nullptr)
    allocation->prev->next = allocation->next;
  if (allocation->next != nullptr)
    allocation->next->prev = allocation->prev;
  else
    tracker->prev = allocation->prev;
  fy_deallocate(tracker->backing_allocator, allocation);
}

static fy_allocator_vtable_t tacker_vtable = {
    .deallocate_impl = tracker_deallocate,
    .reallocate_impl = tracker_reallocate,
    .allocate_impl = tracker_allocate,
};

fy_tracker_t *fy_new_tracker(fy_allocator_t backing_allocator) {
  fy_tracker_t *tracker = fy_allocate(backing_allocator, fy_tracker_t);
  tracker->backing_allocator = backing_allocator;
  tracker->prev = nullptr;
  return tracker;
}

fy_allocator_t fy_to_allocator_tracker(fy_tracker_t *tracker) {
  return (fy_allocator_t){
      .vtable = &tacker_vtable,
      .data = tracker,
  };
}

void fy_destroy_tracker(fy_tracker_t *tracker) {
  while (tracker->prev != nullptr) {
    fy_tracker_allocation_t *allocation = tracker->prev;
    tracker->prev = allocation->prev;
    fy_deallocate(tracker->backing_allocator, allocation);
  }
  fy_deallocate(tracker->backing_allocator, tracker);
}

void *fy_new_vec_manual(fy_allocator_t allocator, size_t item_size,
                        fy_vec_t *vec, size_t items_length, void *items) {
  size_t items_size = item_size * items_length;
  if (items_length > 0)
    vec->buffer = fy_allocate_buffer(allocator, items_size);
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

void fy_reserve(fy_vec_t *vec, size_t size) {
  if (vec->capacity >= size)
    return;
  vec->capacity = size;
  vec->buffer = fy_reallocate_buffer(vec->allocator, vec->buffer,
                                     vec->capacity * vec->item_size);
}

static inline bool vec_full(fy_vec_t *vec) {
  return vec->length >= vec->capacity;
}

static inline void *vec_grow(fy_vec_t *vec) {
  size_t next_capacity = vec_next_capacity(vec->capacity, vec->length + 1);
  fy_reserve(vec, next_capacity);
  return vec->buffer;
}

void fy_pre_append(void **buffer, fy_vec_t *vec) {
  if (*buffer != vec->buffer)
    fy_panic("vector paired with incorrect buffer");
  if (vec_full(vec))
    *buffer = vec_grow(vec);
  vec->length++;
}

fy_string_t fy_substring_start_length(fy_string_t s, size_t start,
                                      size_t length) {
  return (fy_string_t){.characters = s.characters + start, .length = length};
}

fy_string_t fy_to_string(char *cstr) {
  return (fy_string_t){.characters = cstr, .length = strlen(cstr)};
}

bool fy_equals_string(fy_string_t a, fy_string_t b) {
  return strncmp(a.characters, b.characters, fy_min(fy_len(a), fy_len(b))) == 0;
}

#define fvn_offset_basis 0xcbf29ce484222325
#define fvn_prime 0x100000001b3

uint64_t fy_hash(fy_string_t s) {
  uint64_t result = fvn_offset_basis;
  for (size_t i = 0; i < fy_len(s); i++) {
    result ^= s.characters[i];
    result *= fvn_prime;
  }
  return result;
}

void *fy_new_dict_manual(fy_allocator_t allocator, size_t value_size,
                         fy_dict_t *dict, size_t pairs_length, size_t pair_size,
                         size_t pair_value_offset, void *pairs) {
  dict->value_size = value_size;
  dict->allocator = allocator;
  dict->buckets = nullptr;
  dict->capacity = 0;
  dict->size = 0;
  dict->buffer = nullptr;
  for (size_t i = 0; i < pairs_length; i++) {
    void *pair = pairs + pair_size * i;
    fy_string_t *key = pair;
    void *value = pair + pair_value_offset;
    void *buffer = dict->buffer;
    fy_pre_put(dict, &buffer, *key);
    memcpy(buffer, value, value_size);
  }
  return dict->buffer;
}

struct fy_bucket {
  struct fy_bucket *next;
  fy_string_t key;
  char buffer[];
};

#define dict_load_factor 0.6

static inline bool dict_over_threshold(fy_dict_t *dict) {
  return dict->size >= (dict->capacity * dict_load_factor);
}

static inline void dict_grow(fy_dict_t *dict) {
  fy_bucket_t **old_buckets = dict->buckets;
  size_t old_capacity = dict->capacity;
  dict->capacity = dict->capacity > 0 ? dict->capacity * 2 : 1;
  dict->buckets = fy_allocate(dict->allocator, fy_bucket_t *, *dict->capacity);
  memset(dict->buckets, 0, sizeof(fy_bucket_t *) * dict->capacity);
  for (size_t old_i = 0; old_i < old_capacity; old_i++) {
    fy_bucket_t *bucket = old_buckets[old_i];
    while (bucket != nullptr) {
      fy_bucket_t *next = bucket->next;
      size_t new_i = fy_hash(bucket->key) % dict->capacity;
      bucket->next = dict->buckets[new_i];
      dict->buckets[new_i] = bucket;
      bucket = next;
    }
  }
  fy_deallocate(dict->allocator, old_buckets);
}

void fy_pre_put(fy_dict_t *dict, void **buffer, fy_string_t key) {
  if (dict_over_threshold(dict))
    dict_grow(dict);
  fy_bucket_t **bucket_ptr = &dict->buckets[fy_hash(key) % dict->capacity];
  while (*bucket_ptr != nullptr && !fy_equals((*bucket_ptr)->key, key))
    bucket_ptr = &(*bucket_ptr)->next;
  if (*bucket_ptr == nullptr) {
    *bucket_ptr = fy_allocate(dict->allocator, fy_bucket_t, +dict->value_size);
    fy_bucket_t *bucket = *bucket_ptr;
    bucket->key.length = key.length;
    bucket->key.characters = fy_allocate(dict->allocator, char, *key.length);
    memcpy(bucket->key.characters, key.characters, key.length);
    bucket->next = nullptr;
    dict->size++;
  }
  dict->buffer = *buffer = (*bucket_ptr)->buffer;
}

static fy_bucket_t **dict_get_bucket(fy_dict_t *dict, fy_string_t key) {
  for (fy_bucket_t **bucket = &dict->buckets[fy_hash(key) % dict->capacity];
       *bucket != nullptr; bucket = &(*bucket)->next)
    if (fy_equals((*bucket)->key, key))
      return bucket;
  return nullptr;
}

void fy_pre_get(fy_dict_t *dict, void **buffer, fy_string_t key) {
  fy_bucket_t **bucket = dict_get_bucket(dict, key);
  if (bucket == nullptr)
    fy_panic("key `%.*s` could not be found in dict", fy_fmt(key));
  *buffer = (*bucket)->buffer;
}

bool fy_contains(fy_dict_t *dict, fy_string_t key) {
  return dict_get_bucket(dict, key) != nullptr;
}

bool fy_erase(fy_dict_t *dict, fy_string_t key) {
  fy_bucket_t **bucket_ptr = dict_get_bucket(dict, key);
  if (bucket_ptr == nullptr)
    return false;
  fy_bucket_t *bucket = *bucket_ptr;
  *bucket_ptr = bucket->next;
  fy_deallocate(dict->allocator, bucket);
  return true;
}

#define context_empty 0
#define context_unhandled 1
#define context_handled 2

static void context_set_state(fy_context_t *ctx) {
  if (ctx->state == context_unhandled)
    fy_panic(
        "fatal context error: attempted to raise an error in a context which "
        "already contained an unhandled error (type = %d, message = %s)",
        ctx->error.type, ctx->error.message);
  ctx->state = context_unhandled;
}

static void raise_args(fy_context_t *ctx, uint32_t type, char *fmt,
                       va_list args) {
  context_set_state(ctx);
  ctx->error.type = type;
  vsnprintf(ctx->error.message, sizeof(ctx->error.message), fmt, args);
}

void fy_raise_type(fy_context_t *ctx, uint32_t type, char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  raise_args(ctx, type, fmt, args);
  va_end(args);
}

void fy_raise_message(fy_context_t *ctx, char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  raise_args(ctx, fy_error_unknown, fmt, args);
  va_end(args);
}

void fy_raise_error(fy_context_t *ctx, fy_error_t *error) {
  context_set_state(ctx);
  ctx->error = *error;
}

void fy_raise_context(fy_context_t *new_ctx, fy_context_t *old_ctx) {
  fy_raise_error(new_ctx, &old_ctx->error);
}

bool fy_check(fy_context_t *ctx) { return ctx->state != context_empty; }

bool fy_handle(fy_context_t *ctx) {
  if (ctx->state == context_unhandled) {
    ctx->state = context_handled;
    return true;
  }
  return false;
}

fy_context_t fy_make_context() {
  return (fy_context_t){
      .state = context_empty,
      .error =
          {
              .type = fy_error_unknown,
              .message = "",
          },
  };
}
