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
#include <dlfcn.h>

#include <jpeglib.h>
#include <png.h>

#include "soft_image.h"
#include "libnsbmp/libnsbmp.h"
#include "libnsgif/libnsgif.h"

#define ALIGN16(x) (((x+0xf)>>4)<<4)

#define MIN_FRAME_DELAY_CS 2
#define BUMP_UP_FRAME_DELAY_CS 10

static const char magExif[] = {0x45, 0x78, 0x69, 0x66, 0x00, 0x00};

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
		return SOFT_IMAGE_ERROR_DECODING;
	}

	jpeg_create_decompress(&cinfo);
	jpeg_stdio_src(&cinfo, infile);
	jpeg_save_markers(&cinfo, JPEG_APP0+1, 0xffff);
	jpeg_read_header(&cinfo, TRUE);
	
	if(cinfo.progressive_mode)
		jpegInfo->mode = JPEG_MODE_PROGRESSIVE;
	else
		jpegInfo->mode = JPEG_MODE_NON_PROGRESSIVE;
		
	jpegInfo->nColorComponents = cinfo.num_components;
	
	
	// read EXIF orientation
	// losely base on: http://sylvana.net/jpegcrop/jpegexiforient.c
	jpegInfo->orientation = 1; // Default
	jpeg_saved_marker_ptr pMarker = cinfo.marker_list;
	
	if(pMarker != NULL && pMarker->data_length >= 20 && 
		memcmp(pMarker->data, magExif, sizeof(magExif)) == 0) {
			
		unsigned int exifLen = pMarker->data_length;
		uint8_t* exifData = pMarker->data;
		short motorola;
		
		// byte order 
		if(exifData[6] == 0x49 && exifData[7] == 0x49)
			motorola = 0;
		else if(exifData[6] == 0x4D && exifData[7] == 0x4D)
			motorola = 1;
		else
			goto cleanExit;

		if (motorola) {
			if(exifData[8] != 0 || exifData[9] != 0x2A) 
				goto cleanExit;
		} else {
			if(exifData[9] != 0 || exifData[8] != 0x2A) 
				goto cleanExit;
		}
		
		unsigned int offset;
		// read offset to IFD0
		if(motorola) {
			if (exifData[10] != 0 || exifData[11] != 0)
				goto cleanExit;
			offset = (exifData[12]<<8) + exifData[13] + 6;
		} else {
			if (exifData[12] != 0 || exifData[13] != 0)
				goto cleanExit;
			offset = (exifData[11]<<8) + exifData[10] + 6;
		}
		if(offset > exifLen - 14)
			goto cleanExit;
		
		unsigned int nTags;
		
		// read number of tags in IFD0
		if(motorola)
			nTags = (exifData[offset]<<8) + exifData[offset+1];
		else 
			nTags = (exifData[offset+1]<<8) + exifData[offset];

		offset += 2;

		while(1) {
			if (nTags-- == 0 || offset > exifLen - 12)
				goto cleanExit;
			
			unsigned int tag;
			if (motorola)
				tag = (exifData[offset]<<8) + exifData[offset+1];
			else 
				tag = (exifData[offset+1]<<8) + exifData[offset];
				
			if (tag == 0x0112) break; // orientation tag found
			
			offset += 12;
		}
		
		unsigned char orientation = 9;
		
		if (motorola && exifData[offset+8] == 0) {
			orientation = exifData[offset+9];
		} else if(exifData[offset+9] == 0) {
			orientation = exifData[offset+8];
		}
		
		if (orientation <= 8 && orientation != 0)
			jpegInfo->orientation = orientation;
	}
	
cleanExit:	
	
	jpeg_destroy_decompress(&cinfo);
	
	return SOFT_IMAGE_OK;
}

