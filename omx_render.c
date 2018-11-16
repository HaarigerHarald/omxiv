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

#define ALIGN2(x) (((x+1)>>1)<<1)

#define TIMEOUT_MS 2000

static int initRender(OMX_RENDER *render){
	int ret = ilclient_create_component(render->client,
					&render->renderComponent,"video_render",
					ILCLIENT_DISABLE_ALL_PORTS|
					ILCLIENT_ENABLE_INPUT_BUFFERS);
							
	if (ret != 0) {
		return OMX_RENDER_ERROR_CREATE_COMP;
	}
	
	render->renderHandle = ILC_GET_HANDLE(render->renderComponent);
	
	OMX_PORT_PARAM_TYPE port;
	port.nSize = sizeof(OMX_PORT_PARAM_TYPE);
	port.nVersion.nVersion = OMX_VERSION;
	
	OMX_GetParameter(render->renderHandle, OMX_IndexParamVideoInit, &port);
	if (port.nPorts != 1) {
		return OMX_RENDER_ERROR_PORTS;
	}
	render->renderInPort = port.nStartPortNumber;
	
	// Init resize
	ret = ilclient_create_component(render->client,
					&render->resizeComponent, "resize",
					ILCLIENT_DISABLE_ALL_PORTS|
					ILCLIENT_ENABLE_INPUT_BUFFERS|
					ILCLIENT_ENABLE_OUTPUT_BUFFERS);
	if (ret != 0) {
		return OMX_RENDER_ERROR_CREATE_COMP;
	}

	render->resizeHandle = ILC_GET_HANDLE(render->resizeComponent);

	OMX_GetParameter(render->resizeHandle,OMX_IndexParamImageInit, &port);
	if (port.nPorts != 2) {
		return OMX_RENDER_ERROR_PORTS;
	}
	render->resizeInPort = port.nStartPortNumber;
	render->resizeOutPort = port.nStartPortNumber + 1;
	
	return OMX_RENDER_OK;
}

static int initResizer(OMX_RENDER *render, IMAGE *inImage){
	OMX_PARAM_PORTDEFINITIONTYPE portdef;
	int ret;

	ilclient_change_component_state(render->resizeComponent, OMX_StateIdle);

	portdef.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
	portdef.nVersion.nVersion = OMX_VERSION;
	portdef.nPortIndex = render->resizeInPort;

	ret = OMX_GetParameter(render->resizeHandle, OMX_IndexParamPortDefinition, &portdef);
	if (ret != OMX_ErrorNone) {
		return OMX_RENDER_ERROR_PARAMETER;
	}
	
	portdef.format.image.eCompressionFormat = OMX_IMAGE_CodingUnused;
	portdef.format.image.bFlagErrorConcealment = OMX_FALSE;
	portdef.format.image.nFrameWidth = inImage->width;
	portdef.format.image.nFrameHeight = inImage->height;
	portdef.format.image.nStride = 0;
	portdef.format.image.nSliceHeight = 0;
	
	
	if(inImage->colorSpace == COLOR_SPACE_YUV420P)
		portdef.format.image.eColorFormat = OMX_COLOR_FormatYUV420PackedPlanar;
	else if(inImage->colorSpace == COLOR_SPACE_RGBA)
		portdef.format.image.eColorFormat = OMX_COLOR_Format32bitABGR8888;
	else if(inImage->colorSpace == COLOR_SPACE_RGB16)
		portdef.format.image.eColorFormat = OMX_COLOR_Format16bitRGB565;
		
	portdef.nBufferSize=inImage->nData;
		

	ret = OMX_SetParameter(render->resizeHandle, OMX_IndexParamPortDefinition, &portdef);
	if (ret != OMX_ErrorNone) {	
		return OMX_RENDER_ERROR_PARAMETER;
	}
	
	ret = OMX_SendCommand(render->resizeHandle, OMX_CommandPortEnable, render->resizeInPort, NULL);
	if(ret != OMX_ErrorNone){
		return OMX_RENDER_ERROR_PORTS;
	}

	ret = OMX_UseBuffer(render->resizeHandle,&render->pInputBufferHeader,render->resizeInPort, 
			NULL, portdef.nBufferSize, (OMX_U8 *) inImage->pData);
			
	if(ret != OMX_ErrorNone){
		return OMX_RENDER_ERROR_MEMORY;
	}
	
	render->pSettingsChanged = 0;
	ilclient_change_component_state(render->resizeComponent, OMX_StateExecuting);
	
	return OMX_RENDER_OK;
}

