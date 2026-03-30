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
#include "gstyoloforward.h"
#include <opencv2/dnn/dnn.hpp>

GST_DEBUG_CATEGORY_STATIC (gst_yolo_forward_debug);
#define GST_CAT_DEFAULT gst_yolo_forward_debug

enum
{
  PROP_0,
  PROP_MODEL_PATH,
};

#define IN_VIDEO_CAPS "video/x-raw, format=(string)BGR, width=(int)640, height=(int)640"
#define OUT_TENSOR_CAPS "application/x-tensor, type=(string)float32"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (IN_VIDEO_CAPS)
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (OUT_TENSOR_CAPS)
    );

G_DEFINE_TYPE (GstYoloForward, gst_yolo_forward, GST_TYPE_BASE_TRANSFORM);

static void gst_yolo_forward_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_yolo_forward_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_yolo_forward_finalize (GObject * object);

static gboolean gst_yolo_forward_start (GstBaseTransform * trans);
static gboolean gst_yolo_forward_stop (GstBaseTransform * trans);

static GstCaps *gst_yolo_forward_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);

static GstFlowReturn gst_yolo_forward_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);

static gboolean gst_yolo_forward_set_caps (GstBaseTransform * trans, GstCaps * incaps, GstCaps * outcaps);

static void
gst_yolo_forward_class_init (GstYoloForwardClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *transform_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = gst_yolo_forward_set_property;
  gobject_class->get_property = gst_yolo_forward_get_property;
  gobject_class->finalize = gst_yolo_forward_finalize;

  transform_class->start = GST_DEBUG_FUNCPTR (gst_yolo_forward_start);
  transform_class->stop = GST_DEBUG_FUNCPTR (gst_yolo_forward_stop);
  transform_class->transform_caps = GST_DEBUG_FUNCPTR (gst_yolo_forward_transform_caps);
  transform_class->set_caps = GST_DEBUG_FUNCPTR(gst_yolo_forward_set_caps);

  // We must override transform() because output size is different from input
  transform_class->transform = GST_DEBUG_FUNCPTR(gst_yolo_forward_transform);
  transform_class->transform_ip = NULL;

  g_object_class_install_property (gobject_class, PROP_MODEL_PATH,
      g_param_spec_string ("model", "Model",
          "Path to the ONNX model file for part 1",
          "yolo_part1.onnx", G_PARAM_READWRITE));

  gst_element_class_add_static_pad_template (element_class, &src_template);
  gst_element_class_add_static_pad_template (element_class, &sink_template);

  gst_element_class_set_static_metadata (element_class,
      "YOLO Forward Element", "Generic",
      "Performs pre-processing and runs the first part of a YOLO model.",
      "<your name here>");

  GST_DEBUG_CATEGORY_INIT (gst_yolo_forward_debug, "yoloforward", 0,
      "YOLO Forward element");
}

static void
gst_yolo_forward_init (GstYoloForward * self)
{
  self->model_path = g_strdup ("yolo_part1.onnx");
  self->yolo_engine = nullptr;
  self->video_info = gst_video_info_new();
}

static void
gst_yolo_forward_finalize (GObject * object)
{
  GstYoloForward *self = GST_YOLO_FORWARD (object);

  GST_DEBUG_OBJECT (self, "finalize");

  g_free (self->model_path);
  if(self->yolo_engine) {
    delete self->yolo_engine;
    self->yolo_engine = nullptr;
  }
  if (self->video_info) {
      gst_video_info_free(self->video_info);
      self->video_info = nullptr;
  }

  G_OBJECT_CLASS (gst_yolo_forward_parent_class)->finalize (object);
}

