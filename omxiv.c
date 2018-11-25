#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <dirent.h>
#include <getopt.h>

#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "omx_render.h"
#include "omx_image.h"
#include "soft_image.h"
#include "bcm_host.h"
#include "help.h"

#ifndef VERSION
#define VERSION "UNKNOWN"
#endif

static const char magNumJpeg[] = {0xff, 0xd8, 0xff};
static const char magNumPng[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1A, 0x0A};
static const char magNumBmp[] = {0x42, 0x4d};
static const char magNumGif[] = {0x47, 0x49, 0x46, 0x38};
static const char magNumTifLE[] = {0x49, 0x49, 0x2a, 0x00};
static const char magNumTifBE[] = {0x4d, 0x4d, 0x00, 0x2a};

static const struct option longOpts[] = {
	{"help", no_argument, 0, 'h'},
	{"version", no_argument, 0, 'v'},
	{"blank", no_argument, 0, 'b'},
	{"transition", required_argument, 0, 'T'},
	{"duration", required_argument, 0, 0x101},
	{"win", required_argument, 0, 0x102},
	{"aspect", required_argument, 0, 'a'},
	{"orientation", required_argument, 0, 'o'},
	{"mirror", no_argument, 0, 'm'},
	{"layer", required_argument, 0, 'l'},
	{"display", required_argument, 0, 'd'},
	{"info", no_argument, 0, 'i'},
	{"no-keys", no_argument, 0, 'k'},
	{"soft", no_argument, 0, 's'},
	{"ignore-exif", no_argument, 0, 0x103},
	{0, 0, 0, 0}
};

static ILCLIENT_T *client=NULL;
static char end = 0;

static char info = 0, blank = 0, soft = 0, keys = 1, center = 0, exifOrient = 1, mirror = 0;
static uint32_t sWidth, sHeight;
static int initRotation = 0, rotateInc = 90;

static OMX_RENDER render = INIT_OMX_RENDER, render2;
static OMX_RENDER* pCurRender = &render;
static OMX_RENDER_DISP_CONF dispConfig = INIT_OMX_DISP_CONF;


static void resetTerm(){
	struct termios old = {0};
	if (tcgetattr(0, &old) < 0)
		perror("tcsetattr()");
	old.c_lflag |= ICANON;
	old.c_lflag |= ECHO;
	if (tcsetattr(0, TCSADRAIN, &old) < 0)
		perror("tcsetattr ~ICANON");
}

void sig_handler(int sigNum){
	end=1;
	signal(SIGTERM, SIG_DFL);
	signal(SIGINT, SIG_DFL);
}

static int isDir(char *path){
	struct stat statb;
	if(stat(path, &statb)== -1)
		return 0;
	if(S_ISDIR(statb.st_mode))
		return 1;
	else
		return 0;
}

static unsigned long getCurrentTimeMs(){
	struct timeval  tv;
	gettimeofday(&tv, NULL);
	return (tv.tv_sec) * 1000UL + (tv.tv_usec) / 1000UL ;
}

static int imageFilter(const struct dirent *entry){
	char* ext = strrchr(entry->d_name, '.');
	if(ext!=NULL && (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0 
			|| strcasecmp(ext, ".jpe") == 0 || strcasecmp(ext, ".png") == 0
			|| strcasecmp(ext, ".bmp") == 0 || strcasecmp(ext, ".gif") == 0 
			|| strcasecmp(ext, ".tif") == 0 || strcasecmp(ext, ".tiff") == 0))
		return 1;
	else
		return 0;
}

