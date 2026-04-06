#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstseiidinserter.h"
#include <cstring>
#include <cstdint>

GST_DEBUG_CATEGORY_STATIC (gst_sei_id_inserter_debug);
#define GST_CAT_DEFAULT gst_sei_id_inserter_debug

#define SUPPORTED_CAPS "video/x-h264, " \
    "stream-format = (string) byte-stream, " \
    "alignment = (string) nal"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SUPPORTED_CAPS));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SUPPORTED_CAPS));

#define gst_sei_id_inserter_parent_class parent_class
G_DEFINE_TYPE (GstSeiIdInserter, gst_sei_id_inserter, GST_TYPE_ELEMENT);

/* Forward declarations */
static GstFlowReturn gst_sei_id_inserter_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer);
static gboolean gst_sei_id_inserter_sink_event (GstPad * pad, GstObject * parent, GstEvent * event);
static GstBuffer *gst_sei_id_inserter_create_sei_nalu (GstSeiIdInserter * self);
static GstBuffer *gst_sei_id_inserter_combine_buffers (GstBuffer * sei_buf, GstBuffer * data_buf);
static guint8 gst_sei_id_inserter_get_nalu_type (const guint8 * data, gsize size);
static gboolean gst_sei_id_inserter_is_frame_start (guint8 nalu_type);

static void
gst_sei_id_inserter_class_init (GstSeiIdInserterClass * klass)
{
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  gst_element_class_set_details_simple (gstelement_class,
      "H.264 SEI ID Inserter",
      "Filter/Video",
      "Inserts SEI NALUs with frame counter into H.264 stream",
      "SEI ID Inserter Contributors");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));

  GST_DEBUG_CATEGORY_INIT (gst_sei_id_inserter_debug, "seiidinserter", 0,
      "H.264 SEI ID Inserter");
}

static void
gst_sei_id_inserter_init (GstSeiIdInserter * self)
{
  GstPadTemplate *templ;

  templ = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (self), "sink");
  self->sink_pad = gst_pad_new_from_template (templ, "sink");
  gst_pad_set_chain_function (self->sink_pad, GST_DEBUG_FUNCPTR (gst_sei_id_inserter_chain));
  gst_pad_set_event_function (self->sink_pad, GST_DEBUG_FUNCPTR (gst_sei_id_inserter_sink_event));
  gst_element_add_pad (GST_ELEMENT (self), self->sink_pad);

  templ = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (self), "src");
  self->src_pad = gst_pad_new_from_template (templ, "src");
  gst_element_add_pad (GST_ELEMENT (self), self->src_pad);

  self->frame_counter = 0;
  const guint8 default_uuid[16] = {
    0x12, 0x34, 0x56, 0x78, 0x12, 0x34, 0x12, 0x34,
    0x12, 0x34, 0x56, 0x78, 0x90, 0x12, 0x34, 0x56
  };
  memcpy (self->uuid, default_uuid, 16);
}

/**
 * gst_sei_id_inserter_get_nalu_type:
 * Extracts NALU type from H.264 byte-stream format
 */
static guint8
gst_sei_id_inserter_get_nalu_type (const guint8 * data, gsize size)
{
  if (size < 5)
    return 0;

  guint start_code_pos = 0;

  /* Look for start code: 00 00 00 01 or 00 00 01 */
  if (size >= 4 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x00 && data[3] == 0x01) {
    start_code_pos = 4;
  } else if (size >= 3 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01) {
    start_code_pos = 3;
  } else {
    return 0;
  }

  /* Extract NALU type from header byte */
  guint8 nalu_header = data[start_code_pos];
  guint8 nalu_type = nalu_header & 0x1F;

  return nalu_type;
}

/**
 * gst_sei_id_inserter_is_frame_start:
 * Checks if NALU type starts a new frame
 */
