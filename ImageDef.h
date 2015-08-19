#ifndef IMAGEDEF_H
#define IMAGEDEF_H

/* Color spaces OMX-Components support */
#define COLOR_SPACE_RGB24 0
#define COLOR_SPACE_RGBA 1
#define COLOR_SPACE_YUV420_PACKED 2
#define COLOR_SPACE_YUV420_PACKED_SEMI 3
#define COLOR_SPACE_RGB16 4

typedef struct IMAGE{
	char *pData;	/* Image pixel data */
	size_t nData;	/* Alloc data length (=stride*height) */
	
	unsigned int width;
	unsigned int height;
	char colorSpace; 
} IMAGE;

#endif