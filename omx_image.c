/* Copyright (c) 2015, Benjamin Huber
 *               2012, Matt Ownby
 *                     Anthong Sale
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
#include <semaphore.h>

#include "omx_image.h"
#include "bcm_host.h"

#define TIMEOUT_MS 1500
#define DECODER_BUFFER_NUM 3

#define ALIGN2(x) (((x+1)>>1)<<1)

// Jpeg decoder

typedef struct JPEG_DECODER {
	ILCLIENT_T *client;
	COMPONENT_T *component;
	OMX_HANDLETYPE handle;
	int inPort;
	int outPort;
	sem_t semaphore;
	
	OMX_BUFFERHEADERTYPE *ppInputBufferHeader[DECODER_BUFFER_NUM];
	OMX_BUFFERHEADERTYPE *pOutputBufferHeader;
	
} JPEG_DECODER;

static void emptyBufferDone(void *data, COMPONENT_T *comp){
	JPEG_DECODER *decoder= (JPEG_DECODER*) data;
	sem_post(&decoder->semaphore);
}

static int portSettingsChanged(JPEG_DECODER *decoder, IMAGE *jpeg){
	
	OMX_PARAM_PORTDEFINITIONTYPE portdef;
	int ret;
	
	portdef.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
	portdef.nVersion.nVersion = OMX_VERSION;
	portdef.nPortIndex = decoder->outPort;
	OMX_GetParameter(decoder->handle,OMX_IndexParamPortDefinition, &portdef);
	
	jpeg->width = portdef.format.image.nFrameWidth;
	jpeg->height = portdef.format.image.nFrameHeight;
	jpeg->nData = portdef.nBufferSize;
	
	jpeg->colorSpace = COLOR_SPACE_YUV420P;
	
	OMX_SendCommand(decoder->handle, OMX_CommandPortEnable, decoder->outPort, NULL);  
	
	jpeg->pData=malloc(jpeg->nData);
	if(jpeg->pData == NULL){
		jpeg->nData=0;
		return OMX_IMAGE_ERROR_MEMORY;
	}
	
	ret = OMX_UseBuffer(decoder->handle, &decoder->pOutputBufferHeader, 
			decoder->outPort, NULL, portdef.nBufferSize, (OMX_U8 *) jpeg->pData);
	
	if (ret != OMX_ErrorNone) {
		return OMX_IMAGE_ERROR_MEMORY;
	}
	
	ret = OMX_FillThisBuffer(decoder->handle, decoder->pOutputBufferHeader);
	
	if (ret != OMX_ErrorNone) {
		return OMX_IMAGE_ERROR_MEMORY;
	} 
	
	return OMX_IMAGE_OK;
}

static int prepareDecoder(JPEG_DECODER *decoder){

	int ret = ilclient_create_component(decoder->client,
				&decoder->component, "image_decode",
				ILCLIENT_DISABLE_ALL_PORTS|
				ILCLIENT_ENABLE_INPUT_BUFFERS|
				ILCLIENT_ENABLE_OUTPUT_BUFFERS);

	if (ret != 0) {
		return OMX_IMAGE_ERROR_CREATING_COMP;
	}
	
	decoder->handle =ILC_GET_HANDLE(decoder->component);
	
	
	OMX_PORT_PARAM_TYPE port;
	port.nSize = sizeof(OMX_PORT_PARAM_TYPE);
	port.nVersion.nVersion = OMX_VERSION;
	
	OMX_GetParameter(decoder->handle, OMX_IndexParamImageInit, &port);
	if (port.nPorts != 2) {
		return OMX_IMAGE_ERROR_PORTS;
	}
	decoder->inPort = port.nStartPortNumber;
	decoder->outPort = port.nStartPortNumber + 1;
	
	decoder->pOutputBufferHeader = NULL;
	
	return OMX_IMAGE_OK;
}

static int startupDecoder(JPEG_DECODER * decoder){
	ilclient_change_component_state(decoder->component,OMX_StateIdle);
	
	OMX_IMAGE_PARAM_PORTFORMATTYPE imagePortFormat;
	memset(&imagePortFormat, 0, sizeof(OMX_IMAGE_PARAM_PORTFORMATTYPE));
	imagePortFormat.nSize = sizeof(OMX_IMAGE_PARAM_PORTFORMATTYPE);
	imagePortFormat.nVersion.nVersion = OMX_VERSION;
	imagePortFormat.nPortIndex = decoder->inPort;
	imagePortFormat.eCompressionFormat = OMX_IMAGE_CodingJPEG;
	OMX_SetParameter(decoder->handle,OMX_IndexParamImagePortFormat, &imagePortFormat);
	
	OMX_PARAM_PORTDEFINITIONTYPE portdef;
	portdef.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
	portdef.nVersion.nVersion = OMX_VERSION;
	portdef.nPortIndex = decoder->inPort;
	OMX_GetParameter(decoder->handle,OMX_IndexParamPortDefinition, &portdef);
	
	portdef.nBufferCountActual = DECODER_BUFFER_NUM;
	
	OMX_SetParameter(decoder->handle,OMX_IndexParamPortDefinition, &portdef);
	
	OMX_SendCommand(decoder->handle, OMX_CommandPortEnable, decoder->inPort, NULL);
	
	int i;
	for (i = 0; i < DECODER_BUFFER_NUM; i++) {
		if (OMX_AllocateBuffer(decoder->handle,
					&(decoder->ppInputBufferHeader[i]),
					decoder->inPort,
					NULL, portdef.nBufferSize) != OMX_ErrorNone) {
			return OMX_IMAGE_ERROR_MEMORY;
		}
	}
	
	int ret = ilclient_wait_for_event(decoder->component, OMX_EventCmdComplete, 
				OMX_CommandPortEnable, 0, decoder->inPort, 0, 0, TIMEOUT_MS);
	if (ret != 0) {
		return OMX_IMAGE_ERROR_PORTS;
	}
	
	ret = OMX_SendCommand(decoder->handle, OMX_CommandStateSet, OMX_StateExecuting, NULL);
	if (ret != OMX_ErrorNone) {
		return OMX_IMAGE_ERROR_EXECUTING;
	}
	
	return OMX_IMAGE_OK;
}

static int decodeJpeg(JPEG_DECODER * decoder, FILE *sourceImage, IMAGE *jpeg){
	
	char pSettingsChanged = 0, end = 0, eos = 0; 
	int bufferIndex = 0;
	int retVal = OMX_IMAGE_OK;
	
	OMX_BUFFERHEADERTYPE *pBufHeader = decoder->ppInputBufferHeader[bufferIndex];
	sem_init(&decoder->semaphore, 0, DECODER_BUFFER_NUM-1);
	ilclient_set_empty_buffer_done_callback(decoder->client, emptyBufferDone, decoder);
	
	bufferIndex=1;
	
	pBufHeader->nFilledLen = fread(pBufHeader->pBuffer, 1, pBufHeader->nAllocLen, sourceImage);
	
	pBufHeader->nOffset = 0;
	pBufHeader->nFlags = 0;
	
	if(feof(sourceImage)){
		pBufHeader->nFlags = OMX_BUFFERFLAG_EOS;
	}else if( pBufHeader->nFilledLen !=  pBufHeader->nAllocLen){
		retVal|=OMX_IMAGE_ERROR_READING;
		end=1;
	}
	
	while(end == 0 && retVal == OMX_IMAGE_OK){
		// We've got an eos event early this usually means that we are done decoding
		if(ilclient_remove_event(decoder->component, OMX_EventBufferFlag, decoder->outPort, 
				0, OMX_BUFFERFLAG_EOS, 0 )==0){
				eos=1;
				break;
		}

		int ret = OMX_EmptyThisBuffer(decoder->handle, pBufHeader);
		if (ret != OMX_ErrorNone) {
			retVal|=OMX_IMAGE_ERROR_MEMORY;
			break;
		}
		
		int s = sem_trywait(&decoder->semaphore);
		if (s == -1 && pSettingsChanged == 0) {
		
			if (ilclient_wait_for_event(decoder->component,OMX_EventPortSettingsChanged,decoder->outPort,
				0, 0, 1, ILCLIENT_EVENT_ERROR | ILCLIENT_PARAMETER_CHANGED, TIMEOUT_MS) == 0) {
			
				if ((retVal |= portSettingsChanged(decoder, jpeg)) != OMX_IMAGE_OK)
				{
					break;
				}
				pSettingsChanged=1;
				
			} else {
				
				retVal |= OMX_IMAGE_ERROR_PORTS;
				break;
			}
		}

		if (s == -1 && sem_wait(&decoder->semaphore) == -1 && errno == EINTR) {
				
			retVal |= OMX_IMAGE_ERROR_EXECUTING;
			break;
		}
		
		if(!feof(sourceImage)){
			
			pBufHeader = decoder->ppInputBufferHeader[bufferIndex];
			
			bufferIndex= (bufferIndex+1)% DECODER_BUFFER_NUM;
		
			pBufHeader->nFilledLen = fread(pBufHeader->pBuffer, 1, pBufHeader->nAllocLen, sourceImage);
			
			pBufHeader->nOffset = 0;
			pBufHeader->nFlags = 0;
		
			if(feof(sourceImage)){
				pBufHeader->nFlags = OMX_BUFFERFLAG_EOS;
			}else if( pBufHeader->nFilledLen !=  pBufHeader->nAllocLen){
				retVal |= OMX_IMAGE_ERROR_READING;
				break;
			}
			
		}else{
			end=1;
		}
		
		if(pSettingsChanged == 0 && ilclient_remove_event(decoder->component,
				OMX_EventPortSettingsChanged,decoder->outPort, 0, 0, 1) == 0){	 
			
			if ((retVal |= portSettingsChanged(decoder, jpeg)) != OMX_IMAGE_OK)
			{
				break;
			}
			pSettingsChanged=1;
		}
	}
	ilclient_set_empty_buffer_done_callback(decoder->client, NULL, NULL);
	
	if(pSettingsChanged == 0 && ilclient_wait_for_event(decoder->component,
				OMX_EventPortSettingsChanged,decoder->outPort, 0, 0, 1, 
				ILCLIENT_EVENT_ERROR | ILCLIENT_PARAMETER_CHANGED, TIMEOUT_MS) == 0){	 
		
		if ((retVal |= portSettingsChanged(decoder, jpeg)) == OMX_IMAGE_OK)
		{
			pSettingsChanged=1;
		}
		
	}
	
	if(pSettingsChanged == 1 && !eos && ilclient_wait_for_event(decoder->component, OMX_EventBufferFlag, 
				decoder->outPort, 0, OMX_BUFFERFLAG_EOS, 0, ILCLIENT_BUFFER_FLAG_EOS, TIMEOUT_MS) != 0 ){

		retVal|=OMX_IMAGE_ERROR_NO_EOS;
	}
	
	sem_destroy(&decoder->semaphore);
	
	int i = 0;
	for (i = 0; i < DECODER_BUFFER_NUM; i++) {
		int ret = OMX_FreeBuffer(decoder->handle,decoder->inPort, decoder->ppInputBufferHeader[i]);
		if(ret!= OMX_ErrorNone){
			retVal|=OMX_IMAGE_ERROR_MEMORY;
		}
	}
	
	int ret=OMX_SendCommand(decoder->handle, OMX_CommandPortDisable, decoder->inPort, NULL);
	
	if(ret!= OMX_ErrorNone){
		retVal|=OMX_IMAGE_ERROR_PORTS;
	}
			
	ilclient_wait_for_event(decoder->component, OMX_EventCmdComplete, 
			OMX_CommandPortDisable, 0, decoder->inPort, 0, 
			ILCLIENT_PORT_DISABLED, TIMEOUT_MS);		
	
	if(pSettingsChanged==1){	
		OMX_SendCommand(decoder->handle, OMX_CommandFlush, decoder->outPort, NULL);
			
		ilclient_wait_for_event(decoder->component,OMX_EventCmdComplete, OMX_CommandFlush, 
			0, decoder->outPort, 0, ILCLIENT_PORT_FLUSH ,TIMEOUT_MS);	
				
		ret= OMX_FreeBuffer(decoder->handle, decoder->outPort, decoder->pOutputBufferHeader);
		
		if(ret!= OMX_ErrorNone){
			retVal|=OMX_IMAGE_ERROR_MEMORY;
		}
		
	}
	
	if(pSettingsChanged==1){
		ret= OMX_SendCommand(decoder->handle, OMX_CommandPortDisable, decoder->outPort, NULL);
		
		if(ret!= OMX_ErrorNone){
			retVal|=OMX_IMAGE_ERROR_PORTS;
		}
	}
	
	ilclient_change_component_state(decoder->component, OMX_StateIdle);
	ilclient_change_component_state(decoder->component, OMX_StateLoaded);
					
	return retVal;
}


int omxDecodeJpeg(ILCLIENT_T *client, FILE *sourceFile, IMAGE *jpeg){
	JPEG_DECODER decoder;
	decoder.client=client;
	if(!sourceFile)
		return OMX_IMAGE_ERROR_FILE_NOT_FOUND;
	
	int ret = prepareDecoder(&decoder);
	if (ret != OMX_IMAGE_OK)
		return ret;
		
	ret = startupDecoder(&decoder);
	if (ret != OMX_IMAGE_OK)
		return ret;
		
	ret = decodeJpeg(&decoder, sourceFile, jpeg);
	if (ret != OMX_IMAGE_OK)
		return ret;
	
	COMPONENT_T *list[2];
	list[0]=decoder.component;
	list[1]=NULL;
	ilclient_cleanup_components(list);
	return ret;
}

// Resizer

typedef struct OMX_RESIZER {
	ILCLIENT_T *client;
	COMPONENT_T *component;
	OMX_HANDLETYPE handle;
	int inPort;
	int outPort;
	
	OMX_BUFFERHEADERTYPE *pInputBufferHeader;
	OMX_BUFFERHEADERTYPE *pOutputBufferHeader;
	
} OMX_RESIZER;

static int prepareResizer(OMX_RESIZER *resizer){

	int ret = ilclient_create_component(resizer->client,
					&resizer->component, "resize",
					ILCLIENT_DISABLE_ALL_PORTS|
					ILCLIENT_ENABLE_INPUT_BUFFERS|
					ILCLIENT_ENABLE_OUTPUT_BUFFERS);
	if (ret != 0) {
		return OMX_IMAGE_ERROR_CREATING_COMP;
	}

	resizer->handle = ILC_GET_HANDLE(resizer->component);
	
	OMX_PORT_PARAM_TYPE port;
	port.nSize = sizeof(OMX_PORT_PARAM_TYPE);
	port.nVersion.nVersion = OMX_VERSION;

	OMX_GetParameter(resizer->handle,OMX_IndexParamImageInit, &port);
	if (port.nPorts != 2) {
		return OMX_IMAGE_ERROR_PORTS;
	}
	resizer->inPort = port.nStartPortNumber;
	resizer->outPort = port.nStartPortNumber + 1;

	resizer->pOutputBufferHeader = NULL;
	
	return OMX_IMAGE_OK;
}


static int startupResizer(OMX_RESIZER *resizer, IMAGE *inImage){
	OMX_PARAM_PORTDEFINITIONTYPE portdef;
	int ret;

	ilclient_change_component_state(resizer->component, OMX_StateIdle);

	portdef.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
	portdef.nVersion.nVersion = OMX_VERSION;
	portdef.nPortIndex = resizer->inPort;

	ret = OMX_GetParameter(resizer->handle, OMX_IndexParamPortDefinition, &portdef);
	if (ret != OMX_ErrorNone) {
		return OMX_IMAGE_ERROR_PARAMETER;
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
		

	ret = OMX_SetParameter(resizer->handle, OMX_IndexParamPortDefinition, &portdef);
	if (ret != OMX_ErrorNone) {	
		return OMX_IMAGE_ERROR_PARAMETER;
	}
	
	ret = OMX_SendCommand(resizer->handle, OMX_CommandPortEnable, resizer->inPort, NULL);
	if(ret != OMX_ErrorNone){
		return OMX_IMAGE_ERROR_PORTS;
	}

	ret = OMX_UseBuffer(resizer->handle,&resizer->pInputBufferHeader,resizer->inPort, 
			NULL, portdef.nBufferSize, (OMX_U8 *) inImage->pData);
			
	if(ret != OMX_ErrorNone){
		return OMX_IMAGE_ERROR_MEMORY;
	}
	
	ilclient_change_component_state(resizer->component, OMX_StateExecuting);
	
	return OMX_IMAGE_OK;
}

static int resizePortSettingsChanged(OMX_RESIZER *resizer, IMAGE *outImage){
	OMX_PARAM_PORTDEFINITIONTYPE portdef;
	int ret;
	
	portdef.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
	portdef.nVersion.nVersion = OMX_VERSION;
	portdef.nPortIndex = resizer->outPort;
	OMX_GetParameter(resizer->handle, OMX_IndexParamPortDefinition, &portdef);
	
	portdef.format.image.eCompressionFormat = OMX_IMAGE_CodingUnused;
	portdef.format.image.bFlagErrorConcealment = OMX_FALSE;
	
	if(outImage->colorSpace == COLOR_SPACE_YUV420P)
		portdef.format.image.eColorFormat = OMX_COLOR_FormatYUV420PackedPlanar;
	else if(outImage->colorSpace == COLOR_SPACE_RGBA)
		portdef.format.image.eColorFormat = OMX_COLOR_Format32bitABGR8888;
	else if(outImage->colorSpace == COLOR_SPACE_RGB16)
		portdef.format.image.eColorFormat = OMX_COLOR_Format16bitRGB565;
	
	portdef.format.image.nFrameWidth = outImage->width;
	portdef.format.image.nFrameHeight = outImage->height;
	portdef.format.image.nStride = 0;
	portdef.format.image.nSliceHeight = 0;
	
	ret = OMX_SetParameter(resizer->handle, OMX_IndexParamPortDefinition, &portdef);
	if(ret != OMX_ErrorNone){
		return OMX_IMAGE_ERROR_PARAMETER;
	}
	
	ret = OMX_GetParameter(resizer->handle, OMX_IndexParamPortDefinition, &portdef);
	if(ret != OMX_ErrorNone){
		return OMX_IMAGE_ERROR_PARAMETER;
	}
	
	ret = OMX_SendCommand(resizer->handle, OMX_CommandPortEnable, resizer->outPort, NULL);
	if(ret != OMX_ErrorNone){
		return OMX_IMAGE_ERROR_PORTS;
	}
	
	outImage->nData = portdef.nBufferSize;
	
	outImage->pData=malloc(outImage->nData);
	if(outImage->pData == NULL){
		outImage->nData=0;
		return OMX_IMAGE_ERROR_MEMORY;
	}
	
	ret = OMX_UseBuffer(resizer->handle, &resizer->pOutputBufferHeader, 
			resizer->outPort, NULL, portdef.nBufferSize, (OMX_U8 *) outImage->pData);
	
	if (ret != OMX_ErrorNone) {
		return OMX_IMAGE_ERROR_MEMORY;
	}
	return OMX_IMAGE_OK;
}

static int doResize(OMX_RESIZER *resizer, IMAGE *inImage, IMAGE *outImage){
	int retVal= OMX_IMAGE_OK;
	OMX_BUFFERHEADERTYPE *pBufHeader = resizer->pInputBufferHeader;
	pBufHeader->nFilledLen=inImage->nData;
	pBufHeader->nFlags = OMX_BUFFERFLAG_EOS;
	
	int ret = OMX_EmptyThisBuffer(resizer->handle, pBufHeader);
	if (ret != OMX_ErrorNone) {
		 retVal |= OMX_IMAGE_ERROR_MEMORY;
	}
	
	if(ilclient_wait_for_event(resizer->component,OMX_EventPortSettingsChanged,resizer->outPort, 
			0, 0, 1, ILCLIENT_EVENT_ERROR | ILCLIENT_PARAMETER_CHANGED, TIMEOUT_MS) == 0){	
		
		destroyImage(inImage);
		retVal|= resizePortSettingsChanged(resizer, outImage);
		if(retVal == OMX_IMAGE_OK){
			if (OMX_FillThisBuffer(resizer->handle, 
				resizer->pOutputBufferHeader) != OMX_ErrorNone) {
				retVal|=OMX_IMAGE_ERROR_MEMORY;
			}
		}		
	}
	
	if(ilclient_wait_for_event(resizer->component, OMX_EventBufferFlag, resizer->outPort, 
				0, OMX_BUFFERFLAG_EOS, 0, ILCLIENT_BUFFER_FLAG_EOS, TIMEOUT_MS) != 0){
		
		retVal|= OMX_IMAGE_ERROR_NO_EOS;
	}
	
	ret= OMX_FreeBuffer(resizer->handle, resizer->inPort, resizer->pInputBufferHeader);
	if(ret!= OMX_ErrorNone){
		retVal|=OMX_IMAGE_ERROR_MEMORY;
	}
	
	ret = OMX_SendCommand(resizer->handle, OMX_CommandPortDisable, resizer->inPort, NULL);
	if(ret!= OMX_ErrorNone){
		retVal |= OMX_IMAGE_ERROR_PORTS;
	}
		
	ilclient_wait_for_event(resizer->component, OMX_EventCmdComplete, 
			OMX_CommandPortDisable, 0, resizer->inPort, 0, 
			ILCLIENT_PORT_DISABLED, TIMEOUT_MS);		
	
	OMX_SendCommand(resizer->handle, OMX_CommandFlush, resizer->outPort, NULL);
		
	ilclient_wait_for_event(resizer->component,OMX_EventCmdComplete, OMX_CommandFlush, 
		0, resizer->outPort, 0, ILCLIENT_PORT_FLUSH ,TIMEOUT_MS);	
			
	ret= OMX_FreeBuffer(resizer->handle, resizer->outPort, resizer->pOutputBufferHeader);	
	if(ret!= OMX_ErrorNone){
		retVal|=OMX_IMAGE_ERROR_MEMORY;
	}
	
	ret= OMX_SendCommand(resizer->handle, OMX_CommandPortDisable, resizer->outPort, NULL);	
	if(ret!= OMX_ErrorNone){
		retVal|=OMX_IMAGE_ERROR_PORTS;
	}
	
	ilclient_change_component_state(resizer->component, OMX_StateIdle);
	ilclient_change_component_state(resizer->component, OMX_StateLoaded);
	
	return retVal;
}


int omxResize(ILCLIENT_T *client, IMAGE *inImage, IMAGE *outImage){
	OMX_RESIZER resizer;
	resizer.client=client;
	resizer.pInputBufferHeader=NULL;
	int ret = prepareResizer(&resizer);
	if (ret != OMX_IMAGE_OK){
		return ret;
	}
	ret = startupResizer(&resizer, inImage);
	if (ret != OMX_IMAGE_OK){
		return ret;
	}
	ret = doResize(&resizer, inImage, outImage);
	if (ret != OMX_IMAGE_OK){
		return ret;
	}
	
	COMPONENT_T *list[2];
	list[0]=resizer.component;
	list[1]=NULL;
	ilclient_cleanup_components(list);
	return ret;
}
