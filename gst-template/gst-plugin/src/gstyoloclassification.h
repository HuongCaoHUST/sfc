/* 
 * GStreamer
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
 
#ifndef __GST_YOLOCLASSIFICATION_H__
#define __GST_YOLOCLASSIFICATION_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>
#include "yolo_engine.h"
#include <vector>
#include <string>

G_BEGIN_DECLS

#define GST_TYPE_YOLOCLASSIFICATION (gst_yoloclassification_get_type())
G_DECLARE_FINAL_TYPE (GstYoloClassification, gst_yoloclassification,
    GST, YOLOCLASSIFICATION, GstBaseTransform)

class YoloClassifier;

struct _GstYoloClassification {
  GstBaseTransform element;

  gchar *model_path;      /* Model path */
  gfloat conf_threshold;  /* Confidence threshold */

  YoloClassifier *classifier;
  GstVideoInfo *video_info;

  GstClockTime last_time;
  guint frame_count;
  gdouble current_fps;
};

G_END_DECLS

#endif /* __GST_YOLOCLASSIFICATION_H__ */
