#ifndef fy_included
#define fy_included

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

static_assert(sizeof(char) == 1, "foundry expects a C `char` to be 1 byte");

/**
 * Prints to `stderr` before exiting the program with a failure status.
 */
void fy_panic(char *fmt, ...);

/**
 * V-table of procedures required to implement the `fy_allocator_t` interface.
 */
typedef struct {
  void *(*reallocate_impl)(void *data, void *ptr, size_t size);
  void *(*allocate_impl)(void *data, size_t size);
  void (*deallocate_impl)(void *data, void *ptr);
} fy_allocator_vtable_t;

/**
 * Allocator interface. Allows procedures `fy_allocate`, `fy_reallocate`, and
 * `fy_deallocate`.
 */
typedef struct {
  fy_allocator_vtable_t *vtable;
  void *data;
} fy_allocator_t;

/**
 * Allocates memory for `T` using `allocator`. Optionally takes in a modifier to
 * the size of `T`, for example `*3` to allocate an array of 3 `T` items, or
 * `+sizeof(U)` to allocate `U` after `T`.
 */
#define fy_allocate(allocator, T, ...)                                         \
  ((T *)fy_allocate_buffer((allocator), sizeof(T) __VA_ARGS__))

/**
 * Allocates memory of size `size` using `allocator`.
 */
static inline void *fy_allocate_buffer(fy_allocator_t allocator, size_t size) {
  return allocator.vtable->allocate_impl(allocator.data, size);
}

/**
 * Re-allocates pointer `ptr`, using `allocator`, to a new memory address, with
 * the size of `T`. also takes the same optional parameter as `fy_allocate`. see
 * `zu_allocate` for more information.
 */
#define fy_reallocate(allocator, ptr, T, ...)                                  \
  ((T *)fy_reallocate_buffer((allocator), (ptr), sizeof(T) __VA_ARGS__))

/**
 * Re-allocates pointer `ptr`, using `allocator`, to a new memory address, with
 * the size `size`.
 */
static inline void *fy_reallocate_buffer(fy_allocator_t allocator, void *ptr,
                                         size_t size) {
  return allocator.vtable->reallocate_impl(allocator.data, ptr, size);
}

/**
 * De-allocates pointer `ptr` using `allocator`.
 */
static inline void fy_deallocate(fy_allocator_t allocator, void *ptr) {
  allocator.vtable->deallocate_impl(allocator.data, ptr);
}

/**
 * Implementation of `fy_allocator` that directly calls `malloc`, `realloc`, and
 * `free`. Panics if `malloc` or `realloc` return `nullptr`.
 */
extern fy_allocator_t fy_heap;

/**
 * `n` kibibytes in bytes
 */
static inline size_t fy_kib(size_t n) { return n * 1024; }

/**
 * `n` mebibytes in bytes
 */
static inline size_t fy_mib(size_t n) { return fy_kib(n) * 1024; }

/**
 * `n` gibibytes in bytes
 */
static inline size_t fy_gib(size_t n) { return fy_mib(n) * 1024; }

/**
 * Allocate a new arena allocator with the parent allocator `allocator`.
 * Optionally takes the page size that the arena allocator should work with.
 */
#define fy_new_arena(allocator, ...)                                           \
  fy_new_arena_##__VA_OPT__(page_size)((allocator)__VA_OPT__(, (__VA_ARGS__)))

/**
 * Internal procedure
 */
#define fy_new_arena_(allocator) fy_new_arena_page_size((allocator), zu_kib(4))

/**
 * A page allocated by an arena allocator
 */
typedef struct fy_page {
  struct fy_page *prev;
  char buffer[];
} fy_page_t;

/**
 * An arena allocator. Works by allocating a page of `page_size`, and bump
 * allocating from it. When the page runs out of space, or more data is
 * requested than is free in the current page, a new page is allocated.
 */
typedef struct {
  fy_allocator_t page_allocator;
  fy_allocator_t allocator;
  fy_page_t *page;
  size_t page_size;
  size_t used;
} fy_arena_t;

/**
 * Internal procedure.
 */
fy_arena_t *fy_new_arena_page_size(fy_allocator_t allocator, size_t page_size);

/**
 * Internal procedure.
 */
void fy_destroy_arena(fy_arena_t *arena);

