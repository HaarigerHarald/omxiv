#include "ImageDef.h"

#define SOFT_IMAGE_ERROR_FILE_OPEN 0x02

#define SOFT_JPEG_OK 0x0
#define SOFT_JPEG_ERROR_MEMORY 0x1
#define SOFT_JPEG_ERROR_DECODING 0x04
#define SOFT_JPEG_ERROR_CORRUPT_DATA 0x08

#define JPEG_MODE_PROGRESSIVE 0
#define JPEG_MODE_NON_PROGRESSIVE 1

typedef struct JPEG_INFO {
	int nColorComponents;
	int mode;
} JPEG_INFO;

int readJpegHeader(char *filePath, JPEG_INFO *jpegInfo);
int softDecodeJpeg(char *filePath, IMAGE *jpeg);

#define SOFT_PNG_OK 0x0
#define SOFT_PNG_ERROR_MEMORY 0x1
#define SOFT_PNG_ERROR_DECODING 0x04
#define SOFT_PNG_ERROR_INIT 0x08
#define SOFT_PNG_ERROR_CREATE_STRUCT 0x10

int softDecodePng(char* filePath, IMAGE* png); 
