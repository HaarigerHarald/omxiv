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
 
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "omx_render.h"
#include "bcm_host.h"

#define TIMEOUT_MS 2000

static int initRender(OMX_RENDER *render){
	int ret = ilclient_create_component(render->client,
					&render->component,"video_render",
					ILCLIENT_DISABLE_ALL_PORTS|
					ILCLIENT_ENABLE_INPUT_BUFFERS);
							
	if (ret != 0) {
		return OMX_RENDER_ERROR_CREATE_COMP;
	}
	
	render->handle = ILC_GET_HANDLE(render->component);
	
	OMX_PORT_PARAM_TYPE port;
	port.nSize = sizeof(OMX_PORT_PARAM_TYPE);
	port.nVersion.nVersion = OMX_VERSION;
	
	OMX_GetParameter(render->handle, OMX_IndexParamVideoInit, &port);
	if (port.nPorts != 1) {
		return OMX_RENDER_ERROR_PORTS;
	}
	render->inPort = port.nStartPortNumber;	
	
	return OMX_RENDER_OK;
}

static int setUpRender(OMX_RENDER *render, IMAGE *image){

	OMX_PARAM_PORTDEFINITIONTYPE portdef;
	int ret;
	
	ilclient_change_component_state(render->component, OMX_StateIdle);
	ilclient_change_component_state(render->component, OMX_StateExecuting);

	portdef.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
	portdef.nVersion.nVersion = OMX_VERSION;
	portdef.nPortIndex = render->inPort;

	ret = OMX_GetParameter(render->handle, OMX_IndexParamPortDefinition, &portdef);
	if (ret != OMX_ErrorNone) {
		return OMX_RENDER_ERROR_PARAMETER;
	}
	
	portdef.format.video.bFlagErrorConcealment = OMX_FALSE;
	portdef.format.video.eCompressionFormat = OMX_VIDEO_CodingUnused;
	portdef.format.video.nFrameWidth = image->width;
	portdef.format.video.nFrameHeight = image->height;
	portdef.format.video.nStride = 0;
	portdef.format.video.nSliceHeight = 0;
	
	if(image->colorSpace == COLOR_SPACE_RGB24)
		portdef.format.video.eColorFormat = OMX_COLOR_Format24bitBGR888;
	else if(image->colorSpace == COLOR_SPACE_YUV420P)
		portdef.format.video.eColorFormat = OMX_COLOR_FormatYUV420PackedPlanar;
	else if(image->colorSpace == COLOR_SPACE_RGBA)
		portdef.format.video.eColorFormat = OMX_COLOR_Format32bitABGR8888;
	else if(image->colorSpace == COLOR_SPACE_RGB16)
		portdef.format.video.eColorFormat = OMX_COLOR_Format16bitRGB565;
	
	portdef.nBufferSize=image->nData;
	
	ret = OMX_SetParameter(render->handle, OMX_IndexParamPortDefinition, &portdef);
	if (ret != OMX_ErrorNone) {	
		return OMX_RENDER_ERROR_PARAMETER;
	}
	
	OMX_SendCommand(render->handle, OMX_CommandPortEnable, render->inPort, NULL);
	
	ret = OMX_UseBuffer(render->handle,&render->pInputBufferHeader,render->inPort, 
			NULL, portdef.nBufferSize, (OMX_U8 *) image->pData);
			
	if(ret != OMX_ErrorNone){
		return OMX_RENDER_ERROR_MEMORY;
	}
	
	return OMX_RENDER_OK;
}

static int doRender(OMX_RENDER *render, IMAGE *image){
	int retVal= OMX_RENDER_OK;
	OMX_BUFFERHEADERTYPE *pBufHeader = render->pInputBufferHeader;
	pBufHeader->nFilledLen=image->nData;
	pBufHeader->nFlags = OMX_BUFFERFLAG_EOS;	
	
	int ret = OMX_EmptyThisBuffer(render->handle, pBufHeader);
	if (ret != OMX_ErrorNone) {
		 retVal |= OMX_RENDER_ERROR_MEMORY;
	}
	
	ilclient_wait_for_event(render->component, OMX_EventBufferFlag, render->inPort, 
		0, OMX_BUFFERFLAG_EOS, 0, ILCLIENT_BUFFER_FLAG_EOS, TIMEOUT_MS);
	
	return retVal;
}

