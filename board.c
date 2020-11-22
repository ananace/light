#include "board.h"
#include "gpio.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <asm/ioctl.h>
#include <linux/spi/spidev.h>

#define SPIDEV0  "/dev/spidev0.0"
#define SPIDEV1  "/dev/spidev0.1"
#define SPIBPW   8
#define SPIDELAY 0

#define PIN_CLOCK 0
#define PIN_DATA 1

int board_init_spi(board_t* board, uint8_t channel, uint32_t speed)
{
	if (board->type != board_type_invalid)
		return -2;

	board->type = board_type_spi;

	int fd;
	channel &= 1;

	if ((fd = open(channel == 0 ? SPIDEV0 : SPIDEV1, O_RDWR)) < 0)
		return -1;

	uint8_t mode = SPI_MODE_0;
	if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0) return -1;
	if (ioctl(fd, SPI_IOC_RD_MODE, &board->spi.mode) < 0) return -1;

	mode = SPIBPW;
	if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &mode) < 0) return -1;
	if (ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &board->spi.bpw) < 0) return -1;

	mode = speed;
	if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &mode) < 0) return -1;
	if (ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &board->spi.speed) < 0) return -1;

	board->spi.delay = SPIDELAY;
	board->spi.fd = fd;

	return 0;
}
int board_init_p9813(board_t* board, uint8_t clock_pin, uint8_t data_pin, uint8_t chain_len)
{
	if (board->type != board_type_invalid)
		return -2;

	board->type = board_type_p9813;

	printf("Attaching clock pin to GPIO pin %d\n", clock_pin);
	if (gpio_export(PIN_CLOCK, clock_pin, GPIO_OUT) < 0) return -1;
	printf("Attaching data pin to GPIO pin %d\n", data_pin);
	if (gpio_export(PIN_DATA, data_pin, GPIO_OUT) < 0) return -1;

	board->p9813.clock_pin = clock_pin;
	board->p9813.data_pin = data_pin;
	board->p9813.chain_len = chain_len;

	return 0;
}
int board_init_dummy(board_t* board)
{
	if (board->type != board_type_invalid)
		return -2;

	board->type = board_type_dummy;
	return 0;
}
int board_cleanup(board_t* board)
{
	if (board->type == board_type_p9813)
	{
		gpio_unexport(PIN_CLOCK);
		gpio_unexport(PIN_DATA);
	}

	if (board->type == board_type_dummy)
		fprintf(stderr, "board_cleanup()\n");

	memset(board, 0, sizeof(board_t));
	return 0;
}

int board_set_pwm(const board_t* board)
{
	if (board->type == board_type_dummy)
		fprintf(stderr, "board_set_pwm()\n");

	if (board->type != board_type_spi)
		return -1;

	char data[3];
	data[0] = board->spi.id;
	data[1] = 0x33;
	data[2] = 0xFF;

	return board_write_data(board, data, 3);
}

int board_write_data(const board_t* board, char* data, uint32_t len)
{
	if (board->type == board_type_dummy)
		fprintf(stderr, "board_write_data(%p, %u)\n", (void*)data, len);

	if (board->type != board_type_spi)
		return -1;

	struct spi_ioc_transfer spi;
	memset(&spi, 0, sizeof(spi));

	spi.tx_buf = (unsigned long)data;
	spi.rx_buf = (unsigned long)data;
	spi.len    = len;
	spi.delay_usecs   = board->spi.delay;
	spi.speed_hz      = board->spi.speed;
	spi.bits_per_word = board->spi.bpw;

	return ioctl(board->spi.fd, SPI_IOC_MESSAGE(1), &spi);
}
int board_write_u32(const board_t* board, uint32_t data)
{
	if (board->type == board_type_dummy)
		fprintf(stderr, "board_write_u32(%u)\n", data);

	int val;
	for (uint8_t bit = 0; bit < 32; ++bit)
	{
		val = (data & 0x80000000) != 0 ? GPIO_HIGH : GPIO_LOW;
		data <<= 1;

		gpio_write(PIN_DATA, val);
		gpio_pulse(PIN_CLOCK);
	}
	return 0;
}

int board_write_rgb(const board_t* board, const rgb_t* rgb)
{

	if (board->type == board_type_spi)
	{
		char data[6];
		data[0] = board->spi.id;
		data[1] = 0x58;
		data[2] = rgb->r;
		data[3] = rgb->g;
		data[4] = rgb->b;
		data[5] = 0;

		return board_write_data(board, data, 6);
	}
	else if (board->type == board_type_p9813)
	{
		uint8_t header = (1 << 7) | (1 << 6);
		if ((rgb->b & 0x80) == 0) header |= (1 << 5);
		if ((rgb->b & 0x40) == 0) header |= (1 << 4);
		if ((rgb->g & 0x80) == 0) header |= (1 << 3);
		if ((rgb->g & 0x40) == 0) header |= (1 << 2);
		if ((rgb->r & 0x80) == 0) header |= (1 << 1);
		if ((rgb->r & 0x40) == 0) header |= (1 << 0);

		uint32_t data = 0;
		data |= header << 24;
		data |= rgb->b << 16;
		data |= rgb->g << 8;
		data |= rgb->r << 0;

		board_write_u32(board, 0);

		for (uint8_t i = 0; i < board->p9813.chain_len; ++i)
			board_write_u32(board, data);

		board_write_u32(board, 0);
		return 0;
	}
	else if (board->type == board_type_dummy)
	{
		fprintf(stderr, "board_write_rgb([%u, %u, %u])\n", rgb->r, rgb->g, rgb->b);
		return 0;
	}

	return -1;
}
