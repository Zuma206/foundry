#ifndef zu_included
#define zu_included

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

static_assert(sizeof(char) == 1, "libzuma requires a C `char` to be 1 byte");

/**
 * Prints to `stderr` before exiting the program with a failure status.
 */
void zu_panic(char *fmt, ...);

/**
 * VTable of procedures required to implement the `zu_allocator_t` interface.
 */
typedef struct {
  void *(*reallocate_impl)(void *data, void *ptr, size_t size);
  void *(*allocate_impl)(void *data, size_t size);
  void (*deallocate_impl)(void *data, void *ptr);
} zu_allocator_vtable_t;

/**
 * Allocator interface. Allows procedures `zu_allocate`, `zu_reallocate`, and
 * `zu_deallocate`.
 */
typedef struct {
  zu_allocator_vtable_t *vtable;
  void *data;
} zu_allocator_t;

/**
 * Allocates memory for `T` using `allocator`. Optionally takes in a modifier to
 * the size of `T`, for example `*3` to allocate an array of 3 `T` items, or
 * `+sizeof(U)` to allocate `U` after `T`.
 */
#define zu_allocate(allocator, T, ...)                                         \
  ((T *)zu_allocate_buffer((allocator), sizeof(T) __VA_ARGS__))

/**
 * Allocates memory of size `size` using `allocator`.
 */
static inline void *zu_allocate_buffer(zu_allocator_t allocator, size_t size) {
  return allocator.vtable->allocate_impl(allocator.data, size);
}

/**
 * Re-allocates pointer `ptr`, using `allocator`, to a new memory address, with
 * the size of `T`. also takes the same optional parameter as `zu_allocate`. see
 * `zu_allocate` for more information.
 */
#define zu_reallocate(allocator, ptr, T, ...)                                  \
  ((T *)zu_reallocate_buffer((allocator), (ptr), sizeof(T) __VA_ARGS__))

/**
 * Re-allocates pointer `ptr`, using `allocator`, to a new memory address, with
 * the size `size`.
 */
static inline void *zu_reallocate_buffer(zu_allocator_t allocator, void *ptr,
                                         size_t size) {
  return allocator.vtable->reallocate_impl(allocator.data, ptr, size);
}

/**
 * De-allocates pointer `ptr` using `allocator`.
 */
static inline void zu_deallocate(zu_allocator_t allocator, void *ptr) {
  allocator.vtable->deallocate_impl(allocator.data, ptr);
}

/**
 * Implementation of `zu_allocator` that directly calls `malloc`, `realloc`, and
 * `free`. Panics if `malloc` or `realloc` return `nullptr`.
 */
extern zu_allocator_t zu_heap;

/**
 * `n` kibibytes in bytes
 */
static inline size_t zu_kib(size_t n) { return n * 1024; }

/**
 * `n` mebibytes in bytes
 */
static inline size_t zu_mib(size_t n) { return zu_kib(n) * 1024; }

/**
 * `n` gibibytes in bytes
 */
static inline size_t zu_gib(size_t n) { return zu_mib(n) * 1024; }

/**
 * Allocate a new arena allocator with the parent allocator `allocator`.
 * Optionally takes the page size that the arena allocator should work with.
 */
#define zu_new_arena(allocator, ...)                                           \
  zu_new_arena_##__VA_OPT__(page_size)((allocator)__VA_OPT__(, (__VA_ARGS__)))

/**
 * Internal procedure
 */
#define zu_new_arena_(allocator) zu_new_arena_page_size((allocator), zu_kib(4))

/**
 * A page allocated by an arena allocator
 */
typedef struct zu_page {
  struct zu_page *prev;
  char buffer[];
} zu_page_t;

/**
 * An arena allocator. Works by allocating a page of `page_size`, and bump
 * allocating from it. When the page runs out of space, or more data is
 * requested than is free in the current page, a new page is allocated.
 */
typedef struct {
  zu_allocator_t page_allocator;
  zu_allocator_t allocator;
  zu_page_t *page;
  size_t page_size;
  size_t used;
} zu_arena_t;

/**
 * Internal procedure.
 */