static gboolean
gst_sei_id_inserter_is_frame_start (guint8 nalu_type)
{
  /* Frame starts with a slice (type 1, 2, 3, 4, 5) */
  return (nalu_type >= GST_H264_NAL_SLICE && nalu_type <= GST_H264_NAL_IDR_SLICE);
}

/**
 * gst_sei_id_inserter_create_sei_nalu:
 * Creates SEI NALU with user_data_unregistered payload
 */
static GstBuffer *
gst_sei_id_inserter_create_sei_nalu (GstSeiIdInserter * self)
{
  guint8 sei_data[64];
  gint offset = 0;

  /* Add NAL start code (00 00 00 01) */
  sei_data[offset++] = 0x00;
  sei_data[offset++] = 0x00;
  sei_data[offset++] = 0x00;
  sei_data[offset++] = 0x01;

  /* Add NAL header byte (type 6 for SEI) */
  sei_data[offset++] = 0x06;

  /* Add SEI payload type 5 (user_data_unregistered) */
  sei_data[offset++] = 0x05;

  /* Add payload size (24 bytes: 16 UUID + 8 frame counter) */
  sei_data[offset++] = 0x18;

  /* Add UUID (16 bytes) */
  memcpy (sei_data + offset, self->uuid, 16);
  offset += 16;

  /* Add frame counter in big-endian format (8 bytes) */
  sei_data[offset++] = (self->frame_counter >> 56) & 0xFF;
  sei_data[offset++] = (self->frame_counter >> 48) & 0xFF;
  sei_data[offset++] = (self->frame_counter >> 40) & 0xFF;
  sei_data[offset++] = (self->frame_counter >> 32) & 0xFF;
  sei_data[offset++] = (self->frame_counter >> 24) & 0xFF;
  sei_data[offset++] = (self->frame_counter >> 16) & 0xFF;
  sei_data[offset++] = (self->frame_counter >> 8) & 0xFF;
  sei_data[offset++] = self->frame_counter & 0xFF;

  /* Add RBSP trailing bits (stop bit) */
  sei_data[offset++] = 0x80;

  /* Create GstBuffer */
  GstBuffer *sei_buf = gst_buffer_new_allocate (NULL, offset, NULL);
  GstMapInfo map;
  if (!gst_buffer_map (sei_buf, &map, GST_MAP_WRITE)) {
    gst_buffer_unref (sei_buf);
    return NULL;
  }

  memcpy (map.data, sei_data, offset);
  gst_buffer_unmap (sei_buf, &map);

  return sei_buf;
}

/**
 * gst_sei_id_inserter_combine_buffers:
 * Concatenates SEI and data buffers
 */
static GstBuffer *
gst_sei_id_inserter_combine_buffers (GstBuffer * sei_buf, GstBuffer * data_buf)
{
  gsize sei_size = gst_buffer_get_size (sei_buf);
  gsize data_size = gst_buffer_get_size (data_buf);
  GstBuffer *combined;

  combined = gst_buffer_new_allocate (NULL, sei_size + data_size, NULL);

  GstMapInfo sei_map, data_map, combined_map;

  if (!gst_buffer_map (sei_buf, &sei_map, GST_MAP_READ)) {
    gst_buffer_unref (combined);
    return NULL;
  }

  if (!gst_buffer_map (data_buf, &data_map, GST_MAP_READ)) {
    gst_buffer_unmap (sei_buf, &sei_map);
    gst_buffer_unref (combined);
    return NULL;
  }

  if (!gst_buffer_map (combined, &combined_map, GST_MAP_WRITE)) {
    gst_buffer_unmap (data_buf, &data_map);
    gst_buffer_unmap (sei_buf, &sei_map);
    gst_buffer_unref (combined);
    return NULL;
  }

  memcpy (combined_map.data, sei_map.data, sei_size);
  memcpy (combined_map.data + sei_size, data_map.data, data_size);

  gst_buffer_unmap (combined, &combined_map);
  gst_buffer_unmap (data_buf, &data_map);
  gst_buffer_unmap (sei_buf, &sei_map);

  gst_buffer_copy_into (combined, data_buf, GST_BUFFER_COPY_METADATA, 0, -1);

  return combined;
}