static int resizePortSettingsChanged(OMX_RENDER *render, unsigned int width, unsigned int height){
	OMX_PARAM_PORTDEFINITIONTYPE portdef;
	int ret;
	
	portdef.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
	portdef.nVersion.nVersion = OMX_VERSION;
	portdef.nPortIndex = render->resizeOutPort;
	OMX_GetParameter(render->resizeHandle, OMX_IndexParamPortDefinition, &portdef);
	
	portdef.format.image.eCompressionFormat = OMX_IMAGE_CodingUnused;
	portdef.format.image.bFlagErrorConcealment = OMX_FALSE;
	portdef.format.image.eColorFormat = OMX_COLOR_Format32bitABGR8888;
	
	portdef.format.image.nFrameWidth = width;
	portdef.format.image.nFrameHeight = height;
	portdef.format.image.nStride = 0;
	portdef.format.image.nSliceHeight = 0;
	
	ret = OMX_SetParameter(render->resizeHandle, OMX_IndexParamPortDefinition, &portdef);
	if(ret != OMX_ErrorNone){
		return OMX_RENDER_ERROR_PARAMETER;
	}
	
	ilclient_change_component_state(render->renderComponent, OMX_StateIdle);
	ilclient_change_component_state(render->renderComponent, OMX_StateExecuting);
	
	portdef.nPortIndex = render->renderInPort;
	OMX_GetParameter(render->renderHandle, OMX_IndexParamPortDefinition, &portdef);
	
	portdef.format.image.eCompressionFormat = OMX_IMAGE_CodingUnused;
	portdef.format.image.bFlagErrorConcealment = OMX_FALSE;
	portdef.format.image.eColorFormat = OMX_COLOR_Format32bitABGR8888;
	
	portdef.format.image.nFrameWidth = width;
	portdef.format.image.nFrameHeight = height;
	portdef.format.image.nStride = 0;
	portdef.format.image.nSliceHeight = 0;
	
	ret = OMX_SetParameter(render->renderHandle, OMX_IndexParamPortDefinition, &portdef);
	if(ret != OMX_ErrorNone){
		return OMX_RENDER_ERROR_PARAMETER;
	}
	
	OMX_SetupTunnel(render->resizeHandle, render->resizeOutPort,
		    render->renderHandle, render->renderInPort);
	
	
	ret = OMX_SendCommand(render->resizeHandle, OMX_CommandPortEnable, render->resizeOutPort, NULL);
	if(ret != OMX_ErrorNone){
		return OMX_RENDER_ERROR_PORTS;
	}
	
	ret = OMX_SendCommand(render->renderHandle, OMX_CommandPortEnable, render->renderInPort, NULL);
	if(ret != OMX_ErrorNone){
		return OMX_RENDER_ERROR_PORTS;
	}
	
	return OMX_RENDER_OK;
}

static int doRender(OMX_RENDER *render, IMAGE *inImage, unsigned int width, unsigned int height){
	int retVal= OMX_RENDER_OK;
	OMX_BUFFERHEADERTYPE *pBufHeader = render->pInputBufferHeader;
	pBufHeader->nFilledLen=inImage->nData;
	pBufHeader->nFlags = OMX_BUFFERFLAG_EOS;
	
	int ret = OMX_EmptyThisBuffer(render->resizeHandle, pBufHeader);
	if (ret != OMX_ErrorNone) {
		 retVal |= OMX_RENDER_ERROR_MEMORY;
	}
	
	if(render->pSettingsChanged == 0 && ilclient_wait_for_event(render->resizeComponent,OMX_EventPortSettingsChanged, 
			render->resizeOutPort, 0, 0, 1, ILCLIENT_EVENT_ERROR | ILCLIENT_PARAMETER_CHANGED, TIMEOUT_MS) == 0){	
		
		retVal|= resizePortSettingsChanged(render, width, height);
		render->pSettingsChanged=1;
	}
	
	ilclient_wait_for_event(render->renderComponent, OMX_EventBufferFlag, render->renderInPort, 
		0, OMX_BUFFERFLAG_EOS, 0, ILCLIENT_BUFFER_FLAG_EOS, TIMEOUT_MS);
	
	return retVal;
}

