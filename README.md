# Foundry

<img alt="Foundry Logo" src="foundry.svg" height="100" />

## About

A foundational memory management, data-structure, and error handling library for modern C.

_[License: GPLv3](LICENSE)_

## Features

- Zig-style memory allocators with `arena_t`, `allocator_t`, etc.
- Dynamic arrays with `vec_t`.
- Hashmaps with `dict_t`.
- Generics-style typesafety.
- Go-style error reporting, encoded into function signatures with `error_t` & `context_t`.
- Various utility macros (`min`, `max`, `KiB`, `MiB`, etc).
- Optional namespacing / forced prefixes.
- Fat-pointer strings with upfront lengths.

## Examples

### Dictionaries

```C
dict_t scores_d;
int *scores = new_dict(heap, int, &scores_d,
  {string("Zuma"), 400},
  {string("Player_2"), 600}
);

put(&scores_d, &scores, string("Player_26"), 300);
erase(&scores_d, string("Player_2"));
if (has(&scores_d, string("Zuma"))) {
  int score = get(&scores_d, &scores, string("Zuma"));
  printf("Your score is: %d\n", score);
}
// Fully typesafe, trying to put anything other than integers into this dictionary causes compile time errors.
```

### Memory Allocators

```C
// create a new arena backed by the heap
arena_t *arena = new_arena(heap);
// cast it to an allocator
allocator_t allocator = to_allocator(arena);

// Only one allocation from the heap is made. This is equal to one page size and is used for all 3 allocators. The page size can be optionally specified in `new_arena`.
int *x = allocate(allocator, int);
char *y = allocate(allocator, char, *10);
float *z = allocate(allocator, float);

// x, y, and z are all cleaned up!
destroy(arena);
```

### Strings

```c
// Length is stored with name, and calculated at compile time.
string_t name = string("Zuma");
// Length can also be calculated dynamically at runtime.
char *other_name_cstr = "George";
string_t other_name = to_string(other_name_cstr);

// No O(n) string measuring needed!
printf("%d %d\n", len(name), len(other_name));
```

### Vectors

```c
vec_t users_d;
string_t *users = new_vec(heap, string_t, &users_d,
  string("User 0"),
  string("User 1"),
  string("User 2"),
);

append(&users_d, &users, string("User 3"));

for (size_t i = 0; i < len(users_d); i++)
  printf("Hello, %.*s\n", fmt(users[i]));
```

### Error Handling

```c
// This function accepting a context indicates it can produce an error that must be handled by a caller.
int safer_divide(int a, int b, context_t *ctx) {
  if (b == 0)
    return (raise(ctx, "can't divide %d by %d\n", a, b), 0);
  return a / b;
}

int main() {
  context_t ctx = make_context();
  int result = safer_divide(10, 20, &ctx);
  if (handle(ctx))
    panic("first division failed: %s", ctx->error.message);
  result = safer_divide(10, 0, &ctx);
  // This if branch will execute
  if (handle(ctx))
    panic("second division failed: %s", ctx->error.message);
}
```

### Namespacing / Prefixing

```c
#define fy_force_prefix
// by defining this macro, all non-prefixed symbols are removed from the global namespace.
#include <foundry.h>
// by default, all symbols are available with or without the `fy` prefix.
```
