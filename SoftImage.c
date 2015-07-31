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


int readJpegHeader(char *filePath, JPEG_INFO *jpegInfo){
	struct jpeg_decompress_struct cinfo;
	
	struct my_error_mgr jerr;

	FILE *infile = fopen(filePath, "rb");
	if(infile == NULL){
		return SOFT_IMAGE_ERROR_FILE_OPEN; 
	}

	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;
	if (setjmp(jerr.setjmp_buffer)) {
		jpeg_destroy_decompress(&cinfo);
		fclose(infile);
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
	fclose(infile);
	
	return SOFT_JPEG_OK;
}

int softDecodeJpeg(char *filePath, IMAGE *jpeg){
	struct jpeg_decompress_struct cinfo;
	struct my_error_mgr jerr;
	FILE * infile;
	JSAMPARRAY buffer;
	int rowStride;
	
	if ((infile = fopen(filePath, "rb")) == NULL) {
		return SOFT_IMAGE_ERROR_FILE_OPEN;
	}
	
	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;
	if (setjmp(jerr.setjmp_buffer)) {
		jpeg_destroy_decompress(&cinfo);
		fclose(infile);
		return SOFT_JPEG_ERROR_DECODING;
	}
	
	jpeg_create_decompress(&cinfo);
	jpeg_stdio_src(&cinfo, infile);
	jpeg_read_header(&cinfo, TRUE);
	
	cinfo.out_color_space = JCS_RGB;
	
	jpeg_start_decompress(&cinfo);
	
	rowStride = cinfo.output_width * cinfo.output_components;
	
	/* Width needs to be a multiple of 16, otherwise
	 * resize and render component will bug. We
	 * add some transparent pixels left and right. */
	int wPixelAd = cinfo.output_width%16;
	if(wPixelAd == 0){
		jpeg->width = cinfo.output_width;
	}else{
		wPixelAd=16-wPixelAd;
		jpeg->width = cinfo.output_width+wPixelAd;
	}
	int wPixelAdL= wPixelAd/2;
	int wPixelAdR = wPixelAd/2+wPixelAd%2;
	wPixelAdL*=4;
	wPixelAdR*=4;
	
	jpeg->height = cinfo.output_height;
	jpeg->colorSpace = COLOR_SPACE_RGBA;
	
	buffer = (*cinfo.mem->alloc_sarray)
		((j_common_ptr) &cinfo, JPOOL_IMAGE, rowStride, 1);
	size_t i, x,y;
	
	jpeg->nData = 4 * jpeg->width * cinfo.output_height;
	jpeg->pData = malloc(jpeg->nData);
	if(jpeg->pData == NULL){
		jpeg_finish_decompress(&cinfo);
		jpeg_destroy_decompress(&cinfo);
		fclose(infile);
		return SOFT_JPEG_ERROR_MEMORY;
	}
	
	
	size_t rBytes= cinfo.output_width*4;
	// Copy and convert from RGB to RGBA
	for(i=0; cinfo.output_scanline < cinfo.output_height; i+=(jpeg->width*4)) {
		jpeg_read_scanlines(&cinfo, buffer, 1);
		memset(jpeg->pData, 0 , wPixelAdL);
		for(x = 0,y=0; x < rBytes; x+=4, y+=3){
			jpeg->pData[i+x+wPixelAdL+3]=255;
			memcpy(jpeg->pData+i+x+wPixelAdL, buffer[0]+y, 3);
		}
		memset(jpeg->pData+i+rBytes+ wPixelAdL, 0, wPixelAdR);
	}
	
	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	
	fclose(infile);
	
	if(jerr.pub.num_warnings != 0){
		return SOFT_JPEG_ERROR_CORRUPT_DATA;
	}
	
	return SOFT_JPEG_OK;
}


// Modified from https://gist.github.com/niw/5963798 
int softDecodePng(char* filePath, IMAGE* png){
	png_byte header[8];
	
	png_structp png_ptr;
	png_infop info_ptr;

	FILE *fp = fopen(filePath, "rb");
	if (!fp)
		return SOFT_IMAGE_ERROR_FILE_OPEN;
	fread(header, 1, 8, fp);
	if (png_sig_cmp(header, 0, 8)){
		fclose(fp);
		return SOFT_IMAGE_ERROR_FILE_OPEN;
	}
	
	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

	if (!png_ptr){
		fclose(fp);
		return SOFT_PNG_ERROR_CREATE_STRUCT;
	}
	
	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr){
		fclose(fp);
		return SOFT_PNG_ERROR_CREATE_STRUCT;
	}

	if (setjmp(png_jmpbuf(png_ptr))){
		fclose(fp);
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
	
	/* Width needs to be a multiple of 16, otherwise
	 * resize and render component will bug. We
	 * add some transparent pixels left and right. */
	int wPixelAd = png->width%16;
	if(wPixelAd != 0){
		wPixelAd=16-wPixelAd;
		png->width+=wPixelAd;
	}
	int wPixelAdL= wPixelAd/2;
	int wPixelAdR = wPixelAd/2+wPixelAd%2;
	wPixelAdL*=4;
	wPixelAdR*=4;
	wPixelAd*=4;
	
	png->height = png_get_image_height(png_ptr, info_ptr);
	png->colorSpace = COLOR_SPACE_RGBA;
	
	if (setjmp(png_jmpbuf(png_ptr))){
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		fclose(fp);
		return SOFT_PNG_ERROR_DECODING;	
	}
		
	int rBytes= png_get_rowbytes(png_ptr,info_ptr);
		
	png->nData = 4*png->height*png->width;
	png->pData = malloc(png->nData);
	if(!png->pData){
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		fclose(fp);
		return SOFT_PNG_ERROR_MEMORY;
	}
	
	png_bytep row_pointers[png->height];
	size_t i;
	for (i=0; i < png->height; i++) {
		row_pointers[i] = (png_bytep) png->pData + i*(rBytes+ wPixelAd) +wPixelAdL;
	}
	
	png_read_image(png_ptr, row_pointers);
	
	png_read_end(png_ptr, NULL);
	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
	
	for (i=0; i < png->nData; i+=rBytes+ wPixelAd) {
		memset(png->pData + i, 0, wPixelAdL);
		memset(png->pData +i+rBytes+wPixelAdL, 0 , wPixelAdR);
	}
	
	fclose(fp);
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

int softDecodeBMP(char* filePath, IMAGE* bmpImage){
	bmp_bitmap_callback_vt bitmap_callbacks = {
		bitmap_create,
		NULL,
		bitmap_get_buffer,
		bitmap_get_bpp
	};
	bmp_result code;
	bmp_image bmp;
	short ret = 0;

	bmp_create(&bmp, &bitmap_callbacks);
	
	FILE *fp;
	size_t size;
	unsigned char *data;

	fp = fopen(filePath, "rb");
	if (!fp) {
		return SOFT_IMAGE_ERROR_FILE_OPEN;
	}
	fseek(fp, 0L, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0L, SEEK_SET);

	data = malloc(size);
	if (!data) {
		fclose(fp);
		return SOFT_BMP_ERROR_MEMORY;
	}

	if (fread(data, 1, size, fp) != size) {
		fclose(fp);
		return SOFT_BMP_ERROR_MEMORY;
	}

	fclose(fp);

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
	bmpImage->colorSpace = COLOR_SPACE_RGBA;
		
	bmp_finalise(&bmp);
	free(data);
	
	/* Width needs to be a multiple of 16, otherwise
	 * resize and render component will bug. We
	 * add some transparent pixels left and right. */
	int wPixelAd = bmpWidth%16;
	if(wPixelAd != 0)
		wPixelAd=16-wPixelAd;	
	bmpImage->width=bmpWidth+wPixelAd;
	int wPixelAdL= wPixelAd/2;
	int wPixelAdR = wPixelAd/2+wPixelAd%2;
	wPixelAdL*=BYTES_PER_PIXEL;
	wPixelAdR*=BYTES_PER_PIXEL;
	wPixelAd*=BYTES_PER_PIXEL;
	
	bmpImage->nData = bmpImage->width* bmpImage->height* BYTES_PER_PIXEL;
	bmpImage->pData = malloc(bmpImage->nData);
	if(!bmpImage->pData){
		free(bmpData);
		return SOFT_BMP_ERROR_MEMORY;
	}
	int i;
	for(i=0; i<bmpImage->height; i++){
		memset(bmpImage->pData + i* bmpImage->width* BYTES_PER_PIXEL, 0, wPixelAdL);
		memcpy(bmpImage->pData + i* bmpImage->width* BYTES_PER_PIXEL + wPixelAdL, 
			bmpData +i* bmpWidth*BYTES_PER_PIXEL, bmpWidth*BYTES_PER_PIXEL);
		memset(bmpImage->pData + (i+1)* bmpImage->width* BYTES_PER_PIXEL-wPixelAdR, 
			0, wPixelAdR);
	}
	
	free(bmpData);

	return ret;
	
cleanup:
	bmp_finalise(&bmp);
	free(data);
	return ret;
}

