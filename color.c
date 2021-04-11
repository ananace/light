#include "color.h"

#include <math.h>

#define CLAMP(a, min, max) (a < min ? min : (a > max ? max : a))

int temperature2rgb(const temp_t *temp, rgb_t* rgb)
{
	int temperature = CLAMP(temp->k, 1000, 40000);

	float calc;
	unsigned short frac = temperature / 100;
	if (frac <= 66)
		rgb->r = 255;
	else
	{
		calc = frac - 60;
		calc = 329.698727446f * fmod(calc, -0.1332047592f);
		rgb->r = CLAMP(calc, 0, 255);
	}

	if (frac <= 66)
	{
		calc = frac;
		calc = 99.4708025861f * log(calc) - 161.1195681661f;
		rgb->g = CLAMP(calc, 0, 255);
	}
	else
	{
		calc = frac - 60;
		calc = 288.1221695283f * fmod(calc, -0.0755148492f);
		rgb->g = CLAMP(calc, 0, 255);
	}

	if (frac >= 66)
		rgb->b = 255;
	else if (frac <= 19)
		rgb->b = 0;
	else
	{
		calc = frac - 10;
		calc = 138.5177312231f * log(calc) - 305.0447927307f;
		rgb->b = CLAMP(calc, 0, 255);
	}

	rgb->r *= temp->v;
	rgb->g *= temp->v;
	rgb->b *= temp->v;

	return 0;
}

int hsv2rgb(const hsv_t* hsv, rgb_t* rgb)
{
        if (hsv->h > 360)
            return -1;

	unsigned short region, remainder;
	unsigned char p, q, t;

	if (hsv->s == 0)
	{
		rgb->r = hsv->v;
		rgb->g = hsv->v;
		rgb->b = hsv->v;
		return 0;
	}

	region = (hsv->h / 60) % 6;
	remainder = (hsv->h % 60) * 4;

	p = (hsv->v * (255 - hsv->s)) >> 8;
	q = (hsv->v * (255 - ((hsv->s * remainder) >> 8))) >> 8;
	t = (hsv->v * (255 - ((hsv->s * (255 - remainder)) >> 8))) >> 8;

	switch (region)
	{
		case 0:
			rgb->r = hsv->v; rgb->g = t; rgb->b = p;
			break;

		case 1:
			rgb->r = q; rgb->g = hsv->v; rgb->b = p;
			break;

		case 2:
			rgb->r = p; rgb->g = hsv->v; rgb->b = t;
			break;

		case 3:
			rgb->r = p; rgb->g = q; rgb->b = hsv->v;
			break;

		case 4:
			rgb->r = t; rgb->g = p; rgb->b = hsv->v;
			break;

		default:
			rgb->r = hsv->v; rgb->g = p; rgb->b = q;
			break;
	}

	return 0;
}

int rgb2hsv(const rgb_t* rgb, hsv_t* hsv)
{
	unsigned char rgbMin, rgbMax;

	rgbMin = rgb->r < rgb->g ? (rgb->r < rgb->b ? rgb->r : rgb->b) : (rgb->g < rgb->b ? rgb->g : rgb->b);
	rgbMax = rgb->r > rgb->g ? (rgb->r > rgb->b ? rgb->r : rgb->b) : (rgb->g > rgb->b ? rgb->g : rgb->b);

	hsv->v = rgbMax;
	if (rgbMax == 0 || rgbMax == rgbMin)
	{
		hsv->h = 0;
		hsv->s = 0;
		return 0;
	}

	hsv->s = 255 * (long)(rgbMax - rgbMin) / hsv->v;
	if (hsv->s == 0)
	{
		hsv->h = 0;
		return 0;
	}

	if (rgbMax == rgb->r)
		hsv->h = 0 + 43 * (rgb->g - rgb->b) / (rgbMax - rgbMin);
	else if (rgbMax == rgb->g)
		hsv->h = 85 + 43 * (rgb->b - rgb->r) / (rgbMax - rgbMin);
	else
		hsv->h = 171 + 43 * (rgb->r - rgb->g) / (rgbMax - rgbMin);

	return 0;
}
