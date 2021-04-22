#include "board.h"
#include "color.h"
#include "gpio.h"
#include "http.h"
#include "mqtt.h"
#include <strings.h>
typedef struct mqtt_client mqtt_t;

#include "posix_sockets.h"

#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIGHTSTATE_OFF 0
#define LIGHTSTATE_ON 1

uint8_t mode = 255;
int running;
http_t server;
board_t board;
mqtt_t mqtt;

rgb_t curCol;
hsv_t curHSV;
temp_t curTemp;

uint8_t curBright;

rgb_t offCol;
int lightState = LIGHTSTATE_OFF;

uint8_t *mqtt_sendbuf = NULL,
	*mqtt_recvbuf = NULL;

#define MQTT_QUEUELEN 8
struct mqtt_tosend
{
	char* topic;
	char* message;
	uint8_t flags;
	uint8_t ready;
};
struct mqtt_tosend mqtt_messages[MQTT_QUEUELEN];
int mqtt_message_counter = 0;

int http_enabled = 0, mqtt_enabled = 0;
pthread_t http_thread, mqtt_thread;

void publish_callback(void** unused, struct mqtt_response_publish *published);
void reconnect_callback(mqtt_t* unused, void** reconnect_ptr);

void* http_worker(void*);
void* mqtt_worker(void*);

void sigint(int sig)
{
	(void)sig;

	running = 0;
	if (http_enabled == 1)
		pthread_cancel(http_thread);
	if (mqtt_enabled == 1)
		pthread_cancel(mqtt_thread);

	if (server.socket != 0)
		http_close(&server);
	if (mqtt.socketfd != 0)
		mqtt_disconnect(&mqtt);
	board_cleanup(&board);

	if (mode == 1)
		gpio_uninit();
}

struct
{
	struct {
		uint16_t port;
	} http;

	struct {
		const char* addr;
		const char* topic;
		const char* name;
		const char* slug;
		const char* publish;
		uint16_t port;
	} mqtt;

	union {
		struct {
			uint32_t speed;
			uint8_t id;
		} spi;
		struct {
			uint8_t cpin,
				dpin,
				len,
				dev;
		} p9813;
	};
} args;