/**
 * De-allocates foundry struct `o`.
 */
#define fy_destroy(o)                                                          \
  _Generic((o),                                                                \
      fy_arena_t *: fy_destroy_arena,                                          \
      fy_tracker_t *: fy_destroy_tracker,                                      \
      fy_vec_t *: fy_destroy_vec)((o))

typedef struct {
  void *buffer;
  size_t size;
  size_t used;
} fy_block_t;

/**
 * Construct a fixed-block backed allocator.
 */
fy_block_t fy_make_block(void *buffer, size_t size);

/**
 * Internal procedure.
 */
fy_allocator_t fy_to_allocator_arena(fy_arena_t *arena);

/**
 * Internal procedure.
 */
fy_allocator_t fy_to_allocator_block(fy_block_t *block);

/**
 * Create an `fy_allocator_t` interface from any standard library allocator.
 */
#define fy_to_allocator(o)                                                     \
  _Generic((o),                                                                \
      fy_arena_t *: fy_to_allocator_arena,                                     \
      fy_block_t *: fy_to_allocator_block,                                     \
      fy_tracker_t *: fy_to_allocator_tracker)((o))

/**
 * Internal allocation inside a tracker allocator.
 */
typedef struct fy_tracker_allocation {
  struct fy_tracker_allocation *next;
  struct fy_tracker_allocation *prev;
  char buffer[];
} fy_tracker_allocation_t;

/**
 * A tracking allocator. Allocates directly onto it's backing allocator, but
 * prefixes all allocations with linked-list metadata to store a list of all
 * allocations. These allocations are therefore all deallocated when the tracker
 * is destroyed. Supports both true reallocation and early deallocation.
 */
typedef struct {
  fy_allocator_t backing_allocator;
  fy_tracker_allocation_t *prev;
} fy_tracker_t;

/**
 * Allocate and construct a new tracker allocator on `backing_allocator`.
 */
fy_tracker_t *fy_new_tracker(fy_allocator_t backing_allocator);

/**
 * Internal procedure.
 */
fy_allocator_t fy_to_allocator_tracker(fy_tracker_t *tracker);

/**
 * Destroys a tracker allocator, and deallocates all tracked allocations.
 */
void fy_destroy_tracker(fy_tracker_t *tracker);

/**
 * Metadata information for a vector
 */
typedef struct {
  fy_allocator_t allocator;
  size_t item_size;
  size_t capacity;
  size_t length;
  void *buffer;
} fy_vec_t;

/**
 * Allocates a new vector of type `T` using `allocator`. Optionally takes in
 * values to store in the vector by default.
 */
#define fy_new_vec(allocator, T, vec, ...)                                     \
  ((T *)fy_new_vec_manual((allocator), sizeof(T), (vec),                       \
                          sizeof((T[]){__VA_ARGS__}) / sizeof(T),              \
                          (T[]){__VA_ARGS__}))

/**
 * Internal procedure.
 */
void *fy_new_vec_manual(fy_allocator_t allocator, size_t item_size,
                        fy_vec_t *vec, size_t items_length, void *items);

/**
 * Takes the length of an object.
 */
#define fy_len(o)                                                              \
  _Generic((o), fy_vec_t: fy_len_vec, fy_string_t: fy_len_string)((o))

/**
 * Internal procedure.
 */
static inline size_t fy_len_vec(fy_vec_t vec) { return vec.length; }

/**
 * Internal procedure.
 */
void fy_pre_append(void **buffer, fy_vec_t *vec);

/**
 * Inline function that does nothing and returns the `void` type, for using in
 * `(a, b, c)` expressions to make them return void.
 */
static inline void fy_void() {}

/**
 * Append `value` into `buffer` using `vector`. Requires `buffer` to be a double
 * pointer, as it may re-assign it in the case `buffer` has to be re-allocated.
 */
#define fy_append(vector, buffer, value)                                       \
  (fy_pre_append((void **)(buffer), (vector)),                                 \
   ((*(buffer))[fy_len(*vector) - 1] = (value)), fy_void())

/**
 * A string type containing a length, and a pointer to the character data.
 */
typedef struct {
  char *characters;
  size_t length;
} fy_string_t;

/**
 * Convert a c-string literal into a compile-time generated string.
 */
