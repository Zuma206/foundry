#include "zuma.h"
#include <alloca.h>
#include <stdio.h>
#include <string.h>

int main() {
  { // Heap tests
    int *count = allocate(heap, int);
    *count = 0;
    (*count)++;
    printf("1 = %d\n", *count);
    deallocate(heap, count);
  }

  { // Arena tests
    arena_t *arena = new_arena(heap);
    allocator_t allocator = to_allocator(arena);
    int *count = allocate(allocator, int);
    char *buffer = allocate(allocator, char, *kib(4));
    *count = 22;
    strcpy(buffer, "Hello, World!");
    printf("22 = %d, Hello, World! = %s\n", *count, buffer);
    destroy(arena);
  }

  { // Block tests
    char backing[256];
    block_t block = make_block(backing, sizeof(backing));
    allocator_t allocator = to_allocator(&block);
    char *str = allocate(allocator, char, *50);
    strcpy(str, "hiii");
    printf("hiii = %s\n", str);
  }

  { // Tracker tests
    tracker_t *tracker = new_tracker(heap);
    allocator_t allocator = to_allocator(tracker);
    char *str_1 = allocate(allocator, char, *250);
    strcpy(str_1, "test");
    printf("test = %s\n", str_1);
    destroy(tracker);
  }

  { // Slice tests
    tracker_t *tracker = new_tracker(heap);
    allocator_t allocator = to_allocator(tracker);
    vec_t numbers_vec;
    int *numbers =
        new_vec(allocator, int, &numbers_vec, 0, 1, 2, 3, 4, 5, 6, 7);
    append(&numbers_vec, &numbers, 8);
    append(&numbers_vec, &numbers, 9);
    append(&numbers_vec, &numbers, 10);
    for (size_t i = 0; i < len(numbers_vec); i++)
      printf("%ld = %d\n", i, numbers[i]);
    destroy(tracker);
  }

  { // String tests
    string_t greeting = string("Hello, World!");
    printf("%.*s = ", fmt(greeting));
    for (size_t i = 0; i < len(greeting); i++)
      printf("%c", greeting.characters[i]);
    putchar('\n');
    string_t name = substring(greeting, 7);
    printf("World! = %.*s\n", fmt(name));
    string_t short_name = substring(greeting, 7, len(name) - 1);
    printf("World = %.*s\n", fmt(short_name));
    string_t final = to_string("goodbye world!");
    printf("goodbye world! = %.*s\n", fmt(final));
    {
      string_t a = string("abc");
      string_t b = string("def");
      string_t c = string("def");
      printf("true = %s, false = %s\n", to_cstr(equals(b, c)),
             to_cstr(equals(a, b)));
    }
  }

  { // Hashing
    uint64_t hw = hash(string("Hello World"));
    uint64_t hw2 = hash(string("Hello World"));
    printf("0x3D58DEE72D4E0C27 = 0x%lX = 0x%lX\n", hw, hw2);
  }

  { // Dictionaries
    tracker_t *tracker = new_tracker(heap);
    allocator_t allocator = to_allocator(tracker);
    dict_t scores_d;
    int *scores = new_dict(allocator, int, &scores_d, {string("player_1"), 200},
                           {string("player_2"), 400});
    put(&scores_d, &scores, string("player_3"), 34);
    printf("200 = %d\n", get(&scores_d, &scores, string("player_1")));
    printf("34 = %d\n", get(&scores_d, &scores, string("player_3")));
    printf("400 = %d\n", get(&scores_d, &scores, string("player_2")));
    printf("true = %s = %s\n", to_cstr(has(&scores_d, string("player_1"))),
           to_cstr(has(&scores_d, string("player_2"))));
    printf("false = %s = %s\n", to_cstr(has(&scores_d, string("fred"))),
           to_cstr(has(&scores_d, string("Player_2"))));
    destroy(tracker);
  }

  panic("This is a planned panic! Program should now exit with status 1\n");
}
