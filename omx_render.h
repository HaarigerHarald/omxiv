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

#ifndef OMXRENDER_H
#define OMXRENDER_H
 
#include "image_def.h"
#include "ilclient.h"
#include <pthread.h>

#define OMX_RENDER_OK 0x0
#define OMX_RENDER_ERROR_CREATE_COMP 0x01
#define OMX_RENDER_ERROR_EXECUTING 0x02
#define OMX_RENDER_ERROR_PARAMETER 0x04
#define OMX_RENDER_ERROR_PORTS 0x08
#define OMX_RENDER_ERROR_UNKNOWN 0x10
#define OMX_RENDER_ERROR_MEMORY 0x20
#define OMX_RENDER_ERROR_DISP_CONF 0x40

#define OMX_DISP_CONFIG_FLAG_NO_ASPECT 0x1
#define OMX_DISP_CONFIG_FLAG_MIRROR 0x2
#define OMX_DISP_CONFIG_FLAG_CENTER 0x4

#define INIT_OMX_DISP_CONF {0, 0, 0, 0, 0, 0, 0, 0, OMX_DISPLAY_MODE_LETTERBOX, 0, 0, 0}

typedef struct OMX_RENDER_DISP_CONF{	
	int xOffset; 
	int width; 
	int yOffset;
	int height;
	int rotation;
	int layer;
	int display;
	int alpha;
	int mode;
	int configFlags;
	
	unsigned int cImageWidth;
	unsigned int cImageHeight;
	
} OMX_RENDER_DISP_CONF;

struct OMX_RENDER_TRANSITION{
	enum transition_t{NONE, BLEND} type;
	int durationMs;
	
} OMX_RENDER_TRANSITION;

#define INIT_OMX_RENDER {0, 0, 0, 0, 0, 0, 0, 0, {0}, 0, 0, 0, 0, 0, 0, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER}

typedef struct OMX_RENDER{
	ILCLIENT_T *client;
	
	COMPONENT_T *renderComponent;
	OMX_HANDLETYPE renderHandle;
	int renderInPort;
	
	COMPONENT_T *resizeComponent;
	OMX_HANDLETYPE resizeHandle;
	int resizeInPort;
	int resizeOutPort;
	
	struct OMX_RENDER_TRANSITION transition;
	OMX_RENDER_DISP_CONF *dispConfig;
	
	OMX_BUFFERHEADERTYPE *pInputBufferHeader;
	
	char renderAnimation;
	volatile char stop;
	volatile char pSettingsChanged;
	pthread_t animRenderThread;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	
} OMX_RENDER;

/** Change display configuration of the image render component. */
int setOmxDisplayConfig(OMX_RENDER *render);

/** Renders an image on an omx-video_render component. */
int omxRenderImage(OMX_RENDER *render, IMAGE *image);

/** Stops rendering of current image and cleans up. */
int stopOmxImageRender(OMX_RENDER *render);

int omxRenderAnimation(OMX_RENDER *render, ANIM_IMAGE *anim);

void stopAnimation(OMX_RENDER *render);

#endif