#define fy_string(literal)                                                     \
  (fy_string_t){.characters = (literal), .length = sizeof(literal) - 1}

/**
 * Convert a string into a valid stdio format parameter when matched with the
 * %.*s identifier.
 */
#define fy_fmt(str) (int)str.length, str.characters

/**
 * Internal procedure.
 */
static inline size_t fy_len_string(fy_string_t string) { return string.length; }

/**
 * Internal procedure.
 */
fy_string_t fy_substring_start_length(fy_string_t string, size_t start,
                                      size_t length);
/**
 * Internal procedure.
 */
static inline fy_string_t fy_substring_start(fy_string_t string, size_t start) {
  return fy_substring_start_length(string, start, fy_len(string) - start);
}

/**
 * Takes a subsection of `s`, from `start`, for the optional parameter
 * `length` characters.
 */
#define fy_substring(s, start, ...)                                            \
  fy_substring_start##__VA_OPT__(_length)((s),                                 \
                                          (start)__VA_OPT__(, (__VA_ARGS__)))

/**
 * Converts a value into a string.
 */
fy_string_t fy_to_string(char *cstr);

/**
 * Helper macro to determinte the smalllest of two values.
 */
#define fy_min(a, b) ((a) < (b) ? (a) : (b))

/**
 * Helper macro to determine the largest of two values.
 */
#define fy_max(a, b) ((a) > (b) ? (a) : (b))

/**
 * Internal procedure.
 */
bool fy_equals_string(fy_string_t a, fy_string_t b);

/**
 * Checks if two instance of a type are the same value.
 */
#define fy_equals(o, ...)                                                      \
  _Generic((o), fy_string_t: fy_equals_string)((o)__VA_OPT__(, __VA_ARGS__))

/**
 * Convert a boolean to a c-string.
 */
static inline char *fy_to_chars(bool b) { return b ? "true" : "false"; }

/**
 * Hash a string into a 64-bit number using the FNV-1a algorithm. This algorithm
 * is non-cryptographic.
 */
uint64_t fy_hash(fy_string_t s);

/**
 * Reserves `size` elements in a `fy_vec_t`. Increases the capacity of the
 * vector up to `size`. If the capacity is already greater, it does nothing.
 */
void fy_reserve(fy_vec_t *vec, size_t size);

/**
 * Internal procedure.
 */
static inline void zu_destroy_vec(fy_vec_t *vec) {
  fy_deallocate(vec->allocator, vec->buffer);
}

/**
 * Internal dictionary bucket.
 */
typedef struct fy_bucket fy_bucket_t;

/**
 * A string key to generic value map. Due to the nature of foundry strings, any
 * type *can* be used as a key if reinterpreted into a buffer-style string.
 */
typedef struct {
  fy_allocator_t allocator;
  fy_bucket_t **buckets;
  size_t value_size;
  size_t capacity;
  size_t size;
  void *buffer;
} fy_dict_t;

/**
 * Internal helper "generic" type for hashmap pairs in the constructor.
 */
#define fy_pair(T)                                                             \
  struct {                                                                     \
    fy_string_t key;                                                           \
    T value;                                                                   \
  }

/**
 * Constructs a new `fy_dict_t`
 */
#define fy_new_dict(allocator, T, dict, ...)                                   \
  fy_new_dict_manual((allocator), sizeof(T), (dict),                           \
                     sizeof((fy_pair(T)[]){__VA_ARGS__}) / sizeof(fy_pair(T)), \
                     sizeof(fy_pair(T)), offsetof(fy_pair(T), value),          \
                     (fy_pair(T)[]){__VA_ARGS__})

/**
 * Internal proceudre.
 */
void *fy_new_dict_manual(fy_allocator_t allocator, size_t value_size,
                         fy_dict_t *dict, size_t pairs_length, size_t pair_size,
                         size_t pair_value_offset, void *pairs);

/**
 * Internal procedure.
 */
void fy_pre_put(fy_dict_t *dict, void **buffer, fy_string_t key);

/**
 * Internal procedure.
 */
void fy_pre_get(fy_dict_t *dict, void **buffer, fy_string_t key);

/**
 * Puts `value` into an `fy_dict_t` under the specified `key`.
 */
