#ifndef _LIGHT_H
#define _LIGHT_H

struct _hsv_t
{
	unsigned short h;
	unsigned char s, v;
};
typedef struct _hsv_t hsv_t;

struct _rgb_t
{
	unsigned char r, g, b;
};
typedef struct _rgb_t rgb_t;

struct _temp_t
{
	unsigned int k;
	float v;
};
typedef struct _temp_t temp_t;

int temperature2rgb(const temp_t*, rgb_t*);
int hsv2rgb(const hsv_t*, rgb_t*);
int rgb2hsv(const rgb_t*, hsv_t*);

#endif
