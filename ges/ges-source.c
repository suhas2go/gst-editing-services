/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <edward.hervey@collabora.co.uk>
 *               2009 Nokia Corporation
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:gessource
 * @title: GESSource
 * @short_description: Base Class for single-media sources
 */

#include "ges-internal.h"
#include "ges/ges-meta-container.h"
#include "ges-track-element.h"
#include "ges-source.h"
#include "ges-layer.h"

G_DEFINE_TYPE (GESSource, ges_source, GES_TYPE_TRACK_ELEMENT);

struct _GESSourcePrivate
{
  gdouble rate;
};

static void
ges_source_set_child_property (GESTimelineElement * self G_GNUC_UNUSED,
    GObject * child, GParamSpec * pspec, GValue * value)
{
  g_object_set_property (child, pspec->name, value);

  if (!g_strcmp0 ("rate", pspec->name)) {
    GstElement *nleobject;
    nleobject = ges_track_element_get_nleobject (GES_TRACK_ELEMENT (self));
    g_object_set_property (G_OBJECT (nleobject), "media-duration-factor",
        value);
  }
}

/******************************
 *   Internal helper methods  *
 ******************************/
static void
_pad_added_cb (GstElement * element, GstPad * srcpad, GstPad * sinkpad)
{
  GstPadLinkReturn res;
  gst_element_no_more_pads (element);
  res = gst_pad_link (srcpad, sinkpad);
#ifndef GST_DISABLE_GST_DEBUG
  if (res != GST_PAD_LINK_OK) {
    GstCaps *srccaps = NULL;
    GstCaps *sinkcaps = NULL;

    srccaps = gst_pad_query_caps (srcpad, NULL);
    sinkcaps = gst_pad_query_caps (sinkpad, NULL);

    GST_ERROR_OBJECT (element, "Could not link source with "
        "conversion bin: %s (srcpad caps %" GST_PTR_FORMAT
        " sinkpad caps: %" GST_PTR_FORMAT ")",
        gst_pad_link_get_name (res), srccaps, sinkcaps);
    gst_caps_unref (srccaps);
    gst_caps_unref (sinkcaps);
  }
#endif
}

static void
_ghost_pad_added_cb (GstElement * element, GstPad * srcpad, GstElement * bin)
{
  GstPad *ghost;

  ghost = gst_ghost_pad_new ("src", srcpad);
  gst_pad_set_active (ghost, TRUE);
  gst_element_add_pad (bin, ghost);
  gst_element_no_more_pads (element);
}

GstElement *
ges_source_create_topbin (const gchar * bin_name, GstElement * sub_element, ...)
{
  va_list argp;

  GstElement *element;
  GstElement *prev = NULL;
  GstElement *first = NULL;
  GstElement *bin;
  GstPad *sub_srcpad;

  va_start (argp, sub_element);
  bin = gst_bin_new (bin_name);
  gst_bin_add (GST_BIN (bin), sub_element);

  while ((element = va_arg (argp, GstElement *)) != NULL) {
    gst_bin_add (GST_BIN (bin), element);
    if (prev)
      gst_element_link (prev, element);
    prev = element;
    if (first == NULL)
      first = element;
  }

  va_end (argp);

  sub_srcpad = gst_element_get_static_pad (sub_element, "src");

  if (prev != NULL) {
    GstPad *srcpad, *sinkpad, *ghost;

    srcpad = gst_element_get_static_pad (prev, "src");
    ghost = gst_ghost_pad_new ("src", srcpad);
    gst_pad_set_active (ghost, TRUE);
    gst_element_add_pad (bin, ghost);

    sinkpad = gst_element_get_static_pad (first, "sink");
    if (sub_srcpad)
      gst_pad_link (sub_srcpad, sinkpad);
    else
      g_signal_connect (sub_element, "pad-added", G_CALLBACK (_pad_added_cb),
          sinkpad);

    gst_object_unref (srcpad);
    gst_object_unref (sinkpad);

  } else if (sub_srcpad) {
    GstPad *ghost;

    ghost = gst_ghost_pad_new ("src", sub_srcpad);
    gst_pad_set_active (ghost, TRUE);
    gst_element_add_pad (bin, ghost);
  } else {
    g_signal_connect (sub_element, "pad-added",
        G_CALLBACK (_ghost_pad_added_cb), bin);
  }

  if (sub_srcpad)
    gst_object_unref (sub_srcpad);

  return bin;
}

gboolean
ges_source_set_rate (GESSource * self, gdouble rate)
{
  gboolean res;

  if (!GES_SOURCE_GET_CLASS (self)->set_rate) {
    GST_ERROR_OBJECT (self, "No set rate vmethod implemented");
    return FALSE;
  }

  res = GES_SOURCE_GET_CLASS (self)->set_rate (self, rate);
  if (res)
    self->priv->rate = rate;
  return res;
}

gdouble
ges_source_get_rate (GESSource * self)
{
  return self->priv->rate;
}

static void
ges_source_class_init (GESSourceClass * klass)
{
  GESTimelineElementClass *timeline_class = GES_TIMELINE_ELEMENT_CLASS (klass);
  GESTrackElementClass *track_class = GES_TRACK_ELEMENT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESSourcePrivate));

  timeline_class->set_child_property = ges_source_set_child_property;
  track_class->nleobject_factorytype = "nlesource";
  track_class->create_element = NULL;

  klass->set_rate = ges_source_set_rate;
}

static void
ges_source_init (GESSource * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_SOURCE, GESSourcePrivate);
  self->priv->rate = DEFAULT_CLIP_RATE;
}
