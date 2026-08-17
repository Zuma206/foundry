cc ?= cc
ccf = $(cc) $(cflags) -Wall -Wextra -Werror -std=c23

foundry.o: foundry.c foundry.h
	$(ccf) -o $@ -c foundry.c

test: test.c foundry.o foundry.h
	$(ccf) -o $@ test.c foundry.o
