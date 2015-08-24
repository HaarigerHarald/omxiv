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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jpeglib.h>
#include <png.h>

#include "soft_image.h"
#include "libnsbmp/libnsbmp.h"
#include "libnsgif/libnsgif.h"

#define ALIGN16(x) (((x+0xf)>>4)<<4)

struct my_error_mgr {
	struct jpeg_error_mgr pub;
	jmp_buf setjmp_buffer;
};

typedef struct my_error_mgr * my_error_ptr;

METHODDEF(void) my_error_exit (j_common_ptr cinfo) {
	my_error_ptr myerr = (my_error_ptr) cinfo->err;
	longjmp(myerr->setjmp_buffer, 1);
}


int readJpegHeader(FILE *infile, JPEG_INFO *jpegInfo){
	struct jpeg_decompress_struct cinfo;
	
	struct my_error_mgr jerr;

	if(infile == NULL){
		return SOFT_IMAGE_ERROR_FILE_OPEN; 
	}

	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;
	if (setjmp(jerr.setjmp_buffer)) {
		jpeg_destroy_decompress(&cinfo);
		return SOFT_JPEG_ERROR_DECODING;
	}

	jpeg_create_decompress(&cinfo);
	jpeg_stdio_src(&cinfo, infile);
	jpeg_read_header(&cinfo, TRUE);
	
	if(cinfo.progressive_mode)
		jpegInfo->mode = JPEG_MODE_PROGRESSIVE;
	else
		jpegInfo->mode = JPEG_MODE_NON_PROGRESSIVE;
		
	jpegInfo->nColorComponents = cinfo.num_components;
	
	jpeg_destroy_decompress(&cinfo);
	
	return SOFT_JPEG_OK;
}

int softDecodeJpeg(FILE *infile, IMAGE *jpeg){
	struct jpeg_decompress_struct cinfo;
	struct my_error_mgr jerr;
	JSAMPARRAY buffer;
	int rowStride;
	
	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;
	if (setjmp(jerr.setjmp_buffer)) {
		jpeg_destroy_decompress(&cinfo);
		return SOFT_JPEG_ERROR_DECODING;
	}
	
	jpeg_create_decompress(&cinfo);
	jpeg_stdio_src(&cinfo, infile);
	jpeg_read_header(&cinfo, TRUE);
	
	cinfo.out_color_space = JCS_RGB;
	
	jpeg_start_decompress(&cinfo);
	
	rowStride = cinfo.output_width * cinfo.output_components;
	
	jpeg->width = cinfo.output_width;
	
	/* Stride memory needs to be a multiple of 16, 
	 * otherwise resize and render component will bug. */
	unsigned int stride = ALIGN16(jpeg->width)*4;
	
	jpeg->height = cinfo.output_height;
	jpeg->colorSpace = COLOR_SPACE_RGBA;
	
	buffer = (*cinfo.mem->alloc_sarray)
		((j_common_ptr) &cinfo, JPOOL_IMAGE, rowStride, 1);
	size_t i, x,y;
	
	jpeg->nData = stride * cinfo.output_height;
	jpeg->pData = malloc(jpeg->nData);
	if(jpeg->pData == NULL){
		jpeg_finish_decompress(&cinfo);
		jpeg_destroy_decompress(&cinfo);
		return SOFT_JPEG_ERROR_MEMORY;
	}
	
	
	size_t rBytes= cinfo.output_width*4;
	
	// Copy and convert from RGB to RGBA
	for(i=0; cinfo.output_scanline < cinfo.output_height; i+=stride) {
		jpeg_read_scanlines(&cinfo, buffer, 1);
		for(x = 0,y=0; x < rBytes; x+=4, y+=3){
			jpeg->pData[i+x+3]=255;
			memcpy(jpeg->pData+i+x, buffer[0]+y, 3);
		}
	}
	
	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	
	if(jerr.pub.num_warnings != 0){
		return SOFT_JPEG_ERROR_CORRUPT_DATA;
	}
	
	return SOFT_JPEG_OK;
}

