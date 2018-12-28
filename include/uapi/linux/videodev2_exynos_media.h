/*
 * Video for Linux Two header file for Exynos
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This header file contains several v4l2 APIs to be proposed to v4l2
 * community and until being accepted, will be used restrictly for Exynos.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_VIDEODEV2_EXYNOS_MEDIA_H
#define __LINUX_VIDEODEV2_EXYNOS_MEDIA_H

#include <linux/videodev2.h>
#include <linux/exynos_mfc_media.h>

/* added for lihwjpeg.so */

/* yuv444 of JFIF JPEG */
#define V4L2_PIX_FMT_JPEG_444 v4l2_fourcc('J', 'P', 'G', '4')
/* yuv422 of JFIF JPEG */
#define V4L2_PIX_FMT_JPEG_422 v4l2_fourcc('J', 'P', 'G', '2')
/* yuv420 of JFIF JPEG */
#define V4L2_PIX_FMT_JPEG_420 v4l2_fourcc('J', 'P', 'G', '0')
/* grey of JFIF JPEG */
#define V4L2_PIX_FMT_JPEG_GRAY v4l2_fourcc('J', 'P', 'G', 'G')
/* yuv422v of JFIF JPEG */
#define V4L2_PIX_FMT_JPEG_422V v4l2_fourcc('J', 'P', 'G', '5')
/* yuv411 of JFIF JPEG */
#define V4L2_PIX_FMT_JPEG_411 v4l2_fourcc('J', 'P', 'G', '1')

/*
 *	C O N T R O L S
 */
/* CID base for Exynos controls (USER_CLASS) */
#define V4L2_CID_EXYNOS_BASE		(V4L2_CTRL_CLASS_USER | 0x2000)

/* for rgb alpha function */
#define V4L2_CID_GLOBAL_ALPHA		(V4L2_CID_EXYNOS_BASE + 1)

/* cacheable configuration */
#define V4L2_CID_CACHEABLE		(V4L2_CID_EXYNOS_BASE + 10)

/* jpeg captured size */
#define V4L2_CID_CAM_JPEG_MEMSIZE	(V4L2_CID_EXYNOS_BASE + 20)
#define V4L2_CID_CAM_JPEG_ENCODEDSIZE	(V4L2_CID_EXYNOS_BASE + 21)
#define V4L2_CID_JPEG_TABLE		(V4L2_CID_EXYNOS_BASE + 22)

#define V4L2_CID_SET_SHAREABLE		(V4L2_CID_EXYNOS_BASE + 40)

/* TV configuration */
#define V4L2_CID_TV_LAYER_BLEND_ENABLE	(V4L2_CID_EXYNOS_BASE + 50)
#define V4L2_CID_TV_LAYER_BLEND_ALPHA	(V4L2_CID_EXYNOS_BASE + 51)
#define V4L2_CID_TV_PIXEL_BLEND_ENABLE	(V4L2_CID_EXYNOS_BASE + 52)
#define V4L2_CID_TV_CHROMA_ENABLE	(V4L2_CID_EXYNOS_BASE + 53)
#define V4L2_CID_TV_CHROMA_VALUE	(V4L2_CID_EXYNOS_BASE + 54)
#define V4L2_CID_TV_HPD_STATUS		(V4L2_CID_EXYNOS_BASE + 55)
#define V4L2_CID_TV_LAYER_PRIO		(V4L2_CID_EXYNOS_BASE + 56)
#define V4L2_CID_TV_SET_DVI_MODE	(V4L2_CID_EXYNOS_BASE + 57)
#define V4L2_CID_TV_GET_DVI_MODE	(V4L2_CID_EXYNOS_BASE + 58)
#define V4L2_CID_TV_SET_ASPECT_RATIO	(V4L2_CID_EXYNOS_BASE + 59)
#define V4L2_CID_TV_MAX_AUDIO_CHANNELS	(V4L2_CID_EXYNOS_BASE + 60)
#define V4L2_CID_TV_ENABLE_HDMI_AUDIO	(V4L2_CID_EXYNOS_BASE + 61)
#define V4L2_CID_TV_SET_NUM_CHANNELS	(V4L2_CID_EXYNOS_BASE + 62)
#define V4L2_CID_TV_UPDATE		(V4L2_CID_EXYNOS_BASE + 63)
#define V4L2_CID_TV_SET_COLOR_RANGE	(V4L2_CID_EXYNOS_BASE + 64)
#define V4L2_CID_TV_HDCP_ENABLE		(V4L2_CID_EXYNOS_BASE + 65)
#define V4L2_CID_TV_HDMI_STATUS		(V4L2_CID_EXYNOS_BASE + 66)
#define V4L2_CID_TV_SOURCE_PHY_ADDR	(V4L2_CID_EXYNOS_BASE + 67)
#define V4L2_CID_TV_BLANK		(V4L2_CID_EXYNOS_BASE + 68)