static void
gst_yolo_forward_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstYoloForward *self = GST_YOLO_FORWARD (object);

  switch (prop_id) {
    case PROP_MODEL_PATH:
      g_free (self->model_path);
      self->model_path = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_yolo_forward_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstYoloForward *self = GST_YOLO_FORWARD (object);

  switch (prop_id) {
    case PROP_MODEL_PATH:
      g_value_set_string (value, self->model_path);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_yolo_forward_start (GstBaseTransform * trans)
{
  GstYoloForward *self = GST_YOLO_FORWARD (trans);
  GST_INFO_OBJECT (self, "Starting...");

  try {
    self->yolo_engine = new YoloEngine(self->model_path);
    GST_INFO_OBJECT(self, "ONNX Runtime session created for %s", self->model_path);
  } catch (const std::exception& e) {
    GST_ERROR_OBJECT(self, "Failed to create YoloEngine: %s", e.what());
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_yolo_forward_stop (GstBaseTransform * trans)
{
  GstYoloForward *self = GST_YOLO_FORWARD (trans);
  GST_INFO_OBJECT (self, "Stopping...");

  if(self->yolo_engine) {
    delete self->yolo_engine;
    self->yolo_engine = nullptr;
    GST_INFO_OBJECT(self, "YoloEngine destroyed.");
  }

  return TRUE;
}

static gboolean
gst_yolo_forward_set_caps (GstBaseTransform * trans, GstCaps * incaps, GstCaps * outcaps) {
    GstYoloForward *self = GST_YOLO_FORWARD (trans);
    if (!gst_video_info_from_caps(self->video_info, incaps)) {
        GST_ERROR_OBJECT(self, "Failed to parse input caps");
        return FALSE;
    }
    GST_INFO_OBJECT(self, "Input caps set: %" GST_PTR_FORMAT, incaps);
    return TRUE;
}

static GstCaps *
gst_yolo_forward_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *ret;

  if (direction == GST_PAD_SINK) {
    // Input is video, output is tensor.
    ret = gst_caps_from_string(OUT_TENSOR_CAPS);
  } else { // SRC
    // Input is tensor, output is video (not our case).
    ret = gst_caps_from_string(IN_VIDEO_CAPS);
  }

  if (filter) {
    GstCaps *intersection = gst_caps_intersect_full(filter, ret, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref(ret);
    return intersection;
  }

  return ret;
}

static GstFlowReturn
gst_yolo_forward_transform (GstBaseTransform * trans, GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstYoloForward *self = GST_YOLO_FORWARD (trans);
  GstMapInfo in_map;

  if (gst_buffer_map (inbuf, &in_map, GST_MAP_READ) == FALSE) {
    GST_ERROR_OBJECT(self, "Failed to map input buffer");
    return GST_FLOW_ERROR;
  }

  // Map GstBuffer to cv::Mat without copying
  cv::Mat frame(self->video_info->height, self->video_info->width, CV_8UC3, in_map.data);
  
  if (frame.empty()) {
      gst_buffer_unmap(inbuf, &in_map);
      GST_ERROR_OBJECT(self, "Input frame is empty");
      return GST_FLOW_ERROR;
  }

  // Pre-process and run inference
  // NOTE: This assumes YoloEngine has a method that returns the raw tensor output.
  // You might need to adapt YoloEngine.
  std::vector<float> output_tensor_data;
  try {
    output_tensor_data = self->yolo_engine->run_part1(frame);
  } catch (const std::exception& e) {
    GST_ERROR_OBJECT(self, "Inference failed: %s", e.what());
    gst_buffer_unmap(inbuf, &in_map);
    return GST_FLOW_ERROR;
  }

  gst_buffer_unmap (inbuf, &in_map);

  if (output_tensor_data.empty()) {
      GST_WARNING_OBJECT(self, "Inference produced no output");
      // Return an empty buffer to keep the pipeline flowing
      gst_buffer_resize(outbuf, 0, 0);
      return GST_FLOW_OK;
  }
  
  // Allocate new buffer for the output tensor
  gsize output_size = output_tensor_data.size() * sizeof(float);
  GstBuffer *tensor_buf = gst_buffer_new_allocate(NULL, output_size, NULL);

  if (!tensor_buf) {
      GST_ERROR_OBJECT(self, "Failed to allocate output tensor buffer");
      return GST_FLOW_ERROR;
  }

  // Copy tensor data to the new buffer
  GstMapInfo out_map;
  if (gst_buffer_map(tensor_buf, &out_map, GST_MAP_WRITE)) {
      memcpy(out_map.data, output_tensor_data.data(), output_size);
      gst_buffer_unmap(tensor_buf, &out_map);
  } else {
      GST_ERROR_OBJECT(self, "Failed to map output tensor buffer");
      gst_buffer_unref(tensor_buf);
      return GST_FLOW_ERROR;
  }

  // Copy timestamp and other metadata
  gst_buffer_copy_into(outbuf, tensor_buf, (GstBufferCopyFlags)(GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS), 0, -1);
  gst_buffer_unref(tensor_buf);
  
  return GST_FLOW_OK;
}

// Boilerplate entry point
static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "yoloforward", GST_RANK_NONE,
      GST_TYPE_YOLO_FORWARD);
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
    yoloforward,
    "GStreamer YOLO Forward Plugin",
    plugin_init, VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)