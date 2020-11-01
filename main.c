#include "http.h"
#include "board.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int running;
http_t server;
board_t board;

void sigint(int sig)
{
	(void)sig;

	running = 0;
	http_close(&server);
	board_cleanup(&board);
}

int main(int argc, char** argv)
{
	struct
	{
		uint16_t port;

		union {
			struct {
				uint32_t speed;
				uint8_t id;
			} spi;
			struct {
				uint8_t cpin,
					dpin,
					len;
			} p9813;
		};
	} args;
	memset(&args, 0, sizeof(args));

	if (argc < 1)
		return -1;

	uint8_t mode = 255;

	int i;
	for (i = 1; i < argc; ++i)
	{
		if (strcmp(argv[i], "-S") == 0 || strcmp(argv[i], "--spi") == 0)
			mode = 0;
		else if (strcmp(argv[i], "-P") == 0 || strcmp(argv[i], "--p9813") == 0)
			mode = 1;
		else if (strcmp(argv[i], "-D") == 0 || strcmp(argv[i], "--dummy") == 0)
			mode = 2;
		else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0)
			args.port = atoi(argv[++i]);
		else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
			mode = 254;

		else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--hz") == 0)
			args.spi.speed = atoi(argv[++i]);
		else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--id") == 0)
			args.spi.id = atoi(argv[++i]);

		else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--clock") == 0)
			args.p9813.cpin = atoi(argv[++i]);
		else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--data") == 0)
			args.p9813.dpin = atoi(argv[++i]);
		else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--count") == 0)
			args.p9813.len = atoi(argv[++i]);
	}
	if (args.port == 0) args.port = 4567;

	if (mode == 0)
	{
		if (args.spi.speed == 0) args.spi.speed = 100000;
		if (args.spi.id == 0) args.spi.id = 0x90;

		if (board_init_spi(&board, 0, args.spi.speed) < 0)
		{
			puts("Failed to connect to light board, check SPI bus?");
			return -1;
		}
		board.spi.id = args.spi.id;

		printf("LED board #%i connected as %i with mode %i, bpw %i, speed %iHz\n", board.spi.id, board.spi.fd, board.spi.mode, board.spi.bpw, board.spi.speed);

		if (board_set_pwm(&board) < 0)
		{
			puts("Failed to set PWM on board");
			return -1;
		}
	}
	else if (mode == 1)
	{
		if (args.p9813.len == 0) args.p9813.len = 1;

		if (board_init_p9813(&board, args.p9813.cpin, args.p9813.dpin, args.p9813.len))
		{
			puts("Failed to connect to light board, check pins?");
			return -1;
		}

		printf("LED board chain of %i connected on pins %i/%i\n", args.p9813.len, args.p9813.cpin, args.p9813.dpin);
	}
	else if (mode == 2)
	{
		board_init_dummy(&board);
	}
	else
	{
		printf("Usage: %s [OPTIONS...]\n\n"
		       "Args:\n"
		       "  -S --spi        Use SPI connected BitWizard board\n"
		       "  -P --p9813      Use P9813 board\n"
		       "  -D --dummy      Use dummy board for testing\n"
		       "  -p --port PORT  Specify the HTTP server port to use\n"
		       "  -h --help       Display this text\n\n"
		       "SPI args:\n"
		       "  -h --hz HZ      Change the communication hertz (default 100 000)\n"
		       "  -i --id ID      Change the board ID (default 0x90)\n\n"
		       "P9813 args:\n"
		       "  -c --clock PIN  The pin ID to use for the clock signal\n"
		       "  -d --data PIN   The pin ID to use for the data signal\n"
		       "  -n --count NUM  The number of controllers that are chained\n",
		       argv[0]);
		return mode == 255 ? -1 : 0;
	}

	memset(&server, 0, sizeof(server));
	if (http_init(&server, args.port) < 0)
	{
		puts("Failed to open socket, port in use?");
		return -1;
	}

	server.name = "TheLightMeister v0.1";
	printf("Server running on http://localhost:%i/\n", server.port);


	rgb_t curCol;
	memset(&curCol, 0, sizeof(curCol));

	if (board_write_rgb(&board, &curCol) < 0)
	{
		puts("Failed to write RGB data to board");
		return -1;
	}

	running = 1;
	signal(SIGINT, sigint);

	http_req_t client;
	while (running)
	{
		if (http_accept(&server, &client) < 0)
		{
			http_req_close(&client);
			continue;
		}

		printf("New request to %s %s\n", client.method, client.path);

		if (strcmp(client.path, "/light/temperature"))
		{
			if (strcasecmp(client.method, "GET") == 0)
			{
				http_req_not_implemented(&client);
				http_req_close(&client);
				continue;
			}
			else if (strcasecmp(client.method, "PUT") == 0 || strcasecmp(client.method, "POST") == 0)
			{
				if (client.content_length >= 3 || client.query != NULL)
				{
					char* data = client.content;
					if (client.content_length < 3)
						data = client.query;

					unsigned int temperature = atoi(data);
					temperature2rgb(temperature, &curCol);

					printf("New color: %iK => [ %i, %i, %i ]\n", temperature, curCol.r, curCol.g, curCol.b);
					board_write_rgb(&board, &curCol);
				}
			}
			else if (strcasecmp(client.method, "DELETE") == 0)
			{
				memset(&curCol, 0, sizeof(curCol));
				board_write_rgb(&board, &curCol);
			}

			char buf[128];
			http_req_ok(&client, "application/json");
			sprintf(buf, "{\"r\":%i,\"g\":%i,\"b\":%i}\n", curCol.r, curCol.g, curCol.b);
			http_req_send(&client, buf);
		}
		if (strcmp(client.path, "/light/rgb") == 0)
		{
			if (strcasecmp(client.method, "GET") == 0)
			{
			}
			else if (strcasecmp(client.method, "PUT") == 0 || strcasecmp(client.method, "POST") == 0)
			{
				if (client.content_length >= 3 || client.query != NULL)
				{
					size_t len = client.content_length;
					char* data = client.content;
					if (client.content_length < 3)
					{
						data = client.query;
						len = strlen(data);
					}

					size_t i;
					for (i = 0; i < len; ++i)
						if (data[i] == '&' || data[i] == '=')
							data[i] = 0;

					for (i = 0; i < len;)
					{
						char* cur = &(data[i]);

						i += strlen(cur) + 1;
						if (strcasecmp(cur, "r") == 0 || strcasecmp(cur, "red") == 0)
							curCol.r = atoi(&(data[i]));
						else if (strcasecmp(cur, "g") == 0 || strcasecmp(cur, "green") == 0)
							curCol.g = atoi(&(data[i]));
						else if (strcasecmp(cur, "b") == 0 || strcasecmp(cur, "blue") == 0)
							curCol.b = atoi(&(data[i]));
					}

					printf("New color: [ %i, %i, %i ]\n", curCol.r, curCol.g, curCol.b);
					board_write_rgb(&board, &curCol);
				}
			}
			else if (strcasecmp(client.method, "DELETE") == 0)
			{
				memset(&curCol, 0, sizeof(curCol));
				board_write_rgb(&board, &curCol);
			}
			else
			{
				http_req_not_implemented(&client);
				http_req_close(&client);
				continue;
			}

			char buf[128];
			http_req_ok(&client, "application/json");
			sprintf(buf, "{\"r\":%i,\"g\":%i,\"b\":%i}\n", curCol.r, curCol.g, curCol.b);
			http_req_send(&client, buf);
		}
		else if (strcmp(client.path, "/light/hsv") == 0)
		{
			if (strcasecmp(client.method, "GET") == 0)
			{
			}
			else if (strcasecmp(client.method, "PUT") == 0 || strcasecmp(client.method, "POST") == 0)
			{
				if (client.content_length >= 3 || client.query != NULL)
				{
					hsv_t col;

					size_t len = client.content_length;
					char* data = client.content;
					if (client.content_length < 3)
					{
						data = client.query;
						len = strlen(data);
					}

					size_t i;
					for (i = 0; i < len; ++i)
						if (data[i] == '&' || data[i] == '=')
							data[i] = 0;

					for (i = 0; i < len;)
					{
						char* cur = &(data[i]);

						i += strlen(cur) + 1;
						if (strcasecmp(cur, "h") == 0 || strcasecmp(cur, "hue") == 0)
							col.h = (uint8_t)((atof(data + i) / 360.f) * 255);
						else if (strcasecmp(cur, "s") == 0 || strcasecmp(cur, "saturation") == 0)
							col.s = (uint8_t)(atof(data + i) * 255);
						else if (strcasecmp(cur, "v") == 0 || strcasecmp(cur, "value") == 0)
							col.v = (uint8_t)(atof(data + i) * 255);
					}

					hsv2rgb(&col, &curCol);
					printf("New color: [ %.2f, %.2f, %.2f ] => [ %i, %i, %i ]\n", (col.h / 255.f) * 360.f, col.s / 255.f, col.v / 255.f, curCol.r, curCol.g, curCol.b);
					board_write_rgb(&board, &curCol);
				}
			}
			else if (strcasecmp(client.method, "DELETE") == 0)
			{
				memset(&curCol, 0, sizeof(curCol));
				board_write_rgb(&board, &curCol);
			}
			else
			{
				http_req_not_implemented(&client);
				http_req_close(&client);
				continue;
			}

			hsv_t hsvCol;
			rgb2hsv(&curCol, &hsvCol);

			char buf[128];
			http_req_ok(&client, "application/json");
			sprintf(buf, "{\"h\":%.2f,\"s\":%.2f,\"v\":%.2f}\n", (hsvCol.h / 255.f) * 360.f, hsvCol.s / 255.f, hsvCol.v / 255.f);
			http_req_send(&client, buf);
		}
		else
			http_req_not_found(&client);

		http_req_close(&client);
	}

	http_close(&server);

	return 0;
}
