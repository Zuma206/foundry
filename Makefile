cc ?= cc
ccf = $(cc) $(cflags) -Wall -Wextra -Werror -std=c23

libzuma.o: foundry.c foundry.h
	$(ccf) -o $@ -c foundry.c

test: test.c libzuma.o foundry.h
	$(ccf) -o $@ test.c libzuma.o
