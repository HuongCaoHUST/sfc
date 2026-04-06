/*
 * GStreamer
 * Copyright (C) 2026 SEI ID Inserter Contributors
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

#ifndef __GST_SEI_ID_INSERTER_H__
#define __GST_SEI_ID_INSERTER_H__

#include <gst/gst.h>
#include <cstdint>

G_BEGIN_DECLS

#define GST_TYPE_SEI_ID_INSERTER (gst_sei_id_inserter_get_type())
#define GST_SEI_ID_INSERTER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_SEI_ID_INSERTER, GstSeiIdInserter))
#define GST_SEI_ID_INSERTER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_SEI_ID_INSERTER, GstSeiIdInserterClass))
#define GST_IS_SEI_ID_INSERTER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_SEI_ID_INSERTER))
#define GST_IS_SEI_ID_INSERTER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_SEI_ID_INSERTER))

/* H.264 NAL unit types */
typedef enum {
  GST_H264_NAL_SLICE = 1,
  GST_H264_NAL_DPA = 2,
  GST_H264_NAL_DPB = 3,
  GST_H264_NAL_DPC = 4,
  GST_H264_NAL_IDR_SLICE = 5,
  GST_H264_NAL_SEI = 6,
  GST_H264_NAL_SPS = 7,
  GST_H264_NAL_PPS = 8,
  GST_H264_NAL_AUD = 9,
} GstH264NalUnitType;

typedef struct _GstSeiIdInserter GstSeiIdInserter;
typedef struct _GstSeiIdInserterClass GstSeiIdInserterClass;

struct _GstSeiIdInserter {
  GstElement element;

  GstPad *sink_pad;
  GstPad *src_pad;

  /* SEI UUID - 16 bytes, example: 12345678-1234-1234-1234-123456789012 */
  guint8 uuid[16];

  /* Frame counter - incremented per frame */
  guint64 frame_counter;
};

struct _GstSeiIdInserterClass {
  GstElementClass parent_class;
};

GType gst_sei_id_inserter_get_type (void);

G_END_DECLS

#endif /* __GST_SEI_ID_INSERTER_H__ */

