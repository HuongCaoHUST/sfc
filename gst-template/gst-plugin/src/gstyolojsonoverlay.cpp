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
 * SECTION:element-yolojsonoverlay
 *
 * FIXME:Describe yolojsonoverlay here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! yolojsonoverlay ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/base.h>
#include <gst/controller/controller.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <map>
#include <string>
#include <regex>
#include <atomic>
#include <chrono>

#include "gstyolojsonoverlay.h"
// Network & OpenCV
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <opencv2/opencv.hpp>

GST_DEBUG_CATEGORY_STATIC (gst_yolojsonoverlay_debug);
#define GST_CAT_DEFAULT gst_yolojsonoverlay_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_UDP_PORT,
};

#define SUPPORTED_CAPS "video/x-raw, " \
    "format = (string) { BGR }, " \
    "width = (int) [ 1, 2147483647 ], " \
    "height = (int) [ 1, 2147483647 ], " \
    "framerate = (fraction) [ 0/1, 2147483647/1 ]"

/* the capabilities of the inputs and outputs.
 *
 * FIXME:describe the real formats here.
 */
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS (SUPPORTED_CAPS));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS (SUPPORTED_CAPS));

#define gst_yolojsonoverlay_parent_class parent_class
G_DEFINE_TYPE (Gstyolojsonoverlay, gst_yolojsonoverlay, GST_TYPE_BASE_TRANSFORM);
GST_ELEMENT_REGISTER_DEFINE (yolojsonoverlay, "yolojsonoverlay", GST_RANK_NONE,
    GST_TYPE_YOLOJSONOVERLAY);

struct YoloJsonOverlayContext {
    int udp_sock = -1;
    std::atomic<bool> is_running{false};
    std::thread udp_thread;
    
    std::map<GstClockTime, std::string> json_buffer;
    std::mutex map_mutex;
    std::condition_variable cv;
};

static void gst_yolojsonoverlay_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_yolojsonoverlay_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_yolojsonoverlay_finalize (GObject * object);
static gboolean gst_yolojsonoverlay_set_caps (GstBaseTransform * trans, GstCaps * incaps, GstCaps * outcaps);
static gboolean gst_yolojsonoverlay_start (GstBaseTransform * trans);
static gboolean gst_yolojsonoverlay_stop (GstBaseTransform * trans);

static GstFlowReturn gst_yolojsonoverlay_transform_ip (GstBaseTransform *
    base, GstBuffer * outbuf);

static void udp_listener_thread_func(Gstyolojsonoverlay *filter) {
    YoloJsonOverlayContext *ctx = static_cast<YoloJsonOverlayContext*>(filter->ctx);
    char buffer[8192];
    
    struct pollfd fds[1];
    fds[0].fd = ctx->udp_sock;
    fds[0].events = POLLIN;

    GST_INFO_OBJECT(filter, "UDP Listener thread started on port %d", filter->udp_port);

    std::regex pts_re(R"("pts"\s*:\s*(\d+))");

    while (ctx->is_running) {
        int ret = poll(fds, 1, 100);
        if (ret > 0 && (fds[0].revents & POLLIN)) {
            ssize_t len = recvfrom(ctx->udp_sock, buffer, sizeof(buffer) - 1, 0, NULL, NULL);
            if (len > 0) {
                buffer[len] = '\0';
                std::string json_str(buffer);
                std::smatch match;
                if (std::regex_search(json_str, match, pts_re)) {
                    GstClockTime received_pts = std::stoull(match[1].str());

                    std::unique_lock<std::mutex> lock(ctx->map_mutex);
                    ctx->json_buffer[received_pts] = json_str;
                    for (auto it = ctx->json_buffer.begin(); it != ctx->json_buffer.end(); ) {
                        if (it->first < received_pts - GST_SECOND) {
                            it = ctx->json_buffer.erase(it);
                        } else {
                            ++it;
                        }
                    }
                    ctx->cv.notify_all();
                }
            }
        }
    }
    GST_INFO_OBJECT(filter, "UDP Listener thread stopped");
}

