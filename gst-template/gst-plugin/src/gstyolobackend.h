/*
 * Copyright (C) 2024 <your name here>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef __GST_YOLO_BACKEND_H__
#define __GST_YOLO_BACKEND_H__

#include <gst/base/gstbasetransform.h>
#include <netinet/in.h>
#include "yolo_engine.h"

G_BEGIN_DECLS

#define GST_TYPE_YOLO_BACKEND (gst_yolo_backend_get_type())
G_DECLARE_FINAL_TYPE (GstYoloBackend, gst_yolo_backend, GST, YOLO_BACKEND, GstBaseTransform)

struct _GstYoloBackend {
  GstBaseTransform parent;

  /* properties */
  gchar *model_path;
  gchar *dest_host;
  gint dest_port;

  /* private */
  YoloEngine *yolo_engine;
  
  int udp_sock;
  struct sockaddr_in dest_addr;
  gboolean addr_resolved;

  guint64 frame_count;
  GstClockTime last_time;
  gdouble current_fps;
};

G_END_DECLS

#endif /* __GST_YOLO_BACKEND_H__ */