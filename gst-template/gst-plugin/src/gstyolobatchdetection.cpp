#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstaggregator.h>

#include "gstyolobatchdetection.h"

#include <sstream>
#include <iomanip>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <vector>
#include <opencv2/opencv.hpp>

GST_DEBUG_CATEGORY_STATIC (gst_yolobatchdetection_debug);
#define GST_CAT_DEFAULT gst_yolobatchdetection_debug

enum {
  PROP_0,
  PROP_MODEL_PATH,
  PROP_CONF_THRESHOLD,
  PROP_NMS_THRESHOLD,
  PROP_DEST_HOST,
  PROP_DEST_PORT,
  PROP_USE_GPU,
  PROP_GPU_DEVICE_ID
};

#define SUPPORTED_CAPS "video/x-raw, " \
    "format = (string) { BGR }, " \
    "width = (int) 640, " \
    "height = (int) 640, " \
    "framerate = (fraction) [ 0/1, 2147483647/1 ]"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (SUPPORTED_CAPS)
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SUPPORTED_CAPS)
    );

#define gst_yolobatchdetection_parent_class parent_class
G_DEFINE_TYPE (GstYoloBatchDetection, gst_yolobatchdetection, GST_TYPE_AGGREGATOR);
GST_ELEMENT_REGISTER_DEFINE (yolobatchdetection, "yolobatchdetection",
    GST_RANK_NONE, GST_TYPE_YOLOBATCHDETECTION);

