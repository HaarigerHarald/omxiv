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

#ifndef OMXIMAGE_H
#define OMXIMAGE_H

#include "ilclient.h"
#include "image_def.h"

#define OMX_IMAGE_OK 0x0
#define OMX_IMAGE_ERROR_MEMORY  0x1
#define OMX_IMAGE_ERROR_CREATING_COMP 0x2
#define OMX_IMAGE_ERROR_PORTS 0x4
#define OMX_IMAGE_ERROR_EXECUTING 0x8
#define OMX_IMAGE_ERROR_NO_EOS 0x10
#define OMX_IMAGE_ERROR_FILE_NOT_FOUND 0x20
#define OMX_IMAGE_ERROR_READING 0x40
#define OMX_IMAGE_ERROR_PARAMETER 0x80

/** Decodes jpeg image. Decoded images are in yuv420 color space.
 *  Note: Can't decode progressive jpegs or jpegs with more than
 *  3 color components. */
int omxDecodeJpeg(ILCLIENT_T *client, FILE *jpegFile, IMAGE *jpeg);

/** Resizes inImage to outImage. Make sure to set width, 
 *  height and colorSpace of outImage before calling this. 
 *  Note: The resize component can't handle rgb24 color space 
 *  use rgba instead. */
int omxResize(ILCLIENT_T *client, IMAGE *inImage, IMAGE *outImage);

#endif
