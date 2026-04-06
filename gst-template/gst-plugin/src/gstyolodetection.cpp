/*
 * GStreamer
 * Copyright (C) 2006 Stefan Kost <ensonic@users.sf.net>
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

/**
 * SECTION:element-yolodetection
 *
 * FIXME:Describe yolodetection here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! yolodetection ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/base.h>
#include <gst/controller/controller.h>

#include "gstyolodetection.h"
// yolo_engine.h is now included via gstyolodetection.h
#include <sstream>
#include <iomanip>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>

GST_DEBUG_CATEGORY_STATIC (gst_yolodetection_debug);
#define GST_CAT_DEFAULT gst_yolodetection_debug

enum
{
  PROP_0,
  PROP_SILENT,
  PROP_MODEL_PATH,
  PROP_CONF_THRESHOLD,
  PROP_DEST_HOST,
  PROP_DEST_PORT,
  PROP_BATCH_SIZE,
  PROP_USE_GPU,
  PROP_GPU_DEVICE_ID
};

#define SUPPORTED_CAPS "video/x-raw, " \
    "format = (string) { BGR }, " \
    "width = (int) 640, " \
    "height = (int) 640, " \
    "framerate = (fraction) [ 0/1, 2147483647/1 ]"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SUPPORTED_CAPS)
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SUPPORTED_CAPS)
    );

#define gst_yolodetection_parent_class parent_class
G_DEFINE_TYPE (Gstyolodetection, gst_yolodetection, GST_TYPE_BASE_TRANSFORM);
GST_ELEMENT_REGISTER_DEFINE (yolodetection, "yolodetection", GST_RANK_NONE,
    GST_TYPE_YOLODETECTION);

static void gst_yolodetection_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_yolodetection_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_yolodetection_finalize (GObject * object);
static gboolean gst_yolodetection_set_caps (GstBaseTransform * trans, GstCaps * incaps, GstCaps * outcaps);

static GstFlowReturn gst_yolodetection_transform_ip (GstBaseTransform *
    base, GstBuffer * outbuf);
static gboolean gst_yolodetection_sink_event (GstBaseTransform * trans,
    GstEvent * event);

/* GObject vmethod implementations */
static void
gst_yolodetection_class_init (GstyolodetectionClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_yolodetection_set_property;
  gobject_class->get_property = gst_yolodetection_get_property;
  gobject_class->finalize = gst_yolodetection_finalize;

  g_object_class_install_property (gobject_class, PROP_MODEL_PATH,
      g_param_spec_string ("model-path", "Model Path", "Path to the ONNX model file",
          NULL, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  
  g_object_class_install_property (gobject_class, PROP_CONF_THRESHOLD,
      g_param_spec_float ("conf-threshold", "Confidence Threshold", "Threshold for object detection confidence",
          0.0f, 1.0f, 0.5f, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_DEST_HOST,
      g_param_spec_string ("dest-host", "Destination Host", "Destination IP or hostname for JSON UDP stream",
          "127.0.0.1", (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_DEST_PORT,
      g_param_spec_int ("dest-port", "Destination Port", "Destination port for JSON UDP stream",
          1, 65535, 5002, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_BATCH_SIZE,
      g_param_spec_uint ("batch-size", "Batch Size",
          "Number of frames to accumulate before running batched inference",
          1, 32, 1, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_USE_GPU,
      g_param_spec_boolean ("use-gpu", "Use GPU",
          "Enable CUDA GPU acceleration for inference",
          FALSE,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_GPU_DEVICE_ID,
      g_param_spec_int ("gpu-device-id", "GPU Device ID",
          "CUDA device ID to use (0 = first GPU, 1 = second, etc.)",
          0, 15, 0,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_details_simple (gstelement_class,
      "yolodetection",
      "Generic/Filter",
      "Performs YOLO object detection using an ONNX model", "HuongCao <<huongcao.seee@gmail.com>>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));
  
  GST_BASE_TRANSFORM_CLASS (klass)->set_caps = 
      GST_DEBUG_FUNCPTR (gst_yolodetection_set_caps);
  GST_BASE_TRANSFORM_CLASS (klass)->transform_ip =
      GST_DEBUG_FUNCPTR (gst_yolodetection_transform_ip);
  GST_BASE_TRANSFORM_CLASS (klass)->sink_event =
      GST_DEBUG_FUNCPTR (gst_yolodetection_sink_event);

  GST_DEBUG_CATEGORY_INIT (gst_yolodetection_debug, "yolodetection", 0,
      "Template yolodetection");
}

/* initialize the element */
static void
gst_yolodetection_init (Gstyolodetection * filter)
{
  filter->model_path = NULL;
  filter->conf_threshold = 0.5f;
  filter->yolo_engine = nullptr;
  filter->video_info = gst_video_info_new ();

  filter->last_time = GST_CLOCK_TIME_NONE;
  filter->frame_count = 0;
  filter->current_fps = 0.0;

  filter->dest_host = g_strdup("127.0.0.1");
  filter->dest_port = 5002;

  filter->use_gpu = FALSE;
  filter->gpu_device_id = 0;

  filter->batch_size = 1;
  filter->frame_batch = new std::vector<cv::Mat>();
  filter->pts_batch = new std::vector<GstClockTime>();

  // UDP Socket
  filter->udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
  filter->addr_resolved = FALSE;
  memset(&filter->dest_addr, 0, sizeof(filter->dest_addr));
}

static void gst_yolodetection_finalize (GObject * object)
{
    Gstyolodetection *filter = GST_YOLODETECTION (object);

    // Close UDP Socket
    if (filter->udp_sock >= 0) {
        close(filter->udp_sock);
    }

    g_free (filter->model_path);
    g_free (filter->dest_host);

    if(filter->yolo_engine) {
        delete filter->yolo_engine;
        filter->yolo_engine = nullptr;
    }

    delete filter->frame_batch;
    filter->frame_batch = nullptr;
    delete filter->pts_batch;
    filter->pts_batch = nullptr;

    gst_video_info_free(filter->video_info);

    G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_yolodetection_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  Gstyolodetection *filter = GST_YOLODETECTION (object);

  switch (prop_id) {
    case PROP_MODEL_PATH:
      g_free(filter->model_path);
      filter->model_path = g_value_dup_string(value);
      GST_INFO_OBJECT(filter, "Model path set to: %s", filter->model_path);
      if (filter->yolo_engine) {
          delete filter->yolo_engine;
          filter->yolo_engine = nullptr;
      }
      try {
        filter->yolo_engine = new YoloEngine(filter->model_path, filter->use_gpu, filter->gpu_device_id);
        GST_INFO_OBJECT(filter, "Successfully loaded ONNX model (GPU: %s, device: %d).",
            filter->use_gpu ? "yes" : "no", filter->gpu_device_id);
      } catch (const std::exception& e) {
        GST_ERROR_OBJECT(filter, "Failed to load ONNX model: %s", e.what());
      }
      break;
    case PROP_CONF_THRESHOLD:
      filter->conf_threshold = g_value_get_float (value);
      GST_INFO_OBJECT(filter, "Confidence threshold set to: %f", filter->conf_threshold);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;

    case PROP_DEST_HOST:
      g_free(filter->dest_host);
      filter->dest_host = g_value_dup_string(value);
      filter->addr_resolved = FALSE;
      GST_INFO_OBJECT(filter, "Destination host set to: %s", filter->dest_host);
      break;

    case PROP_DEST_PORT:
      filter->dest_port = g_value_get_int(value);
      filter->addr_resolved = FALSE;
      GST_INFO_OBJECT(filter, "Destination port set to: %d", filter->dest_port);
      break;

    case PROP_BATCH_SIZE:
      filter->batch_size = g_value_get_uint(value);
      GST_INFO_OBJECT(filter, "Batch size set to: %u", filter->batch_size);
      break;

    case PROP_USE_GPU:
      filter->use_gpu = g_value_get_boolean(value);
      GST_INFO_OBJECT(filter, "Use GPU set to: %s", filter->use_gpu ? "yes" : "no");
      break;

    case PROP_GPU_DEVICE_ID:
      filter->gpu_device_id = g_value_get_int(value);
      GST_INFO_OBJECT(filter, "GPU device ID set to: %d", filter->gpu_device_id);
      break;
  }
}

static void
gst_yolodetection_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Gstyolodetection *filter = GST_YOLODETECTION (object);

  switch (prop_id) {
    case PROP_MODEL_PATH:
      g_value_set_string(value, filter->model_path);
      break;
    case PROP_CONF_THRESHOLD:
      g_value_set_float(value, filter->conf_threshold);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;

    case PROP_DEST_HOST:
      g_value_set_string(value, filter->dest_host);
      break;
      
    case PROP_DEST_PORT:
      g_value_set_int(value, filter->dest_port);
      break;

    case PROP_BATCH_SIZE:
      g_value_set_uint(value, filter->batch_size);
      break;

    case PROP_USE_GPU:
      g_value_set_boolean(value, filter->use_gpu);
      break;

    case PROP_GPU_DEVICE_ID:
      g_value_set_int(value, filter->gpu_device_id);
      break;
  }
}

static gboolean gst_yolodetection_set_caps (GstBaseTransform * trans, GstCaps * incaps, GstCaps * outcaps)
{
    Gstyolodetection *filter = GST_YOLODETECTION (trans);
    if (!gst_video_info_from_caps (filter->video_info, incaps)) {
        GST_ERROR_OBJECT (filter, "Failed to parse video caps");
        return FALSE;
    }
    return TRUE;
}

/* Helper: resolve UDP address if needed */
static void
ensure_udp_resolved (Gstyolodetection *filter)
{
    if (filter->udp_sock >= 0 && !filter->addr_resolved) {
        struct addrinfo hints, *res;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;

        std::string port_str = std::to_string(filter->dest_port);
        int err = getaddrinfo(filter->dest_host, port_str.c_str(), &hints, &res);
        if (err == 0) {
            memcpy(&filter->dest_addr, res->ai_addr, res->ai_addrlen);
            filter->addr_resolved = TRUE;
            freeaddrinfo(res);
            GST_INFO_OBJECT(filter, "Resolved '%s:%d' successfully.", filter->dest_host, filter->dest_port);
        } else {
            GST_WARNING_OBJECT(filter, "Could not resolve hostname '%s': %s", filter->dest_host, gai_strerror(err));
        }
    }
}

/* Helper: build JSON and send one frame's detections via UDP */
static void
send_detection_udp (Gstyolodetection *filter,
    const std::vector<Detection>& detections, GstClockTime pts)
{
    guint64 pts_val = (pts == GST_CLOCK_TIME_NONE) ? 0 : (guint64)pts;

    std::stringstream json_ss;
    json_ss << "{\n";
    json_ss << "  \"pts\": " << pts_val << ",\n";
    json_ss << "  \"predictions\": [\n";
    for(size_t i = 0; i < detections.size(); ++i) {
        const auto& d = detections[i];
        std::string class_name = "Class_" + std::to_string(d.class_id);
        std::string detection_id = "uuid-" + std::to_string(filter->frame_count) + "-" + std::to_string(i);

        json_ss << "    {\n"
                << "      \"x\": " << d.box.x << ",\n"
                << "      \"y\": " << d.box.y << ",\n"
                << "      \"width\": " << d.box.width << ",\n"
                << "      \"height\": " << d.box.height << ",\n"
                << "      \"confidence\": " << std::fixed << std::setprecision(3) << d.confidence << ",\n"
                << "      \"class\": \"" << class_name << "\",\n"
                << "      \"class_id\": " << d.class_id << ",\n"
                << "      \"detection_id\": \"" << detection_id << "\"\n"
                << "    }";

        if (i < detections.size() - 1) {
            json_ss << ",";
        }
        json_ss << "\n";
    }
    json_ss << "  ]\n}";
    std::string final_json_string = json_ss.str();

    if (filter->udp_sock >= 0 && filter->addr_resolved) {
        sendto(filter->udp_sock,
               final_json_string.c_str(),
               final_json_string.length(),
               0,
               (struct sockaddr *)&filter->dest_addr,
               sizeof(filter->dest_addr));
    }

    filter->frame_count++;
}

/* Helper: flush accumulated batch — run batch inference + send all UDP */
static void
flush_batch (Gstyolodetection *filter)
{
    if (!filter->frame_batch || filter->frame_batch->empty()) return;
    if (!filter->yolo_engine) return;

    try {
        auto batch_results = filter->yolo_engine->detect_batch(
            *filter->frame_batch, filter->conf_threshold);

        for (size_t i = 0; i < batch_results.size(); i++) {
            send_detection_udp(filter, batch_results[i], (*filter->pts_batch)[i]);
        }
    } catch (const std::exception& e) {
        GST_ERROR_OBJECT(filter, "Batch inference failed: %s", e.what());
    }

    filter->frame_batch->clear();
    filter->pts_batch->clear();
}

/* Helper: update FPS counter */
static void
update_fps (Gstyolodetection *filter)
{
    GstClockTime current_time = gst_util_get_timestamp();

    if (filter->last_time == GST_CLOCK_TIME_NONE) {
        filter->last_time = current_time;
    } else {
        GstClockTime diff = current_time - filter->last_time;
        if (diff >= GST_SECOND) {
            filter->current_fps = (double)filter->frame_count * GST_SECOND / diff;
            GST_INFO_OBJECT(filter, "FPS: %.1f", filter->current_fps);
            filter->frame_count = 0;
            filter->last_time = current_time;
        }
    }
}

/* GstBaseTransform vmethod implementations */
static GstFlowReturn
gst_yolodetection_transform_ip (GstBaseTransform * base, GstBuffer * outbuf)
{
  Gstyolodetection *filter = GST_YOLODETECTION (base);
  GstMapInfo map;

  if (!filter->yolo_engine) {
    GST_WARNING_OBJECT(filter, "YOLO engine not initialized, passing buffer through.");
    return GST_FLOW_OK;
  }

  ensure_udp_resolved(filter);

  if (filter->batch_size <= 1) {
    /* === SINGLE FRAME PATH (original behavior, zero regression) === */
    if (gst_buffer_map (outbuf, &map, (GstMapFlags)GST_MAP_READWRITE)) {
      int width = GST_VIDEO_INFO_WIDTH(filter->video_info);
      int height = GST_VIDEO_INFO_HEIGHT(filter->video_info);

      cv::Mat frame(height, width, CV_8UC3, map.data);
      auto detections = filter->yolo_engine->detect(frame, filter->conf_threshold);

      send_detection_udp(filter, detections, GST_BUFFER_PTS(outbuf));
      update_fps(filter);

      gst_buffer_unmap (outbuf, &map);
    }
  } else {
    /* === BATCH PATH === */
    if (gst_buffer_map (outbuf, &map, GST_MAP_READ)) {
      int width = GST_VIDEO_INFO_WIDTH(filter->video_info);
      int height = GST_VIDEO_INFO_HEIGHT(filter->video_info);

      cv::Mat frame(height, width, CV_8UC3, map.data);
      filter->frame_batch->push_back(frame.clone());
      filter->pts_batch->push_back(GST_BUFFER_PTS(outbuf));

      gst_buffer_unmap (outbuf, &map);
    }

    if (filter->frame_batch->size() >= filter->batch_size) {
      flush_batch(filter);
      update_fps(filter);
    }
  }

  return GST_FLOW_OK;
}


/* Handle EOS and FLUSH_STOP for batch flushing */
static gboolean
gst_yolodetection_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  Gstyolodetection *filter = GST_YOLODETECTION (trans);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      GST_DEBUG_OBJECT (filter, "EOS received, flushing %zu pending frames",
          filter->frame_batch->size());
      flush_batch (filter);
      break;

    case GST_EVENT_FLUSH_STOP:
      GST_DEBUG_OBJECT (filter, "FLUSH_STOP, discarding %zu pending frames",
          filter->frame_batch->size());
      filter->frame_batch->clear();
      filter->pts_batch->clear();
      break;

    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (trans, event);
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
extern "C" {
  static gboolean yolodetection_init (GstPlugin * plugin) {
    return GST_ELEMENT_REGISTER (yolodetection, plugin);
  }

  GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
      GST_VERSION_MINOR,
      yolodetection,
      "YOLO ONNX DETECTION",
      yolodetection_init,
      "1.0", "LGPL", "GStreamer", "https://gstreamer.net"
  )
}