#define fy_put(dict, buffer, key, value)                                       \
  (fy_pre_put((dict), (void **)(buffer), (key)), (**(buffer)) = value,         \
   fy_void())

/**
 * Gets a value from the `fy_dict_t` `dict` if there is one present under `key`.
 * Else, it panics.
 */
#define fy_get(dict, buffer, key)                                              \
  (fy_pre_get((dict), (void **)(buffer), (key)), **(buffer))

/**
 * Checks if a given `key` is contained in a given `dict`.
 */
bool fy_contains(fy_dict_t *dict, fy_string_t key);

/**
 * Erases a given `key` from a given `dict`.
 */
bool fy_erase(fy_dict_t *dict, fy_string_t key);

/**
 * An error. Features a human-readable `message`, and a machine-readable `type`.
 */
typedef struct {
  char message[128];
  uint32_t type;
} fy_error_t;

/*
 * An error context. Stores an error and it's state for part of a
 * callstack. The presence of this type in a procedure's signature indicates it
 * can "raise" an error. You MUST `check` or `handle` the context before passing
 * it into another procedure call.
 */
typedef struct {
  fy_error_t error;
  uint8_t state;
} fy_context_t;

/**
 * Internal procedure.
 */
void fy_raise_type(fy_context_t *ctx, uint32_t type, char *fmt, ...);

/**
 * Internal procedure.
 */
void fy_raise_message(fy_context_t *ctx, char *fmt, ...);

/**
 * Internal procedure.
 */
void fy_raise_error(fy_context_t *ctx, fy_error_t *error);

/**
 * Internal procedure.
 */
void fy_raise_context(fy_context_t *new_ctx, fy_context_t *old_ctx);

/**
 * Checks if an error is present in a given context. Changes nothing about the
 * context. Should be used when the context comes from a frame above, and if an
 * error is present, you wish for it to be "propogated".
 */
bool fy_check(fy_context_t *ctx);

/**
 * Checks if an error is present in a given context. If it is, it marks if as
 * handled inside the context. This allows the context to be used again.
 */
bool fy_handle(fy_context_t *ctx);

/**
 * Sets up a context on the stack.
 */
fy_context_t fy_make_context();

/**
 * Allows you to construct and raise an error into the given error context.
 */
#define fy_raise(a0, a1, ...)                                                  \
  _Generic((a1),                                                               \
      uint32_t: fy_raise_type,                                                 \
      char *: fy_raise_message,                                                \
      fy_error_t *: fy_raise_error,                                            \
      fy_context_t *: fy_raise_context)((a0), (a1)__VA_OPT__(, __VA_ARGS__))

/**
 * Indicates an error of unknown type. If a call to `fy_raise` is made without
 * passing in a type, this value is used.
 */
#define fy_error_unknown UINT32_MAX

#ifndef fy_force_prefix

#define panic fy_panic
typedef fy_allocator_vtable_t allocator_vtable_t;
typedef fy_allocator_t allocator_t;
#define allocate fy_allocate
#define allocate_buffer fy_allocate_buffer
#define reallocate fy_reallocate
#define reallocate_buffer fy_reallocate_buffer
#define deallocate fy_deallocate
#define heap fy_heap
#define kib fy_kib
#define mib fy_mib
#define gib fy_gib
typedef fy_arena_t arena_t;
#define new_arena fy_new_arena
#define destroy fy_destroy
typedef fy_block_t block_t;
#define to_allocator fy_to_allocator
#define make_block fy_make_block
typedef fy_tracker_t tracker_t;
#define new_tracker fy_new_tracker
#define new_vec fy_new_vec
#define len fy_len
typedef fy_vec_t vec_t;
#define append fy_append
#define string fy_string
typedef fy_string_t string_t;
#define substring fy_substring
#define fmt fy_fmt
#define to_string fy_to_string
#define min fy_min
#define max fy_max
#define equals fy_equals
#define to_chars fy_to_chars
#define hash fy_hash
#define put fy_put
#define get fy_get
#define new_dict fy_new_dict
typedef fy_dict_t dict_t;
#define contains fy_contains
#define erase fy_erase
typedef fy_error_t error_t;
typedef fy_context_t context_t;
#define check fy_check
#define handle fy_handle
#define raise fy_raise
#define make_context fy_make_context
#define error_unknown fy_error_unknown

#endif

#endif
