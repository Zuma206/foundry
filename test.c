#define fy_force_prefix
#include "foundry.h"
#include <alloca.h>
#include <stdio.h>
#include <string.h>

static int safe_divide(int a, int b, fy_context_t *ctx) {
  if (b == 0)
    return (fy_raise(ctx, "Not safe to divide %d by %d", a, b), 0);
  return a / b;
}

int main() {
  { // Heap tests
    int *count = fy_allocate(fy_heap, int);
    *count = 0;
    (*count)++;
    printf("1 = %d\n", *count);
    fy_deallocate(fy_heap, count);
  }

  { // Arena tests
    fy_arena_t *arena = fy_new_arena(fy_heap);
    fy_allocator_t allocator = fy_to_allocator(arena);
    int *count = fy_allocate(allocator, int);
    char *buffer = fy_allocate(allocator, char, *fy_kib(4));
    *count = 22;
    strcpy(buffer, "Hello, World!");
    printf("22 = %d, Hello, World! = %s\n", *count, buffer);
    fy_destroy(arena);
  }

  { // Block tests
    char backing[256];
    fy_block_t block = fy_make_block(backing, sizeof(backing));
    fy_allocator_t allocator = fy_to_allocator(&block);
    char *str = fy_allocate(allocator, char, *50);
    strcpy(str, "hiii");
    printf("hiii = %s\n", str);
  }

  { // Tracker tests
    fy_tracker_t *tracker = fy_new_tracker(fy_heap);
    fy_allocator_t allocator = fy_to_allocator(tracker);
    char *str_1 = fy_allocate(allocator, char, *250);
    strcpy(str_1, "test");
    printf("test = %s\n", str_1);
    fy_destroy(tracker);
  }

  { // Slice tests
    fy_tracker_t *tracker = fy_new_tracker(fy_heap);
    fy_allocator_t allocator = fy_to_allocator(tracker);
    fy_vec_t numbers_vec;
    int *numbers =
        fy_new_vec(allocator, int, &numbers_vec, 0, 1, 2, 3, 4, 5, 6, 7);
    fy_append(&numbers_vec, &numbers, 8);
    fy_append(&numbers_vec, &numbers, 9);
    fy_append(&numbers_vec, &numbers, 10);
    for (size_t i = 0; i < fy_len(numbers_vec); i++)
      printf("%ld = %d\n", i, numbers[i]);
    fy_destroy(tracker);
  }

  { // String tests
    fy_string_t greeting = fy_string("Hello, World!");
    printf("%.*s = ", fy_fmt(greeting));
    for (size_t i = 0; i < fy_len(greeting); i++)
      printf("%c", greeting.characters[i]);
    putchar('\n');
    fy_string_t name = fy_substring(greeting, 7);
    printf("World! = %.*s\n", fy_fmt(name));
    fy_string_t short_name = fy_substring(greeting, 7, fy_len(name) - 1);
    printf("World = %.*s\n", fy_fmt(short_name));
    fy_string_t final = fy_to_string("goodbye world!");
    printf("goodbye world! = %.*s\n", fy_fmt(final));
    {
      fy_string_t a = fy_string("abc");
      fy_string_t b = fy_string("def");
      fy_string_t c = fy_string("def");
      printf("true = %s, false = %s\n", fy_to_chars(fy_equals(b, c)),
             fy_to_chars(fy_equals(a, b)));
    }
  }

  { // Hashing
    uint64_t hw = fy_hash(fy_string("Hello World"));
    uint64_t hw2 = fy_hash(fy_string("Hello World"));
    printf("0x3D58DEE72D4E0C27 = 0x%lX = 0x%lX\n", hw, hw2);
  }

  { // Dictionaries
    fy_tracker_t *tracker = fy_new_tracker(fy_heap);
    fy_allocator_t allocator = fy_to_allocator(tracker);
    fy_dict_t scores_d;
    int *scores =
        fy_new_dict(allocator, int, &scores_d, {fy_string("player_1"), 200},
                    {fy_string("player_2"), 400});
    fy_put(&scores_d, &scores, fy_string("player_3"), 34);
    printf("200 = %d\n", fy_get(&scores_d, &scores, fy_string("player_1")));
    printf("34 = %d\n", fy_get(&scores_d, &scores, fy_string("player_3")));
    printf("400 = %d\n", fy_get(&scores_d, &scores, fy_string("player_2")));
    printf("true = %s = %s\n",
           fy_to_chars(fy_contains(&scores_d, fy_string("player_1"))),
           fy_to_chars(fy_contains(&scores_d, fy_string("player_2"))));
    printf("false = %s = %s\n",
           fy_to_chars(fy_contains(&scores_d, fy_string("fred"))),
           fy_to_chars(fy_contains(&scores_d, fy_string("Player_2"))));
    printf("true = %s\n",
           fy_to_chars(fy_contains(&scores_d, fy_string("player_1"))));
    fy_erase(&scores_d, fy_string("player_1"));
    printf("false = %s\n",
           fy_to_chars(fy_contains(&scores_d, fy_string("player_1"))));
    fy_destroy(tracker);
  }

  { // Error handling
    fy_context_t ctx = fy_make_context();
    int result = safe_divide(10, 5, &ctx);
    printf("false = %s\n", fy_to_chars(fy_check(&ctx)));
    result = safe_divide(20, 0, &ctx);
    printf("true = %s\n", fy_to_chars(fy_check(&ctx)));
    printf("0 = %d\n", result);
  }

  fy_panic("This is a planned panic! Program should now exit with status 1\n");
}
