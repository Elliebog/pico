CFLAGS = -Wall -Wextra -pedantic -std=c99
SRC = pico_lib/writebuffer.c	\
	  pico.c
pico: $(SRC) 
		gcc -o $@ $^ $(CFLAGS)