zu_arena_t *zu_new_arena_page_size(zu_allocator_t allocator, size_t page_size);

/**
 * Internal procedure.
 */
void zu_destroy_arena(zu_arena_t *arena);

/**
 * De-allocates zu struct `o`.
 */
#define zu_destroy(o)                                                          \
  _Generic((o),                                                                \
      zu_arena_t *: zu_destroy_arena,                                          \
      zu_tracker_t *: zu_destroy_tracker,                                      \
      zu_vec_t *: zu_destroy_vec)((o))

typedef struct {
  void *buffer;
  size_t size;
  size_t used;
} zu_block_t;

/**
 * Construct a fixed-block backed allocator.
 */
zu_block_t zu_make_block(void *buffer, size_t size);

/**
 * Internal procedure.
 */
zu_allocator_t zu_to_allocator_arena(zu_arena_t *arena);

/**
 * Internal procedure.
 */
zu_allocator_t zu_to_allocator_block(zu_block_t *block);

/**
 * Create a `zu_allocator_t` interface from any standard library allocator.
 */
#define zu_to_allocator(o)                                                     \
  _Generic((o),                                                                \
      zu_arena_t *: zu_to_allocator_arena,                                     \
      zu_block_t *: zu_to_allocator_block,                                     \
      zu_tracker_t *: zu_to_allocator_tracker)((o))

/**
 * Internal allocation inside a tracker allocator.
 */
typedef struct zu_tracker_allocation {
  struct zu_tracker_allocation *next;
  struct zu_tracker_allocation *prev;
  char buffer[];
} zu_tracker_allocation_t;

/**
 * A tracking allocator. Allocates directly onto it's backing allocator, but
 * prefixes all allocations with linked-list metadata to store a list of all
 * allocations. These allocations are therefore all deallocated when the tracker
 * is destroyed. Supports both true reallocation and early deallocation.
 */
typedef struct {
  zu_allocator_t backing_allocator;
  zu_tracker_allocation_t *prev;
} zu_tracker_t;

/**
 * Allocate and construct a new tracker allocator on `backing_allocator`.
 */
zu_tracker_t *zu_new_tracker(zu_allocator_t backing_allocator);

/**
 * Internal procedure.
 */
zu_allocator_t zu_to_allocator_tracker(zu_tracker_t *tracker);

/**
 * Destroys a tracker allocator, and deallocates all tracked allocations.
 */
void zu_destroy_tracker(zu_tracker_t *tracker);

/**
 * Metadata information for a vector
 */
typedef struct {
  zu_allocator_t allocator;
  size_t item_size;
  size_t capacity;
  size_t length;
  void *buffer;
} zu_vec_t;

/**
 * Allocates a new vector of type `T` using `allocator`. Optionally takes in
 * values to store in the vector by default.
 */
#define zu_new_vec(allocator, T, vec, ...)                                     \
  ((T *)zu_new_vec_manual((allocator), sizeof(T), (vec),                       \
                          sizeof((T[]){__VA_ARGS__}) / sizeof(T),              \
                          (T[]){__VA_ARGS__}))

/**
 * Internal procedure.
 */
void *zu_new_vec_manual(zu_allocator_t allocator, size_t item_size,
                        zu_vec_t *vec, size_t items_length, void *items);

/**
 * Takes the length of an object.
 */
#define zu_len(o)                                                              \
  _Generic((o), zu_vec_t: zu_len_vec, zu_string_t: zu_len_string)((o))

/**
 * Internal procedure.
 */
static inline size_t zu_len_vec(zu_vec_t vec) { return vec.length; }

/**
 * Interna procedure.
 */
void zu_pre_append(void **buffer, zu_vec_t *vec);

/**
 * Inline function that does nothinf and returns the `void` type, for using in
 * `(a, b, c)` expressions to make them return void.
 */
static inline void zu_void() {}

/**
 * Append `value` into `buffer` using `vector`. Requires `buffer` to be a double
 * pointer, as it may re-assign it in the case `buffer` has to be re-allocated.
 */
#define zu_append(vector, buffer, value)                                       \
  (zu_pre_append((void **)(buffer), (vector)),                                 \
   ((*(buffer))[zu_len(*vector) - 1] = (value)), zu_void())

