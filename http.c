#include "http.h"

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

int parse_request(int, http_req_t*);
int get_line(int, char*, size_t);

int http_init(http_t* http, uint16_t port)
{
	int fd;

	struct sockaddr_in sock_info;
	if ((fd = socket(PF_INET, SOCK_STREAM, 0)) < 0)
		return -1;

	memset(&sock_info, 0, sizeof(sock_info));
	sock_info.sin_family = AF_INET;
	sock_info.sin_port = htons(port);
	sock_info.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(fd, (struct sockaddr*)&sock_info, sizeof(sock_info)) < 0)
		return -2;

	uint16_t real_port = ntohs(sock_info.sin_port);
	if (port == 0)
	{
		socklen_t len = sizeof(sock_info);
		if (getsockname(fd, (struct sockaddr*)&sock_info, &len) < 0)
			return -3;

		real_port = ntohs(sock_info.sin_port);
	}

	if (listen(fd, 5) < 0)
		return -4;

	http->socket = fd;
	http->port = real_port;

	return 0;
}
int http_accept(const http_t* http, http_req_t* req)
{
	struct sockaddr_in client;
	socklen_t client_len = sizeof(client);
	memset(req, 0, sizeof(http_req_t));

	int fd = accept(http->socket, (struct sockaddr*)&client, &client_len);

	if (fd < 0)
		return -1;

	if (parse_request(fd, req) < 0)
		return -1;

	req->source = http;
	req->socket = fd;

	return 0;
}
void http_close(const http_t* http)
{
	close(http->socket);
}

void http_req_send(const http_req_t* req, const char* data)
{
	send(req->socket, data, strlen(data), 0);
}
void http_req_close(const http_req_t* req)
{
	if (req->socket >= 0)
		close(req->socket);

	if (req->method != NULL)
		free(req->method);
	if (req->path != NULL)
		free(req->path);
	if (req->query != NULL)
		free(req->query);
	if (req->content_type != NULL)
		free(req->content_type);
	if (req->content != NULL)
		free(req->content);
}

void http_req_ok(const http_req_t* req, const char* type)
{
	char buf[1024];

	strcpy(buf, "HTTP/1.0 200 OK\r\n");
	http_req_send(req, buf);
	sprintf(buf, "%s\r\n", req->source->name);
	http_req_send(req, buf);
	sprintf(buf, "Content-Type: %s\r\n", type);
	http_req_send(req, buf);
	strcpy(buf, "\r\n");
	http_req_send(req, buf);
}
void http_req_not_found(const http_req_t* req)
{
	char buf[1024];

	strcpy(buf, "HTTP/1.0 404 NOT FOUND\r\n");
	http_req_send(req, buf);
	sprintf(buf, "%s\r\n", req->source->name);
	http_req_send(req, buf);
	strcpy(buf, "Content-Type: text/html\r\n");
	http_req_send(req, buf);
	strcpy(buf, "\r\n");
	http_req_send(req, buf);
	strcpy(buf, "<html><head><title>Not Found</title></head>\r\n");
	http_req_send(req, buf);
	strcpy(buf, "<body><p>The server could not fulfill\r\n");
	http_req_send(req, buf);
	strcpy(buf, "your request because the resource specified\r\n");
	http_req_send(req, buf);
	strcpy(buf, "is unavailable or nonexistant.</p></body></html>\r\n");
	http_req_send(req, buf);
}
void http_req_not_implemented(const http_req_t* req)
{
	char buf[1024];

	strcpy(buf, "HTTP/1.0 501 METHOD NOT IMPLEMENTED\r\n");
	http_req_send(req, buf);
	sprintf(buf, "%s\r\n", req->source->name);
	http_req_send(req, buf);
	strcpy(buf, "Content-Type: text/html\r\n");
	http_req_send(req, buf);
	strcpy(buf, "\r\n");
	http_req_send(req, buf);
	strcpy(buf, "<html><head><title>Method Not Implemented</title></head>\r\n");
	http_req_send(req, buf);
	strcpy(buf, "<body><p>The server does not recognize the requested Method</p></body></html>\r\n");
	http_req_send(req, buf);
}

int parse_request(int fd, http_req_t* req)
{
	char buf[1024];
	char method[255];
	char url[255];
	char* query = NULL;
	size_t i = 0, j = 0;

	int chars = get_line(fd, buf, sizeof(buf));

	// Read HTTP method
	while (!isspace((int)buf[j]) && i < sizeof(method) - 1 && j < sizeof(buf))
		method[i++] = buf[j++];
	method[i] = 0;

	// Skip whitespace
	while (isspace((int)buf[j]) && j < sizeof(buf))
		++j;

	// Read request URI
	i = 0;
	while (!isspace((int)buf[j]) && i < sizeof(url) - 1 && j < sizeof(buf))
		url[i++] = buf[j++];
	url[i] = 0;

	// Parse out a query string
	query = url;
	while (*query != '?' && *query != 0)
		++query;
	if (*query == '?')
		*query++ = 0;
	else
		query = NULL;

	req->method = malloc(strlen(method) + 1);
	strcpy(req->method, method);
	req->path = malloc(strlen(url) + 1);
	strcpy(req->path, url);

	if (query != NULL)
	{
		req->query = malloc(strlen(query) + 1);
		strcpy(req->query, query);
	}
	else
		req->query = NULL;

	char content_type[255];
	memset(content_type, 0, 255);
	size_t content_length = 0;

	do
	{
		chars = get_line(fd, buf, sizeof(buf));
		if (chars <= 0)
			break;

		char old = buf[15];
		buf[15] = 0;
		if (strcasecmp(buf, "Content-Length:") == 0)
			content_length = atoi(&(buf[16]));
		buf[15] = old;

		old = buf[13];
		buf[13] = 0;
		if (strcasecmp(buf, "Content-Type:") == 0)
			strcpy(content_type, &(buf[14]));
		buf[13] = old;
	}
	while (chars > 0 && strcmp("\n", buf));

	req->content_length = content_length;

	if (content_type[0] != 0)
	{
		req->content_type = malloc(strlen(content_type));
		strncpy(req->content_type, content_type, strlen(content_type) - 1);

		char* content_buffer = malloc(content_length);
		chars = recv(fd, content_buffer, content_length, 0);

		req->content = content_buffer;
	}

	return 0;
}

int get_line(int fd, char* buf, size_t len)
{
	int i = 0;
	char c = '\0';
	int n;

	while ((i < len - 1) && (c != '\n'))
	{
		n = recv(fd, &c, 1, 0);

		if (n > 0)
		{
			if (c == '\r')
			{
				n = recv(fd, &c, 1, MSG_PEEK);

				if (n > 0 && c == '\n')
					recv(fd, &c, 1, 0);
				else
					c = '\n';
			}

			buf[i++] = c;
		}
		else
			c = '\n';
	}

	buf[i] = 0;

	return i;
}