int softDecodeJpeg(FILE *infile, IMAGE *jpeg){
	struct jpeg_decompress_struct cinfo;
	struct my_error_mgr jerr;
	JSAMPARRAY buffer;
	unsigned int rowStride;
	
	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;
	if (setjmp(jerr.setjmp_buffer)) {
		jpeg_destroy_decompress(&cinfo);
		return SOFT_IMAGE_ERROR_DECODING;
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
	
	jpeg->nData = stride * ALIGN16(cinfo.output_height);
	jpeg->pData = malloc(jpeg->nData);
	if(jpeg->pData == NULL){
		jpeg_finish_decompress(&cinfo);
		jpeg_destroy_decompress(&cinfo);
		return SOFT_IMAGE_ERROR_MEMORY;
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
		return SOFT_IMAGE_ERROR_CORRUPT_DATA;
	}
	
	return SOFT_IMAGE_OK;
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
		return SOFT_IMAGE_ERROR_CREATE_STRUCT;
	}
	
	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr){
		return SOFT_IMAGE_ERROR_CREATE_STRUCT;
	}

	if (setjmp(png_jmpbuf(png_ptr))){
		return SOFT_IMAGE_ERROR_INIT;
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
		return SOFT_IMAGE_ERROR_DECODING;	
	}
		
	png->nData = ALIGN16(png->height)*stride;
	png->pData = malloc(png->nData);
	if(!png->pData){
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		return SOFT_IMAGE_ERROR_MEMORY;
	}
	
	png_bytep row_pointers[png->height];
	size_t i;
	for (i=0; i < png->height; i++) {
		row_pointers[i] = (png_bytep) png->pData + i*(stride);
	}
	
	png_read_image(png_ptr, row_pointers);
	
	png_read_end(png_ptr, NULL);
	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
	
	return SOFT_IMAGE_OK;
}

// BMP

static void *bmp_init(int width, int height, unsigned int state){
	return malloc(ALIGN16(width) * ALIGN16(height) * 4);
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
			return SOFT_IMAGE_ERROR_MEMORY;
		}

		if (fread(*data, 1, size, fp) != size) {
			free(*data);
			*data = NULL;
			return SOFT_IMAGE_ERROR_MEMORY;
		}
	}
	
	bmp_create(&bmp, &bitmap_callbacks);

	code = bmp_analyse(&bmp, size, *data);
	if (code != BMP_OK) {
		ret = SOFT_IMAGE_ERROR_ANALYSING;
		goto cleanup;
	}

	code = bmp_decode(&bmp);
	if (code != BMP_OK) {
		ret = SOFT_IMAGE_ERROR_DECODING;
		goto cleanup;
	}
	
	uint8_t* bmpData = bmp.bitmap;
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

	bmpImage->pData = bmpData;
	
	unsigned int pixWidth = bmpWidth*4;
	unsigned int i;
	for(i=bmpImage->height; --i > 0 ;){
		memmove(bmpImage->pData + i* stride, 
			bmpData +i* pixWidth, pixWidth);
	}

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
	if(animIm->frames){
		unsigned int i;
		for(i=0; i < animIm->frameCount; i++){
			destroyImage(&animIm->frames[i]);
		}
		free(animIm->frames);
		animIm->frames = NULL;
	}else if(animIm->curFrame){
		destroyImage(animIm->curFrame);
	}
	free(animIm->imData);
	free(animIm->pExtraData);
	memset(animIm, 0, sizeof(ANIM_IMAGE));
}

static int decodeNextGifFrame(ANIM_IMAGE *gifImage){
	
	if(!gifImage->imData || !gifImage->curFrame->pData){
		return SOFT_IMAGE_ERROR_MEMORY;
	}
	int ret;
	
	gif_animation *gif = (gif_animation*) gifImage->pExtraData;
	gif_result code;
	
	gifImage->frameNum++;
	gifImage->frameNum%=gifImage->frameCount;
	
	unsigned int stride = ALIGN16(gif->width)*4;
	if(gif->frames[gifImage->frameNum].frame_delay < MIN_FRAME_DELAY_CS)
		gifImage->frameDelayCs = BUMP_UP_FRAME_DELAY_CS;
	else
		gifImage->frameDelayCs = gif->frames[gifImage->frameNum].frame_delay;
	
	if(gifImage->frames){
		gifImage->curFrame = &(gifImage->frames[gifImage->frameNum]);
	}
	if(gifImage->frames == NULL || gifImage->decodeCount < gifImage->frameCount){
		code = gif_decode_frame(gif, gifImage->frameNum);
		if (code != GIF_OK){
			ret = SOFT_IMAGE_ERROR_DECODING;
			goto cleanup;
		}
		
		unsigned int pixWidth = gif->width*4;
		unsigned int n, i, gifSize = pixWidth*gif->height;
		for(n=0, i=0; n<gifSize; n+=pixWidth, i+=stride){
			memcpy(gifImage->curFrame->pData + i, 
				gif->frame_image +n, pixWidth);
		}
		
		if(gifImage->frames)
			gifImage->decodeCount++;
	}
	
	return SOFT_IMAGE_OK;
	
cleanup:
	destroyAnimImage(gifImage);
	return ret;
	
}