int main(int argc, char** argv)
{
	memset(mqtt_messages, 0, sizeof(mqtt_messages));
	memset(&args, -1, sizeof(args));
	args.mqtt.addr = "";
	args.mqtt.topic = "";
	args.mqtt.name = "";
	args.mqtt.slug = "";
	args.mqtt.publish = "";

	if (argc < 1)
		return -1;

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
			args.http.port = atoi(argv[++i]);
		else if (strcmp(argv[i], "-ma") == 0 || strcmp(argv[i], "--mqtt-addr") == 0)
			args.mqtt.addr = argv[++i];
		else if (strcmp(argv[i], "-mp") == 0 || strcmp(argv[i], "--mqtt-port") == 0)
			args.mqtt.port = atoi(argv[++i]);
		else if (strcmp(argv[i], "-mt") == 0 || strcmp(argv[i], "--mqtt-topic") == 0)
			args.mqtt.topic = argv[++i];
		else if (strcmp(argv[i], "-mP") == 0 || strcmp(argv[i], "--mqtt-publish") == 0)
			args.mqtt.publish = argv[++i];
		else if (strcmp(argv[i], "-mn") == 0 || strcmp(argv[i], "--mqtt-name") == 0)
			args.mqtt.name = argv[++i];

		else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
			mode = 254;

		else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--hz") == 0)
			args.spi.speed = atoi(argv[++i]);
		else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--id") == 0)
			args.spi.id = atoi(argv[++i]);

		else if (strcmp(argv[i], "--gpiochip") == 0)
			args.p9813.dev = atoi(argv[++i]);
		else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--clock") == 0)
			args.p9813.cpin = atoi(argv[++i]);
		else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--data") == 0)
			args.p9813.dpin = atoi(argv[++i]);
		else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--count") == 0)
			args.p9813.len = atoi(argv[++i]);
	}
	if (args.http.port == UINT16_MAX) args.http.port = 4567;
	if (args.mqtt.port == UINT16_MAX) args.mqtt.port = 1883;
	if (strlen(args.mqtt.topic) == 0)
		args.mqtt.topic = "light";
	if (strlen(args.mqtt.name) == 0)
	{
		args.mqtt.name = args.mqtt.topic;
		args.mqtt.slug = args.mqtt.topic;
	}
	else
	{
		int nameLen = strlen(args.mqtt.name);
		char* slug = malloc(nameLen);

		for (size_t i = 0; i < nameLen; ++i)
		{
			if (isalnum(args.mqtt.name[i]))
				slug[i] = tolower(args.mqtt.name[i]);
			else
				slug[i] = '_';
		}
		slug[nameLen] = 0;

		args.mqtt.slug = slug;
	}

	if (mode == 0)
	{
		if (args.spi.speed == UINT32_MAX) args.spi.speed = 100000;
		if (args.spi.id == UINT8_MAX) args.spi.id = 0x90;

		if (board_init_spi(&board, 0, args.spi.speed) < 0)
		{
			fprintf(stderr, "Failed to connect to light board, check SPI bus?\n");
			return -1;
		}
		board.spi.id = args.spi.id;

		printf("LED board #%i connected as %i with mode %i, bpw %i, speed %iHz\n", board.spi.id, board.spi.fd, board.spi.mode, board.spi.bpw, board.spi.speed);

		if (board_set_pwm(&board) < 0)
		{
			fprintf(stderr, "Failed to set PWM on board\n");
			return -1;
		}
	}
	else if (mode == 1)
	{
		if (args.p9813.dev == UINT8_MAX) args.p9813.dev = 0;
		if (gpio_init(args.p9813.dev) < 0)
			return -1;

		if (args.p9813.cpin == UINT8_MAX) args.p9813.cpin = 0;
		if (args.p9813.dpin == UINT8_MAX) args.p9813.dpin = 1;
		if (args.p9813.len == UINT8_MAX) args.p9813.len = 1;

		if (board_init_p9813(&board, args.p9813.cpin, args.p9813.dpin, args.p9813.len))
		{
			fprintf(stderr, "Failed to connect to light board, check output for more info\n");
			return -1;
		}

		printf("LED board chain of %i P9813 IC(s) connected on pins clock: %i, data: %i\n", args.p9813.len, args.p9813.cpin, args.p9813.dpin);
	}
	else if (mode == 2)
	{
		board_init_dummy(&board);
	}
	else
	{
		printf("Usage: %s [OPTIONS...]\n\n"
			"Args:\n"
			"  -S --spi	    Use SPI connected BitWizard board\n"
			"  -P --p9813	  Use P9813 board\n"
			"  -D --dummy	  Use dummy board for testing\n"
			"\n"
			"  -p --port PORT  Specify the HTTP server port to use\n"
			"  -ma --mqtt-addr Specify the MQTT server address to connect to\n"
			"  -mp --mqtt-port Specify the port of the MQTT server (default 1883)\n"
			"  -mt --mqtt-topic Specify the default topic prefix to handle (default \"light\")\n"
			"  -h --help	   Display this text\n\n"
			"SPI args:\n"
			"  -h --hz HZ	  Change the communication hertz (default 100 000)\n"
			"  -i --id ID	  Change the board ID (default 0x90)\n\n"
			"P9813 args:\n"
			"  --gpiochip DEV  The dev number for /dev/gpiochip* to use (default 0)\n"
			"  -c --clock PIN  The pin ID to use for the clock signal\n"
			"  -d --data PIN   The pin ID to use for the data signal\n"
			"  -n --count NUM  The number of controllers that are chained\n",
			argv[0]);
		return mode == 255 ? -1 : 0;
	}

	memset(&curCol, 0, sizeof(curCol));
	memset(&curHSV, 0, sizeof(curHSV));
	memset(&curTemp, 0, sizeof(curTemp));

	memset(&offCol, 0, sizeof(offCol));

	if (board_write_rgb(&board, &offCol) < 0)
	{
		fprintf(stderr, "Failed to write initial RGB data to board\n");
		return -1;
	}

	running = 1;

	memset(&server, 0, sizeof(server));
	if (args.http.port != 0)
	{
		http_enabled = 1;
		if (http_init(&server, args.http.port) < 0)
		{
			fprintf(stderr, "Failed to open socket, port in use?\n");
			return -1;
		}

		if (pthread_create(&http_thread, NULL, &http_worker, NULL))
		{
			fprintf(stderr, "Failed to start HTTP worker thread.\n");
			return -1;
		}

		server.name = "TheLightMeister v0.1";
		printf("HTTP Server running on http://localhost:%i/\n", server.port);
	}

	if (strlen(args.mqtt.addr) != 0)
	{
		mqtt_enabled = 1;
		mqtt_sendbuf = malloc(2048);
		mqtt_recvbuf = malloc(1024);

		mqtt_init_reconnect(&mqtt, reconnect_callback, NULL, publish_callback);

		if (pthread_create(&mqtt_thread, NULL, &mqtt_worker, NULL))
		{
			fprintf(stderr, "Failed to start MQTT worker thread.\n");
			return -1;
		}
	}

	signal(SIGINT, sigint);

	void* status;
	if (args.http.port != 0)
		pthread_join(http_thread, &status);

	if (strlen(args.mqtt.addr) != 0)
		pthread_join(mqtt_thread, &status);

	sigint(0);
}