/**
 * Modified from https://gist.github.com/niw/5963798
 * Copyright (C) Guillaume Cottenceau, Yoshimasa Niwa
 * Distributed under the MIT License.
**/
int softDecodePng(FILE *fp, IMAGE* png){
	png_byte header[8];
	
	png_structp png_ptr;
	png_infop info_ptr;

	if (!fp)
		return SOFT_IMAGE_ERROR_FILE_OPEN;
	fread(header, 1, 8, fp);
	if (png_sig_cmp(header, 0, 8)){
		return SOFT_IMAGE_ERROR_FILE_OPEN;
	}
	
	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

	if (!png_ptr){
		return SOFT_PNG_ERROR_CREATE_STRUCT;
	}
	
	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr){
		return SOFT_PNG_ERROR_CREATE_STRUCT;
	}

	if (setjmp(png_jmpbuf(png_ptr))){
		return SOFT_PNG_ERROR_INIT;
	}
	
	png_init_io(png_ptr, fp);
	png_set_sig_bytes(png_ptr, 8);
	
	png_read_info(png_ptr, info_ptr);
	
	png_byte color_type = png_get_color_type(png_ptr, info_ptr);
	png_byte bit_depth	= png_get_bit_depth(png_ptr, info_ptr);
	
	if(bit_depth == 16)
		png_set_strip_16(png_ptr);
 
	if(color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb(png_ptr);
	
	if(color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
		png_set_expand_gray_1_2_4_to_8(png_ptr);
	
	if(png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(png_ptr);
	
	if(color_type == PNG_COLOR_TYPE_RGB ||
		color_type == PNG_COLOR_TYPE_GRAY ||
		color_type == PNG_COLOR_TYPE_PALETTE){
		
		png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);
	}
	
	if(color_type == PNG_COLOR_TYPE_GRAY ||
		color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb(png_ptr);
	
	png_read_update_info(png_ptr, info_ptr);
	
	png->width = png_get_image_width(png_ptr, info_ptr);
	
	/* Stride memory needs to be a multiple of 16, 
	 * otherwise resize and render component will bug. */	
	unsigned int stride = ALIGN16(png->width)*4;
	
	
	png->height = png_get_image_height(png_ptr, info_ptr);
	
	png->colorSpace = COLOR_SPACE_RGBA;
	
	if (setjmp(png_jmpbuf(png_ptr))){
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		return SOFT_PNG_ERROR_DECODING;	
	}
		
	png->nData = ALIGN16(png->height)*stride;
	png->pData = malloc(png->nData);
	if(!png->pData){
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		return SOFT_PNG_ERROR_MEMORY;
	}
	
	png_bytep row_pointers[png->height];
	size_t i;
	for (i=0; i < png->height; i++) {
		row_pointers[i] = (png_bytep) png->pData + i*(stride);
	}
	
	png_read_image(png_ptr, row_pointers);
	
	png_read_end(png_ptr, NULL);
	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
	
	return SOFT_PNG_OK;
}

// BMP

static void *bmp_init(int width, int height, unsigned int state){
	return malloc(width * height * 4);
}


static unsigned char *bmp_get_buffer(void *bitmap){
	return bitmap;
}


static size_t bmp_get_bpp(void *bitmap){
	return 4;
}

int softDecodeBMP(FILE *fp, IMAGE* bmpImage, unsigned char** data, size_t size){
	bmp_bitmap_callback_vt bitmap_callbacks = {
		bmp_init,
		NULL,
		bmp_get_buffer,
		bmp_get_bpp
	};
	bmp_result code;
	bmp_image bmp;
	short ret = 0;
	
	if(!fp){
		return SOFT_IMAGE_ERROR_FILE_OPEN;
	}
	
	if(data == NULL || *data == NULL){
		
		fseek(fp, 0L, SEEK_END);
		size = ftell(fp);
		fseek(fp, 0L, SEEK_SET);

		*data = malloc(size);
		if (!*data) {
			return SOFT_BMP_ERROR_MEMORY;
		}

		if (fread(*data, 1, size, fp) != size) {
			free(*data);
			*data = NULL;
			return SOFT_BMP_ERROR_MEMORY;
		}
	}
	
	bmp_create(&bmp, &bitmap_callbacks);

	code = bmp_analyse(&bmp, size, *data);
	if (code != BMP_OK) {
		ret = SOFT_BMP_ERROR_ANALYSING;
		goto cleanup;
	}

	code = bmp_decode(&bmp);
	if (code != BMP_OK) {
		ret = SOFT_BMP_ERROR_DECODING;
		goto cleanup;
	}
	
	char* bmpData = bmp.bitmap;
	int bmpWidth = bmp.width;
	
	bmpImage->height = bmp.height;
	bmpImage->width=bmpWidth;
	bmpImage->colorSpace = COLOR_SPACE_RGBA;
		
	bmp_finalise(&bmp);
	free(*data);
	*data = NULL;
	
	/* Stride memory needs to be a multiple of 16, 
	 * otherwise resize and render component will bug. */
	unsigned int stride= ALIGN16(bmpWidth) *4;
	
	bmpImage->nData = stride* ALIGN16(bmpImage->height);
	bmpImage->pData = malloc(bmpImage->nData);
	if(!bmpImage->pData){
		free(bmpData);
		return SOFT_BMP_ERROR_MEMORY;
	}
	int i;
	for(i=0; i<bmpImage->height; i++){
		memcpy(bmpImage->pData + i* stride, 
			bmpData +i* bmpWidth*4, bmpWidth*4);
	}
	
	free(bmpData);

	return ret;
	
cleanup:
	bmp_finalise(&bmp);
	free(*data);
	*data = NULL;
	return ret;
}

// GIF

static void *gif_init(int width, int height){
	return malloc(width * height * 4);
}

static unsigned char *gif_get_buffer(void *bitmap){
	return bitmap;
}

static void gif_destroy(void *bitmap){
	free(bitmap);
}

void destroyAnimImage(ANIM_IMAGE *animIm){
	gif_animation *gif = (gif_animation*) animIm->pExtraData;
	gif_finalise(gif);
	
	if(animIm->curFrame){
		destroyImage(animIm->curFrame);
	}
	free(animIm->imData);
	free(animIm->pExtraData);
	memset(animIm, 0, sizeof(ANIM_IMAGE));
}

static int decodeNextGifFrame(ANIM_IMAGE *gifImage){
	
	if(!gifImage->imData || !gifImage->curFrame->pData){
		return SOFT_GIF_ERROR_MEMORY;
	}
	int ret;
	
	gif_animation *gif = (gif_animation*) gifImage->pExtraData;
	gif_result code;
	
	gifImage->frameNum++;
	gifImage->frameNum%=gifImage->frameCount;
	
	unsigned int stride = ALIGN16(gif->width)*4;
	gifImage->frameDelayCs = gif->frames[gifImage->frameNum].frame_delay;
	
	code = gif_decode_frame(gif, gifImage->frameNum);
	if (code != GIF_OK){
		ret = SOFT_GIF_ERROR_DECODING;
		goto cleanup;
	}
	
	unsigned int n;
	for(n=0; n<gif->height; n++){
		memcpy(gifImage->curFrame->pData + n* stride, 
			gif->frame_image +n* gif->width*4, 
			gif->width*4);
	}
	
	return SOFT_GIF_OK;
	
cleanup:
	destroyAnimImage(gifImage);
	return ret;
	
}

int softDecodeGif(FILE *fp, ANIM_IMAGE *gifImage, IMAGE *frame, unsigned char** data, size_t size){
	gif_bitmap_callback_vt bitmap_callbacks = {
		gif_init,
		gif_destroy,
		gif_get_buffer,
		NULL,
		NULL,
		NULL
	};
	
	int ret;
	gif_animation* gif = malloc(sizeof(gif_animation));
	if(!gif)
		return SOFT_GIF_ERROR_MEMORY;
	gif_result code;
	gifImage->pExtraData = gif;
	gifImage->curFrame = frame;
	gifImage->frameCount = 0;
	
	if (!fp) {
		return SOFT_IMAGE_ERROR_FILE_OPEN;
	}
	
	if(data == NULL || *data == NULL){
		
		fseek(fp, 0L, SEEK_END);
		size = ftell(fp);
		fseek(fp, 0L, SEEK_SET);

		*data = malloc(size);
		if (!*data) {
			return SOFT_GIF_ERROR_MEMORY;
		}

		if (fread(*data, 1, size, fp) != size) {
			free(*data);
			*data = NULL;
			return SOFT_GIF_ERROR_MEMORY;
		}
	}
	
	gifImage->size = size;
	gifImage->imData = *data;
	
	gif_create(gif, &bitmap_callbacks);
	gifImage->decodeNextFrame = decodeNextGifFrame;
	gifImage->finaliseDecoding = destroyAnimImage;

	do {
		code = gif_initialise(gif, size, *data);
		if (code != GIF_OK && code != GIF_WORKING){
			ret = SOFT_GIF_ERROR_ANALYSING;
			goto cleanup;
		}
	} while (code != GIF_OK);
	
	gifImage->frameCount = gif->frame_count;
	gifImage->loopCount = gif->loop_count;
	
	unsigned int stride = ALIGN16(gif->width)*4;
	
	size_t nData = stride* ALIGN16(gif->height);
	
	gifImage->curFrame->pData = malloc(nData);
	if(!gifImage->curFrame->pData){
		ret = SOFT_GIF_ERROR_MEMORY;
		goto cleanup;
	}
	
	gifImage->curFrame->nData = nData;
	gifImage->curFrame->width = gif->width;
	gifImage->curFrame->height = gif->height;
	gifImage->curFrame->colorSpace = COLOR_SPACE_RGBA;
	gifImage->frameNum = 0;
	gifImage->frameDelayCs = gif->frames[gifImage->frameNum].frame_delay;

	code = gif_decode_frame(gif, gifImage->frameNum);
	if (code != GIF_OK){
		ret = SOFT_GIF_ERROR_DECODING;
		goto cleanup;
	}
	
	unsigned int n;
	for(n=0; n<gif->height; n++){
		memcpy(gifImage->curFrame->pData + n* stride, 
			gif->frame_image +n* gif->width*4, 
			gif->width*4);
	}
	
	*data = NULL;
	return SOFT_GIF_OK;
	
cleanup:
	destroyAnimImage(gifImage);
	*data = NULL;
	return ret;
}

#ifdef USE_LIBCURL

/**
 * Modified from: http://curl.haxx.se/libcurl/c/getinmemory.html 
 * Copyright (C) 1998 - 2015, Daniel Stenberg, <daniel@haxx.se>, et al.
 * Distributed under: http://curl.haxx.se/docs/copyright.html
**/

#include <curl/curl.h>

struct MemoryStruct {
	unsigned char *memory;
	size_t size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
	size_t realsize = size * nmemb;
	struct MemoryStruct *mem = (struct MemoryStruct *)userp;

	mem->memory = realloc(mem->memory, mem->size + realsize);
	if(!mem->memory) {
		return 0;
	}

	memcpy(&(mem->memory[mem->size]), contents, realsize);
	mem->size += realsize;

	return realsize;
}

unsigned char* getImageFromUrl(const char *url, size_t *size){
	CURL *curl_handle;
	CURLcode res;

	struct MemoryStruct chunk;

	chunk.memory = NULL;
	chunk.size = 0;

	curl_global_init(CURL_GLOBAL_ALL);

	curl_handle = curl_easy_init();

	curl_easy_setopt(curl_handle, CURLOPT_URL, url);

	curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);

	curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);

	curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

	res = curl_easy_perform(curl_handle);

	if(res != CURLE_OK) {
		free(chunk.memory);
		chunk.memory=NULL;
		chunk.size = 0;
		printf("libCurl returned error code %d\n", res);
	}
	
	*size= chunk.size;

	curl_easy_cleanup(curl_handle);
	curl_global_cleanup();
	
	return chunk.memory;
}
#endif	