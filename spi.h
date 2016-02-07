#ifndef _SPI_H
#define _SPI_H

#include <stdint.h>
#include "color.h"

struct _board_t
{
	int fd;
	uint8_t id;

	uint8_t mode;
	uint8_t bpw;
	uint32_t speed;
	uint16_t delay;
};
typedef struct _board_t board_t;

int board_init(board_t*, uint8_t channel, uint32_t speed);
int board_set_pwm(const board_t*);

int board_write_data(const board_t*, char*, uint32_t);

int board_write_hsv(const board_t*, const hsv_t*);
int board_write_rgb(const board_t*, const rgb_t*);

#endif
