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
 * SECTION:gesaudiosource
 * @title: GESAudioSource
 * @short_description: Base Class for audio sources
 *
 * ## Children Properties
 *
 * You can use the following children properties through the
 * #ges_track_element_set_child_property and alike set of methods:
 *
 * <informaltable frame="none">
 * <tgroup cols="3">
 * <colspec colname="properties_type" colwidth="150px"/>
 * <colspec colname="properties_name" colwidth="200px"/>
 * <colspec colname="properties_flags" colwidth="400px"/>
 * <tbody>
 * <row>
 *  <entry role="property_type"><link linkend="gdouble"><type>double</type></link></entry>
 *  <entry role="property_name"><link linkend="GESAudioSource--volume">volume</link></entry>
 *  <entry>volume factor, 1.0=100%.</entry>
 * </row>
 * <row>
 *  <entry role="property_type"><link linkend="gboolean"><type>gboolean</type></link></entry>
 *  <entry role="property_name"><link linkend="GESAudioSource--mute">mute</link></entry>
 *  <entry>mute channel.</entry>
 * </row>
 * <row>
 *  <entry role="property_type"><link linkend="gdouble"><type>gdouble</type></link></entry>
 *  <entry role="property_name"><link linkend="GESAudioSource--rate">rate</link></entry>
 *  <entry>Audio stream rate. Set to 1.0 by default. </entry>
 * </row>
 * </tbody>
 * </tgroup>
 * </informaltable>
 */

#include "ges-internal.h"
#include "ges/ges-meta-container.h"
#include "ges-track-element.h"
#include "ges-audio-source.h"
#include "ges-layer.h"

G_DEFINE_ABSTRACT_TYPE (GESAudioSource, ges_audio_source, GES_TYPE_SOURCE);

struct _GESAudioSourcePrivate
{
  GstElement *capsfilter;
  GESTrack *current_track;
  GstElement *pitch;
};

static void
_sync_element_to_layer_property_float (GESTrackElement * trksrc,
    GstElement * element, const gchar * meta, const gchar * propname)
{
  GESTimelineElement *parent;
  GESLayer *layer;
  gfloat value;

  parent = ges_timeline_element_get_parent (GES_TIMELINE_ELEMENT (trksrc));
  if (!parent) {
    GST_DEBUG_OBJECT (trksrc, "Not in a clip... doing nothing");

    return;
  }

  layer = ges_clip_get_layer (GES_CLIP (parent));

  gst_object_unref (parent);

  if (layer != NULL) {

    ges_meta_container_get_float (GES_META_CONTAINER (layer), meta, &value);
    g_object_set (element, propname, value, NULL);
    GST_DEBUG_OBJECT (trksrc, "Setting %s to %f", propname, value);


    gst_object_unref (layer);
  } else {

    GST_DEBUG_OBJECT (trksrc, "NOT setting the %s", propname);
  }
}

static void
restriction_caps_cb (GESTrack * track,
    GParamSpec * arg G_GNUC_UNUSED, GESAudioSource * self)
{
  GstCaps *caps;

  g_object_get (track, "restriction-caps", &caps, NULL);

  GST_DEBUG_OBJECT (self, "Setting capsfilter caps to %" GST_PTR_FORMAT, caps);
  g_object_set (self->priv->capsfilter, "caps", caps, NULL);

  if (caps)
    gst_caps_unref (caps);
}

static void
_track_changed_cb (GESAudioSource * self, GParamSpec * arg G_GNUC_UNUSED,
    gpointer udata)
{
  GESTrack *track = ges_track_element_get_track (GES_TRACK_ELEMENT (self));

  if (self->priv->current_track) {
    g_signal_handlers_disconnect_by_func (self->priv->current_track,
        (GCallback) restriction_caps_cb, self);
  }

  self->priv->current_track = track;
  if (track) {
    restriction_caps_cb (track, NULL, self);

    g_signal_connect (track, "notify::restriction-caps",
        G_CALLBACK (restriction_caps_cb), self);
  }
}

static GstElement *
ges_audio_source_create_element (GESTrackElement * trksrc)
{
  GstElement *volume, *vpbin, *pitch;
  GstElement *topbin;
  GstElement *sub_element;
  GESAudioSourceClass *source_class = GES_AUDIO_SOURCE_GET_CLASS (trksrc);
  const gchar *volume_props[] = { "volume", "mute", NULL };
  const gchar *pitch_props[] = { "rate", NULL };
  GESAudioSource *self = GES_AUDIO_SOURCE (trksrc);

  if (!source_class->create_source)
    return NULL;

  sub_element = source_class->create_source (trksrc);

  GST_DEBUG_OBJECT (trksrc, "Creating a bin sub_element ! volume");
  vpbin =
      gst_parse_bin_from_description
      ("audioconvert ! audioresample ! volume name=v  ! pitch name=p ! audioconvert ! capsfilter name=audio-track-caps-filter",
      TRUE, NULL);
  topbin = ges_source_create_topbin ("audiosrcbin", sub_element, vpbin, NULL);
  volume = gst_bin_get_by_name (GST_BIN (vpbin), "v");
  pitch = gst_bin_get_by_name (GST_BIN (vpbin), "p");
  self->priv->capsfilter = gst_bin_get_by_name (GST_BIN (vpbin),
      "audio-track-caps-filter");
  self->priv->pitch = pitch;

  g_signal_connect (self, "notify::track", (GCallback) _track_changed_cb, NULL);
  _track_changed_cb (self, NULL, NULL);

  _sync_element_to_layer_property_float (trksrc, volume, GES_META_VOLUME,
      "volume");
  ges_track_element_add_children_props (trksrc, volume, NULL, NULL,
      volume_props);
  ges_track_element_add_children_props (trksrc, pitch, NULL, NULL, pitch_props);
  gst_object_unref (volume);

  return topbin;
}

static void
ges_audio_source_dispose (GObject * object)
{
  GESAudioSource *self = GES_AUDIO_SOURCE (object);

  if (self->priv->capsfilter) {
    gst_object_unref (self->priv->capsfilter);
    self->priv->capsfilter = NULL;
  }

  G_OBJECT_CLASS (ges_audio_source_parent_class)->dispose (object);
}

static gboolean
ges_audio_source_set_rate (GESSource * source, gdouble rate)
{
  GESAudioSource *self = GES_AUDIO_SOURCE (source);
  GstElement *nleobject;

  if (!self->priv->pitch) {
    GST_ERROR_OBJECT (self, "Can't set rate, pitch element not found");
    return FALSE;
  }

  g_object_set (self->priv->pitch, "rate", rate, NULL);
  nleobject = ges_track_element_get_nleobject (GES_TRACK_ELEMENT (self));
  g_object_set (nleobject, "media-duration-factor", rate, NULL);
  return TRUE;
}

static void
ges_audio_source_class_init (GESAudioSourceClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GESTrackElementClass *track_class = GES_TRACK_ELEMENT_CLASS (klass);
  GESSourceClass *source_class = GES_SOURCE_CLASS (klass);
  GESAudioSourceClass *audio_source_class = GES_AUDIO_SOURCE_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESAudioSourcePrivate));

  gobject_class->dispose = ges_audio_source_dispose;
  track_class->nleobject_factorytype = "nlesource";
  track_class->create_element = ges_audio_source_create_element;
  source_class->set_rate = ges_audio_source_set_rate;
  audio_source_class->create_source = NULL;
}

static void
ges_audio_source_init (GESAudioSource * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_AUDIO_SOURCE, GESAudioSourcePrivate);
}
