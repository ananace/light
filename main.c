#include "http.h"
#include "spi.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int running;
http_t server;

void sigint(int _)
{
	running = 0;
	http_close(&server);
}

int main(int argc, char** argv)
{
	uint16_t port = 4567;
	uint32_t speed = 100000;
	uint8_t id = 0x90;

	if (argc > 1)
	{
		int i;
		for (i = 1; i < argc; ++i)
		{
			if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0)
				port = atoi(argv[++i]);
			else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--hz") == 0)
				speed = atoi(argv[++i]);
			else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--id") == 0)
				id = atoi(argv[++i]);
		}
	}

	memset(&server, 0, sizeof(server));
	if (http_init(&server, port) < 0)
	{
		puts("Failed to open socket, port in use?");
		return -1;
	}

	server.name = "TheLightMeister v0.1";
	printf("Server running on http://localhost:%i/\n", server.port);

	board_t board;
	board.id = id;
	if (board_init(&board, 0, speed) < 0)
	{
		puts("Failed to connect to light board, check SPI bus?");
		return -1;
	}

	printf("LED board #%i connected as %i with mode %i, bpw %i, speed %iHz\n", board.id, board.fd, board.mode, board.bpw, board.speed);

	if (board_set_pwm(&board) < 0)
	{
		puts("Failed to set PWM on board");
		return -1;
	}

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

					int i;
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

					int i;
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