int setOmxDisplayConfig(OMX_RENDER *render, OMX_RENDER_DISP_CONF *dispConf){
	OMX_CONFIG_DISPLAYREGIONTYPE dispConfRT;
	memset(&dispConfRT, 0 , sizeof(OMX_CONFIG_DISPLAYREGIONTYPE));
	dispConfRT.nPortIndex = render->inPort;
	dispConfRT.nSize= sizeof(OMX_CONFIG_DISPLAYREGIONTYPE);
	dispConfRT.nVersion.nVersion = OMX_VERSION;
	
	OMX_DISPLAYSETTYPE set = OMX_DISPLAY_SET_FULLSCREEN|OMX_DISPLAY_SET_NOASPECT|
			OMX_DISPLAY_SET_MODE|OMX_DISPLAY_SET_TRANSFORM|OMX_DISPLAY_SET_NUM;

	
	dispConfRT.noaspect = (dispConf->configFlags & OMX_DISP_CONFIG_FLAG_NO_ASPECT) ? OMX_TRUE : OMX_FALSE;

	if(dispConf->width != 0 && dispConf->height != 0){
		set|= OMX_DISPLAY_SET_DEST_RECT;
		dispConfRT.dest_rect.x_offset = dispConf->xOffset;
		dispConfRT.dest_rect.y_offset = dispConf->yOffset;
		dispConfRT.dest_rect.width = dispConf->width;
		dispConfRT.dest_rect.height = dispConf->height;
		dispConfRT.fullscreen = OMX_FALSE;
	}else{
		dispConfRT.fullscreen = OMX_TRUE;
	}
	
	switch(dispConf->rotation){
		case 0:
			if(dispConf->configFlags & OMX_DISP_CONFIG_FLAG_MIRROR)
				dispConfRT.transform = OMX_DISPLAY_MIRROR_ROT0;
			else
				dispConfRT.transform = OMX_DISPLAY_ROT0;
			break;
		case 90:
			if(dispConf->configFlags & OMX_DISP_CONFIG_FLAG_MIRROR)
				dispConfRT.transform = OMX_DISPLAY_MIRROR_ROT90;
			else
				dispConfRT.transform = OMX_DISPLAY_ROT90;
			break;
		case 180:
			if(dispConf->configFlags & OMX_DISP_CONFIG_FLAG_MIRROR)
				dispConfRT.transform = OMX_DISPLAY_MIRROR_ROT180;
			else
				dispConfRT.transform = OMX_DISPLAY_ROT180;
			break;
		case 270:
			if(dispConf->configFlags & OMX_DISP_CONFIG_FLAG_MIRROR)
				dispConfRT.transform = OMX_DISPLAY_MIRROR_ROT270;
			else
				dispConfRT.transform = OMX_DISPLAY_ROT270;
			break;	
	}
	
	if(dispConf->alpha != 0){
		set|=OMX_DISPLAY_SET_ALPHA;
		dispConfRT.alpha=dispConf->alpha;
	}
	
	if(dispConf->layer != 0){
		set|= OMX_DISPLAY_SET_LAYER;
		dispConfRT.layer=dispConf->layer;
	}
	
	dispConfRT.num = dispConf->display;
	dispConfRT.mode = dispConf->mode;
	dispConfRT.set= set;
	if(OMX_SetConfig(render->handle, OMX_IndexConfigDisplayRegion, &dispConfRT) != OMX_ErrorNone){
		return OMX_RENDER_ERROR_DISP_CONF;
	}
	return OMX_RENDER_OK;
}


int omxRenderImage(OMX_RENDER *render, IMAGE *image, OMX_RENDER_DISP_CONF *dispConfig,
		OMX_RENDER_TRANSITION *transition){
	render->renderAnimation = 0;
	int ret = initRender(render);
	if(ret!= OMX_RENDER_OK){
		return ret;
	}
	if(transition != NULL && transition->type == BLEND)
		dispConfig->alpha = 15;
	
	ret = setOmxDisplayConfig(render, dispConfig);
	if(ret!= OMX_RENDER_OK){
		return ret;
	}
	ret = setUpRender(render, image);
	if(ret!= OMX_RENDER_OK){
		return ret;
	}
	ret = doRender(render, image);
	if(ret!= OMX_RENDER_OK){
		return ret;
	}
	
	if(transition != NULL && transition->type == BLEND){
		while(dispConfig->alpha<255){
			usleep(transition->durationMs*1000/24);
			dispConfig->alpha+=20;
			setOmxDisplayConfig(render, dispConfig);
		}
	}
	
	return OMX_RENDER_OK;
}

struct ANIM_RENDER_PARAMS{
	OMX_RENDER *render;
	ANIM_IMAGE *anim;
};

