#include "spi.h"
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <asm/ioctl.h>
#include <linux/spi/spidev.h>

#define SPIDEV0  "/dev/spidev0.0"
#define SPIDEV1  "/dev/spidev0.1"
#define SPIBPW   8
#define SPIDELAY 0

int board_init(board_t* board, uint8_t channel, uint32_t speed)
{
	int fd;
	channel &= 1;

	if ((fd = open(channel == 0 ? SPIDEV0 : SPIDEV1, O_RDWR)) < 0)
		return -1;

	uint8_t mode = SPI_MODE_0;
	if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0) return -1;
	if (ioctl(fd, SPI_IOC_RD_MODE, &board->mode) < 0) return -1;

	mode = SPIBPW;
	if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &mode) < 0) return -1;
	if (ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &board->bpw) < 0) return -1;

	mode = speed;
	if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &mode) < 0) return -1;
	if (ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &board->speed) < 0) return -1;

	board->delay = SPIDELAY;
	board->fd = fd;

	return 0;
}
int board_set_pwm(const board_t* board)
{
	char data[3];
	data[0] = board->id;
	data[1] = 0x33;
	data[2] = 0xFF;

	return board_write_data(board, data, 3);
}

int board_write_data(const board_t* board, char* data, uint32_t len)
{
	struct spi_ioc_transfer spi;
	memset(&spi, 0, sizeof(spi));

	spi.tx_buf = (unsigned long)data;
	spi.rx_buf = (unsigned long)data;
	spi.len    = len;
	spi.delay_usecs   = board->delay;
	spi.speed_hz      = board->speed;
	spi.bits_per_word = board->bpw;

	return ioctl(board->fd, SPI_IOC_MESSAGE(1), &spi);
}

int board_write_hsv(const board_t* board, const hsv_t* hsv)
{
	rgb_t col;

	hsv2rgb(hsv, &col);

	return board_write_rgb(board, &col);
}
int board_write_rgb(const board_t* board, const rgb_t* rgb)
{
	char data[6];
	data[0] = board->id;
	data[1] = 0x58;
	data[2] = rgb->r;
	data[3] = rgb->g;
	data[4] = rgb->b;
	data[5] = 0;

	return board_write_data(board, data, 6);
}