int softDecodeGif(FILE *fp, ANIM_IMAGE *gifImage, unsigned char** data, size_t size){
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
		return SOFT_IMAGE_ERROR_MEMORY;
	gif_result code;
	gifImage->pExtraData = gif;
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
			return SOFT_IMAGE_ERROR_MEMORY;
		}

		if (fread(*data, 1, size, fp) != size) {
			free(*data);
			*data = NULL;
			return SOFT_IMAGE_ERROR_MEMORY;
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
			ret = SOFT_IMAGE_ERROR_ANALYSING;
			goto cleanup;
		}
	} while (code != GIF_OK);
	
	gifImage->frameCount = gif->frame_count;
	gifImage->loopCount = gif->loop_count;
	
	unsigned int stride = ALIGN16(gif->width)*4;
	
	size_t nData = stride* ALIGN16(gif->height);
	
	unsigned int i = 0;
	if(gifImage->frameCount > 1){
		gifImage->frames = malloc(gifImage->frameCount * sizeof(IMAGE));
		if(!gifImage->frames){
			ret = SOFT_IMAGE_ERROR_MEMORY;
			goto cleanup;
		}
		
		for(; i<gifImage->frameCount; i++){
			gifImage->frames[i].pData = malloc(nData);
			gifImage->frames[i].nData = nData;
			gifImage->frames[i].width = gif->width;
			gifImage->frames[i].height = gif->height;
			gifImage->frames[i].colorSpace = COLOR_SPACE_RGBA;
			if(!gifImage->frames[i].pData){
				break;
			}
		}
	}
	
	if(i < gifImage->frameCount){
		if(gifImage->frames){
			for(;i<gifImage->frameCount;i--){
				if(gifImage->frames[i].pData)
					destroyImage(&gifImage->frames[i]);
			}
			free(gifImage->frames);
			gifImage->frames = NULL;
		}
		gifImage->curFrame->pData = malloc(nData);
		if(!gifImage->curFrame->pData){
			ret = SOFT_IMAGE_ERROR_MEMORY;
			goto cleanup;
		}
		gifImage->curFrame->nData = nData;
		gifImage->curFrame->width = gif->width;
		gifImage->curFrame->height = gif->height;
		gifImage->curFrame->colorSpace = COLOR_SPACE_RGBA;
	}else{
		gifImage->curFrame = gifImage->frames;
	}
	
	gifImage->frameNum = 0;
	
	if(gif->frames[gifImage->frameNum].frame_delay < MIN_FRAME_DELAY_CS)
		gifImage->frameDelayCs = BUMP_UP_FRAME_DELAY_CS;
	else
		gifImage->frameDelayCs = gif->frames[gifImage->frameNum].frame_delay;

	code = gif_decode_frame(gif, gifImage->frameNum);
	if (code != GIF_OK){
		ret = SOFT_IMAGE_ERROR_DECODING;
		goto cleanup;
	}
	
	unsigned int pixWidth = gif->width*4;
	unsigned int n, gifSize = pixWidth*gif->height;
	for(n=0, i=0; n<gifSize; n+=pixWidth, i+=stride){
		memcpy(gifImage->curFrame->pData + i, 
			gif->frame_image +n, pixWidth);
	}
	gifImage->decodeCount = 1;
	
	*data = NULL;
	if(gifImage->frameCount < 2){
		gif_finalise(gif);
		free(gifImage->imData);
		free(gifImage->pExtraData);
	}
	
	return SOFT_IMAGE_OK;
	
cleanup:
	destroyAnimImage(gifImage);
	*data = NULL;
	return ret;
}

// TIFF