static int getImageFilesInDir(char ***list, const char* path){
	struct dirent **namelist;
	int imageNum;
	imageNum = scandir(path, &namelist, imageFilter, alphasort);
	if (imageNum < 0)
		return imageNum;
	else {
		*list=malloc(sizeof(char*) *imageNum);
		int i;
		for(i=0; i<imageNum; i++) {
			if(strcmp(path, ".") == 0 || strcmp(path, "./") == 0){
				(*list)[i]= malloc(strlen(namelist[i]->d_name)+1);
				strcpy((*list)[i], namelist[i]->d_name);
			}else{
				if(strrchr(path, '/')- path != strlen(path)-1){
					(*list)[i]= malloc(strlen(path)+strlen(namelist[i]->d_name)+2);
					strcpy((*list)[i],path);
					(*list)[i][strlen(path)]='/';
					strcpy((*list)[i]+strlen(path)+1,namelist[i]->d_name);
				}else{
					(*list)[i]= malloc(strlen(path)+strlen(namelist[i]->d_name)+1);
					strcpy((*list)[i],path);
					strcpy((*list)[i]+strlen(path),namelist[i]->d_name);
				}
			}
			free(namelist[i]);
		}
		free(namelist);
	}
	return imageNum;
}

// http://stackoverflow.com/a/912796
static char getch(const int timeout) {
	char buf = 0;
	struct termios old = {0};
	if (tcgetattr(0, &old) < 0)
		perror("tcsetattr()");
	old.c_lflag &= ~ICANON;
	old.c_lflag &= ~ECHO;
	old.c_cc[VMIN] = 0;
	old.c_cc[VTIME] = timeout;
	if (tcsetattr(0, TCSANOW, &old) < 0)
		perror("tcsetattr ICANON");
	if (read(0, &buf, 1) < 0)
		perror("read()");
	return (buf);
}

static int renderImage(IMAGE *image, ANIM_IMAGE *anim){
	int ret;
	OMX_RENDER *stopRender = NULL;
	
	if(render.transition.type == BLEND){
		stopRender = pCurRender;
		if(stopRender->renderComponent){
			dispConfig.layer--;
			setOmxDisplayConfig(stopRender);
			dispConfig.layer++;
		}
		
		pCurRender = (pCurRender == &render) ? &render2 : &render;
	}else{
		if(pCurRender->renderComponent){
			ret = stopOmxImageRender(pCurRender);
			if(ret != 0){
				fprintf(stderr, "render cleanup returned 0x%x\n", ret);
				return ret;
			}
		}
	}
	
	dispConfig.rotation = 0;
	switch(exifOrient){
		case 7:
			dispConfig.rotation += 90;
		case 4:
			dispConfig.rotation += 90;
		case 5:
			dispConfig.rotation += 90;
		case 2:
			dispConfig.rotation += (360 - initRotation);
			if(mirror == 0){
				rotateInc = 270;
				dispConfig.configFlags |= OMX_DISP_CONFIG_FLAG_MIRROR;
			}else{
				rotateInc = 90;
				dispConfig.configFlags &= ~OMX_DISP_CONFIG_FLAG_MIRROR;
			}
			break;
		case 8: 
			dispConfig.rotation += 90;
		case 3: 
			dispConfig.rotation += 90;
		case 6:
			dispConfig.rotation += 90;
		case 1:
		case 0:
			dispConfig.rotation += initRotation;
			if(mirror == 1){
				rotateInc = 270;
				dispConfig.configFlags |= OMX_DISP_CONFIG_FLAG_MIRROR;
			}else{
				rotateInc = 90;
				dispConfig.configFlags &= ~OMX_DISP_CONFIG_FLAG_MIRROR;
			}
			break;
	}
	dispConfig.rotation%=360;
	
	if(anim->frameCount < 2){
		ret = omxRenderImage(pCurRender, image);
		destroyImage(image);
	}else{
		ret = omxRenderAnimation(pCurRender, anim);
	}
	if(ret != 0){
		fprintf(stderr, "render returned 0x%x\n", ret);
	}
	
	if(stopRender && stopRender->renderComponent){
		ret = stopOmxImageRender(stopRender);
		if(ret != 0)
			fprintf(stderr, "render cleanup returned 0x%x\n", ret);
	}
	return ret;
}