void print_rgb()
{
	printf("New color: [ %i, %i, %i ]\n", curCol.r, curCol.g, curCol.b);
}

void print_hsv()
{
	printf("New color: [ %u, %.2f, %.2f ] => [ %i, %i, %i ]\n", curHSV.h, (curHSV.s / 255.f) * 100.f, (curHSV.v / 255.f) * 100.f, curCol.r, curCol.g, curCol.b);
}

void print_temp()
{
	printf("New color: %iK @%i%% => [ %i, %i, %i ]\n", curTemp.k, (int)(curTemp.v * 100), curCol.r, curCol.g, curCol.b);
}

void mqtt_publish_state()
{
	if (mqtt_enabled == 0)
		return;

	struct mqtt_tosend *msg = &mqtt_messages[++mqtt_message_counter % MQTT_QUEUELEN];
	msg->topic = malloc(128);
	msg->message = malloc(4);
	msg->flags = MQTT_PUBLISH_QOS_0 | MQTT_PUBLISH_RETAIN;
	snprintf(msg->topic, 128, "%s/state", args.mqtt.topic);
	snprintf(msg->message, 4, "%s", (lightState == LIGHTSTATE_ON) ? "on" : "off");
	msg->ready = 1;
}
void mqtt_publish_brightness()
{
	if (mqtt_enabled == 0)
		return;

	struct mqtt_tosend *msg = &mqtt_messages[++mqtt_message_counter % MQTT_QUEUELEN];
	msg->topic = malloc(128);
	msg->message = malloc(4);
	msg->flags = MQTT_PUBLISH_QOS_0 | MQTT_PUBLISH_RETAIN;
	snprintf(msg->topic, 128, "%s/brightness", args.mqtt.topic);
	snprintf(msg->message, 4, "%u", curBright);
	msg->ready = 1;
}
void mqtt_publish_temperature(int withBright)
{
	if (mqtt_enabled == 0)
		return;

	struct mqtt_tosend *msg = &mqtt_messages[++mqtt_message_counter % MQTT_QUEUELEN];
	msg->topic = malloc(128);
	msg->message = malloc(6);
	msg->flags = MQTT_PUBLISH_QOS_0 | MQTT_PUBLISH_RETAIN;
	snprintf(msg->topic, 128, "%s/temperature", args.mqtt.topic);
	snprintf(msg->message, 6, "%u", curTemp.k);
	msg->ready = 1;

	if (withBright == 1)
		mqtt_publish_brightness();
}
void mqtt_publish_rgb(int withBright)
{
	if (mqtt_enabled == 0)
		return;

	struct mqtt_tosend *msg = &mqtt_messages[++mqtt_message_counter % MQTT_QUEUELEN];
	msg->topic = malloc(128);
	msg->message = malloc(12);
	msg->flags = MQTT_PUBLISH_QOS_0 | MQTT_PUBLISH_RETAIN;
	snprintf(msg->topic, 128, "%s/rgb", args.mqtt.topic);
	snprintf(msg->message, 12, "%u,%u,%u", curCol.r, curCol.g, curCol.b);
	msg->ready = 1;

	if (withBright == 1)
		mqtt_publish_brightness();
}
void mqtt_publish_color(int withBright)
{
	if (mqtt_enabled == 0)
		return;

	struct mqtt_tosend *msg = &mqtt_messages[++mqtt_message_counter % MQTT_QUEUELEN];
	msg->topic = malloc(128);
	msg->message = malloc(8);
	msg->flags = MQTT_PUBLISH_QOS_0 | MQTT_PUBLISH_RETAIN;
	snprintf(msg->topic, 128, "%s/color", args.mqtt.topic);
	snprintf(msg->message, 8, "%u,%u", curHSV.h, (uint8_t)((curHSV.s / 255.f) * 100));
	msg->ready = 1;

	if (withBright == 1)
		mqtt_publish_brightness();
}

