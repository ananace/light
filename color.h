#ifndef _LIGHT_H
#define _LIGHT_H

struct _hsv_t
{
	unsigned char h, s, v;
};
typedef struct _hsv_t hsv_t;

struct _rgb_t
{
	unsigned char r, g, b;
};
typedef struct _rgb_t rgb_t;

int temperature2rgb(unsigned int temp, rgb_t* rgb);
int hsv2rgb(const hsv_t*, rgb_t*);
int rgb2hsv(const rgb_t*, hsv_t*);

#endif
