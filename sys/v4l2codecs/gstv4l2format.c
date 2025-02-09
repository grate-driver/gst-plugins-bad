/* GStreamer
 * Copyright (C) 2020 Nicolas Dufresne <nicolas.dufresne@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "gstv4l2format.h"

#define GST_CAT_DEFAULT gstv4l2codecs_debug
GST_DEBUG_CATEGORY_EXTERN (gstv4l2codecs_debug);

struct FormatEntry
{
  guint32 v4l2_pix_fmt;
  gint num_planes;
  GstVideoFormat gst_fmt;
  guint bitdepth;
  gint subsampling;
};

static struct FormatEntry format_map[] = {
  {V4L2_PIX_FMT_NV12, 1, GST_VIDEO_FORMAT_NV12, 8, 420},
  {V4L2_PIX_FMT_YUYV, 1, GST_VIDEO_FORMAT_YUY2, 8, 422},
  {V4L2_PIX_FMT_YUV420M, 3, GST_VIDEO_FORMAT_I420, 8, 420},
  {V4L2_PIX_FMT_SUNXI_TILED_NV12, 1, GST_VIDEO_FORMAT_NV12_32L32, 8, 422},
  {0,}
};

static struct FormatEntry *
lookup_v4l2_fmt (guint v4l2_pix_fmt)
{
  gint i;
  struct FormatEntry *ret = NULL;

  for (i = 0; format_map[i].v4l2_pix_fmt; i++) {
    if (format_map[i].v4l2_pix_fmt == v4l2_pix_fmt) {
      ret = format_map + i;
      break;
    }
  }

  return ret;
}

static struct FormatEntry *
lookup_gst_fmt (GstVideoFormat gst_fmt)
{
  gint i;
  struct FormatEntry *ret = NULL;

  for (i = 0; format_map[i].v4l2_pix_fmt; i++) {
    if (format_map[i].gst_fmt == gst_fmt) {
      ret = format_map + i;
      break;
    }
  }

  return ret;
}

static gint
extrapolate_stride (const GstVideoFormatInfo * finfo, gint plane, gint stride)
{
  gint estride;

  switch (finfo->format) {
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV12_32L32:
    case GST_VIDEO_FORMAT_NV12_64Z32:
    case GST_VIDEO_FORMAT_NV16:
    case GST_VIDEO_FORMAT_NV21:
    case GST_VIDEO_FORMAT_NV24:
    case GST_VIDEO_FORMAT_NV61:
    case GST_VIDEO_FORMAT_I420:
      estride = (plane == 0 ? 1 : 2) *
          GST_VIDEO_FORMAT_INFO_SCALE_WIDTH (finfo, plane, stride);
      break;
    default:
      estride = GST_VIDEO_FORMAT_INFO_SCALE_WIDTH (finfo, plane, stride);
      break;
  }

  return estride;
}

static void
set_stride (GstVideoInfo * info, gint plane, gint stride)
{
  const GstVideoFormatInfo *finfo = info->finfo;

  if (GST_VIDEO_FORMAT_INFO_IS_TILED (finfo)) {
    gint x_tiles, y_tiles, ws, hs, padded_height;

    ws = GST_VIDEO_FORMAT_INFO_TILE_WS (finfo);
    hs = GST_VIDEO_FORMAT_INFO_TILE_HS (finfo);

    padded_height = GST_VIDEO_FORMAT_INFO_SCALE_HEIGHT (finfo, plane,
        info->height);
    padded_height = GST_ROUND_UP_N (padded_height, 1 << hs);

    x_tiles = stride >> ws;
    y_tiles = padded_height >> hs;
    info->stride[plane] = GST_VIDEO_TILE_MAKE_STRIDE (x_tiles, y_tiles);
  } else {
    info->stride[plane] = stride;
  }
}

gboolean
gst_v4l2_format_to_video_info (struct v4l2_format *fmt, GstVideoInfo * out_info)
{
  struct FormatEntry *entry = lookup_v4l2_fmt (fmt->fmt.pix_mp.pixelformat);
  struct v4l2_pix_format_mplane *pix_mp = &fmt->fmt.pix_mp;
  struct v4l2_pix_format *pix = &fmt->fmt.pix;
  gint plane;
  gsize offset = 0;

  if (!entry)
    return FALSE;

  if (!gst_video_info_set_format (out_info, entry->gst_fmt,
          pix_mp->width, pix_mp->height))
    return FALSE;

  for (plane = 0; plane < GST_VIDEO_INFO_N_PLANES (out_info); plane++) {
    gint stride;

    if (V4L2_TYPE_IS_MULTIPLANAR (fmt->type)) {
      stride = extrapolate_stride (out_info->finfo, plane,
          pix_mp->plane_fmt[plane].bytesperline);
      out_info->size += pix_mp->plane_fmt[plane].sizeimage;
    } else {
      stride = extrapolate_stride (out_info->finfo, plane, pix->bytesperline);
      out_info->size += pix->sizeimage;
    }

    set_stride (out_info, plane, stride);
    out_info->offset[plane] = offset;

    offset += stride * GST_VIDEO_FORMAT_INFO_SCALE_HEIGHT (out_info->finfo,
        plane, pix_mp->height);
  }

  return TRUE;
}

gboolean
gst_v4l2_format_to_video_format (guint32 pix_fmt, GstVideoFormat * out_format)
{
  struct FormatEntry *entry = lookup_v4l2_fmt (pix_fmt);

  if (!entry)
    return FALSE;

  *out_format = entry->gst_fmt;
  return TRUE;
}

gboolean
gst_v4l2_format_from_video_format (GstVideoFormat format, guint32 * out_pix_fmt)
{
  struct FormatEntry *entry = lookup_gst_fmt (format);

  if (!entry)
    return FALSE;

  *out_pix_fmt = entry->v4l2_pix_fmt;
  return TRUE;
}