void* http_worker(void* unused)
{
	(void)unused;

	http_req_t client;
	while (running)
	{
		if (http_accept(&server, &client) < 0)
		{
			http_req_close(&client);
			continue;
		}

		printf("New request to %s %s\n", client.method, client.path);

		if (strcmp(client.path, "/light/state") == 0)
		{
			if (strcasecmp(client.method, "GET") == 0)
			{
			}
			else if (strcasecmp(client.method, "PUT") == 0 || strcasecmp(client.method, "POST") == 0)
			{
				if (client.content_length >= 1 || client.query != NULL)
				{
					size_t len = client.content_length;
					char* data = client.content;
					if (client.content_length < 1)
					{
						data = client.query;
						len = strlen(data);
					}

					size_t i;
					for (i = 0; i < len; ++i)
						if (data[i] == '&' || data[i] == '=')
							data[i] = 0;

					int state = -1;
					for (i = 0; i < len;)
					{
						char* cur = &(data[i]);

						i += strlen(cur) + 1;
						if (strcasecmp(cur, "state") == 0)
						{
							char* statestr = &data[i];
							if (strcasecmp(statestr, "on") == 0 || strcmp(statestr, "1") == 0)
								state = LIGHTSTATE_ON;
							else if (strcasecmp(statestr, "off") == 0 || strcmp(statestr, "0") == 0)
								state = LIGHTSTATE_OFF;
						}
					}

					if (state >= 0 && state <= 1 && state != lightState)
					{
						printf("Changing state from %d to %d\n", lightState, state);
						lightState = state;
						if (lightState == LIGHTSTATE_ON)
							board_write_rgb(&board, &curCol);
						else
							board_write_rgb(&board, &offCol);
						mqtt_publish_state();
					}
				}
			}
			else if (strcasecmp(client.method, "DELETE") == 0)
			{
				lightState = LIGHTSTATE_OFF;
				board_write_rgb(&board, &offCol);
				mqtt_publish_state();
			}

			char buf[128];
			http_req_ok(&client, "application/json");
			sprintf(buf, "{\"state\":%i}\n", lightState);
			http_req_send(&client, buf);
		}
		else if (strcmp(client.path, "/light/temperature") == 0)
		{
			if (strcasecmp(client.method, "GET") == 0)
			{
			}
			else if (strcasecmp(client.method, "PUT") == 0 || strcasecmp(client.method, "POST") == 0)
			{
				if (client.content_length >= 3 || client.query != NULL)
				{
					memset(&curCol, 0, sizeof(curCol));
					memset(&curHSV, 0, sizeof(curHSV));

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
						if (strcasecmp(cur, "k") == 0 || strcasecmp(cur, "temp") == 0 || strcasecmp(cur, "temperature") == 0)
							curTemp.k = atoi(&(data[i]));
						else if (strcasecmp(cur, "v") == 0 || strcasecmp(cur, "value") == 0)
							curTemp.v = atof(&(data[i]));
					}

					curBright = (uint8_t)(curTemp.v * 255);
					temperature2rgb(&curTemp, &curCol);

					if (curCol.r > 0 || curCol.g > 0 || curCol.b > 0)
						lightState = LIGHTSTATE_ON;
					else
						lightState = LIGHTSTATE_OFF;

					print_temp();
					board_write_rgb(&board, &curCol);

					mqtt_publish_temperature(1);
					mqtt_publish_state();
				}
			}
			else if (strcasecmp(client.method, "DELETE") == 0)
			{
				lightState = LIGHTSTATE_OFF;

				curBright = 0;
				memset(&curTemp, 0, sizeof(curTemp));
				memset(&curCol, 0, sizeof(curCol));
				board_write_rgb(&board, &curCol);

				mqtt_publish_temperature(1);
				mqtt_publish_state();
			}

			char buf[128];
			http_req_ok(&client, "application/json");
			sprintf(buf, "{\"k\":%u,\"v\":%f}\n", curTemp.k, curTemp.v);
			http_req_send(&client, buf);
		}
		else if (strcmp(client.path, "/light/rgb") == 0)
		{
			if (strcasecmp(client.method, "GET") == 0)
			{
			}
			else if (strcasecmp(client.method, "PUT") == 0 || strcasecmp(client.method, "POST") == 0)
			{
				if (client.content_length >= 3 || client.query != NULL)
				{
					memset(&curHSV, 0, sizeof(curHSV));
					memset(&curTemp, 0, sizeof(curTemp));

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

					if (curCol.r > 0 || curCol.g > 0 || curCol.b > 0)
						lightState = LIGHTSTATE_ON;
					else
						lightState = LIGHTSTATE_OFF;

					curBright = (uint8_t)((int)(curCol.r + curCol.g + curCol.b) / 3);

					print_rgb();
					board_write_rgb(&board, &curCol);

					mqtt_publish_rgb(1);
					mqtt_publish_state();
				}
			}
			else if (strcasecmp(client.method, "DELETE") == 0)
			{
				lightState = LIGHTSTATE_OFF;
				curBright = 0;

				memset(&curCol, 0, sizeof(curCol));
				board_write_rgb(&board, &curCol);

				mqtt_publish_rgb(1);
				mqtt_publish_state();
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
					memset(&curCol, 0, sizeof(curCol));
					memset(&curTemp, 0, sizeof(curTemp));

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
							curHSV.h = (uint16_t)(atoi(data + i));
						else if (strcasecmp(cur, "s") == 0 || strcasecmp(cur, "saturation") == 0)
							curHSV.s = (uint8_t)(atof(data + i) * 255);
						else if (strcasecmp(cur, "v") == 0 || strcasecmp(cur, "value") == 0)
							curHSV.v = (uint8_t)(atof(data + i) * 255);
					}

					if (curHSV.h > 360)
						curHSV.h = 360;

					curBright = curHSV.v;
					hsv2rgb(&curHSV, &curCol);

					if (curCol.r > 0 || curCol.g > 0 || curCol.b > 0)
						lightState = LIGHTSTATE_ON;
					else
						lightState = LIGHTSTATE_OFF;

					print_hsv();
					board_write_rgb(&board, &curCol);

					mqtt_publish_color(1);
					mqtt_publish_state();
				}
			}
			else if (strcasecmp(client.method, "DELETE") == 0)
			{
				lightState = LIGHTSTATE_OFF;
				curBright = 0;

				memset(&curHSV, 0, sizeof(curHSV));
				memset(&curCol, 0, sizeof(curCol));
				board_write_rgb(&board, &curCol);

				mqtt_publish_color(1);
				mqtt_publish_state();
			}
			else
			{
				http_req_not_implemented(&client);
				http_req_close(&client);
				continue;
			}

			char buf[128];
			http_req_ok(&client, "application/json");
			sprintf(buf, "{\"h\":%.2f,\"s\":%.2f,\"v\":%.2f}\n", (curHSV.h / 255.f) * 360.f, curHSV.s / 255.f, curHSV.v / 255.f);
			http_req_send(&client, buf);
		}
		else
			http_req_not_found(&client);

		http_req_close(&client);
	}

	http_close(&server);

	return NULL;
}

