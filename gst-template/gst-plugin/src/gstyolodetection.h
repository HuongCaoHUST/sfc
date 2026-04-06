/* 
 * GStreamer
 * Copyright (C) 2006 Stefan Kost <ensonic@users.sf.net>
 * Copyright (C) 2020 Niels De Graef <niels.degraef@gmail.com>
 * Copyright (C) 2026 HuongCao <<user@hostname.org>>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
 
#ifndef __GST_YOLODETECTION_H__
#define __GST_YOLODETECTION_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>
#include <onnxruntime_cxx_api.h>
#include <vector>
#include <string>
#include <netinet/in.h>

#include "yolo_engine.h"

G_BEGIN_DECLS

#define GST_TYPE_YOLODETECTION (gst_yolodetection_get_type())
G_DECLARE_FINAL_TYPE (Gstyolodetection, gst_yolodetection,
    GST, YOLODETECTION, GstBaseTransform)

struct _Gstyolodetection {
  GstBaseTransform element;

  gchar *model_path;      /* Model path */
  gfloat conf_threshold;  /* Confidence threshold */

  gchar *dest_host;       /* UDP Destination Host/IP */
  gint dest_port;         /* UDP Destination Port */

  gboolean use_gpu;       /* Enable CUDA GPU acceleration */
  gint gpu_device_id;     /* CUDA device ID */

  guint batch_size;       /* Batch size for inference (default 1) */

  YoloEngine *yolo_engine;
  GstVideoInfo *video_info;

  GstClockTime last_time;
  guint frame_count;
  gdouble current_fps;

  int udp_sock;
  struct sockaddr_in dest_addr;
  gboolean addr_resolved;

  /* Batch accumulation (heap-allocated, GObject doesn't call C++ ctors) */
  std::vector<cv::Mat> *frame_batch;
  std::vector<GstClockTime> *pts_batch;
};

G_END_DECLS

#endif /* __GST_YOLODETECTION_H__ */
