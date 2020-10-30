#include "gpio.h"

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#if defined(RPI_GPIO)

#else
// sysctl implementation
int gpio_export(int pin)
{
#define BUFFER_MAX 3
	char buffer[BUFFER_MAX];
	size_t bytes_written;
	int fd;

	fd = open("/sys/class/gpio/export", O_WRONLY);
	if (-1 == fd) {
		fprintf(stderr, "Failed to open gpio export for writing!\n");
		return(-1);
	}

	bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
	write(fd, buffer, bytes_written);
	close(fd);
	return(0);
}
int gpio_unexport(int pin)
{
	char buffer[BUFFER_MAX];
	size_t bytes_written;
	int fd;

	fd = open("/sys/class/gpio/unexport", O_WRONLY);
	if (-1 == fd) {
		fprintf(stderr, "Failed to open gpio unexport for writing!\n");
		return(-1);
	}

	bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
	write(fd, buffer, bytes_written);
	close(fd);
	return(0);
}
int gpio_direction(int pin, int dir)
{
	static const char s_directions_str[]  = "in\0out";

#define DIRECTION_MAX 35
	char path[DIRECTION_MAX];
	int fd;

	snprintf(path, DIRECTION_MAX, "/sys/class/gpio/gpio%d/direction", pin);
	fd = open(path, O_WRONLY);
	if (-1 == fd) {
		fprintf(stderr, "Failed to open gpio pin direction for writing!\n");
		return(-1);
	}

	if (-1 == write(fd, &s_directions_str[dir == GPIO_IN ? 0 : 3], dir == GPIO_IN ? 2 : 3)) {
		fprintf(stderr, "Failed to set gpio pin direction!\n");
		return(-1);
	}

	close(fd);
	return(0);
}
int gpio_write(int pin, int value)
{
#define VALUE_MAX 30
	static const char s_values_str[] = "01";

	char path[VALUE_MAX];
	int fd;

	snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
	fd = open(path, O_WRONLY);
	if (-1 == fd) {
		fprintf(stderr, "Failed to open gpio pin value for writing!\n");
		return(-1);
	}

	if (1 != write(fd, &s_values_str[value == GPIO_LOW ? GPIO_LOW : GPIO_HIGH], 1)) {
		fprintf(stderr, "Failed to write gpio pin value!\n");
		return(-1);
	}

	close(fd);
	return(0);
}
#endif

int gpio_pulse(int pin)
{
	gpio_write(pin, GPIO_LOW);
	usleep(GPIO_DELAY_MICROSEC);
	gpio_write(pin, GPIO_HIGH);
	usleep(GPIO_DELAY_MICROSEC);

	return 0;
}
