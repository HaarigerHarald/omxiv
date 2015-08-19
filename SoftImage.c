#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jpeglib.h>
#include <png.h>

#include "SoftImage.h"
#include "libnsbmp/libnsbmp.h"

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
	int wPixelAd = cinfo.output_width%16;
	if(wPixelAd != 0){
		wPixelAd=16-wPixelAd;
	}
	int stride = jpeg->width+wPixelAd;
	
	jpeg->height = cinfo.output_height;
	jpeg->colorSpace = COLOR_SPACE_RGBA;
	
	buffer = (*cinfo.mem->alloc_sarray)
		((j_common_ptr) &cinfo, JPOOL_IMAGE, rowStride, 1);
	size_t i, x,y;
	
	jpeg->nData = 4 * stride * cinfo.output_height;
	jpeg->pData = malloc(jpeg->nData);
	if(jpeg->pData == NULL){
		jpeg_finish_decompress(&cinfo);
		jpeg_destroy_decompress(&cinfo);
		return SOFT_JPEG_ERROR_MEMORY;
	}
	
	
	size_t rBytes= cinfo.output_width*4;
	// Copy and convert from RGB to RGBA
	for(i=0; cinfo.output_scanline < cinfo.output_height; i+=(stride*4)) {
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


// Modified from https://gist.github.com/niw/5963798 
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
	int wPixelAd = png->width%16;
	if(wPixelAd != 0)
		wPixelAd=16-wPixelAd;
	
	int stride = png->width+wPixelAd;
	wPixelAd*=4;
	
	png->height = png_get_image_height(png_ptr, info_ptr);
	png->colorSpace = COLOR_SPACE_RGBA;
	
	if (setjmp(png_jmpbuf(png_ptr))){
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		return SOFT_PNG_ERROR_DECODING;	
	}
		
	int rBytes= png_get_rowbytes(png_ptr,info_ptr);
		
	png->nData = 4*png->height*stride;
	png->pData = malloc(png->nData);
	if(!png->pData){
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		return SOFT_PNG_ERROR_MEMORY;
	}
	
	png_bytep row_pointers[png->height];
	size_t i;
	for (i=0; i < png->height; i++) {
		row_pointers[i] = (png_bytep) png->pData + i*(rBytes+ wPixelAd);
	}
	
	png_read_image(png_ptr, row_pointers);
	
	png_read_end(png_ptr, NULL);
	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
	
	return SOFT_PNG_OK;
}

#define BYTES_PER_PIXEL 4

// BMP

static void *bitmap_create(int width, int height, unsigned int state){
	return malloc(width * height * BYTES_PER_PIXEL);
}


static unsigned char *bitmap_get_buffer(void *bitmap){
	return bitmap;
}


static size_t bitmap_get_bpp(void *bitmap){
	return BYTES_PER_PIXEL;
}

int softDecodeBMP(FILE *fp, IMAGE* bmpImage){
	bmp_bitmap_callback_vt bitmap_callbacks = {
		bitmap_create,
		NULL,
		bitmap_get_buffer,
		bitmap_get_bpp
	};
	bmp_result code;
	bmp_image bmp;
	short ret = 0;
	
	if (!fp) {
		return SOFT_IMAGE_ERROR_FILE_OPEN;
	}

	bmp_create(&bmp, &bitmap_callbacks);
	
	size_t size;
	unsigned char *data;

	fseek(fp, 0L, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0L, SEEK_SET);

	data = malloc(size);
	if (!data) {
		return SOFT_BMP_ERROR_MEMORY;
	}

	if (fread(data, 1, size, fp) != size) {
		return SOFT_BMP_ERROR_MEMORY;
	}


	code = bmp_analyse(&bmp, size, data);
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
	free(data);
	
	/* Stride memory needs to be a multiple of 16, 
	 * otherwise resize and render component will bug. */
	int wPixelAd = bmpWidth%16;
	if(wPixelAd != 0)
		wPixelAd=16-wPixelAd;	
	int stride=bmpWidth+wPixelAd;
	
	bmpImage->nData = stride* bmpImage->height* BYTES_PER_PIXEL;
	bmpImage->pData = malloc(bmpImage->nData);
	if(!bmpImage->pData){
		free(bmpData);
		return SOFT_BMP_ERROR_MEMORY;
	}
	int i;
	for(i=0; i<bmpImage->height; i++){
		memcpy(bmpImage->pData + i* stride* BYTES_PER_PIXEL, 
			bmpData +i* bmpWidth*BYTES_PER_PIXEL, bmpWidth*BYTES_PER_PIXEL);
	}
	
	free(bmpData);

	return ret;
	
cleanup:
	bmp_finalise(&bmp);
	free(data);
	return ret;
}

