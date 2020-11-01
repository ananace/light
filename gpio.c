#include "gpio.h"

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#if defined(RPI_GPIO)
#include <sys/mman.h>
#if RPI_GPIO >= 4
#define BCM2708_PERI_BASE 0xFE000000
#elif RPI_GPIO >= 2
#define BCM2708_PERI_BASE 0x3F000000
#else
#define BCM2708_PERI_BASE 0x20000000
#endif

#define GPIO_BASE (BCM2708_PERI_BASE + 0x200000)

volatile unsigned *g_gpio = NULL;

#define INP_GPIO(g) *(g_gpio+((g)/10)) &= ~(7<<(((g)%10)*3))
#define OUT_GPIO(g) *(g_gpio+((g)/10)) |=  (1<<(((g)%10)*3))

#define GPIO_SET *(g_gpio+7)
#define GPIO_CLR *(g_gpio+10)

int _gpio_init()
{
#define PAGE_SIZE (4*1024)
#define BLOCK_SIZE (4*1024)
	int mem_fd;
	void *gpio_map;

	if ((mem_fd = open("/dev/mem", O_RDWR|O_SYNC) ) < 0) {
		fprintf(stderr, "gpio init, can't open /dev/mem \n");
		return -1;
	}

	/* mmap GPIO */
	gpio_map = mmap(
		NULL,
		BLOCK_SIZE,
		PROT_READ|PROT_WRITE,
		MAP_SHARED,
		mem_fd,
		GPIO_BASE
	);

	close(mem_fd);

	if (gpio_map == MAP_FAILED) {
		fprintf(stderr, "gpio init, mmap error %ld\n", (long)gpio_map);
		return -1;
	}

	g_gpio = (volatile unsigned *)gpio_map;

	return 0;
#undef BLOCK_SIZE
#undef PAGE_SIZE
}

int gpio_export(int pin)
{
	(void)pin;
	if (g_gpio == NULL)
	{
		int r = _gpio_init();
		if (r < 0)
			return r;
	}

	return 0;
}
int gpio_unexport(int pin)
{
	(void)pin;
	if (g_gpio == NULL)
		return -1;

	return 0;
}

int gpio_direction(int pin, int dir)
{
	if (g_gpio == NULL)
		return -1;

	INP_GPIO(pin);
	if (dir == GPIO_OUT)
		OUT_GPIO(pin);

	return 0;
}
int gpio_write(int pin, int value)
{
	if (g_gpio == NULL)
		return -1;

	if (value == GPIO_HIGH)
		GPIO_SET = 1 << pin;
	else
		GPIO_CLR = 1 << pin;

	return 0;
}
#else
// sysctl implementation
int gpio_export(int pin)
{
#define BUFFER_MAX 3
	char buffer[BUFFER_MAX];
	size_t bytes_written;
	int fd;

	fd = open("/sys/class/gpio/export", O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "Failed to open gpio export for writing!\n");
		return -1;
	}

	bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
	write(fd, buffer, bytes_written);
	close(fd);
	return 0;
}
int gpio_unexport(int pin)
{
	char buffer[BUFFER_MAX];
	size_t bytes_written;
	int fd;

	fd = open("/sys/class/gpio/unexport", O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "Failed to open gpio unexport for writing!\n");
		return -1;
	}

	bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
	write(fd, buffer, bytes_written);
	close(fd);
	return 0;
}
int gpio_direction(int pin, int dir)
{
	static const char s_directions_str[]  = "in\0out";

#define DIRECTION_MAX 35
	char path[DIRECTION_MAX];
	int fd;

	snprintf(path, DIRECTION_MAX, "/sys/class/gpio/gpio%d/direction", pin);
	fd = open(path, O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "Failed to open gpio pin direction for writing!\n");
		return -1;
	}

	if (write(fd, &s_directions_str[dir == GPIO_IN ? 0 : 3], dir == GPIO_IN ? 2 : 3) <= 0) {
		fprintf(stderr, "Failed to set gpio pin direction!\n");
		return -1;
	}

	close(fd);
	return 0;
}
int gpio_write(int pin, int value)
{
#define VALUE_MAX 30
	static const char s_values_str[] = "01";

	char path[VALUE_MAX];
	int fd;

	snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
	fd = open(path, O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "Failed to open gpio pin value for writing!\n");
		return -1;
	}

	if (write(fd, &s_values_str[value == GPIO_LOW ? GPIO_LOW : GPIO_HIGH], 1) <= 0) {
		fprintf(stderr, "Failed to write gpio pin value!\n");
		return -1;
	}

	close(fd);
	return 0;
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