int setOmxDisplayConfig(OMX_RENDER *render){
	OMX_CONFIG_DISPLAYREGIONTYPE dispConfRT;
	OMX_RENDER_DISP_CONF *dispConf = render->dispConfig;
	memset(&dispConfRT, 0 , sizeof(OMX_CONFIG_DISPLAYREGIONTYPE));
	dispConfRT.nPortIndex = render->renderInPort;
	dispConfRT.nSize= sizeof(OMX_CONFIG_DISPLAYREGIONTYPE);
	dispConfRT.nVersion.nVersion = OMX_VERSION;
	
	OMX_DISPLAYSETTYPE set = OMX_DISPLAY_SET_FULLSCREEN|OMX_DISPLAY_SET_NOASPECT|
			OMX_DISPLAY_SET_MODE|OMX_DISPLAY_SET_TRANSFORM|OMX_DISPLAY_SET_NUM;

	
	dispConfRT.noaspect = (dispConf->configFlags & OMX_DISP_CONFIG_FLAG_NO_ASPECT) ? OMX_TRUE : OMX_FALSE;

	if(dispConf->width != 0 && dispConf->height != 0){
		set|= OMX_DISPLAY_SET_DEST_RECT;
		if(dispConf->configFlags & OMX_DISP_CONFIG_FLAG_CENTER){
			if(dispConf->rotation == 90 || dispConf->rotation == 270){
				if(dispConf->cImageHeight > dispConf->width){
					float shrink = (float) dispConf->width / dispConf->cImageHeight;
					dispConfRT.dest_rect.width = dispConf->width;
					dispConfRT.dest_rect.height = dispConf->cImageWidth*shrink;
				}else if(dispConf->cImageWidth > dispConf->height){
					float shrink = (float) dispConf->height / dispConf->cImageWidth;
					dispConfRT.dest_rect.height = dispConf->height;
					dispConfRT.dest_rect.width = dispConf->cImageHeight*shrink;
				}else{
					dispConfRT.dest_rect.width = dispConf->cImageHeight;
					dispConfRT.dest_rect.height = dispConf->cImageWidth;
				}
				
				dispConfRT.dest_rect.x_offset = dispConf->xOffset + 
					(dispConf->width - dispConf->cImageHeight)/2;
				dispConfRT.dest_rect.y_offset = dispConf->yOffset + 
					(dispConf->height - dispConf->cImageWidth)/2;
			}else{
				dispConfRT.dest_rect.width = dispConf->cImageWidth;
				dispConfRT.dest_rect.height = dispConf->cImageHeight;
			}
			
			dispConfRT.dest_rect.x_offset = dispConf->xOffset + 
				(dispConf->width - dispConfRT.dest_rect.width)/2;
			dispConfRT.dest_rect.y_offset = dispConf->yOffset + 
				(dispConf->height - dispConfRT.dest_rect.height)/2;
			
		}else{
			dispConfRT.dest_rect.x_offset = dispConf->xOffset;
			dispConfRT.dest_rect.y_offset = dispConf->yOffset;
			dispConfRT.dest_rect.width = dispConf->width;
			dispConfRT.dest_rect.height = dispConf->height;
		}
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
		dispConfRT.alpha|=OMX_DISPLAY_ALPHA_FLAGS_MIX;
	}
	
	if(dispConf->layer != 0){
		set|= OMX_DISPLAY_SET_LAYER;
		dispConfRT.layer=dispConf->layer;
	}
	
	dispConfRT.num = dispConf->display;
	dispConfRT.mode = dispConf->mode;
	dispConfRT.set= set;
	if(OMX_SetConfig(render->renderHandle, OMX_IndexConfigDisplayRegion, &dispConfRT) != OMX_ErrorNone){
		return OMX_RENDER_ERROR_DISP_CONF;
	}
	return OMX_RENDER_OK;
}

