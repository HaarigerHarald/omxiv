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

#ifndef IMAGEDEF_H
#define IMAGEDEF_H

#include <stdint.h>

#define destroyImage(im) {free((im)->pData); (im)->pData = NULL;}

/* Color spaces OMX-Components support */
#define COLOR_SPACE_RGB24 0
#define COLOR_SPACE_RGBA 1
#define COLOR_SPACE_YUV420P 2
#define COLOR_SPACE_RGB16 3

typedef struct IMAGE{
	uint8_t* pData;	/* Image pixel data */
	size_t nData;	/* Alloc data length */
	
	unsigned int width;
	unsigned int height;
	unsigned char colorSpace; 
} IMAGE;

typedef struct ANIM_IMAGE{
	IMAGE *curFrame;
	IMAGE *frames;
	unsigned int decodeCount;
	unsigned int frameNum;
	
	uint8_t* imData;
	size_t size;
	
	void* pExtraData;
	int (*decodeNextFrame)(struct ANIM_IMAGE *);
	void (*finaliseDecoding)(struct ANIM_IMAGE *);
	
	unsigned int frameCount;
	unsigned int frameDelayCs;
	int loopCount;

} ANIM_IMAGE;

#endif
