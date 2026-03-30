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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include "gstyolobackend.h"
#include <nlohmann/json.hpp>

GST_DEBUG_CATEGORY_STATIC (gst_yolo_backend_debug);
#define GST_CAT_DEFAULT gst_yolo_backend_debug

using json = nlohmann::json;

enum
{
  PROP_0,
  PROP_MODEL_PATH,
  PROP_DEST_HOST,
  PROP_DEST_PORT,
};

#define TENSOR_CAPS "application/x-tensor, type=(string)float32"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (TENSOR_CAPS)
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (TENSOR_CAPS)
    );

G_DEFINE_TYPE (GstYoloBackend, gst_yolo_backend, GST_TYPE_BASE_TRANSFORM);

static void gst_yolo_backend_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_yolo_backend_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_yolo_backend_finalize (GObject * object);

static gboolean gst_yolo_backend_start (GstBaseTransform * trans);
static gboolean gst_yolo_backend_stop (GstBaseTransform * trans);

static GstFlowReturn gst_yolo_backend_transform_ip (GstBaseTransform * trans, GstBuffer * buf);

static void
gst_yolo_backend_class_init (GstYoloBackendClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *transform_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = gst_yolo_backend_set_property;
  gobject_class->get_property = gst_yolo_backend_get_property;
  gobject_class->finalize = gst_yolo_backend_finalize;

  transform_class->start = GST_DEBUG_FUNCPTR (gst_yolo_backend_start);
  transform_class->stop = GST_DEBUG_FUNCPTR (gst_yolo_backend_stop);
  transform_class->transform_ip = GST_DEBUG_FUNCPTR(gst_yolo_backend_transform_ip);

  g_object_class_install_property (gobject_class, PROP_MODEL_PATH,
      g_param_spec_string ("model", "Model",
          "Path to the ONNX model file for part 2",
          "yolo_part2.onnx", G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_DEST_HOST,
      g_param_spec_string ("dest-host", "Destination Host",
          "The destination hostname or IP address for UDP packets.",
          "127.0.0.1", G_PARAM_READWRITE));
    
  g_object_class_install_property (gobject_class, PROP_DEST_PORT,
      g_param_spec_int("dest-port", "Destination Port",
          "The destination port for UDP packets.",
          0, G_MAXINT, 5005, G_PARAM_READWRITE));

  gst_element_class_add_static_pad_template (element_class, &src_template);
  gst_element_class_add_static_pad_template (element_class, &sink_template);

  gst_element_class_set_static_metadata (element_class,
      "YOLO Backend Element", "Generic",
      "Runs the second part of a YOLO model and sends results via UDP.",
      "<your name here>");

  GST_DEBUG_CATEGORY_INIT (gst_yolo_backend_debug, "yolobackend", 0,
      "YOLO Backend element");
}

static void
gst_yolo_backend_init (GstYoloBackend * self)
{
  self->model_path = g_strdup ("yolo_part2.onnx");
  self->dest_host = g_strdup("127.0.0.1");
  self->dest_port = 5005;
  self->yolo_engine = nullptr;
  self->udp_socket = nullptr;
  self->dest_address = nullptr;
  self->socket_address = nullptr;

  // Set transform to be in-place
  gst_base_transform_set_in_place(GST_BASE_TRANSFORM(self), TRUE);
}

static void
gst_yolo_backend_finalize (GObject * object)
{
  GstYoloBackend *self = GST_YOLO_BACKEND (object);

  GST_DEBUG_OBJECT (self, "finalize");

  g_free (self->model_path);
  g_free (self->dest_host);

  if (self->yolo_engine) {
    delete self->yolo_engine;
  }
  if (self->socket_address) {
      g_object_unref(self->socket_address);
  }
  if (self->dest_address) {
      g_object_unref(self->dest_address);
  }
  if (self->udp_socket) {
      g_object_unref(self->udp_socket);
  }
  
  G_OBJECT_CLASS (gst_yolo_backend_parent_class)->finalize (object);
}

static void
gst_yolo_backend_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstYoloBackend *self = GST_YOLO_BACKEND (object);

  switch (prop_id) {
    case PROP_MODEL_PATH:
      g_free (self->model_path);
      self->model_path = g_value_dup_string (value);
      break;
    case PROP_DEST_HOST:
      g_free(self->dest_host);
      self->dest_host = g_value_dup_string(value);
      break;
    case PROP_DEST_PORT:
      self->dest_port = g_value_get_int(value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_yolo_backend_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstYoloBackend *self = GST_YOLO_BACKEND (object);

  switch (prop_id) {
    case PROP_MODEL_PATH:
      g_value_set_string (value, self->model_path);
      break;
    case PROP_DEST_HOST:
      g_value_set_string(value, self->dest_host);
      break;
    case PROP_DEST_PORT:
      g_value_set_int(value, self->dest_port);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_yolo_backend_start (GstBaseTransform * trans)
{
  GstYoloBackend *self = GST_YOLO_BACKEND (trans);
  GError *error = NULL;
  GST_INFO_OBJECT (self, "Starting...");

  // Init Yolo Engine
  try {
    self->yolo_engine = new YoloEngine(self->model_path);
    GST_INFO_OBJECT(self, "ONNX Runtime session created for %s", self->model_path);
  } catch (const std::exception& e) {
    GST_ERROR_OBJECT(self, "Failed to create YoloEngine: %s", e.what());
    return FALSE;
  }

  // Init UDP Socket
  self->udp_socket = g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, &error);
  if (error) {
      GST_ERROR_OBJECT(self, "Failed to create UDP socket: %s", error->message);
      g_error_free(error);
      return FALSE;
  }

  self->dest_address = g_inet_address_new_from_string(self->dest_host);
  self->socket_address = (GInetSocketAddress*)g_inet_socket_address_new(self->dest_address, self->dest_port);

  GST_INFO_OBJECT(self, "UDP socket configured to send to %s:%d", self->dest_host, self->dest_port);

  return TRUE;
}

static gboolean
gst_yolo_backend_stop (GstBaseTransform * trans)
{
  GstYoloBackend *self = GST_YOLO_BACKEND (trans);
  GST_INFO_OBJECT (self, "Stopping...");

  if(self->yolo_engine) {
    delete self->yolo_engine;
    self->yolo_engine = nullptr;
    GST_INFO_OBJECT(self, "YoloEngine destroyed.");
  }

  if (self->udp_socket) {
      g_socket_close(self->udp_socket, NULL);
      g_object_unref(self->udp_socket);
      self->udp_socket = nullptr;
      g_object_unref(self->socket_address);
      self->socket_address = nullptr;
      g_object_unref(self->dest_address);
      self->dest_address = nullptr;
      GST_INFO_OBJECT(self, "UDP socket closed.");
  }


  return TRUE;
}

static GstFlowReturn
gst_yolo_backend_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
    GstYoloBackend *self = GST_YOLO_BACKEND (trans);
    GstMapInfo map;
    GError *error = NULL;

    if (gst_buffer_map (buf, &map, GST_MAP_READ) == FALSE) {
        GST_ERROR_OBJECT(self, "Failed to map input buffer");
        return GST_FLOW_ERROR;
    }

    const size_t EXPECTED_BYTES = 1024000 * sizeof(float);

    if (map.size != EXPECTED_BYTES) {
        GST_WARNING_OBJECT(self, "Dữ liệu không đủ! Nhận được %zu bytes, cần %zu bytes. Bỏ qua frame này.", 
                           map.size, EXPECTED_BYTES);
        gst_buffer_unmap(buf, &map);
        // Trả về GST_FLOW_OK thay vì ERROR để pipeline tiếp tục chạy frame sau
        return GST_FLOW_OK; 
    }

    // This buffer comes from yoloforward, containing the raw float tensor
    float* tensor_data = (float*)map.data;
    size_t tensor_size = map.size / sizeof(float);

    // NOTE: This assumes YoloEngine has a method that takes the raw tensor
    // and performs part 2 inference + post-processing.
    // You might need to adapt YoloEngine.
    std::vector<Detection> detections;
    try {
        // Since we don't have the original frame size, we assume output coordinates
        // are relative to the model input size (e.g., 640x640).
        detections = self->yolo_engine->run_part2_and_postprocess(tensor_data, tensor_size, 640, 640, 0.5f, 0.45f);
    } catch (const std::exception& e) {
        GST_ERROR_OBJECT(self, "Backend inference/post-processing failed: %s", e.what());
        gst_buffer_unmap(buf, &map);
        return GST_FLOW_ERROR;
    }
    
    gst_buffer_unmap (buf, &map);

    // Create JSON
    json result_json;
    result_json["pts"] = GST_BUFFER_PTS(buf);
    json predictions = json::array();
    
    for(const auto& d : detections) {
        json det;
        det["x"] = d.box.x;
        det["y"] = d.box.y;
        det["w"] = d.box.width;
        det["h"] = d.box.height;
        det["confidence"] = d.confidence;
        det["class_id"] = d.class_id;
        predictions.push_back(det);
    }
    result_json["predictions"] = predictions;

    // Send over UDP
    std::string json_str = result_json.dump();
    g_print("DEBUG: Sending JSON: %s\n", json_str.c_str());
    g_socket_send_to(self->udp_socket, (GSocketAddress*)self->socket_address, json_str.c_str(), json_str.length(), NULL, &error);
    
    if (error) {
        GST_WARNING_OBJECT(self, "Failed to send UDP packet: %s", error->message);
        g_error_free(error);
    } else {
        GST_DEBUG_OBJECT(self, "Sent %ld bytes of JSON data", json_str.length());
    }

    return GST_FLOW_OK;
}

// Boilerplate entry point
static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "yolobackend", GST_RANK_NONE,
      GST_TYPE_YOLO_BACKEND);
}

#ifndef VERSION
#define VERSION "0.0.1"
#endif
#ifndef PACKAGE
#define PACKAGE "gst-yolo-plugins"
#endif
#ifndef PACKAGE_NAME
#define PACKAGE_NAME "GStreamer YOLO Plugins"
#endif
#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "http://example.com/"
#endif

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    yolobackend,
    "GStreamer YOLO Backend Plugin",
    plugin_init, VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)