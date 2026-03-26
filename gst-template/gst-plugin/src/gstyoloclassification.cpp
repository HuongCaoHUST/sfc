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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/base.h>
#include <gst/controller/controller.h>

#include "gstyoloclassification.h"
#include "yolo_engine.h"
#include <sstream>
#include <iomanip>

GST_DEBUG_CATEGORY_STATIC (gst_yoloclassification_debug);
#define GST_CAT_DEFAULT gst_yoloclassification_debug

enum
{
  PROP_0,
  PROP_MODEL_PATH,
  PROP_CONF_THRESHOLD,
};

#define SUPPORTED_CAPS "video/x-raw, format = (string) { BGR }, width = (int) [ 1, 2147483647 ], height = (int) [ 1, 2147483647 ], framerate = (fraction) [ 0/1, 2147483647/1 ]"

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

#define gst_yoloclassification_parent_class parent_class
G_DEFINE_TYPE (GstYoloClassification, gst_yoloclassification, GST_TYPE_BASE_TRANSFORM);
GST_ELEMENT_REGISTER_DEFINE (yoloclassification, "yoloclassification", GST_RANK_NONE,
    GST_TYPE_YOLOCLASSIFICATION);

static void gst_yoloclassification_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_yoloclassification_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_yoloclassification_finalize (GObject * object);
static gboolean gst_yoloclassification_set_caps (GstBaseTransform * trans, GstCaps * incaps, GstCaps * outcaps);

static GstFlowReturn gst_yoloclassification_transform_ip (GstBaseTransform *
    base, GstBuffer * outbuf);

static void
gst_yoloclassification_class_init (GstYoloClassificationClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_yoloclassification_set_property;
  gobject_class->get_property = gst_yoloclassification_get_property;
  gobject_class->finalize = gst_yoloclassification_finalize;

  g_object_class_install_property (gobject_class, PROP_MODEL_PATH,
      g_param_spec_string ("model-path", "Model Path", "Path to the ONNX model file",
          NULL, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  
  g_object_class_install_property (gobject_class, PROP_CONF_THRESHOLD,
      g_param_spec_float ("conf-threshold", "Confidence Threshold", "Threshold for classification confidence",
          0.0f, 1.0f, 0.5f, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_details_simple (gstelement_class,
      "yoloclassification",
      "Generic/Filter",
      "Performs YOLO image classification using an ONNX model", "HuongCao <<huongcao.seee@gmail.com>>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));
  
  GST_BASE_TRANSFORM_CLASS (klass)->set_caps = 
      GST_DEBUG_FUNCPTR (gst_yoloclassification_set_caps);
  GST_BASE_TRANSFORM_CLASS (klass)->transform_ip =
      GST_DEBUG_FUNCPTR (gst_yoloclassification_transform_ip);

  GST_DEBUG_CATEGORY_INIT (gst_yoloclassification_debug, "yoloclassification", 0,
      "Template yoloclassification");
}

static void
gst_yoloclassification_init (GstYoloClassification * filter)
{
  filter->model_path = NULL;
  filter->conf_threshold = 0.5f;
  filter->classifier = NULL;
  filter->video_info = gst_video_info_new ();

  filter->last_time = GST_CLOCK_TIME_NONE;
  filter->frame_count = 0;
  filter->current_fps = 0.0;
}

static void gst_yoloclassification_finalize (GObject * object)
{
    GstYoloClassification *filter = GST_YOLOCLASSIFICATION (object);
    g_free (filter->model_path);
    if(filter->classifier) {
        delete filter->classifier;
        filter->classifier = NULL;
    }
    gst_video_info_free(filter->video_info);

    G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_yoloclassification_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstYoloClassification *filter = GST_YOLOCLASSIFICATION (object);

  switch (prop_id) {
    case PROP_MODEL_PATH:
      g_free(filter->model_path);
      filter->model_path = g_value_dup_string(value);
      GST_INFO_OBJECT(filter, "Model path set to: %s", filter->model_path);
      if (filter->classifier) {
          delete filter->classifier;
      }
      filter->classifier = new YoloClassifier();
      if (!filter->classifier->load_model(filter->model_path)) {
          GST_ERROR_OBJECT(filter, "Failed to load ONNX model from %s", filter->model_path);
      } else {
          GST_INFO_OBJECT(filter, "Successfully loaded ONNX model.");
      }
      break;
    case PROP_CONF_THRESHOLD:
      filter->conf_threshold = g_value_get_float (value);
      GST_INFO_OBJECT(filter, "Confidence threshold set to: %f", filter->conf_threshold);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_yoloclassification_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstYoloClassification *filter = GST_YOLOCLASSIFICATION (object);

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
  }
}

static gboolean gst_yoloclassification_set_caps (GstBaseTransform * trans, GstCaps * incaps, GstCaps * outcaps)
{
    GstYoloClassification *filter = GST_YOLOCLASSIFICATION (trans);
    if (!gst_video_info_from_caps (filter->video_info, incaps)) {
        GST_ERROR_OBJECT (filter, "Failed to parse video caps");
        return FALSE;
    }
    return TRUE;
}

static GstFlowReturn
gst_yoloclassification_transform_ip (GstBaseTransform * base, GstBuffer * outbuf)
{
  GstYoloClassification *filter = GST_YOLOCLASSIFICATION (base);
  GstMapInfo map;

  if (!filter->classifier) {
    GST_WARNING_OBJECT(filter, "Classifier not initialized, passing buffer through.");
    return GST_FLOW_OK;
  }
  
  if (gst_buffer_map (outbuf, &map, (GstMapFlags)GST_MAP_READWRITE)) {
    int width = GST_VIDEO_INFO_WIDTH(filter->video_info);
    int height = GST_VIDEO_INFO_HEIGHT(filter->video_info);

    cv::Mat frame(height, width, CV_8UC3, map.data);

    auto classifications = filter->classifier->classify(frame, filter->conf_threshold);

    // Draw classifications on the frame
    int y_pos = 30;
    for(const auto& c : classifications) {
        std::stringstream ss;
        ss << "Class " << c.class_id << ": " << std::fixed << std::setprecision(2) << c.confidence;
        std::string label = ss.str();
        cv::putText(frame, label, 
                    cv::Point(10, y_pos), 
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);
        y_pos += 30;
    }

    gst_buffer_unmap (outbuf, &map);
  }

  return GST_FLOW_OK;
}

extern "C" {
  static gboolean yoloclassification_init (GstPlugin * plugin) {
    return GST_ELEMENT_REGISTER (yoloclassification, plugin);
  }

  GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
      GST_VERSION_MINOR,
      yoloclassification,
      "YOLO ONNX CLASSIFICATION",
      yoloclassification_init,
      "1.0", "LGPL", "GStreamer", "https://gstreamer.net"
  )
}
