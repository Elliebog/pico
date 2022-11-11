CFLAGS = -Wall -Wextra -pedantic -std=c99 -g
SRC = pico_lib/writebuffer.c	\
	  pico.c
pico: $(SRC) 
		gcc -o $@ $^ $(CFLAGS)