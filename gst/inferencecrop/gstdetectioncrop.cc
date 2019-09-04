/*
 * GStreamer
 * Copyright (C) 2019 RidgeRun
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
 *
 */

/**
 * SECTION:element-gstdetectioncrop
 *
 * The detectioncrop element parses a detectionmeta and crops the incomming
 * image to the bounding box.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v videotestsrc ! detectioncrop ! xvimagesink
 * ]|
 * Process video frames from the camera using a detectioncrop model.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdetectioncrop.h"

#include "gst/r2inference/gstinferencemeta.h"
#include "videocrop.h"

/* generic templates */
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_detection_crop_debug_category);
#define GST_CAT_DEFAULT gst_detection_crop_debug_category

static void gst_detection_crop_finalize (GObject * object);
static GstStateChangeReturn gst_detection_crop_change_state (GstElement *
    element, GstStateChange transition);
static gboolean gst_detection_crop_start (GstDetectionCrop * self);
static void gst_detection_crop_set_caps (GstPad * pad, GParamSpec * unused,
    GstDetectionCrop * self);

enum
{
  PROP_0
};

struct _GstDetectionCrop
{
  GstBin parent;
  CropElement *element;
};

struct _GstDetectionCropClass
{
  GstBinClass parent;
};

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstDetectionCrop, gst_detection_crop,
    GST_TYPE_BIN,
    GST_DEBUG_CATEGORY_INIT (gst_detection_crop_debug_category, "detectioncrop",
        0, "debug category for detectioncrop element"));

static void
gst_detection_crop_class_init (GstDetectionCropClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "detectioncrop", "Filter",
      "Crops an incoming image based on an inference prediction bounding box",
      "   Michael Gruner <michael.gruner@ridgerun.com>");

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_detection_crop_change_state);

  object_class->finalize = gst_detection_crop_finalize;
}

static void
gst_detection_crop_init (GstDetectionCrop * self)
{
  GstElement *element;
  GstPad *sinkpad, *sinkgpad, *srcpad, *srcgpad;

  self->element = new VideoCrop ();

  if (FALSE == self->element->Validate ()) {
    const std::string factory = self->element->GetFactory ();
    GST_ERROR_OBJECT (self, "Unable to find element %s", factory.c_str ());
    return;
  }

  element = self->element->GetElement ();
  gst_bin_add (GST_BIN (self), GST_ELEMENT (gst_object_ref (element)));

  sinkpad = self->element->GetSinkPad ();
  g_return_if_fail (sinkpad);

  sinkgpad = gst_ghost_pad_new ("sink", sinkpad);
  gst_pad_set_active (sinkgpad, TRUE);
  gst_element_add_pad (GST_ELEMENT (self), sinkgpad);

  g_signal_connect (sinkgpad, "notify::caps",
      G_CALLBACK (gst_detection_crop_set_caps), self);

  srcpad = self->element->GetSrcPad ();
  g_return_if_fail (srcpad);

  srcgpad = gst_ghost_pad_new ("src", srcpad);
  gst_pad_set_active (srcgpad, TRUE);
  gst_element_add_pad (GST_ELEMENT (self), srcgpad);

  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);
}

static void
gst_detection_crop_finalize (GObject * object)
{
  GstDetectionCrop *self = GST_DETECTION_CROP (object);

  delete (self->element);

  G_OBJECT_CLASS (gst_detection_crop_parent_class)->finalize (object);
}

static gboolean
gst_detection_crop_start (GstDetectionCrop * self)
{
  g_return_val_if_fail (self, FALSE);

  return self->element->Validate ();
}

static GstStateChangeReturn
gst_detection_crop_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstDetectionCrop *self = GST_DETECTION_CROP (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (FALSE == gst_detection_crop_start (self)) {
        GST_ERROR_OBJECT (self, "Failed to start");
        ret = GST_STATE_CHANGE_FAILURE;
        goto out;
      }
    default:
      break;
  }
  ret =
      GST_ELEMENT_CLASS (gst_detection_crop_parent_class)->change_state
      (element, transition);
  if (GST_STATE_CHANGE_FAILURE == ret) {
    GST_ERROR_OBJECT (self, "Parent failed to change state");
    goto out;
  }

out:
  return ret;
}

static void
gst_detection_crop_set_caps (GstPad * pad, GParamSpec * unused,
    GstDetectionCrop * self)
{
  GstCaps *caps;
  GstStructure *st;
  gint width;
  gint height;

  g_object_get (pad, "caps", &caps, NULL);
  if (NULL == caps) {
    return;
  }

  st = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (st, "width", &width);
  gst_structure_get_int (st, "height", &height);

  GST_INFO_OBJECT (self, "Set new caps to %" GST_PTR_FORMAT, caps);

  self->element->SetImageSize (width, height);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret = TRUE;

  ret =
      gst_element_register (plugin, "detectioncrop", GST_RANK_NONE,
      GST_TYPE_DETECTION_CROP);

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    inferencecrop,
    "Crops an incoming image based on an inference prediction bounding box",
    plugin_init, VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)