void publish_callback(void** unused, struct mqtt_response_publish *published)
{
	(void)unused;

	char* topic_name = (char*) malloc(published->topic_name_size + 1);
	memcpy(topic_name, published->topic_name, published->topic_name_size);
	topic_name[published->topic_name_size] = '\0';

	char* tmpdata = (char*) malloc(published->application_message_size + 1);
	memcpy(tmpdata, published->application_message, published->application_message_size);
	tmpdata[published->application_message_size] = '\0';

	printf("Received %zuB publish \"%s\" to %s.\n", published->application_message_size, tmpdata, topic_name);

	if (strncmp(topic_name, args.mqtt.topic, strlen(args.mqtt.topic)) == 0)
	{
		char* subtopic_name = topic_name + strlen(args.mqtt.topic) + 1;

		if (strcmp(subtopic_name, "state/set") == 0)
		{
			int state = -1;
			const char* statestr = (const char*)published->application_message;

			if ((published->application_message_size == 2 && strncasecmp(statestr, "on", 2) == 0) || (published->application_message_size == 1 && strncmp(statestr, "1", 1) == 0))
				state = LIGHTSTATE_ON;
			else if ((published->application_message_size == 3 && strncasecmp(statestr, "off", 3) == 0) || (published->application_message_size == 1 && strncmp(statestr, "0", 1) == 0))
				state = LIGHTSTATE_OFF;

			if (state >= 0 && state <= 1 && state != lightState)
			{
				printf("Changing state from %d to %d\n", lightState, state);
				lightState = state;
				if (lightState == LIGHTSTATE_ON)
					board_write_rgb(&board, &curCol);
				else
					board_write_rgb(&board, &offCol);
			}

			mqtt_publish_state();
		}
		else if (strcmp(subtopic_name, "temperature/set") == 0)
		{
			memset(&curCol, 0, sizeof(curCol));
			memset(&curHSV, 0, sizeof(curHSV));

			curTemp.k = atoi(tmpdata);
			curTemp.v = curBright;
			temperature2rgb(&curTemp, &curCol);

			int oldstate = lightState;
			if (curCol.r > 0 || curCol.g > 0 || curCol.b > 0)
				lightState = LIGHTSTATE_ON;
			else
				lightState = LIGHTSTATE_OFF;

			print_temp();
			board_write_rgb(&board, &curCol);

			mqtt_publish_temperature(0);
			if (oldstate != lightState)
				mqtt_publish_state();
		}
		else if (strcmp(subtopic_name, "color/set") == 0)
		{
			memset(&curCol, 0, sizeof(curCol));
			memset(&curTemp, 0, sizeof(curTemp));

			size_t len = published->application_message_size;
			char *data = tmpdata;

			size_t i;
			for (i = 0; i < len; ++i)
				if (data[i] == ',')
					data[i] = 0;

			uint8_t segment = 0;
			for (i = 0; i < len && segment < 2;)
			{
				char* cur = &(data[i]);

				i += strlen(cur) + 1;
				if (segment == 0)
					curHSV.h = (uint16_t)atof(cur);
				else if (segment == 1)
					curHSV.s = (uint8_t)((atof(cur) / 100.f) * 255);
				segment++;
			}

			curHSV.v = curBright;
			hsv2rgb(&curHSV, &curCol);

			int oldstate = lightState;
			if (curCol.r > 0 || curCol.g > 0 || curCol.b > 0)
				lightState = LIGHTSTATE_ON;
			else
				lightState = LIGHTSTATE_OFF;

			print_hsv();
			board_write_rgb(&board, &curCol);

			mqtt_publish_color(0);
			if (oldstate != lightState)
				mqtt_publish_state();
		}
		else if (strcmp(subtopic_name, "brightness/set") == 0)
		{
			memset(&curCol, 0, sizeof(curCol));
			memset(&curTemp, 0, sizeof(curTemp));

			curBright = (uint8_t)atoi(tmpdata);
			curHSV.v = curBright;

			hsv2rgb(&curHSV, &curCol);

			int oldstate = lightState;
			if (curCol.r > 0 || curCol.g > 0 || curCol.b > 0)
				lightState = LIGHTSTATE_ON;
			else
				lightState = LIGHTSTATE_OFF;

			print_hsv();
			board_write_rgb(&board, &curCol);

			mqtt_publish_brightness();
			if (oldstate != lightState)
				mqtt_publish_state();
		}
		else if (strcmp(subtopic_name, "rgb/set") == 0)
		{
			memset(&curHSV, 0, sizeof(curHSV));
			memset(&curTemp, 0, sizeof(curTemp));

			size_t len = published->application_message_size;
			char *data = tmpdata;

			size_t i;
			for (i = 0; i < len; ++i)
				if (data[i] == ',')
					data[i] = 0;

			uint8_t segment = 0;
			for (i = 0; i < len && segment < 3;)
			{
				char* cur = &(data[i]);

				i += strlen(cur) + 1;
				if (segment == 0)
					curCol.r = (uint8_t)((atoi(cur) / 255.f) * curBright);
				else if (segment == 1)
					curCol.g = (uint8_t)((atoi(cur) / 255.f) * curBright);
				else if (segment == 2)
					curCol.b = (uint8_t)((atoi(cur) / 255.f) * curBright);
				segment++;
			}

			int oldstate = lightState;
			if (curCol.r > 0 || curCol.g > 0 || curCol.b > 0)
				lightState = LIGHTSTATE_ON;
			else
				lightState = LIGHTSTATE_OFF;

			// Store RGB as HSV, to support changing brightness
			rgb2hsv(&curCol, &curHSV);
			curHSV.v = curBright;

			print_rgb();
			board_write_rgb(&board, &curCol);

			mqtt_publish_rgb(0);
			if (oldstate != lightState)
				mqtt_publish_state();
		}
	}

	free(tmpdata);
	free(topic_name);
}