static void calculateResize(OMX_RENDER_DISP_CONF *dispConf, uint32_t* pWidth, uint32_t* pHeight){
	uint32_t sWidth, sHeight;
	
	if(dispConf->height > 0 && dispConf->width > 0){
		sWidth = dispConf->width;
		sHeight = dispConf->height;
	}else{
		graphics_get_display_size(dispConf->display, &sWidth, &sHeight);
	}
	
	if(dispConf->configFlags & OMX_DISP_CONFIG_FLAG_CENTER){
		if(dispConf->rotation == 90 || dispConf->rotation == 270){
			if(dispConf->cImageWidth < sHeight &&  dispConf->cImageHeight < sWidth){
				sWidth = dispConf->cImageHeight;
				sHeight = dispConf->cImageWidth;
			}else{
				uint32_t rotHeight = sHeight;
				sHeight = sWidth;
				sWidth = rotHeight;
			}
		}else if(dispConf->cImageWidth < sWidth &&  dispConf->cImageHeight < sHeight){
			sWidth = dispConf->cImageWidth;
			sHeight = dispConf->cImageHeight;
		}
	}else if(dispConf->rotation == 90 || dispConf->rotation == 270){
		uint32_t rotHeight = sHeight;
		sHeight = sWidth;
		sWidth = rotHeight;
	}
	
	if(!(dispConf->configFlags & OMX_DISP_CONFIG_FLAG_NO_ASPECT)){
		float dAspect = (float) sWidth / sHeight;
		float iAspect = (float) dispConf->cImageWidth / dispConf->cImageHeight;

		if(dAspect > iAspect){
			(*pWidth) = ALIGN2((int) (sHeight * iAspect));
			(*pHeight) = sHeight;
		}else{
			(*pHeight) = ALIGN2((int) (sWidth / iAspect));
			(*pWidth) = sWidth;
		}
	}else{
		(*pHeight) = sHeight;
		(*pWidth) = sWidth;
	}
}


int omxRenderImage(OMX_RENDER *render, IMAGE *image){
	uint32_t width, height;
	render->dispConfig->cImageWidth = image->width;
	render->dispConfig->cImageHeight = image->height;
	calculateResize(render->dispConfig, &width, &height);
	render->dispConfig->cImageWidth = width;
	render->dispConfig->cImageHeight = height;
	
	render->renderAnimation = 0;
	int ret = initRender(render);
	if(ret!= OMX_RENDER_OK){
		return ret;
	}
	
	ret = initResizer(render, image);
	if(ret!= OMX_RENDER_OK){
		return ret;
	}
	
	if(render->transition.type == BLEND)
		render->dispConfig->alpha = 15;
	
	ret = setOmxDisplayConfig(render);
	if(ret!= OMX_RENDER_OK){
		return ret;
	}
	ret = doRender(render, image, width, height);
	if(ret!= OMX_RENDER_OK){
		return ret;
	}
	
	ilclient_change_component_state(render->resizeComponent, OMX_StateIdle);
	
	if(render->transition.type == BLEND){
		while(render->dispConfig->alpha<255){
			usleep(render->transition.durationMs*1000/48);
			render->dispConfig->alpha+=10;
			setOmxDisplayConfig(render);
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
				clock_gettime(CLOCK_REALTIME, &wait);
				doRender(render, anim->curFrame,
					render->dispConfig->cImageWidth, render->dispConfig->cImageHeight);
				
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
				
				render->pInputBufferHeader->pBuffer = (OMX_U8*) anim->curFrame->pData;
			}
		}
	}else{
		while(anim->loopCount-- && ret == 0 && render->stop == 0){
			for(i=anim->frameCount; i--;){
				clock_gettime(CLOCK_REALTIME, &wait);
				doRender(render, anim->curFrame, 
					render->dispConfig->cImageWidth, render->dispConfig->cImageHeight);
				
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
				
				render->pInputBufferHeader->pBuffer = (OMX_U8*) anim->curFrame->pData;
			}
		}
	}
	