/**
 * A string type containing a length, and a pointer to the character data.
 */
typedef struct {
  char *characters;
  size_t length;
} zu_string_t;

/**
 * Convert a c-string literal into a compile-time generated string.
 */
#define zu_string(literal)                                                     \
  (zu_string_t){.characters = (literal), .length = sizeof(literal) - 1}

/**
 * Convert a string into
 */
#define zu_fmt(str) (int)str.length, str.characters

/**
 * Internal procedure.
 */
static inline size_t zu_len_string(zu_string_t string) { return string.length; }

/**
 * Internal procedure.
 */
zu_string_t zu_substring_start_length(zu_string_t string, size_t start,
                                      size_t length);
/**
 * Internal procedure.
 */
static inline zu_string_t zu_substring_start(zu_string_t string, size_t start) {
  return zu_substring_start_length(string, start, zu_len(string) - start);
}

/**
 * Takes a subsection of `string`, from `start`, for the optional parameter
 * `length` characters.
 */
#define zu_substring(string, start, ...)                                       \
  zu_substring_start##__VA_OPT__(_length)((string),                            \
                                          (start)__VA_OPT__(, (__VA_ARGS__)))

/**
 * Converts a value into a string.
 */
zu_string_t zu_to_string(char *cstr);

/**
 * Helper macro to determinte the smalllest of two values.
 */
#define zu_min(a, b) ((a) < (b) ? (a) : (b))

/**
 * Helper macro to determine the largest of two values.
 */
#define zu_max(a, b) ((a) > (b) ? (a) : (b))

/**
 * Internal procedure.
 */
bool zu_equals_string(zu_string_t a, zu_string_t b);

/**
 * Checks if two instance of a type are the same value.
 */
#define zu_equals(o, ...)                                                      \
  _Generic((o), string_t: zu_equals_string)((o)__VA_OPT__(, __VA_ARGS__))

/**
 * Convert a boolean to a c-string.
 */
static inline char *zu_to_cstr(bool b) { return b ? "true" : "false"; }

/**
 * Internal procedure
 */
uint64_t zu_hash_buffer(uint8_t *buffer, size_t length, bool stop_at_null);

/**
 * Internal procedure
 */
static inline uint64_t zu_hash_string(zu_string_t s) {
  return zu_hash_buffer((uint8_t *)s.characters, zu_len(s), false);
}

/**
 * Internal procedure
 */
static inline uint64_t zu_hash_cstr(char *cstr) {
  return zu_hash_buffer((uint8_t *)cstr, SIZE_MAX, true);
}

/**
 * Hash a string into a 64-bit number using the FNV-1a algorithm. This algorithm
 * is non-cryptographic.
 */
uint64_t zu_hash(zu_string_t s);

void zu_resize(zu_vec_t *vec, size_t size);

static inline void zu_destroy_vec(zu_vec_t *vec) {
  zu_deallocate(vec->allocator, vec->buffer);
}

#ifndef zu_force_prefix

#define panic zu_panic
typedef zu_allocator_vtable_t allocator_vtable_t;
typedef zu_allocator_t allocator_t;
#define allocate zu_allocate
#define allocate_buffer zu_allocate_buffer
#define reallocate zu_reallocate
#define reallocate_buffer zu_reallocate_buffer
#define deallocate zu_deallocate
#define heap zu_heap
#define kib zu_kib
#define mib zu_mib
#define gib zu_gib
typedef zu_arena_t arena_t;
#define new_arena zu_new_arena
#define destroy zu_destroy
typedef zu_block_t block_t;
#define to_allocator zu_to_allocator
#define make_block zu_make_block
typedef zu_tracker_t tracker_t;
#define new_tracker zu_new_tracker
#define new_vec zu_new_vec
#define len zu_len
typedef zu_vec_t vec_t;
#define append zu_append
#define string zu_string
typedef zu_string_t string_t;
#define substring zu_substring
#define fmt zu_fmt
#define to_string zu_to_string
#define min zu_min
#define max zu_max
#define equals zu_equals
#define to_cstr zu_to_cstr
#define hash zu_hash

#endif

#endif