static void* doRenderAnimation(void* params){
	OMX_RENDER *render = ((struct ANIM_RENDER_PARAMS *)params)->render;
	ANIM_IMAGE *anim = ((struct ANIM_RENDER_PARAMS *)params)->anim;
	free(params);
	int ret=0;
	unsigned int i;
	struct timespec wait;
	if(anim->loopCount <= 0){
		while(ret == 0 && render->stop == 0){
			for(i=anim->frameCount; i--;){
				doRender(render, anim->curFrame);
				clock_gettime(CLOCK_REALTIME, &wait);
				
				unsigned int sec = anim->frameDelayCs/100;
				anim->frameDelayCs -= sec*100;
				wait.tv_nsec += anim->frameDelayCs*10000000L;
				wait.tv_sec += sec + wait.tv_nsec/1000000000L;
				wait.tv_nsec%=1000000000L;
				
				ret=anim->decodeNextFrame(anim);
				if(ret != 0 || render->stop != 0)
					goto end;
						
				pthread_mutex_lock(&render->lock);
				pthread_cond_timedwait(&render->cond, &render->lock, &wait);
				pthread_mutex_unlock(&render->lock);
				
				if(render->stop != 0)
					goto end;
			}
		}
	}else{
		while(anim->loopCount-- && ret == 0 && render->stop == 0){
			for(i=anim->frameCount; i--;){
				doRender(render, anim->curFrame);
				clock_gettime(CLOCK_REALTIME, &wait);
				
				unsigned int sec = anim->frameDelayCs/100;
				anim->frameDelayCs -= sec*100;
				wait.tv_nsec += anim->frameDelayCs*10000000L;
				wait.tv_sec += sec + wait.tv_nsec/1000000000L;
				wait.tv_nsec%=1000000000L;
				
				ret=anim->decodeNextFrame(anim);
				if(ret != 0 || render->stop != 0)
					goto end;
				
				pthread_mutex_lock(&render->lock);
				pthread_cond_timedwait(&render->cond, &render->lock, &wait);
				pthread_mutex_unlock(&render->lock);
				
				if(render->stop != 0)
					goto end;
			}
		}
	}
	
end:
	anim->finaliseDecoding(anim);
	return NULL;
}

int omxRenderAnimation(OMX_RENDER *render, ANIM_IMAGE *anim, OMX_RENDER_DISP_CONF *dispConfig,
		OMX_RENDER_TRANSITION *transition){
	render->renderAnimation = 1;
	int ret = initRender(render);
	if(ret!= OMX_RENDER_OK){
		return ret;
	}
	if(transition != NULL && transition->type == BLEND)
		dispConfig->alpha = 15;
	
	ret = setOmxDisplayConfig(render, dispConfig);
	if(ret!= OMX_RENDER_OK){
		return ret;
	}
	ret = setUpRender(render, anim->curFrame);
	if(ret!= OMX_RENDER_OK){
		return ret;
	}
	
	struct ANIM_RENDER_PARAMS* animRenderParams = malloc(sizeof(struct ANIM_RENDER_PARAMS));
	animRenderParams->anim = anim;
	animRenderParams->render =  render;
	render->stop = 0;
	
	pthread_mutex_init(&render->lock, NULL);
	pthread_cond_init(&render->cond, NULL);
	
	pthread_create(&render->animRenderThread, NULL, doRenderAnimation, animRenderParams);
	
	if(transition != NULL && transition->type == BLEND){
		while(dispConfig->alpha<255){
			usleep(transition->durationMs*1000/24);
			dispConfig->alpha+=20;
			setOmxDisplayConfig(render, dispConfig);
		}
	}
	
	return OMX_RENDER_OK;
}

void stopAnimation(OMX_RENDER *render){
	if(render->renderAnimation){
		render->stop = 1;
		pthread_mutex_lock(&render->lock);
		pthread_cond_signal(&render->cond);
		pthread_mutex_unlock(&render->lock);
		pthread_join(render->animRenderThread, NULL);
		
		pthread_mutex_destroy(&render->lock);
		pthread_cond_destroy(&render->cond);
		render->renderAnimation = 0;
	}
}

int stopOmxImageRender(OMX_RENDER *render){
	int retVal=OMX_RENDER_OK;
	
	stopAnimation(render);
	
	// OMX_SendCommand(render->handle, OMX_CommandFlush, render->inPort, NULL);
	
	// ilclient_wait_for_event(render->component,OMX_EventCmdComplete, OMX_CommandFlush, 
			// 0, render->inPort, 0, ILCLIENT_PORT_FLUSH, TIMEOUT_MS);
			
	OMX_SendCommand(render->handle, OMX_CommandPortDisable, render->inPort, NULL);
		
	int ret=OMX_FreeBuffer(render->handle, render->inPort, render->pInputBufferHeader);
	if(ret!= OMX_ErrorNone){
		retVal|=OMX_RENDER_ERROR_MEMORY;
	}				
								
	ilclient_change_component_state(render->component, OMX_StateIdle);				
	ilclient_change_component_state(render->component, OMX_StateLoaded);
	
	COMPONENT_T *list[2];
	list[0]=render->component;
	list[1]=NULL;
	ilclient_cleanup_components(list);
	
	render->component = NULL;
	
	return retVal;
}
