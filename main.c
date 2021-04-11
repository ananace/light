#include "board.h"
#include "gpio.h"
#include "http.h"
#include "mqtt.h"
#include <strings.h>
typedef struct mqtt_client mqtt_t;

#include "posix_sockets.h"

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

rgb_t offCol;
int lightState = LIGHTSTATE_OFF;

uint8_t *mqtt_sendbuf = NULL,
	*mqtt_recvbuf = NULL;

#define MQTT_QUEUELEN 4
struct mqtt_tosend
{
	char* topic;
	char* message;
	int flags;
};
struct mqtt_tosend mqtt_messages[MQTT_QUEUELEN];
int mqtt_message_counter = 0;

int http_enabled = 0, mqtt_enabled = 0;
pthread_t http_thread, mqtt_thread;

void publish_callback(void** unused, struct mqtt_response_publish *published);

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

	if (mqtt_sendbuf != NULL)
		free(mqtt_sendbuf);
	if (mqtt_recvbuf != NULL)
		free(mqtt_recvbuf);

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
	if (strlen(args.mqtt.topic) == 0) args.mqtt.topic = "light";

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
			"  -S --spi        Use SPI connected BitWizard board\n"
			"  -P --p9813      Use P9813 board\n"
			"  -D --dummy      Use dummy board for testing\n"
			"\n"
			"  -p --port PORT  Specify the HTTP server port to use\n"
			"  -ma --mqtt-addr Specify the MQTT server address to connect to\n"
			"  -mp --mqtt-port Specify the port of the MQTT server (default 1883)\n"
			"  -mt --mqtt-topic Specify the default topic prefix to handle (default \"light\")\n"
			"  -h --help       Display this text\n\n"
			"SPI args:\n"
			"  -h --hz HZ      Change the communication hertz (default 100 000)\n"
			"  -i --id ID      Change the board ID (default 0x90)\n\n"
			"P9813 args:\n"
			"  --gpiochip DEV  The dev number for /dev/gpiochip* to use (default 0)\n"
			"  -c --clock PIN  The pin ID to use for the clock signal\n"
			"  -d --data PIN   The pin ID to use for the data signal\n"
			"  -n --count NUM  The number of controllers that are chained\n",
			argv[0]);
		return mode == 255 ? -1 : 0;
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
		int fd = open_nb_socket(args.mqtt.addr, args.mqtt.port);
		if (fd == -1) {
			fprintf(stderr, "Failed to open mqtt socket\n");
			return -1;
		}

		mqtt_sendbuf = malloc(2048);
		mqtt_recvbuf = malloc(1024);

		mqtt_init(&mqtt, fd, mqtt_sendbuf, 2048, mqtt_recvbuf, 1024, publish_callback);

		// TODO Authed sessions
		const char* client_id = "thelightmeister";

		printf("Connecting to MQTT broker...\n");
		uint8_t connect_flags = MQTT_CONNECT_CLEAN_SESSION;
		mqtt_connect(&mqtt, client_id, NULL, NULL, 0, NULL, NULL, connect_flags, 400);

		if (mqtt.error != MQTT_OK)
		{
			fprintf(stderr, "Failed to connect to MQTT (%s)\n", mqtt_error_str(mqtt.error));
			return -1;
		}

		printf("Subscribing to topics...\n");
		char topic[128];
		sprintf(topic, "%s/state/set", args.mqtt.topic);
		mqtt_subscribe(&mqtt, topic, 0);
		sprintf(topic, "%s/temperature/set", args.mqtt.topic);
		mqtt_subscribe(&mqtt, topic, 0);
		sprintf(topic, "%s/hs/set", args.mqtt.topic);
		mqtt_subscribe(&mqtt, topic, 0);
		sprintf(topic, "%s/v/set", args.mqtt.topic);
		mqtt_subscribe(&mqtt, topic, 0);
		sprintf(topic, "%s/hsv/set", args.mqtt.topic);
		mqtt_subscribe(&mqtt, topic, 0);
		sprintf(topic, "%s/rgb/set", args.mqtt.topic);
		mqtt_subscribe(&mqtt, topic, 0);

		if (pthread_create(&mqtt_thread, NULL, &mqtt_worker, NULL))
		{
			fprintf(stderr, "Failed to start MQTT worker thread.\n");
			return -1;
		}

		printf("MQTT online and handling %s/*.\n", args.mqtt.topic);
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

	signal(SIGINT, sigint);

	void* status;
	if (args.http.port != 0)
		pthread_join(http_thread, &status);

	if (strlen(args.mqtt.addr) != 0)
		pthread_join(mqtt_thread, &status);

	sigint(0);
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

						if (mqtt_enabled != 0)
						{
							char topic[128];
							char message[128];
							size_t len;

							sprintf(topic, "%s/state", args.mqtt.topic);
							len = sprintf(message, "{\"state\":%d}", lightState);
							mqtt_publish(&mqtt, topic, message, len, MQTT_PUBLISH_QOS_0);
						}
					}
				}
			}
			else if (strcasecmp(client.method, "DELETE") == 0)
			{
				lightState = LIGHTSTATE_OFF;
				board_write_rgb(&board, &offCol);

				if (mqtt_enabled != 0)
				{
					char topic[128];
					const char *msg = "{\"state\":0}";

					sprintf(topic, "%s/state", args.mqtt.topic);
					mqtt_publish(&mqtt, topic, msg, strlen(msg), MQTT_PUBLISH_QOS_0);
				}
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

					temperature2rgb(&curTemp, &curCol);

					if (curCol.r > 0 || curCol.g > 0 || curCol.b > 0)
						lightState = LIGHTSTATE_ON;
					else
						lightState = LIGHTSTATE_OFF;

					printf("New color: %iK @%i%% => [ %i, %i, %i ]\n", curTemp.k, (int)(curTemp.v * 100), curCol.r, curCol.g, curCol.b);
					board_write_rgb(&board, &curCol);

					if (mqtt_enabled != 0)
					{
						char topic[128];
						char msg[128];
						size_t len;

						sprintf(topic, "%s/temperature", args.mqtt.topic);
						len = sprintf(msg, "{\"k\":%d,\"v\":%f}", curTemp.k, curTemp.v);
						mqtt_publish(&mqtt, topic, msg, len, MQTT_PUBLISH_QOS_0);

						sprintf(topic, "%s/state", args.mqtt.topic);
						len = sprintf(msg, "{\"state\":%d}", lightState);
						mqtt_publish(&mqtt, topic, msg, len, MQTT_PUBLISH_QOS_0);
					}
				}
			}
			else if (strcasecmp(client.method, "DELETE") == 0)
			{
				lightState = LIGHTSTATE_OFF;

				memset(&curTemp, 0, sizeof(curTemp));
				memset(&curCol, 0, sizeof(curCol));
				board_write_rgb(&board, &curCol);

				if (mqtt_enabled != 0)
				{
					char topic[128];
					sprintf(topic, "%s/temperature", args.mqtt.topic);
					mqtt_publish(&mqtt, topic, "{\"k\":0,\"v\":0}", 13, MQTT_PUBLISH_QOS_0);
					sprintf(topic, "%s/state", args.mqtt.topic);
					mqtt_publish(&mqtt, topic, "{\"state\":0}", 11, MQTT_PUBLISH_QOS_0);
				}
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

					printf("New color: [ %i, %i, %i ]\n", curCol.r, curCol.g, curCol.b);
					board_write_rgb(&board, &curCol);

					if (mqtt_enabled != 0)
					{
						char topic[128];
						char msg[128];
						size_t len;

						sprintf(topic, "%s/rgb", args.mqtt.topic);
						len = sprintf(msg, "{\"r\": %d, \"g\": %d, \"b\": %d}", curCol.r, curCol.g, curCol.b);
						mqtt_publish(&mqtt, topic, msg, len, MQTT_PUBLISH_QOS_0);

						sprintf(topic, "%s/state", args.mqtt.topic);
						len = sprintf(msg, "{\"state\":%d}", lightState);
						mqtt_publish(&mqtt, topic, msg, len, MQTT_PUBLISH_QOS_0);
					}
				}
			}
			else if (strcasecmp(client.method, "DELETE") == 0)
			{
				lightState = LIGHTSTATE_OFF;

				memset(&curCol, 0, sizeof(curCol));
				board_write_rgb(&board, &curCol);

				if (mqtt_enabled != 0)
				{
					char topic[128];
					sprintf(topic, "%s/rgb", args.mqtt.topic);
					mqtt_publish(&mqtt, topic, "{\"r\":0,\"g\":0,\"b\":0}", 19, MQTT_PUBLISH_QOS_0);
					sprintf(topic, "%s/state", args.mqtt.topic);
					mqtt_publish(&mqtt, topic, "{\"state\":0}", 11, MQTT_PUBLISH_QOS_0);
				}
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
							curHSV.h = (uint8_t)((atof(data + i) / 360.f) * 255);
						else if (strcasecmp(cur, "s") == 0 || strcasecmp(cur, "saturation") == 0)
							curHSV.s = (uint8_t)(atof(data + i) * 255);
						else if (strcasecmp(cur, "v") == 0 || strcasecmp(cur, "value") == 0)
							curHSV.v = (uint8_t)(atof(data + i) * 255);
					}

					hsv2rgb(&curHSV, &curCol);

					if (curCol.r > 0 || curCol.g > 0 || curCol.b > 0)
						lightState = LIGHTSTATE_ON;
					else
						lightState = LIGHTSTATE_OFF;

					printf("New color: [ %.2f, %.2f, %.2f ] => [ %i, %i, %i ]\n", (curHSV.h / 255.f) * 360.f, curHSV.s / 255.f, curHSV.v / 255.f, curCol.r, curCol.g, curCol.b);
					board_write_rgb(&board, &curCol);

					if (mqtt_enabled != 0)
					{
						char topic[128];
						char msg[128];
						size_t len;

						sprintf(topic, "%s/hsv", args.mqtt.topic);
						len = sprintf(msg, "{\"h\":%f,\"s\":%f,\"v\":%f}", (curHSV.h / 255.f) * 360.f, curHSV.s / 255.f, curHSV.v / 255.f);
						mqtt_publish(&mqtt, topic, msg, len, MQTT_PUBLISH_QOS_0);

						sprintf(topic, "%s/state", args.mqtt.topic);
						len = sprintf(msg, "{\"state\":%d}", lightState);
						mqtt_publish(&mqtt, topic, msg, len, MQTT_PUBLISH_QOS_0);
					}
				}
			}
			else if (strcasecmp(client.method, "DELETE") == 0)
			{
				lightState = LIGHTSTATE_OFF;

				memset(&curHSV, 0, sizeof(curHSV));
				memset(&curCol, 0, sizeof(curCol));
				board_write_rgb(&board, &curCol);

				if (mqtt_enabled != 0)
				{
					char topic[128];
					sprintf(topic, "%s/hsv", args.mqtt.topic);
					mqtt_publish(&mqtt, topic, "{\"h\":0,\"s\":0,\"v\":0}", 19, MQTT_PUBLISH_QOS_0);
					sprintf(topic, "%s/state", args.mqtt.topic);
					mqtt_publish(&mqtt, topic, "{\"state\":0}", 11, MQTT_PUBLISH_QOS_0);
				}
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

	printf("Received %luB publish to %s.\n", published->application_message_size, topic_name);

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

				struct mqtt_tosend *msg = &mqtt_messages[++mqtt_message_counter % MQTT_QUEUELEN];
				msg->topic = malloc(128);
				msg->message = malloc(128);
				msg->flags = MQTT_PUBLISH_QOS_0 | MQTT_PUBLISH_RETAIN;
				sprintf(msg->topic, "%s/state", args.mqtt.topic);
				sprintf(msg->message, "{\"state\":%d}", lightState);
			}
		}
		else if (strcmp(subtopic_name, "temperature/set") == 0)
		{
			memset(&curCol, 0, sizeof(curCol));
			memset(&curHSV, 0, sizeof(curHSV));

			size_t len = published->application_message_size;
			char *data = malloc(len + 1);
			memcpy(data, published->application_message, len);

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

			free(data);

			temperature2rgb(&curTemp, &curCol);

			if (curCol.r > 0 || curCol.g > 0 || curCol.b > 0)
				lightState = LIGHTSTATE_ON;
			else
				lightState = LIGHTSTATE_OFF;

			printf("New color: %iK @%i%% => [ %i, %i, %i ]\n", curTemp.k, (int)(curTemp.v * 100), curCol.r, curCol.g, curCol.b);
			board_write_rgb(&board, &curCol);

			struct mqtt_tosend *msg = &mqtt_messages[++mqtt_message_counter % MQTT_QUEUELEN];
			msg->topic = malloc(128);
			msg->message = malloc(128);
			msg->flags = MQTT_PUBLISH_QOS_0;
			sprintf(msg->topic, "%s/temperature", args.mqtt.topic);
			sprintf(msg->message, "{\"k\":%d,\"v\":%f}", curTemp.k, curTemp.v);

			msg = &mqtt_messages[++mqtt_message_counter % MQTT_QUEUELEN];
			msg->topic = malloc(128);
			msg->message = malloc(128);
			msg->flags = MQTT_PUBLISH_QOS_0 | MQTT_PUBLISH_RETAIN;
			sprintf(msg->topic, "%s/state", args.mqtt.topic);
			sprintf(msg->message, "{\"state\":%d}", lightState);
		}
		else if (strcmp(subtopic_name, "hs/set") == 0)
		{
			memset(&curCol, 0, sizeof(curCol));
			memset(&curTemp, 0, sizeof(curTemp));

			size_t len = published->application_message_size;
			char *data = malloc(len + 1);
			memcpy(data, published->application_message, len);

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
					curHSV.h = (uint8_t)((atof(cur) / 360.f) * 255);
				else if (segment == 1)
					curHSV.s = (uint8_t)((atof(cur) / 100.f) * 255);
				segment++;
			}

			free(data);

			hsv2rgb(&curHSV, &curCol);

			if (curCol.r > 0 || curCol.g > 0 || curCol.b > 0)
				lightState = LIGHTSTATE_ON;
			else
				lightState = LIGHTSTATE_OFF;

			printf("New color: [ %.2f, %.2f, %.2f ] => [ %i, %i, %i ]\n", (curHSV.h / 255.f) * 360.f, curHSV.s / 255.f, curHSV.v / 255.f, curCol.r, curCol.g, curCol.b);
			board_write_rgb(&board, &curCol);

			struct mqtt_tosend *msg = &mqtt_messages[++mqtt_message_counter % MQTT_QUEUELEN];
			msg->topic = malloc(128);
			msg->message = malloc(128);
			msg->flags = MQTT_PUBLISH_QOS_0;
			sprintf(msg->topic, "%s/hsv", args.mqtt.topic);
			sprintf(msg->message, "{\"h\":%f,\"s\":%f,\"v\":%f}", (curHSV.h / 255.f) * 360.f, curHSV.s / 255.f, curHSV.v / 255.f);

			msg = &mqtt_messages[++mqtt_message_counter % MQTT_QUEUELEN];
			msg->topic = malloc(128);
			msg->message = malloc(128);
			msg->flags = MQTT_PUBLISH_QOS_0 | MQTT_PUBLISH_RETAIN;
			sprintf(msg->topic, "%s/state", args.mqtt.topic);
			sprintf(msg->message, "{\"state\":%d}", lightState);
		}
		else if (strcmp(subtopic_name, "v/set") == 0)
		{
			memset(&curCol, 0, sizeof(curCol));
			memset(&curTemp, 0, sizeof(curTemp));

			curHSV.v = (uint8_t)atoi((const char*)published->application_message);

			hsv2rgb(&curHSV, &curCol);

			if (curCol.r > 0 || curCol.g > 0 || curCol.b > 0)
				lightState = LIGHTSTATE_ON;
			else
				lightState = LIGHTSTATE_OFF;

			printf("New color: [ %.2f, %.2f, %.2f ] => [ %i, %i, %i ]\n", (curHSV.h / 255.f) * 360.f, curHSV.s / 255.f, curHSV.v / 255.f, curCol.r, curCol.g, curCol.b);
			board_write_rgb(&board, &curCol);

			struct mqtt_tosend *msg = &mqtt_messages[++mqtt_message_counter % MQTT_QUEUELEN];
			msg->topic = malloc(128);
			msg->message = malloc(128);
			msg->flags = MQTT_PUBLISH_QOS_0;
			sprintf(msg->topic, "%s/hsv", args.mqtt.topic);
			sprintf(msg->message, "{\"h\":%f,\"s\":%f,\"v\":%f}", (curHSV.h / 255.f) * 360.f, curHSV.s / 255.f, curHSV.v / 255.f);

			msg = &mqtt_messages[++mqtt_message_counter % MQTT_QUEUELEN];
			msg->topic = malloc(128);
			msg->message = malloc(128);
			msg->flags = MQTT_PUBLISH_QOS_0 | MQTT_PUBLISH_RETAIN;
			sprintf(msg->topic, "%s/state", args.mqtt.topic);
			sprintf(msg->message, "{\"state\":%d}", lightState);
		}
		else if (strcmp(subtopic_name, "hsv/set") == 0)
		{
			memset(&curCol, 0, sizeof(curCol));
			memset(&curTemp, 0, sizeof(curTemp));

			size_t len = published->application_message_size;
			char *data = malloc(len + 1);
			memcpy(data, published->application_message, len);

			size_t i;
			for (i = 0; i < len; ++i)
				if (data[i] == '&' || data[i] == '=')
					data[i] = 0;

			for (i = 0; i < len;)
			{
				char* cur = &(data[i]);

				i += strlen(cur) + 1;
				if (strcasecmp(cur, "h") == 0 || strcasecmp(cur, "hue") == 0)
					curHSV.h = (uint8_t)((atof(data + i) / 360.f) * 255);
				else if (strcasecmp(cur, "s") == 0 || strcasecmp(cur, "saturation") == 0)
					curHSV.s = (uint8_t)(atof(data + i) * 255);
				else if (strcasecmp(cur, "v") == 0 || strcasecmp(cur, "value") == 0)
					curHSV.v = (uint8_t)(atof(data + i) * 255);
			}

			free(data);

			hsv2rgb(&curHSV, &curCol);

			if (curCol.r > 0 || curCol.g > 0 || curCol.b > 0)
				lightState = LIGHTSTATE_ON;
			else
				lightState = LIGHTSTATE_OFF;

			printf("New color: [ %.2f, %.2f, %.2f ] => [ %i, %i, %i ]\n", (curHSV.h / 255.f) * 360.f, curHSV.s / 255.f, curHSV.v / 255.f, curCol.r, curCol.g, curCol.b);
			board_write_rgb(&board, &curCol);

			struct mqtt_tosend *msg = &mqtt_messages[++mqtt_message_counter % MQTT_QUEUELEN];
			msg->topic = malloc(128);
			msg->message = malloc(128);
			msg->flags = MQTT_PUBLISH_QOS_0;
			sprintf(msg->topic, "%s/hsv", args.mqtt.topic);
			sprintf(msg->message, "{\"h\":%f,\"s\":%f,\"v\":%f}", (curHSV.h / 255.f) * 360.f, curHSV.s / 255.f, curHSV.v / 255.f);

			msg = &mqtt_messages[++mqtt_message_counter % MQTT_QUEUELEN];
			msg->topic = malloc(128);
			msg->message = malloc(128);
			msg->flags = MQTT_PUBLISH_QOS_0 | MQTT_PUBLISH_RETAIN;
			sprintf(msg->topic, "%s/state", args.mqtt.topic);
			sprintf(msg->message, "{\"state\":%d}", lightState);
		}
		else if (strcmp(subtopic_name, "rgb/set") == 0)
		{
			memset(&curHSV, 0, sizeof(curHSV));
			memset(&curTemp, 0, sizeof(curTemp));

			size_t len = published->application_message_size;
			char *data = malloc(len + 1);
			memcpy(data, published->application_message, len);

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

			free(data);

			if (curCol.r > 0 || curCol.g > 0 || curCol.b > 0)
				lightState = LIGHTSTATE_ON;
			else
				lightState = LIGHTSTATE_OFF;

			printf("New color: [ %i, %i, %i ]\n", curCol.r, curCol.g, curCol.b);
			board_write_rgb(&board, &curCol);

			struct mqtt_tosend *msg = &mqtt_messages[++mqtt_message_counter % MQTT_QUEUELEN];
			msg->topic = malloc(128);
			msg->message = malloc(128);
			msg->flags = MQTT_PUBLISH_QOS_0;
			sprintf(msg->topic, "%s/rgb", args.mqtt.topic);
			sprintf(msg->message, "{\"r\":%d,\"g\":%d,\"b\":%d}", curCol.r, curCol.g, curCol.b);

			msg = &mqtt_messages[++mqtt_message_counter % MQTT_QUEUELEN];
			msg->topic = malloc(128);
			msg->message = malloc(128);
			msg->flags = MQTT_PUBLISH_QOS_0 | MQTT_PUBLISH_RETAIN;
			sprintf(msg->topic, "%s/state", args.mqtt.topic);
			sprintf(msg->message, "{\"state\":%d}", lightState);
		}
	}

	free(topic_name);
}

void* mqtt_worker(void* unused)
{
	(void)unused;

	while (running)
	{
		mqtt_sync(&mqtt);
		usleep(100000U);

		for (int i = 0; i < 4; ++i)
		{
			if (mqtt_messages[i].topic != NULL && mqtt_messages[i].message != NULL)
			{
				printf("Publishing %s to MQTT topic %s\n", mqtt_messages[i].message, mqtt_messages[i].topic);
				mqtt_publish(&mqtt, mqtt_messages[i].topic, mqtt_messages[i].message, strlen(mqtt_messages[i].message), mqtt_messages[i].flags);

				free(mqtt_messages[i].topic);
				free(mqtt_messages[i].message);

				mqtt_messages[i].topic = NULL;
				mqtt_messages[i].message = NULL;
				mqtt_messages[i].flags = 0;
			}
		}
	}

	return NULL;
}