static int32_t tiffRead(void* st, void* buffer, int32_t size){
	return fread(buffer, 1, size, (FILE*)st);
}

static uint32_t tiffSeek32(void* st, uint32_t pos, int whence){
	int ret = fseek((FILE*)st, pos, whence);
	if(ret == 0)
		return pos;
	else
		return -1;
}

static uint64_t tiffSeek64(void* st, uint64_t pos, int whence){
	int ret = fseek((FILE*)st, pos, whence);
	if(ret == 0)
		return pos;
	else
		return -1;
}

static int32_t dummyTiffWrite(void* st, void* buffer, int32_t size){return 0;}
static int dummyTiffClose(void* st){return 0;}
static uint32_t dummyTiffSize(void* st){return 0;}
static int dummyTiffMap(void* st, void** addr, uint32_t* size){return 0;}
static void dummyTiffUnmap(void* st, void* addr, uint32_t size){}

static void *libTiffHandle = NULL;
static int libTiffVersion = 5;
static void* (*TIFFClientOpen)(const char*, const char*, void*,
	    void*, void*, void*, void*, void*, void*, void*);
static int (*TIFFGetField)(void*, uint32_t, ...);
static int (*TIFFReadRGBAImageOriented)(void*, uint32_t, uint32_t, uint32_t*, int, int);
static void (*TIFFClose)(void*);

void unloadLibTiff(){
	if(libTiffHandle)
		dlclose(libTiffHandle);
	libTiffHandle = NULL;
}

static int loadLibTiff(){
	char *error= NULL;
	if(libTiffHandle == NULL){
		
		libTiffHandle = dlopen("libtiff.so.5", RTLD_LAZY);
		if (!libTiffHandle){
			libTiffHandle = dlopen("libtiff.so.4", RTLD_LAZY);
			if (!libTiffHandle){
				goto error;
			}
			libTiffVersion = 4;
		}

		TIFFClientOpen = dlsym(libTiffHandle, "TIFFClientOpen");
		if ((error = dlerror()) != NULL)  
			goto error;
		
		TIFFGetField = dlsym(libTiffHandle, "TIFFGetField");
		if ((error = dlerror()) != NULL)  
			goto error;
		
		TIFFReadRGBAImageOriented = dlsym(libTiffHandle, "TIFFReadRGBAImageOriented");
		if ((error = dlerror()) != NULL)  
			goto error;	
		
		TIFFClose = dlsym(libTiffHandle, "TIFFClose");
		if ((error = dlerror()) != NULL)  
			goto error;	
		
	}
	return 0;
error:
	fprintf(stderr, "%s\n", error);
	unloadLibTiff();
	return 1;
}

int softDecodeTIFF(FILE *fp, IMAGE* im){
	int ret = SOFT_IMAGE_OK;
	
	if(loadLibTiff() == 1)
		return SOFT_IMAGE_ERROR_INIT;
	
	void* tif;
	if(libTiffVersion == 5)
		tif = TIFFClientOpen("FILE", "r", (void *)fp,
			tiffRead, dummyTiffWrite, tiffSeek64, dummyTiffClose, 
			dummyTiffSize, dummyTiffMap, dummyTiffUnmap);
	else
		tif = TIFFClientOpen("FILE", "r", (void *)fp,
			tiffRead, dummyTiffWrite, tiffSeek32, dummyTiffClose, 
			dummyTiffSize, dummyTiffMap, dummyTiffUnmap);
	if(tif != NULL){
		TIFFGetField(tif, /* TIFFTAG_IMAGEWIDTH */ 256,(uint32_t*) &im->width);
		TIFFGetField(tif, /* TIFFTAG_IMAGELENGTH */ 257,(uint32_t*) &im->height);
		unsigned int stride = ALIGN16(im->width)*4;
		im->nData = stride*ALIGN16(im->height);
		im->pData = malloc(im->nData);
		im->colorSpace = COLOR_SPACE_RGBA;
		if (im->pData != NULL){
			if (TIFFReadRGBAImageOriented(tif, im->width, im->height,(uint32_t*) im->pData, 
					/* ORIENTATION_TOPLEFT */ 1, 0)){
				unsigned int pixWidth = im->width*4;
				unsigned int i;
				for(i=im->height-1;i>0; i--){
					memmove(im->pData + i* stride, 
						im->pData +i* pixWidth, pixWidth);
				}
			}else
				ret = SOFT_IMAGE_ERROR_DECODING;
		}else
			ret = SOFT_IMAGE_ERROR_MEMORY;
		TIFFClose(tif);
	}else{
		ret = SOFT_IMAGE_ERROR_FILE_OPEN;
	}
	return ret;
}