/* Forward declarations */
static void gst_yolobatchdetection_set_property (GObject *object,
    guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_yolobatchdetection_get_property (GObject *object,
    guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_yolobatchdetection_finalize (GObject *object);

static gboolean gst_yolobatchdetection_start (GstAggregator *agg);
static gboolean gst_yolobatchdetection_stop (GstAggregator *agg);
static GstFlowReturn gst_yolobatchdetection_aggregate (GstAggregator *agg,
    gboolean timeout);
static GstFlowReturn gst_yolobatchdetection_flush (GstAggregator *agg);

/* ================================================================ */
/*                         class_init                                */
/* ================================================================ */
static void
gst_yolobatchdetection_class_init (GstYoloBatchDetectionClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstAggregatorClass *agg_class = GST_AGGREGATOR_CLASS (klass);

  gobject_class->set_property = gst_yolobatchdetection_set_property;
  gobject_class->get_property = gst_yolobatchdetection_get_property;
  gobject_class->finalize = gst_yolobatchdetection_finalize;

  g_object_class_install_property (gobject_class, PROP_MODEL_PATH,
      g_param_spec_string ("model-path", "Model Path",
          "Path to the ONNX model file",
          NULL, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_CONF_THRESHOLD,
      g_param_spec_float ("conf-threshold", "Confidence Threshold",
          "Threshold for object detection confidence",
          0.0f, 1.0f, 0.5f,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_NMS_THRESHOLD,
      g_param_spec_float ("nms-threshold", "NMS Threshold",
          "Threshold for Non-Maximum Suppression",
          0.0f, 1.0f, 0.45f,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_DEST_HOST,
      g_param_spec_string ("dest-host", "Destination Host",
          "Destination IP or hostname for JSON UDP stream",
          "127.0.0.1",
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_DEST_PORT,
      g_param_spec_int ("dest-port", "Destination Port",
          "UDP port to send all detection results to",
          1, 65535, 5101,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

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
      "yolobatchdetection",
      "Video/Analyzer",
      "Aggregates N video streams and runs batched YOLO inference, "
      "sending per-stream results via UDP",
      "HuongCao <huongcao.seee@gmail.com>");

  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &sink_template, GST_TYPE_AGGREGATOR_PAD);
  gst_element_class_add_static_pad_template (gstelement_class, &src_template);

  agg_class->aggregate = GST_DEBUG_FUNCPTR (gst_yolobatchdetection_aggregate);
  agg_class->start     = GST_DEBUG_FUNCPTR (gst_yolobatchdetection_start);
  agg_class->stop      = GST_DEBUG_FUNCPTR (gst_yolobatchdetection_stop);
  agg_class->flush     = GST_DEBUG_FUNCPTR (gst_yolobatchdetection_flush);

  GST_DEBUG_CATEGORY_INIT (gst_yolobatchdetection_debug, "yolobatchdetection", 0,
      "YOLO batch detection aggregator");
}

/* ================================================================ */
/*                        Instance init                              */
/* ================================================================ */
static void
gst_yolobatchdetection_init (GstYoloBatchDetection *self)
{
  self->model_path     = NULL;
  self->conf_threshold = 0.5f;
  self->nms_threshold  = 0.45f;
  self->dest_host      = g_strdup ("127.0.0.1");
  self->dest_port      = 5101;
  self->use_gpu        = FALSE;
  self->gpu_device_id  = 0;
  self->yolo_engine    = nullptr;

  self->udp_sock       = -1;
  self->host_resolved  = FALSE;
  memset (&self->resolved_addr, 0, sizeof (self->resolved_addr));

  self->last_time   = GST_CLOCK_TIME_NONE;
  self->frame_count = 0;
  self->current_fps = 0.0;
}

/* ================================================================ */
/*                          finalize                                 */
/* ================================================================ */
static void
gst_yolobatchdetection_finalize (GObject *object)
{
  GstYoloBatchDetection *self = GST_YOLOBATCHDETECTION (object);

  g_free (self->model_path);
  g_free (self->dest_host);

  if (self->yolo_engine) {
    delete self->yolo_engine;
    self->yolo_engine = nullptr;
  }

  if (self->udp_sock >= 0) {
    close (self->udp_sock);
    self->udp_sock = -1;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* ================================================================ */
/*                    set_property / get_property                     */
/* ================================================================ */
static void
gst_yolobatchdetection_set_property (GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec)
{
  GstYoloBatchDetection *self = GST_YOLOBATCHDETECTION (object);
  switch (prop_id) {
    case PROP_MODEL_PATH:
      g_free (self->model_path);
      self->model_path = g_value_dup_string (value);
      break;
    case PROP_CONF_THRESHOLD:
      self->conf_threshold = g_value_get_float (value);
      break;
    case PROP_NMS_THRESHOLD:
      self->nms_threshold = g_value_get_float (value);
      break;
    case PROP_DEST_HOST:
      g_free (self->dest_host);
      self->dest_host = g_value_dup_string (value);
      self->host_resolved = FALSE;
      break;
    case PROP_DEST_PORT:
      self->dest_port = g_value_get_int (value);
      self->host_resolved = FALSE;
      break;
    case PROP_USE_GPU:
      self->use_gpu = g_value_get_boolean (value);
      break;
    case PROP_GPU_DEVICE_ID:
      self->gpu_device_id = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_yolobatchdetection_get_property (GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec)
{
  GstYoloBatchDetection *self = GST_YOLOBATCHDETECTION (object);
  switch (prop_id) {
    case PROP_MODEL_PATH:
      g_value_set_string (value, self->model_path);
      break;
    case PROP_CONF_THRESHOLD:
      g_value_set_float (value, self->conf_threshold);
      break;
    case PROP_NMS_THRESHOLD:
      g_value_set_float (value, self->nms_threshold);
      break;
    case PROP_DEST_HOST:
      g_value_set_string (value, self->dest_host);
      break;
    case PROP_DEST_PORT:
      g_value_set_int (value, self->dest_port);
      break;
    case PROP_USE_GPU:
      g_value_set_boolean (value, self->use_gpu);
      break;
    case PROP_GPU_DEVICE_ID:
      g_value_set_int (value, self->gpu_device_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* ================================================================ */
/*                        start / stop                               */
/* ================================================================ */
static gboolean
gst_yolobatchdetection_start (GstAggregator *agg)
{
  GstYoloBatchDetection *self = GST_YOLOBATCHDETECTION (agg);

  if (!self->model_path || self->model_path[0] == '\0') {
    GST_ERROR_OBJECT (self, "model-path property not set");
    return FALSE;
  }

  try {
    self->yolo_engine = new YoloEngine (self->model_path, self->use_gpu, self->gpu_device_id);
    GST_INFO_OBJECT (self, "Loaded ONNX model from: %s (GPU: %s, device: %d)",
        self->model_path, self->use_gpu ? "yes" : "no", self->gpu_device_id);
  } catch (const std::exception &e) {
    GST_ERROR_OBJECT (self, "Failed to load ONNX model: %s", e.what ());
    return FALSE;
  }

  self->udp_sock = socket (AF_INET, SOCK_DGRAM, 0);
  if (self->udp_sock < 0) {
    GST_ERROR_OBJECT (self, "Failed to create UDP socket");
    return FALSE;
  }

  self->host_resolved = FALSE;
  self->last_time     = GST_CLOCK_TIME_NONE;
  self->frame_count   = 0;
  self->current_fps   = 0.0;

  return TRUE;
}

static gboolean
gst_yolobatchdetection_stop (GstAggregator *agg)
{
  GstYoloBatchDetection *self = GST_YOLOBATCHDETECTION (agg);

  if (self->yolo_engine) {
    delete self->yolo_engine;
    self->yolo_engine = nullptr;
  }

  if (self->udp_sock >= 0) {
    close (self->udp_sock);
    self->udp_sock = -1;
  }

  return TRUE;
}

/* ================================================================ */
/*                           Helpers                                 */
/* ================================================================ */
static void
ensure_host_resolved (GstYoloBatchDetection *self)
{
  if (self->udp_sock < 0 || self->host_resolved)
    return;

  struct addrinfo hints, *res;
  memset (&hints, 0, sizeof (hints));
  hints.ai_family   = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;

  std::string port_str = std::to_string (self->dest_port);
  int err = getaddrinfo (self->dest_host, port_str.c_str (), &hints, &res);
  if (err == 0) {
    memcpy (&self->resolved_addr, res->ai_addr, res->ai_addrlen);
    self->host_resolved = TRUE;
    freeaddrinfo (res);
    GST_INFO_OBJECT (self, "Resolved host '%s' successfully", self->dest_host);
  } else {
    GST_WARNING_OBJECT (self, "Could not resolve '%s': %s",
        self->dest_host, gai_strerror (err));
  }
}

static void
send_detection_udp_to_port (GstYoloBatchDetection *self,
    const std::vector<Detection> &detections, int port, guint pad_index)
{
  if (self->udp_sock < 0 || !self->host_resolved)
    return;

  std::stringstream json_ss;
  json_ss << "{\n";
  json_ss << "  \"pad_index\": " << pad_index << ",\n";
  json_ss << "  \"predictions\": [\n";

  for (size_t i = 0; i < detections.size (); ++i) {
    const auto &d = detections[i];
    std::string class_name = "Class_" + std::to_string (d.class_id);
    std::string detection_id = "det-" + std::to_string (pad_index)
        + "-" + std::to_string (self->frame_count) + "-" + std::to_string (i);

    json_ss << "    {\n"
            << "      \"x\": " << d.box.x << ",\n"
            << "      \"y\": " << d.box.y << ",\n"
            << "      \"width\": " << d.box.width << ",\n"
            << "      \"height\": " << d.box.height << ",\n"
            << "      \"confidence\": " << std::fixed << std::setprecision (3)
            << d.confidence << ",\n"
            << "      \"class\": \"" << class_name << "\",\n"
            << "      \"class_id\": " << d.class_id << ",\n"
            << "      \"detection_id\": \"" << detection_id << "\"\n"
            << "    }";
    if (i < detections.size () - 1)
      json_ss << ",";
    json_ss << "\n";
  }
  json_ss << "  ]\n}";
  std::string json_str = json_ss.str ();

  /* Copy resolved_addr and overwrite port — don't mutate the shared struct */
  struct sockaddr_in addr = self->resolved_addr;
  addr.sin_port = htons ((uint16_t) port);

  sendto (self->udp_sock, json_str.c_str (), json_str.length (), 0,
      (struct sockaddr *) &addr, sizeof (addr));
}

static guint
extract_pad_index (GstPad *pad)
{
  const gchar *name = GST_PAD_NAME (pad);
  guint index = 0;
  if (sscanf (name, "sink_%u", &index) != 1) {
    GST_WARNING ("Could not parse pad index from name '%s'", name);
    index = 0;
  }
  return index;
}

static void
update_fps (GstYoloBatchDetection *self)
{
  GstClockTime current_time = gst_util_get_timestamp ();
  if (self->last_time == GST_CLOCK_TIME_NONE) {
    self->last_time = current_time;
  } else {
    GstClockTime diff = current_time - self->last_time;
    if (diff >= GST_SECOND) {
      self->current_fps = (double) self->frame_count * GST_SECOND / diff;
      GST_INFO_OBJECT (self, "Batch FPS: %.1f", self->current_fps);
      self->frame_count = 0;
      self->last_time = current_time;
    }
  }
}

/* ================================================================ */
/*                     aggregate() — CORE LOGIC                      */
/* ================================================================ */
static GstFlowReturn
gst_yolobatchdetection_aggregate (GstAggregator *agg, gboolean timeout)
{
  GstYoloBatchDetection *self = GST_YOLOBATCHDETECTION (agg);

  if (!self->yolo_engine) {
    GST_ERROR_OBJECT (self, "YOLO engine not initialized");
    return GST_FLOW_ERROR;
  }

  ensure_host_resolved (self);

  /* --- Step 1: Collect frames from all sink pads --- */
  std::vector<cv::Mat> frames;
  std::vector<guint>   pad_indices;
  GstBuffer           *first_buf = NULL;
  gboolean             all_eos = TRUE;

  GList *walk;
  GST_OBJECT_LOCK (agg);
  for (walk = GST_ELEMENT (agg)->sinkpads; walk; walk = walk->next) {
    GstAggregatorPad *aggpad = GST_AGGREGATOR_PAD (walk->data);

    if (gst_aggregator_pad_is_eos (aggpad)) {
      continue;
    }
    all_eos = FALSE;

    GstBuffer *buf = gst_aggregator_pad_pop_buffer (aggpad);
    if (!buf) {
      continue;
    }

    guint idx = extract_pad_index (GST_PAD (aggpad));

    GstMapInfo map;
    if (gst_buffer_map (buf, &map, GST_MAP_READ)) {
      cv::Mat frame (640, 640, CV_8UC3, map.data);
      frames.push_back (frame.clone ());
      pad_indices.push_back (idx);
      gst_buffer_unmap (buf, &map);
    }

    if (!first_buf) {
      first_buf = buf;
    } else {
      gst_buffer_unref (buf);
    }
  }
  GST_OBJECT_UNLOCK (agg);

  /* --- Step 2: Handle EOS --- */
  if (all_eos) {
    GST_INFO_OBJECT (self, "All pads EOS, finishing");
    if (first_buf)
      gst_buffer_unref (first_buf);
    return GST_FLOW_EOS;
  }

  /* --- Step 3: Handle empty batch --- */
  if (frames.empty ()) {
    if (first_buf)
      gst_buffer_unref (first_buf);
    return GST_FLOW_OK;
  }

  /* --- Step 4: Run batch inference --- */
  std::vector<std::vector<Detection>> batch_results;
  try {
    batch_results = self->yolo_engine->detect_batch (
        frames, self->conf_threshold, self->nms_threshold);
  } catch (const std::exception &e) {
    GST_ERROR_OBJECT (self, "Batch inference failed: %s", e.what ());
    if (first_buf)
      gst_buffer_unref (first_buf);
    return GST_FLOW_ERROR;
  }

  /* --- Step 5: Send per-pad UDP results --- */
  for (size_t i = 0; i < batch_results.size (); i++) {
    send_detection_udp_to_port (self, batch_results[i], self->dest_port, pad_indices[i]);
    GST_DEBUG_OBJECT (self, "Sent %zu detections for sink_%u to port %d",
        batch_results[i].size (), pad_indices[i], self->dest_port);
  }

  /* --- Step 6: Update FPS --- */
  self->frame_count++;
  update_fps (self);

  /* --- Step 7: Push buffer downstream (for fakesink) --- */
  return gst_aggregator_finish_buffer (agg, first_buf);
}

/* ================================================================ */
/*                           flush                                   */
/* ================================================================ */
static GstFlowReturn
gst_yolobatchdetection_flush (GstAggregator *agg)
{
  GstYoloBatchDetection *self = GST_YOLOBATCHDETECTION (agg);
  self->frame_count = 0;
  self->last_time   = GST_CLOCK_TIME_NONE;
  self->current_fps = 0.0;
  GST_DEBUG_OBJECT (self, "Flushed");
  return GST_FLOW_OK;
}

/* ================================================================ */
/*                     Plugin registration                           */
/* ================================================================ */
extern "C" {
  static gboolean
  yolobatchdetection_init (GstPlugin *plugin)
  {
    return GST_ELEMENT_REGISTER (yolobatchdetection, plugin);
  }

  GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
      GST_VERSION_MINOR,
      yolobatchdetection,
      "YOLO batch detection aggregator for multi-camera inference",
      yolobatchdetection_init,
      "1.0", "LGPL", "GStreamer", "https://gstreamer.net"
  )
}
