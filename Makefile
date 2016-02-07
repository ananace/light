CFLAGS ?= -Wall

.PHONY: all
all: light

http.o: http.h http.c
	$(CC) -c http.c $(CFLAGS)

color.o: color.h color.c
	$(CC) -c color.c $(CFLAGS)

spi.o: spi.h spi.c
	$(CC) -c spi.c $(CFLAGS)

light: main.c color.o http.o spi.o
	$(CC) main.c color.o http.o spi.o -o light $(CFLAGS)

.PHONY: clean
clean:
	$(RM) light color.o http.o spi.o