static int decodeImage(const char *filePath, IMAGE *image, ANIM_IMAGE *anim){
	int ret = 0;
	FILE *imageFile;
	unsigned char *httpImMem = NULL;
	size_t size = 0;
	char magNum[8];
	
	exifOrient = (exifOrient == 0) ? 0 : 1;
	
	if(strncmp(filePath, "http://", 7) == 0 || strncmp(filePath, "https://", 8) == 0){
		if(info)
			printf("Open Url: %s\n", filePath);
		httpImMem = getImageFromUrl(filePath, &size);
		if(httpImMem == NULL){
			fprintf(stderr, "Couldn't get Image from Url\n");
			return 0x200;
		}
		imageFile = fmemopen((void*) httpImMem, size, "rb");
	}else{
		if(info)
			printf("Open file: %s\n", filePath);
	
		imageFile = fopen(filePath, "rb");
	}
	
	if(!imageFile){
		return SOFT_IMAGE_ERROR_FILE_OPEN;
	}
	
	if(fread(&magNum, 1, 8, imageFile) != 8){
		fclose(imageFile);
		free(httpImMem);
		return 0x100;
	}
	rewind(imageFile);
	
	if(memcmp(magNum, magNumJpeg, sizeof(magNumJpeg)) == 0){

		JPEG_INFO jInfo;
		ret=readJpegHeader(imageFile, &jInfo);
		if(ret != SOFT_IMAGE_OK){
			fclose(imageFile);
			free(httpImMem);
			return ret;
		}
		
		if(exifOrient != 0)
			exifOrient = jInfo.orientation;
		
		rewind(imageFile);

		if(soft || jInfo.mode == JPEG_MODE_PROGRESSIVE || jInfo.nColorComponents != 3){
			if(info)
				printf("Soft decode jpeg\n");
			ret = softDecodeJpeg(imageFile, image);
		}else{
			if(info)
				printf("Hard decode jpeg\n");
			ret = omxDecodeJpeg(client, imageFile, image);
		}
	}else if(memcmp(magNum, magNumPng, sizeof(magNumPng)) == 0){
		ret = softDecodePng(imageFile, image);
	}else if(memcmp(magNum, magNumBmp, sizeof(magNumBmp)) == 0){
		ret = softDecodeBMP(imageFile, image, &httpImMem, size);
	}else if(memcmp(magNum, magNumTifLE, sizeof(magNumTifLE)) == 0 ||
			memcmp(magNum, magNumTifBE, sizeof(magNumTifBE)) == 0){
		ret = softDecodeTIFF(imageFile, image);
	}else if(memcmp(magNum, magNumGif, sizeof(magNumGif)) == 0){
		anim->curFrame = image;
		ret = softDecodeGif(imageFile, anim, &httpImMem, size);
	}else{
		printf("Unsupported image\n");
		fclose(imageFile);
		free(httpImMem);
		return 0x100;
	}
	
	fclose(imageFile);
	free(httpImMem);

	if(info)
		printf("Width: %u, Height: %u\n", image->width, image->height);

	return ret;
}

/* From: https://github.com/popcornmix/omxplayer/blob/master/omxplayer.cpp#L455
 * Licensed under the GPLv2 */
static void blankBackground(const int imageLayer, const int displayNum){
	// we create a 1x1 black pixel image that is added to display just behind video
	DISPMANX_DISPLAY_HANDLE_T display;
	DISPMANX_UPDATE_HANDLE_T update;
	DISPMANX_RESOURCE_HANDLE_T resource;
	uint32_t vc_image_ptr;
	VC_IMAGE_TYPE_T type = VC_IMAGE_RGB565;
	uint16_t image = 0x0000; // black
	int layer = imageLayer-1;

	VC_RECT_T dst_rect, src_rect;

	display = vc_dispmanx_display_open(displayNum);

	resource = vc_dispmanx_resource_create( type, 1 /*width*/, 1 /*height*/, &vc_image_ptr );

	vc_dispmanx_rect_set( &dst_rect, 0, 0, 1, 1);

	vc_dispmanx_resource_write_data( resource, type, sizeof(image), &image, &dst_rect );

	vc_dispmanx_rect_set( &src_rect, 0, 0, 1<<16, 1<<16);
	vc_dispmanx_rect_set( &dst_rect, 0, 0, 0, 0);

	update = vc_dispmanx_update_start(0);

	vc_dispmanx_element_add(update, display, layer, &dst_rect, resource, &src_rect,
									DISPMANX_PROTECTION_NONE, NULL, NULL, DISPMANX_STEREOSCOPIC_MONO );

	vc_dispmanx_update_submit_sync(update);
}