/**
 * Modified from: http://curl.haxx.se/libcurl/c/getinmemory.html 
 * Copyright (C) 1998 - 2015, Daniel Stenberg, <daniel@haxx.se>, et al.
 * Distributed under: http://curl.haxx.se/docs/copyright.html
**/

static void *libcurlHandle = NULL;
static void (*curl_global_init)(int);
static void* (*curl_easy_init)(void);
static void (*curl_easy_setopt)(void*, int, ...);
static int (*curl_easy_perform)(void*);
static void (*curl_easy_cleanup)(void*);
static void (*curl_global_cleanup)(void);

void unloadLibCurl(){
	if(libcurlHandle)
		dlclose(libcurlHandle);
	libcurlHandle = NULL;
}

static int loadLibCurl(){
	char *error= NULL;
	if(libcurlHandle == NULL){
		
#ifdef LCURL_NAME
		if(strcmp(LCURL_NAME, "") != 0)
			libcurlHandle = dlopen(LCURL_NAME, RTLD_LAZY);
#endif
		if(!libcurlHandle)
			libcurlHandle = dlopen("libcurl.so.4", RTLD_LAZY);
		if (!libcurlHandle){
			libcurlHandle = dlopen("libcurl.so", RTLD_LAZY);
		
			if (!libcurlHandle){
				libcurlHandle = dlopen("libcurl.so.3", RTLD_LAZY);
			
				if (!libcurlHandle)
					goto error;
			}
		}

		curl_global_init = dlsym(libcurlHandle, "curl_global_init");
		if ((error = dlerror()) != NULL)  
			goto error;
		
		curl_easy_init = dlsym(libcurlHandle, "curl_easy_init");
		if ((error = dlerror()) != NULL)  
			goto error;
		
		curl_easy_setopt = dlsym(libcurlHandle, "curl_easy_setopt");
		if ((error = dlerror()) != NULL)  
			goto error;
		
		curl_easy_perform = dlsym(libcurlHandle, "curl_easy_perform");
		if ((error = dlerror()) != NULL)  
			goto error;
		
		curl_easy_cleanup = dlsym(libcurlHandle, "curl_easy_cleanup");
		if ((error = dlerror()) != NULL)  
			goto error;
		
		curl_global_cleanup = dlsym(libcurlHandle, "curl_global_cleanup");
		if ((error = dlerror()) != NULL)  
			goto error;			
	}
	return 0;
error:
	fprintf(stderr, "%s\n", error);
	unloadLibCurl();
	return 1;
}

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
	void *curl_handle;
	int res;

	struct MemoryStruct chunk = {0};
	
	if(loadLibCurl() != 0){
		return NULL;
	}

	(*curl_global_init)(/* CURL_GLOBAL_ALL */ 3 );

	curl_handle = (*curl_easy_init)();

	(*curl_easy_setopt)(curl_handle, /* CURLOPT_URL */ 10002, url);

	(*curl_easy_setopt)(curl_handle, /* CURLOPT_WRITEFUNCTION */ 20011,
			WriteMemoryCallback);

	(*curl_easy_setopt)(curl_handle, /* CURLOPT_WRITEDATA */ 10001, (void *)&chunk);

	(*curl_easy_setopt)(curl_handle, /* CURLOPT_USERAGENT */ 10018, "libcurl-agent/1.0");

	res = (*curl_easy_perform)(curl_handle);

	if(res != 0) {
		free(chunk.memory);
		chunk.memory=NULL;
		chunk.size = 0;
		fprintf(stderr, "libCurl returned error code %d\n", res);
	}
	
	*size= chunk.size;

	(*curl_easy_cleanup)(curl_handle);
	(*curl_global_cleanup)();
	
	return chunk.memory;
}
