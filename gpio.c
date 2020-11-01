#include "gpio.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <linux/gpio.h>
#include <sys/ioctl.h>

int g_gpio_fd = -1;
int g_gpio_pin_fds[GPIO_PINS];

int gpio_init(uint8_t dev)
{
	if (g_gpio_fd >= 0)
		return 0;

	memset(g_gpio_pin_fds, -1, sizeof(g_gpio_pin_fds));

	char chrdev_name[20];
	// TODO: Does this need to be configurable?
	snprintf(chrdev_name, 20, "/dev/gpiochip%i", dev);
	int fd = open(chrdev_name, 0);
	if (fd == -1) {
		int ret = -errno;
		fprintf(stderr, "Failed to open %s: %s (%d)\n", chrdev_name, strerror(-ret), ret);

		return ret;
	}

	g_gpio_fd = fd;

	struct gpiochip_info info;
	int ret = ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &info);
	if (ret < 0)
	{
		ret = -errno;
		fprintf(stderr, "Failed to read GPIO chip info: %s (%d)\n", strerror(-ret), ret);
		return ret;
	}

	printf("Attached to GPIO device /dev/%s (%s) with %d lines\n", info.name, info.label, info.lines);

	return 0;
}

int gpio_uninit()
{
	if (g_gpio_fd < 0)
		return 0;

	for (size_t i = 0; i < 32; ++i)
		if (g_gpio_pin_fds[i] >= 0)
			gpio_unexport(i);

	if (close(g_gpio_fd) < 0)
	{
		int ret = -errno;
		fprintf(stderr, "Failed to close GPIO device: %s (%d)\n", strerror(-ret), ret);

		return ret;
	}

	g_gpio_fd = -1;

	return 0;
}

int gpio_export(uint8_t id, int pin, int dir)
{
	if (g_gpio_fd < 0)
		return -1;

	struct gpiohandle_request req;
	req.lineoffsets[0] = pin;
	req.flags = (dir == GPIO_IN ? GPIOHANDLE_REQUEST_INPUT : GPIOHANDLE_REQUEST_OUTPUT);
	req.lines = 1;
	snprintf(req.consumer_label, 32, "lightmeister_gpio%d", id);

	int ret = ioctl(g_gpio_fd, GPIO_GET_LINEHANDLE_IOCTL, &req);
	if (ret < 0)
	{
		ret = -errno;
		fprintf(stderr, "Failed to export GPIO pin id %d as %d (%s): %s (%d)\n", id, pin, (dir == GPIO_OUT ? "OUT" : "IN"), strerror(-ret), ret);
		return ret;
	}

	g_gpio_pin_fds[id] = req.fd;

	return 0;
}

int gpio_unexport(uint8_t id)
{
	if (id > GPIO_PINS)
		return -2;

	if (g_gpio_pin_fds[id] <= 0)
		return 0;

	if (close(g_gpio_pin_fds[id]) < 0)
	{
		int ret = -errno;
		fprintf(stderr, "Failed to close GPIO pin id %d: %s (%d)\n", id, strerror(-ret), ret);
		return ret;
	}

	g_gpio_pin_fds[id] = -1;

	return 0;
}

int gpio_write(uint8_t id, int value)
{
	if (g_gpio_fd < 0)
		return -1;

	struct gpiohandle_data data;
	data.values[0] = value;

	int ret = ioctl(g_gpio_pin_fds[id], GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
	if (ret < 0)
	{
		ret = -errno;
		fprintf(stderr, "Failed to write GPIO value %d to pin id %d: %s (%d)\n", value, id, strerror(-ret), ret);
		return ret;
	}

	return 0;
}

int gpio_pulse(uint8_t id)
{
	gpio_write(id, GPIO_LOW);
	usleep(GPIO_DELAY_MICROSEC);
	gpio_write(id, GPIO_HIGH);
	usleep(GPIO_DELAY_MICROSEC);

	return 0;
}