end:
	anim->finaliseDecoding(anim);
	return NULL;
}

int omxRenderAnimation(OMX_RENDER *render, ANIM_IMAGE *anim){
	render->renderAnimation = 1;
	render->dispConfig->cImageWidth = anim->curFrame->width;
	render->dispConfig->cImageHeight = anim->curFrame->height;
	
	uint32_t width, height;
	calculateResize(render->dispConfig, &width, &height);
	render->dispConfig->cImageWidth = width;
	render->dispConfig->cImageHeight = height;
	
	int ret = initRender(render);
	if(ret!= OMX_RENDER_OK){
		return ret;
	}
	
	ret = initResizer(render, anim->curFrame);
	if(ret!= OMX_RENDER_OK){
		return ret;
	}
	
	if(render->transition.type == BLEND)
		render->dispConfig->alpha = 15;
	
	ret = setOmxDisplayConfig(render);
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
	
	if(render->transition.type == BLEND){
		while(render->dispConfig->alpha<255){
			usleep(render->transition.durationMs*1000/48);
			render->dispConfig->alpha+=10;
			setOmxDisplayConfig(render);
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
	
	// OMX_SendCommand(render->renderHandle, OMX_CommandFlush, render->renderInPort, NULL);
	
	// ilclient_wait_for_event(render->renderComponent,OMX_EventCmdComplete, OMX_CommandFlush, 
			// 0, render->renderInPort, 0, ILCLIENT_PORT_FLUSH, TIMEOUT_MS);
			
	int ret= OMX_FreeBuffer(render->resizeHandle, render->resizeInPort, render->pInputBufferHeader);
	if(ret!= OMX_ErrorNone){
		retVal|=OMX_RENDER_ERROR_MEMORY;
	}
	
	ret = OMX_SendCommand(render->resizeHandle, OMX_CommandPortDisable, render->resizeInPort, NULL);
	if(ret!= OMX_ErrorNone){
		retVal |= OMX_RENDER_ERROR_PORTS;
	}
		
	ilclient_wait_for_event(render->resizeComponent, OMX_EventCmdComplete, 
			OMX_CommandPortDisable, 0, render->resizeInPort, 0, 
			ILCLIENT_PORT_DISABLED, TIMEOUT_MS);		
	
	OMX_SendCommand(render->resizeHandle, OMX_CommandFlush, render->resizeOutPort, NULL);
	OMX_SendCommand(render->renderHandle, OMX_CommandFlush, render->renderInPort, NULL);
		
	ilclient_wait_for_event(render->resizeComponent,OMX_EventCmdComplete, OMX_CommandFlush, 
		0, render->resizeOutPort, 0, ILCLIENT_PORT_FLUSH ,TIMEOUT_MS);
		
	ilclient_wait_for_event(render->renderComponent,OMX_EventCmdComplete, OMX_CommandFlush, 
		0, render->renderInPort, 0, ILCLIENT_PORT_FLUSH ,TIMEOUT_MS);

	ret= OMX_SendCommand(render->resizeHandle, OMX_CommandPortDisable, render->resizeOutPort, NULL);	
	if(ret!= OMX_ErrorNone){
		retVal|=OMX_RENDER_ERROR_PORTS;
	}
			
	ret = OMX_SendCommand(render->renderHandle, OMX_CommandPortDisable, render->renderInPort, NULL);
	if(ret!= OMX_ErrorNone){
		retVal|=OMX_RENDER_ERROR_PORTS;
	}
	
	ilclient_change_component_state(render->resizeComponent, OMX_StateIdle);
	ilclient_change_component_state(render->resizeComponent, OMX_StateLoaded);	
								
	ilclient_change_component_state(render->renderComponent, OMX_StateIdle);				
	ilclient_change_component_state(render->renderComponent, OMX_StateLoaded);
	
	COMPONENT_T *list[3];
	list[0]=render->renderComponent;
	list[1]=render->resizeComponent;
	list[2]=NULL;
	ilclient_cleanup_components(list);
	
	render->renderComponent = NULL;
	render->resizeComponent = NULL;
	
	return retVal;
}