/**
 * gst_sei_id_inserter_chain:
 * Main processing function for incoming H.264 buffers
 */
static GstFlowReturn
gst_sei_id_inserter_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstSeiIdInserter *self = GST_SEI_ID_INSERTER (parent);
  GstFlowReturn flow_ret = GST_FLOW_OK;
  GstBuffer *output_buffer = NULL;
  gboolean should_insert_sei = FALSE;

  g_return_val_if_fail (GST_IS_SEI_ID_INSERTER (self), GST_FLOW_ERROR);

  GstMapInfo map;
  if (!gst_buffer_map (buffer, &map, GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "Failed to map input buffer");
    gst_buffer_unref (buffer);
    return GST_FLOW_ERROR;
  }

  if (map.size > 0) {
    guint8 nalu_type = gst_sei_id_inserter_get_nalu_type (map.data, map.size);
    GST_DEBUG_OBJECT (self, "NALU type: %u", nalu_type);

    if (gst_sei_id_inserter_is_frame_start (nalu_type)) {
      should_insert_sei = TRUE;
      GST_DEBUG_OBJECT (self, "Frame-starting NALU detected (type %u)", nalu_type);
    }
  }

  gst_buffer_unmap (buffer, &map);

  if (should_insert_sei) {
    GstBuffer *sei_buf = gst_sei_id_inserter_create_sei_nalu (self);
    if (sei_buf) {
      output_buffer = gst_sei_id_inserter_combine_buffers (sei_buf, buffer);
      gst_buffer_unref (sei_buf);
      gst_buffer_unref (buffer);

      if (output_buffer) {
        self->frame_counter++;
      } else {
        GST_ERROR_OBJECT (self, "Failed to combine buffers");
        return GST_FLOW_ERROR;
      }
    } else {
      GST_ERROR_OBJECT (self, "Failed to create SEI NALU");
      gst_buffer_unref (buffer);
      return GST_FLOW_ERROR;
    }
  } else {
    output_buffer = buffer;
  }

  flow_ret = gst_pad_push (self->src_pad, output_buffer);
  return flow_ret;
}

static gboolean
gst_sei_id_inserter_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstSeiIdInserter *self = GST_SEI_ID_INSERTER (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;
      gst_event_parse_caps (event, &caps);

      GstStructure *s = gst_caps_get_structure (caps, 0);
      if (!gst_structure_has_name (s, "video/x-h264")) {
        GST_ERROR_OBJECT (self, "Unsupported media type");
        gst_event_unref (event);
        return FALSE;
      }

      const gchar *stream_format = gst_structure_get_string (s, "stream-format");
      const gchar *alignment = gst_structure_get_string (s, "alignment");

      if (g_strcmp0 (stream_format, "byte-stream") != 0 ||
          g_strcmp0 (alignment, "nal") != 0) {
        GST_ERROR_OBJECT (self, "Invalid H.264 format");
        gst_event_unref (event);
        return FALSE;
      }

      gst_pad_set_caps (self->src_pad, caps);
      GST_DEBUG_OBJECT (self, "Caps set successfully");
      break;
    }
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

GST_ELEMENT_REGISTER_DEFINE (seiidinserter, "seiidinserter", GST_RANK_NONE,
    GST_TYPE_SEI_ID_INSERTER);

extern "C" {

static gboolean
seiidinserter_init (GstPlugin * plugin)
{
  return GST_ELEMENT_REGISTER (seiidinserter, plugin);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    seiidinserter,
    "H.264 SEI ID Inserter",
    seiidinserter_init,
    "1.0", "LGPL", "GStreamer", "https://gstreamer.net")

}
