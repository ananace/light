CFLAGS = -Wall -Wextra
ifdef RPI_GPIO
CFLAGS := $(CFLAGS) -DRPI_GPIO=$(RPI_GPIO)
endif
ifdef DEBUG
CFLAGS := $(CFLAGS) -ggdb
endif

.PHONY: all
all: light

http.o: http.h http.c
	$(CC) -c http.c $(CFLAGS)

color.o: color.h color.c
	$(CC) -c color.c $(CFLAGS)

board.o: board.h board.c
	$(CC) -c board.c $(CFLAGS)

gpio.o: gpio.h gpio.c
	$(CC) -c gpio.c $(CFLAGS)

light: main.c color.o http.o gpio.o board.o
	$(CC) -lm main.c color.o http.o gpio.o board.o -o light $(CFLAGS)

.PHONY: clean
clean:
	$(RM) light color.o http.o gpio.o board.o
