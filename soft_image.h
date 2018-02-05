/* Copyright (c) 2015, Benjamin Huber
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *    * Neither the name of the copyright holder nor the
 *      names of its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef SOFTIMAGE_H
#define SOFTIMAGE_H

#include "image_def.h"

void destroyAnimIm(ANIM_IMAGE *animIm);

#define SOFT_IMAGE_OK 0x0
#define SOFT_IMAGE_ERROR_MEMORY 0x1
#define SOFT_IMAGE_ERROR_FILE_OPEN 0x02
#define SOFT_IMAGE_ERROR_DECODING 0x04
#define SOFT_IMAGE_ERROR_CORRUPT_DATA 0x08
#define SOFT_IMAGE_ERROR_INIT 0x10
#define SOFT_IMAGE_ERROR_CREATE_STRUCT 0x20
#define SOFT_IMAGE_ERROR_ANALYSING 0x40

#define JPEG_MODE_PROGRESSIVE 0
#define JPEG_MODE_NON_PROGRESSIVE 1

typedef struct JPEG_INFO {
	int nColorComponents; // number of color components 
	int mode; // progressive or non progressive 
	char orientation; // orientation according to exif tag (1..8, default: 1)
} JPEG_INFO;

int readJpegHeader(FILE *jpegFile, JPEG_INFO *jpegInfo);
int softDecodeJpeg(FILE *jpegFile, IMAGE *jpeg);

int softDecodePng(FILE *pngFile, IMAGE* png);

int softDecodeTIFF(FILE *fp, IMAGE* im);
void unloadLibTiff();

int softDecodeBMP(FILE *fp, IMAGE* bmpImage, unsigned char** data, size_t size);

int softDecodeGif(FILE *fp, ANIM_IMAGE *gifImage, unsigned char** data, size_t size);


/* Get Image from Url */
unsigned char* getImageFromUrl(const char *url, size_t *size);
void unloadLibCurl();

#endif
