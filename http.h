#ifndef _HTTP_H
#define _HTTP_H

#include <stdint.h>

struct _http_t
{
	int socket;
	uint16_t port;

	char* name;
};
typedef struct _http_t http_t;

struct _http_req_t
{
	const http_t* source;

	int socket;
	char* method;
	char* path;
	char* query;

	uint32_t content_length;
	char* content_type;
	char* content;
};
typedef struct _http_req_t http_req_t;

int http_init(http_t*, uint16_t);
int http_accept(const http_t*, http_req_t*);
void http_close(const http_t*);

void http_req_send(const http_req_t*, const char* data);
void http_req_close(const http_req_t*);

void http_req_ok(const http_req_t*, const char*);
void http_req_not_found(const http_req_t*);
void http_req_not_implemented(const http_req_t*);

#endif
