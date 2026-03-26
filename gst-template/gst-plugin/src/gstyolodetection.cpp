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
#include "yolo_engine.h"
#include <sstream>
#include <iomanip>

GST_DEBUG_CATEGORY_STATIC (gst_yolodetection_debug);
#define GST_CAT_DEFAULT gst_yolodetection_debug

enum
{
  PROP_0,
  PROP_SILENT,
  PROP_MODEL_PATH,
  PROP_LABEL_PATH,
  PROP_CONF_THRESHOLD,
};

#define SUPPORTED_CAPS "video/x-raw, " \
    "format = (string) { BGR }, " \
    "width = (int) [ 1, 2147483647 ], " \
    "height = (int) [ 1, 2147483647 ], " \
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

/* Helper fuction */
// Hex (Label) to cv
static cv::Scalar hex_to_cv_scalar(const std::string& hex_str) {
    unsigned int hex_val = 0;
    std::stringstream ss;
    std::string clean_hex = (hex_str.find("0x") == 0) ? hex_str.substr(2) : hex_str;
    ss << std::hex << clean_hex;
    ss >> hex_val;
    int r = 0, g = 0, b = 0;

    if (clean_hex.length() == 8) {
        r = (hex_val >> 24) & 0xFF;
        g = (hex_val >> 16) & 0xFF;
        b = (hex_val >> 8) & 0xFF;
    } else if (clean_hex.length() == 6) {
        r = (hex_val >> 16) & 0xFF;
        g = (hex_val >> 8) & 0xFF;
        b = hex_val & 0xFF;
    } else {
        return cv::Scalar(255, 0, 0);
    }

    return cv::Scalar(b, g, r);
}

// Read label file
static gboolean gst_yolodetection_load_labels(Gstyolodetection *filter, const gchar *path) {
    if (!path || !filter->labels) return FALSE;

    try {
        std::ifstream f(path);
        if (!f.is_open()) {
            GST_ERROR_OBJECT(filter, "Could not open label file: %s", path);
            return FALSE;
        }

        nlohmann::json data = nlohmann::json::parse(f);
        filter->labels->clear();

        for (auto& item : data) {
            YoloLabel label;
            label.id = item["id"];
            label.name = item["label"];
            label.color = hex_to_cv_scalar(item["color"].get<std::string>());
            filter->labels->push_back(label);
        }

        GST_INFO_OBJECT(filter, "Successfully loaded %lu labels from %s", filter->labels->size(), path);
        return TRUE;
    } catch (const std::exception& e) {
        GST_ERROR_OBJECT(filter, "Error parsing JSON labels: %s", e.what());
        return FALSE;
    }
}

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

  g_object_class_install_property (gobject_class, PROP_LABEL_PATH,
      g_param_spec_string ("label-path", "Label Path", "Path to the JSON label file",
          NULL, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_CONF_THRESHOLD,
      g_param_spec_float ("conf-threshold", "Confidence Threshold", "Threshold for object detection confidence",
          0.0f, 1.0f, 0.5f, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));


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

  GST_DEBUG_CATEGORY_INIT (gst_yolodetection_debug, "yolodetection", 0,
      "Template yolodetection");
}

/* initialize the element */
static void
gst_yolodetection_init (Gstyolodetection * filter)
{
  filter->model_path = NULL;
  filter->label_path = NULL;
  filter->labels = new std::vector<YoloLabel>(); // Vector labels
  filter->conf_threshold = 0.5f;
  filter->detector = NULL;
  filter->video_info = gst_video_info_new ();

  filter->last_time = GST_CLOCK_TIME_NONE;
  filter->frame_count = 0;
  filter->current_fps = 0.0;
}

static void gst_yolodetection_finalize (GObject * object)
{
    Gstyolodetection *filter = GST_YOLODETECTION (object);
    g_free (filter->model_path);
    g_free (filter->label_path);

    if (filter->labels) {
        delete filter->labels;
        filter->labels = NULL;
    }

    if(filter->detector) {
        delete filter->detector;
        filter->detector = NULL;
    }
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
      if (filter->detector) {
          delete filter->detector;
      }
      filter->detector = new YoloDetector();
      if (!filter->detector->load_model(filter->model_path)) {
          GST_ERROR_OBJECT(filter, "Failed to load ONNX model from %s", filter->model_path);
      } else {
          GST_INFO_OBJECT(filter, "Successfully loaded ONNX model.");
      }
      break;

    case PROP_LABEL_PATH:
      g_free(filter->label_path);
      filter->label_path = g_value_dup_string(value);
      if (filter->label_path) {
          GST_INFO_OBJECT(filter, "Label path set to: %s", filter->label_path);
          gst_yolodetection_load_labels(filter, filter->label_path);
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

/* GstBaseTransform vmethod implementations */
static GstFlowReturn
gst_yolodetection_transform_ip (GstBaseTransform * base, GstBuffer * outbuf)
{
  Gstyolodetection *filter = GST_YOLODETECTION (base);
  GstMapInfo map;

  if (!filter->detector) {
    GST_WARNING_OBJECT(filter, "Detector not initialized, passing buffer through.");
    return GST_FLOW_OK;
  }
  
  if (gst_buffer_map (outbuf, &map, (GstMapFlags)GST_MAP_READWRITE)) {
    int width = GST_VIDEO_INFO_WIDTH(filter->video_info);
    int height = GST_VIDEO_INFO_HEIGHT(filter->video_info);

    // Assuming BGR format as per caps
    cv::Mat frame(height, width, CV_8UC3, map.data);

    // Perform detection
    auto detections = filter->detector->detect(frame, filter->conf_threshold);

    // Draw detections on the frame
    for(const auto& d : detections) {
        cv::Scalar color(255, 0, 0);
        std::string label_name = "Class " + std::to_string(d.class_id);
        if (filter->labels && !filter->labels->empty()) {
            for (const auto& l : *(filter->labels)) {
                if (l.id == d.class_id) {
                    color = l.color;
                    label_name = l.name;
                    break;
                }
            }
        }

        cv::rectangle(frame, d.box, color, 2);
        std::stringstream ss;
        ss << label_name << " " << (int)(d.confidence * 100) << "%";
        std::string display_text = ss.str();
        int baseline = 0;
        cv::Size textSize = cv::getTextSize(display_text, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseline);
        cv::rectangle(frame, 
                      cv::Point(d.box.x, d.box.y - textSize.height - 5),
                      cv::Point(d.box.x + textSize.width, d.box.y), 
                      color, -1);
        cv::putText(frame, display_text, 
                    cv::Point(d.box.x, d.box.y - 2), 
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);
    }

    filter->frame_count++;
    GstClockTime current_time = gst_util_get_timestamp();
    
    if (filter->last_time == GST_CLOCK_TIME_NONE) {
        filter->last_time = current_time;
    } else {
        GstClockTime diff = current_time - filter->last_time;
        if (diff >= GST_SECOND) {
            filter->current_fps = (double)filter->frame_count * GST_SECOND / diff;
            filter->frame_count = 0;
            filter->last_time = current_time;
        }
    }

    if (filter->current_fps > 0) {
        std::stringstream fps_ss;
        fps_ss << "FPS: " << std::fixed << std::setprecision(1) << filter->current_fps;
        cv::putText(frame, fps_ss.str(), 
                    cv::Point(15, 35),
                    cv::FONT_HERSHEY_SIMPLEX, 1.0, 
                    cv::Scalar(0, 255, 0),
                    2);
    }

    gst_buffer_unmap (outbuf, &map);
  }

  return GST_FLOW_OK;
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