/* for color space conversion equation selection */
#define V4L2_CID_CSC_EQ_MODE		(V4L2_CID_EXYNOS_BASE + 100)
#define V4L2_CID_CSC_EQ			(V4L2_CID_EXYNOS_BASE + 101)
#define V4L2_CID_CSC_RANGE		(V4L2_CID_EXYNOS_BASE + 102)

/* for gscaler m2m context information */
#define V4L2_CID_M2M_CTX_NUM		(V4L2_CID_EXYNOS_BASE + 200)
/* for DRM playback scenario */
#define V4L2_CID_CONTENT_PROTECTION	(V4L2_CID_EXYNOS_BASE + 201)

/* for 2d operation */
#define V4L2_CID_2D_BLEND_OP		(V4L2_CID_EXYNOS_BASE + 103)
#define V4L2_CID_2D_COLOR_FILL		(V4L2_CID_EXYNOS_BASE + 104)
#define V4L2_CID_2D_DITH		(V4L2_CID_EXYNOS_BASE + 105)
#define V4L2_CID_2D_FMT_PREMULTI	(V4L2_CID_EXYNOS_BASE + 106)
#define V4L2_CID_2D_SRC_COLOR		(V4L2_CID_EXYNOS_BASE + 107)

/* for additional 2d operation */
#define V4L2_CID_2D_SRC_COLOR		(V4L2_CID_EXYNOS_BASE + 107)
#define V4L2_CID_2D_CLIP		(V4L2_CID_EXYNOS_BASE + 108)
#define V4L2_CID_2D_SCALE_WIDTH		(V4L2_CID_EXYNOS_BASE + 109)
#define V4L2_CID_2D_SCALE_HEIGHT	(V4L2_CID_EXYNOS_BASE + 110)
#define V4L2_CID_2D_REPEAT		(V4L2_CID_EXYNOS_BASE + 111)
#define V4L2_CID_2D_SCALE_MODE		(V4L2_CID_EXYNOS_BASE + 112)
#define V4L2_CID_2D_BLUESCREEN		(V4L2_CID_EXYNOS_BASE + 113)
#define V4L2_CID_2D_BG_COLOR		(V4L2_CID_EXYNOS_BASE + 114)
#define V4L2_CID_2D_BS_COLOR		(V4L2_CID_EXYNOS_BASE + 115)

/* for scaler blend set format */
#define V4L2_CID_2D_SRC_BLEND_SET_FMT		(V4L2_CID_EXYNOS_BASE + 116)
#define V4L2_CID_2D_SRC_BLEND_SET_H_POS		(V4L2_CID_EXYNOS_BASE + 117)
#define V4L2_CID_2D_SRC_BLEND_SET_V_POS		(V4L2_CID_EXYNOS_BASE + 118)
#define V4L2_CID_2D_SRC_BLEND_FMT_PREMULTI	(V4L2_CID_EXYNOS_BASE + 119)
#define V4L2_CID_2D_SRC_BLEND_SET_STRIDE	(V4L2_CID_EXYNOS_BASE + 120)
#define V4L2_CID_2D_SRC_BLEND_SET_WIDTH		(V4L2_CID_EXYNOS_BASE + 121)
#define V4L2_CID_2D_SRC_BLEND_SET_HEIGHT	(V4L2_CID_EXYNOS_BASE + 122)

/* for gscaler m2m context information */
#define V4L2_CID_M2M_CTX_NUM		(V4L2_CID_EXYNOS_BASE + 200)
/* for DRM playback scenario */
#define V4L2_CID_CONTENT_PROTECTION	(V4L2_CID_EXYNOS_BASE + 201)

#endif /* __LINUX_VIDEODEV2_EXYNOS_MEDIA_H */