// http://stackoverflow.com/a/3940758
static int isBackgroundProc() {
    pid_t fg = tcgetpgrp(STDIN_FILENO);
    if(fg == -1) {
        return 1;
    }  else if (fg == getpgrp()) {
        return 0;
    } else {
        return 1;
    }
}

static void printVersion(){
	printf("Version: %s\n", VERSION);
	printf("Build date: %s\n", __DATE__);
}

int main(int argc, char *argv[]){
	int ret = 1;
	long timeout = 0;

	render.transition.type = NONE;
	render.transition.durationMs = 400;

	if(isBackgroundProc())
		keys=0;
	
	int opt;
	while((opt = getopt_long(argc, argv, "hvt:bT:a:o:ml:d:iks", 
			longOpts, NULL)) != -1){
		
		switch(opt){
			case 'h':
				printUsage();
				return 0;
			case 'v':
				printVersion();
				return 0;
			case 't':
				timeout = strtol(optarg, NULL, 10)*1000;
				break;
			case 'b':
				blank = 1; break;
			case 'T':
				if(strcmp(optarg, "blend") == 0)
					render.transition.type = BLEND;
				break;
			case 0x101:
				render.transition.durationMs = strtol(optarg, NULL, 10);
				break;
			case 0x102:;
				char *pos = strtok(optarg ,", '");
				dispConfig.xOffset = strtol(pos, NULL, 10);
				pos = strtok (NULL,", ");
				if(pos!=NULL){
					dispConfig.yOffset = strtol(pos, NULL, 10);
					pos = strtok (NULL,", ");
					dispConfig.width = strtol(pos, NULL, 10) - dispConfig.xOffset;
					pos = strtok (NULL,", '");
					dispConfig.height =  strtol(pos, NULL, 10) - dispConfig.yOffset;
				}
				break;
			case 'a':
				if(strcmp(optarg, "fill") == 0)
					dispConfig.configFlags |= OMX_DISP_CONFIG_FLAG_NO_ASPECT;
				else if(strcmp(optarg, "center") == 0){
					center = 1;
					dispConfig.configFlags |= OMX_DISP_CONFIG_FLAG_CENTER;
				}
				break;
			case 'o':
				initRotation = strtol(optarg, NULL, 10);
				dispConfig.rotation = initRotation;
				break;
			case 'm':
				mirror = 1; break;
			case 'l':
				dispConfig.layer = strtol(optarg, NULL, 10);
				break;
			case 'd':
				dispConfig.display = strtol(optarg, NULL, 10);
				break;
			case 'i':
				info = 1; break;
			case 'k':
				keys = 0; break;
			case 's':
				soft = 1; break;
			case 0x103:
				exifOrient = 0; break;
			default:
				return EXIT_FAILURE;	
		}
	}

	int imageNum;
	char **files;
	if(argc-optind <= 0){
		imageNum=getImageFilesInDir(&files, "./");
	}else if(isDir(argv[optind])){
		imageNum=getImageFilesInDir(&files, argv[optind]);
	}else{
		imageNum = argc-optind;

		files=malloc(sizeof(char*) *imageNum);
		int x;
		for(x =0; optind+x<argc; x++){
			files[x]=argv[optind+x];
		}
	}

	if(imageNum<1){
		fprintf(stderr, "No images to display\n");
		return 1;
	}

	bcm_host_init();

	if ((client = ilclient_init()) == NULL) {
		fprintf(stderr, "Error init ilclient\n");
		return 1;
	}

	if (OMX_Init() != OMX_ErrorNone) {
		fprintf(stderr, "Error init omx. There may be not enough gpu memory.\n");
		ilclient_destroy(client);
		return 1;
	}
	
	if(center){
		if(dispConfig.width == 0 || dispConfig.height == 0){
			graphics_get_display_size(dispConfig.display, &sWidth, &sHeight);
			dispConfig.width = sWidth;
			dispConfig.height = sHeight;
		}else{
			sWidth = dispConfig.width;
			sHeight = dispConfig.height;
		}
	}

	render.client = client;
	render.dispConfig = &dispConfig;
	memcpy(&render2, &render, sizeof(OMX_RENDER));
	unsigned long lShowTime = 0;
	unsigned long cTime;
	IMAGE image = {0};
	ANIM_IMAGE anim = {0};
	
	ret=decodeImage(files[0], &image, &anim);

	if(ret==0){
		if(blank)
			blankBackground(dispConfig.layer, dispConfig.display);
		lShowTime = getCurrentTimeMs();
		if(renderImage(&image, &anim) != 0)
			end = 1;
	}else{
		if(ret == SOFT_IMAGE_ERROR_FILE_OPEN){
			fprintf(stderr, "Error file does not exist or is corrupted.\n");
		}else if(ret!= 0x100){
			fprintf(stderr, "decoder returned 0x%x\n", ret);
		}
		end=1;
	}

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	int i = 0;
	char c = 0, paused = 0;
	while(!end){
		if(keys){
			c = getch(1);
			if(end)
				break;
		}else{
			usleep(20000);
		}
		if(timeout != 0 && imageNum > 1 && !paused){
			cTime = getCurrentTimeMs();
			if( (cTime-lShowTime) > timeout){
				if(imageNum <= ++i)
					i=0;
				stopAnimation(pCurRender);
				ret=decodeImage(files[i], &image, &anim);
				if(ret==0){
					lShowTime = getCurrentTimeMs();
					if(renderImage(&image, &anim) != 0)
						break;
				}
			}
		}

		if(c == 0){
			continue;
		}else if(c == 'q' || c =='Q'){
			break;
		}else if(c == 'm' || c =='M'){
			tcflush(0, TCIFLUSH);
			dispConfig.configFlags^= OMX_DISP_CONFIG_FLAG_MIRROR;
			rotateInc = (rotateInc + 180)%360;
			ret = setOmxDisplayConfig(pCurRender);
			if(ret != 0){
				fprintf(stderr, "dispConfig set returned 0x%x\n", ret);
				break;
			}
		}else if(c == 0x1b){
			c=getch(1);
			if(c == 0)
				break;
			c = getch(1);
			tcflush(0, TCIFLUSH);
			if(c == 0x41){
				dispConfig.rotation = (dispConfig.rotation + 360 -rotateInc)%360;
				ret = setOmxDisplayConfig(pCurRender);
				if(ret != 0){
					fprintf(stderr, "dispConfig set returned 0x%x\n", ret);
					break;
				}
			}else if(c == 0x42){
				dispConfig.rotation = (dispConfig.rotation + rotateInc)%360;
				ret = setOmxDisplayConfig(pCurRender);
				if(ret != 0){
					fprintf(stderr, "dispConfig set returned 0x%x\n", ret);
					break;
				}
			}else if(c == 0x43 && imageNum > 1){
				if(imageNum <= ++i)
					i=0;
				stopAnimation(pCurRender);
				ret=decodeImage(files[i], &image, &anim);
				if(ret==0){
					lShowTime = getCurrentTimeMs();
					if(renderImage(&image, &anim) != 0)
						break;
				}
			}else if(c == 0x44 && imageNum > 1){
				if(0 > --i)
					i=imageNum-1;
				stopAnimation(pCurRender);
				ret=decodeImage(files[i], &image, &anim);
				if(ret==0){
					lShowTime = getCurrentTimeMs();
					if(renderImage(&image, &anim) != 0)
						break;
				}
			}
		}else if(timeout > 0 && (c=='p' || c=='P')){
			tcflush(0, TCIFLUSH);
			paused ^=1;
			if(paused)
				printf("Paused\n");
			else
				printf("Continue\n");
		}
	}

	if(ret == 0){
		ret = stopOmxImageRender(pCurRender);
		if(ret != 0)
			fprintf(stderr, "render cleanup returned 0x%x\n", ret);
	}

	free(image.pData);
	unloadLibCurl();
	unloadLibTiff();

	if(keys)
		resetTerm();

	OMX_Deinit();

	if (client != NULL) {
		ilclient_destroy(client);
	}

	bcm_host_deinit();

	return ret;
}