/* GObject vmethod implementations */

/* initialize the yolojsonoverlay's class */
static void
gst_yolojsonoverlay_class_init (GstyolojsonoverlayClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseTransformClass *btrans_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_yolojsonoverlay_set_property;
  gobject_class->get_property = gst_yolojsonoverlay_get_property;
  gobject_class->finalize = gst_yolojsonoverlay_finalize;

  g_object_class_install_property (gobject_class, PROP_UDP_PORT,
      g_param_spec_int ("udp-port", "UDP Port", "UDP port to listen for JSON metadata",
          1, 65535, 5002, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_details_simple ((GstElementClass*)klass,
      "yolojsonoverlay", "Generic/Filter",
      "Draws bounding boxes from UDP JSON stream", "HuongCao");

  gst_element_class_add_pad_template ((GstElementClass*)klass, gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template ((GstElementClass*)klass, gst_static_pad_template_get (&sink_template));
  
  btrans_class->set_caps = GST_DEBUG_FUNCPTR (gst_yolojsonoverlay_set_caps);
  btrans_class->start = GST_DEBUG_FUNCPTR (gst_yolojsonoverlay_start);
  btrans_class->stop = GST_DEBUG_FUNCPTR (gst_yolojsonoverlay_stop);
  btrans_class->transform_ip = GST_DEBUG_FUNCPTR (gst_yolojsonoverlay_transform_ip);
  GST_DEBUG_CATEGORY_INIT (gst_yolojsonoverlay_debug, "yolojsonoverlay", 0,
      "Template yolojsonoverlay");
}

/* initialize the new element
 * initialize instance structure
 */
static void
gst_yolojsonoverlay_init (Gstyolojsonoverlay * filter)
{
  filter->udp_port = 5002;
  filter->video_info = gst_video_info_new ();
  filter->ctx = new YoloJsonOverlayContext();
}

static void gst_yolojsonoverlay_finalize (GObject * object) {
    Gstyolojsonoverlay *filter = GST_YOLOJSONOVERLAY (object);
    gst_video_info_free(filter->video_info);
    
    YoloJsonOverlayContext *ctx = static_cast<YoloJsonOverlayContext*>(filter->ctx);
    delete ctx;

    G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_yolojsonoverlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  Gstyolojsonoverlay *filter = GST_YOLOJSONOVERLAY (object);

  if (prop_id == PROP_UDP_PORT) {
      filter->udp_port = g_value_get_int (value);
  } else {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_yolojsonoverlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Gstyolojsonoverlay *filter = GST_YOLOJSONOVERLAY (object);

  if (prop_id == PROP_UDP_PORT) {
      g_value_set_int (value, filter->udp_port);
  } else {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static gboolean gst_yolojsonoverlay_set_caps (GstBaseTransform * trans, GstCaps * incaps, GstCaps * outcaps) {
    Gstyolojsonoverlay *filter = GST_YOLOJSONOVERLAY (trans);
    return gst_video_info_from_caps (filter->video_info, incaps);
}

static gboolean gst_yolojsonoverlay_start (GstBaseTransform * trans) {
    Gstyolojsonoverlay *filter = GST_YOLOJSONOVERLAY (trans);
    YoloJsonOverlayContext *ctx = static_cast<YoloJsonOverlayContext*>(filter->ctx);

    ctx->udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (ctx->udp_sock < 0) {
        GST_ERROR_OBJECT(filter, "Failed to create UDP socket");
        return FALSE;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(filter->udp_port);

    if (bind(ctx->udp_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        GST_ERROR_OBJECT(filter, "Failed to bind UDP socket to port %d", filter->udp_port);
        close(ctx->udp_sock);
        return FALSE;
    }

    ctx->is_running = true;
    ctx->udp_thread = std::thread(udp_listener_thread_func, filter);
    return TRUE;
}

static gboolean gst_yolojsonoverlay_stop (GstBaseTransform * trans) {
    Gstyolojsonoverlay *filter = GST_YOLOJSONOVERLAY (trans);
    YoloJsonOverlayContext *ctx = static_cast<YoloJsonOverlayContext*>(filter->ctx);

    ctx->is_running = false;
    if (ctx->udp_thread.joinable()) {
        ctx->udp_thread.join();
    }
    if (ctx->udp_sock >= 0) {
        close(ctx->udp_sock);
        ctx->udp_sock = -1;
    }
    return TRUE;
}

/* GstBaseTransform vmethod implementations */

/* this function does the actual processing
 */
static GstFlowReturn
gst_yolojsonoverlay_transform_ip (GstBaseTransform * base, GstBuffer * outbuf)
{
  Gstyolojsonoverlay *filter = GST_YOLOJSONOVERLAY (base);
  
  YoloJsonOverlayContext *ctx = static_cast<YoloJsonOverlayContext*>(filter->ctx);
  
  GstClockTime frame_pts = GST_BUFFER_PTS(outbuf);
  std::string matched_json = "";

  // 1. Chờ dữ liệu JSON (Tối đa 40ms)
  {
      std::unique_lock<std::mutex> lock(ctx->map_mutex);
      bool found = ctx->cv.wait_for(lock, std::chrono::milliseconds(40), 
          [&]() { return ctx->json_buffer.find(frame_pts) != ctx->json_buffer.end(); }
      );

      if (found) {
          matched_json = ctx->json_buffer[frame_pts];
      }
  }

  if (!matched_json.empty()) {
      GstMapInfo map;
      if (gst_buffer_map (outbuf, &map, GST_MAP_READWRITE)) {
          int width = GST_VIDEO_INFO_WIDTH(filter->video_info);
          int height = GST_VIDEO_INFO_HEIGHT(filter->video_info);
          cv::Mat frame(height, width, CV_8UC3, map.data);

          std::regex box_re(R"REGEX("x"\s*:\s*([\d.]+)\s*,\s*"y"\s*:\s*([\d.]+)\s*,\s*"width"\s*:\s*([\d.]+)\s*,\s*"height"\s*:\s*([\d.]+)\s*,\s*"confidence"\s*:\s*([\d.]+)[^}]*"class"\s*:\s*"([^"]+)")REGEX");
          auto words_begin = std::sregex_iterator(matched_json.begin(), matched_json.end(), box_re);
          auto words_end = std::sregex_iterator();

          for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
              std::smatch match = *i;
              int x = std::stoi(match[1].str());
              int y = std::stoi(match[2].str());
              int w = std::stoi(match[3].str());
              int h = std::stoi(match[4].str());
              float conf = std::stof(match[5].str());
              std::string cls_name = match[6].str();

              cv::rectangle(frame, cv::Rect(x, y, w, h), cv::Scalar(0, 255, 0), 2);
              
              std::stringstream label;
              label << cls_name << " " << std::fixed << std::setprecision(2) << conf;
              cv::putText(frame, label.str(), cv::Point(x, y - 5), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);
          }

          gst_buffer_unmap (outbuf, &map);
      }
  } else {
      GST_DEBUG_OBJECT(filter, "Missed JSON for PTS %" GST_TIME_FORMAT ". Frame will be clean.", GST_TIME_ARGS(frame_pts));
  }

  return GST_FLOW_OK;
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
extern "C" {
  static gboolean yolojsonoverlay_init (GstPlugin * plugin) {
    return GST_ELEMENT_REGISTER (yolojsonoverlay, plugin);
  }

  GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
      GST_VERSION_MINOR,
      yolojsonoverlay,
      "yolojsonoverlay",
      yolojsonoverlay_init,
      "1.0", "LGPL", "GStreamer", "https://gstreamer.net"
  )
}