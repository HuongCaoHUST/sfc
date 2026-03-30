#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstmyfilter.h"
// yolo_engine.h is now included via gstmyfilter.h
#include <sstream>
#include <iomanip>
#include <stdexcept>

GST_DEBUG_CATEGORY_STATIC (gst_myfilter_debug);
#define GST_CAT_DEFAULT gst_myfilter_debug

enum
{
  PROP_0,
  PROP_MODEL_PATH,
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

#define gst_myfilter_parent_class parent_class
G_DEFINE_TYPE (Gstmyfilter, gst_myfilter, GST_TYPE_BASE_TRANSFORM);
GST_ELEMENT_REGISTER_DEFINE (myfilter, "myfilter", GST_RANK_NONE,
    GST_TYPE_MYFILTER);

static void gst_myfilter_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_myfilter_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_myfilter_finalize (GObject * object);
static gboolean gst_myfilter_set_caps (GstBaseTransform * trans, GstCaps * incaps, GstCaps * outcaps);
static GstFlowReturn gst_myfilter_transform_ip (GstBaseTransform *
    base, GstBuffer * outbuf);

static void
gst_myfilter_class_init (GstmyfilterClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_myfilter_set_property;
  gobject_class->get_property = gst_myfilter_get_property;
  gobject_class->finalize = gst_myfilter_finalize;

  g_object_class_install_property (gobject_class, PROP_MODEL_PATH,
      g_param_spec_string ("model-path", "Model Path", "Path to the ONNX model file",
          NULL, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_CONF_THRESHOLD,
      g_param_spec_float ("conf-threshold", "Confidence Threshold", "Threshold for object detection confidence",
          0.0f, 1.0f, 0.5f, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_details_simple (gstelement_class,
      "YOLO ONNX Filter",
      "Filter/Video",
      "Performs YOLO object detection using an ONNX model", "HuongCao <<user@hostname.org>>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));

  GST_BASE_TRANSFORM_CLASS (klass)->set_caps = GST_DEBUG_FUNCPTR (gst_myfilter_set_caps);
  GST_BASE_TRANSFORM_CLASS (klass)->transform_ip = GST_DEBUG_FUNCPTR (gst_myfilter_transform_ip);

  GST_DEBUG_CATEGORY_INIT (gst_myfilter_debug, "myfilter", 0, "YOLO ONNX Filter");
}

static void
gst_myfilter_init (Gstmyfilter * filter)
{
  filter->model_path = NULL;
  filter->conf_threshold = 0.5f;
  filter->yolo_engine = nullptr;
  filter->video_info = gst_video_info_new ();

  filter->last_time = GST_CLOCK_TIME_NONE;
  filter->frame_count = 0;
  filter->current_fps = 0.0;
}

static void gst_myfilter_finalize (GObject * object)
{
    Gstmyfilter *filter = GST_MYFILTER (object);
    g_free (filter->model_path);
    if(filter->yolo_engine) {
        delete filter->yolo_engine;
        filter->yolo_engine = nullptr;
    }
    gst_video_info_free(filter->video_info);

    G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_myfilter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  Gstmyfilter *filter = GST_MYFILTER (object);

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
        filter->yolo_engine = new YoloEngine(filter->model_path);
        GST_INFO_OBJECT(filter, "Successfully loaded ONNX model.");
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
  }
}

static void
gst_myfilter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Gstmyfilter *filter = GST_MYFILTER (object);

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

static gboolean gst_myfilter_set_caps (GstBaseTransform * trans, GstCaps * incaps, GstCaps * outcaps)
{
    Gstmyfilter *filter = GST_MYFILTER (trans);
    if (!gst_video_info_from_caps (filter->video_info, incaps)) {
        GST_ERROR_OBJECT (filter, "Failed to parse video caps");
        return FALSE;
    }
    return TRUE;
}

static GstFlowReturn
gst_myfilter_transform_ip (GstBaseTransform * base, GstBuffer * outbuf)
{
  Gstmyfilter *filter = GST_MYFILTER (base);
  GstMapInfo map;

  if (!filter->yolo_engine) {
    GST_WARNING_OBJECT(filter, "Yolo engine not initialized, passing buffer through.");
    return GST_FLOW_OK;
  }
  
  if (gst_buffer_map (outbuf, &map, (GstMapFlags)GST_MAP_READWRITE)) {
    int width = GST_VIDEO_INFO_WIDTH(filter->video_info);
    int height = GST_VIDEO_INFO_HEIGHT(filter->video_info);

    // Assuming BGR format as per caps
    cv::Mat frame(height, width, CV_8UC3, map.data);

    // Perform detection
    auto detections = filter->yolo_engine->detect(frame, filter->conf_threshold);

    // Draw detections on the frame
    for(const auto& d : detections) {
        cv::rectangle(frame, d.box, cv::Scalar(255, 0, 0), 2);
        std::stringstream ss;
        ss << "Class " << d.class_id << ": " << std::fixed << std::setprecision(2) << d.confidence;
        std::string label = ss.str();
        cv::putText(frame, label, 
                    cv::Point(d.box.x, d.box.y - 5), 
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 0, 0), 1);
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

extern "C" {
  static gboolean myfilter_init (GstPlugin * plugin) {
    return GST_ELEMENT_REGISTER (myfilter, plugin);
  }

  GST_PLUGIN_DEFINE (
      GST_VERSION_MAJOR,
      GST_VERSION_MINOR,
      myfilter,
      "YOLO ONNX Filter",
      myfilter_init,
      "1.0", "LGPL", "GStreamer", "https://gstreamer.net"
  )
}
