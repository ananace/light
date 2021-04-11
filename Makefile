CFLAGS = -Wall -Wextra
ifdef RPI_GPIO
CFLAGS := $(CFLAGS) -DRPI_GPIO=$(RPI_GPIO)
endif
ifdef DEBUG
CFLAGS := $(CFLAGS) -ggdb
endif

OBJECTS := color.o http.o gpio.o board.o mqtt.o mqtt_pal.o

.PHONY: all
all: light

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)

light: main.c $(OBJECTS)
	$(CC) -lm -lpthread main.c $(OBJECTS) -o light $(CFLAGS)

.PHONY: clean
clean:
	$(RM) light $(OBJECTS)
