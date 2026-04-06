#ifndef __GST_YOLOBATCHDETECTION_H__
#define __GST_YOLOBATCHDETECTION_H__

#include <gst/gst.h>
#include <gst/base/gstaggregator.h>
#include <gst/video/video.h>
#include <vector>
#include <netinet/in.h>

#include "yolo_engine.h"

G_BEGIN_DECLS

#define GST_TYPE_YOLOBATCHDETECTION (gst_yolobatchdetection_get_type())
G_DECLARE_FINAL_TYPE (GstYoloBatchDetection, gst_yolobatchdetection,
    GST, YOLOBATCHDETECTION, GstAggregator)

struct _GstYoloBatchDetection {
  GstAggregator parent;

  /* Properties */
  gchar *model_path;
  gfloat conf_threshold;
  gchar *dest_host;
  gint dest_port;
  gboolean use_gpu;
  gint gpu_device_id;

  /* Engine */
  YoloEngine *yolo_engine;

  /* UDP socket state */
  int udp_sock;
  gboolean host_resolved;
  struct sockaddr_in resolved_addr;

  /* FPS tracking */
  GstClockTime last_time;
  guint frame_count;
  gdouble current_fps;
};

G_END_DECLS

#endif /* __GST_YOLOBATCHDETECTION_H__ */