void reconnect_callback(mqtt_t* unused, void **unused2)
{
	(void)unused;
	(void)unused2;

	if (mqtt.error != MQTT_ERROR_INITIAL_RECONNECT)
	{
		close(mqtt.socketfd);
		fprintf(stderr, "Reconnecting MQTT in error state %s.\n", mqtt_error_str(mqtt.error));
	}

	mqtt_enabled = 1;
	int fd = open_nb_socket(args.mqtt.addr, args.mqtt.port);
	if (fd == -1) {
		fprintf(stderr, "Failed to open mqtt socket\n");
		running = 0;
		return;
	}

	mqtt_reinit(&mqtt, fd, mqtt_sendbuf, 2048, mqtt_recvbuf, 1024);

	// TODO Authed sessions
	const char* client_id = "thelightmeister";

	printf("Connecting to MQTT broker...\n");
	uint8_t connect_flags = MQTT_CONNECT_CLEAN_SESSION;
	mqtt_connect(&mqtt, client_id, NULL, NULL, 0, NULL, NULL, connect_flags, 400);

	if (mqtt.error != MQTT_OK)
	{
		fprintf(stderr, "Failed to connect to MQTT (%s)\n", mqtt_error_str(mqtt.error));
		running = 0;
		return;
	}

	printf("Subscribing to topics...\n");
	char topic[128];
	sprintf(topic, "%s/state/set", args.mqtt.topic);
	mqtt_subscribe(&mqtt, topic, 0);
	sprintf(topic, "%s/temperature/set", args.mqtt.topic);
	mqtt_subscribe(&mqtt, topic, 0);
	sprintf(topic, "%s/color/set", args.mqtt.topic);
	mqtt_subscribe(&mqtt, topic, 0);
	sprintf(topic, "%s/brightness/set", args.mqtt.topic);
	mqtt_subscribe(&mqtt, topic, 0);
	sprintf(topic, "%s/rgb/set", args.mqtt.topic);
	mqtt_subscribe(&mqtt, topic, 0);

	if (strlen(args.mqtt.publish) > 0)
	{
		snprintf(topic, 128, "%s/light/%s/config", args.mqtt.publish, args.mqtt.slug);

		char data[512];
		int len = snprintf(data, 512, "{"
			"\"name\":\"%s\","
			"\"~\":\"%s/\","
			"\"stat_t\":\"~state\","
			"\"pl_on\":\"on\","
			"\"pl_off\":\"off\","
			"\"cmd_t\":\"~state/set\","
			"\"bri_stat_t\":\"~brightness\","
			"\"bri_cmd_t\":\"~brightness/set\","
			"\"hs_stat_t\":\"~color\","
			"\"hs_cmd_t\":\"~color/set\","
			"\"ret\":true"
			"}", args.mqtt.name, args.mqtt.topic);

		printf("Publishing %dB of configuration information to %s.\n", len, topic);
		mqtt_publish(&mqtt, topic, data, len, MQTT_PUBLISH_QOS_0 | MQTT_PUBLISH_RETAIN);
	}

	printf("MQTT connected and handling %s/*.\n", args.mqtt.topic);
}

void* mqtt_worker(void* unused)
{
	(void)unused;

	while (running)
	{
		mqtt_sync(&mqtt);
		usleep(100000U);

		for (int i = 0; i < MQTT_QUEUELEN; ++i)
		{
			if (mqtt_messages[i].ready == 1)
			{
				printf("Publishing %s to MQTT topic %s\n", mqtt_messages[i].message, mqtt_messages[i].topic);
				mqtt_publish(&mqtt, mqtt_messages[i].topic, mqtt_messages[i].message, strlen(mqtt_messages[i].message), mqtt_messages[i].flags);

				free(mqtt_messages[i].topic);
				free(mqtt_messages[i].message);

				mqtt_messages[i].topic = NULL;
				mqtt_messages[i].message = NULL;
				mqtt_messages[i].flags = 0;
				mqtt_messages[i].ready = 0;
			}
		}
	}

	return NULL;
}
