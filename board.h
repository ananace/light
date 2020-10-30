#ifndef _BOARD_H
#define _BOARD_H

#include <stdint.h>
#include "color.h"

enum board_type_t
{
	board_type_invalid = 0,
	board_type_spi,
	board_type_p9813
};

struct _board_t
{
	enum board_type_t type;

	union
	{
		struct {
			int fd;
			uint8_t id;

			uint8_t mode;
			uint8_t bpw;
			uint32_t speed;
			uint16_t delay;
		} spi;
		struct {
			uint8_t clock_pin,
				data_pin,
				chain_len;
		} p9813;
	};
};
typedef struct _board_t board_t;

int board_init_spi(board_t*, uint8_t channel, uint32_t speed);
int board_init_p9813(board_t*, uint8_t clock_pin, uint8_t data_pin, uint8_t chain_len);
int board_cleanup(board_t*);

int board_set_pwm(const board_t*);

int board_write_data(const board_t*, char*, uint32_t);
int board_write_u32(const board_t*, uint32_t data);

int board_write_rgb(const board_t*, const rgb_t*);

#endif
