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
 * SECTION:gessourceclip
 * @title: GESSourceClip
 * @short_description: Base Class for sources of a GESLayer
 */

#include "ges-internal.h"
#include "ges-clip.h"
#include "ges-source-clip.h"
#include "ges-source.h"
#include "ges-video-source.h"
#include "ges-audio-source.h"
#include "ges-uri-asset.h"
#include "ges-effect.h"

typedef enum
{
  GES_SOURCE_CLIP_IS_SPEEDING = (1 << 0),
} GESSourceClipFlags;

#define FLAGS(obj)             (GES_SOURCE_CLIP(obj)->priv->flags)
#define SET_FLAG(obj,flag)     (FLAGS(obj) |= (flag))
#define UNSET_FLAG(obj,flag)   (FLAGS(obj) &= ~(flag))
#define FLAG_IS_SET(obj,flag)  (FLAGS(obj) == (flag))

struct _GESSourceClipPrivate
{
  /*< public > */
  gdouble rate;

  /*< private > */
  GstClockTime input_duration;
  GESSourceClipFlags flags;
};

enum
{
  PROP_0,
  PROP_RATE,
  PROP_INPUT_DURATION,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

G_DEFINE_TYPE (GESSourceClip, ges_source_clip, GES_TYPE_CLIP);

static gboolean
_set_inpoint (GESTimelineElement * element, GstClockTime inpoint)
{
  GESAsset *asset;
  GESSourceClip *source_clip = GES_SOURCE_CLIP (element);
  GParamSpec *pspec = NULL;

  asset = ges_extractable_get_asset (GES_EXTRACTABLE (source_clip));
  if (asset) {
    pspec =
        g_object_class_find_property (G_OBJECT_GET_CLASS (asset), "duration");
  }
  if (pspec != NULL) {
    GstClockTime asset_duration, new_max_duration;
    g_object_get (asset, "duration", &asset_duration, NULL);

    new_max_duration =
        (asset_duration - inpoint) / source_clip->priv->rate + inpoint;

    ges_timeline_element_set_max_duration (GES_TIMELINE_ELEMENT (source_clip),
        new_max_duration);
  }

  return
      GES_TIMELINE_ELEMENT_CLASS (ges_source_clip_parent_class)->set_inpoint
      (element, inpoint);
}

static gboolean
_set_duration (GESTimelineElement * element, GstClockTime duration)
{
  GESSourceClip *source_clip = GES_SOURCE_CLIP (element);

  if (!FLAG_IS_SET (source_clip, GES_SOURCE_CLIP_IS_SPEEDING))
    source_clip->priv->input_duration = duration * source_clip->priv->rate;

  return
      GES_TIMELINE_ELEMENT_CLASS (ges_source_clip_parent_class)->set_duration
      (element, duration);

}

static void
_get_media_duration_factor (GESTimelineElement * element,
    gdouble * media_duration_factor)
{
  GESSourceClip *source_clip = GES_SOURCE_CLIP (element);
  GList *tmp;

  GES_TIMELINE_ELEMENT_CLASS
      (ges_source_clip_parent_class)->get_media_duration_factor (element,
      media_duration_factor);

  for (tmp = GES_CONTAINER_CHILDREN (source_clip); tmp; tmp = tmp->next) {
    GESTrackElement *tmpo = GES_TRACK_ELEMENT (tmp->data);
    if (GES_IS_SOURCE (tmpo)) {
      *media_duration_factor /= source_clip->priv->rate;
    }
  }
  *media_duration_factor *= source_clip->priv->rate;
}

static void
ges_source_clip_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESSourceClip *source_clip = GES_SOURCE_CLIP (object);
  switch (property_id) {
    case PROP_RATE:
      g_value_set_double (value, source_clip->priv->rate);
      break;
    case PROP_INPUT_DURATION:
      g_value_set_uint64 (value, source_clip->priv->input_duration);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_source_clip_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESSourceClip *source_clip = GES_SOURCE_CLIP (object);
  switch (property_id) {
    case PROP_RATE:
      ges_source_clip_set_rate (source_clip, g_value_get_double (value));
      break;
    case PROP_INPUT_DURATION:
      ges_source_clip_set_input_duration (source_clip,
          g_value_get_uint64 (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_source_clip_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_source_clip_parent_class)->finalize (object);
}

static void
ges_source_clip_class_init (GESSourceClipClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTimelineElementClass *element_class = GES_TIMELINE_ELEMENT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESSourceClipPrivate));

  object_class->get_property = ges_source_clip_get_property;
  object_class->set_property = ges_source_clip_set_property;
  object_class->finalize = ges_source_clip_finalize;

  /**
   * GESSourceClip:rate:
   * 
   * The rate at which the #GESSourceClip will be played. Setting the rate changes
   * the duration and max-duration of the clip.
   * 
   * Precisely,
   * new-duration = duration / rate
   * new-max-duration = (asset-duration - inpoint) / rate + inpoint
   */
  properties[PROP_RATE] =
      g_param_spec_double ("rate", "Rate", "Rate at which the clip is played.",
      0, G_MAXDOUBLE, DEFAULT_CLIP_RATE, G_PARAM_READWRITE);

  /**
   * GESSourceClip:input-duration:
   * 
   * The duration of the asset that is used.
   * 
   * Ex: An asset with duration = 20, with inpoint = 10 and rate = 2.0 would play for
   * 5 seconds. Since we are consuming 10 seconds of the asset, the input duration = 10 
   */
  properties[PROP_INPUT_DURATION] =
      g_param_spec_uint64 ("input-duration", "Input duration",
      "Consumed asset duration", 0, G_MAXUINT64, GST_CLOCK_TIME_NONE,
      G_PARAM_READWRITE);

  g_object_class_install_properties (object_class, PROP_LAST, properties);

  element_class->set_inpoint = _set_inpoint;
  element_class->set_duration = _set_duration;
  element_class->get_media_duration_factor = _get_media_duration_factor;
}

static void
ges_source_clip_init (GESSourceClip * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_SOURCE_CLIP, GESSourceClipPrivate);
  self->priv->rate = DEFAULT_CLIP_RATE;
  self->priv->input_duration = GST_CLOCK_TIME_NONE;
}

void
ges_source_clip_set_input_duration (GESSourceClip * self, GstClockTime duration)
{
  self->priv->input_duration = duration;
}

GstClockTime
ges_source_clip_get_input_duration (GESSourceClip * self)
{
  return self->priv->input_duration;
}

/**
 * ges_source_clip_set_rate:
 * @source_clip: the #GESSourceClip to set rate on
 * @rate: the rate @source_clip is being set to
 *
 * Sets the rate of the source clip.
 * 
 * Changes duration and max-duration of the #GESSourceClip accordingly,
 * new-duration = duration / rate
 * new-max-duration = (asset-duration - inpoint) / rate + inpoint
 */
void
ges_source_clip_set_rate (GESSourceClip * source_clip, gdouble rate)
{
  GList *tmp;
  GstClockTime new_duration, new_max_duration;
  GESAsset *asset;
  GParamSpec *pspec = NULL;

  if (source_clip->priv->rate != rate) {

    if (!GES_CONTAINER_CHILDREN (source_clip)) {
      source_clip->priv->rate = rate;
      return;
    }

    for (tmp = GES_CONTAINER_CHILDREN (source_clip); tmp; tmp = tmp->next) {
      GESTrackElement *tmpo = GES_TRACK_ELEMENT (tmp->data);
      if (GES_IS_SOURCE (tmpo)) {
        ges_source_set_rate (GES_SOURCE (tmpo), rate);
      }
    }

    asset = ges_extractable_get_asset (GES_EXTRACTABLE (source_clip));
    if (asset) {
      pspec =
          g_object_class_find_property (G_OBJECT_GET_CLASS (asset), "duration");
    }
    if (pspec != NULL) {
      GstClockTime asset_duration;
      g_object_get (asset, "duration", &asset_duration, NULL);

      new_max_duration =
          (asset_duration - _INPOINT (source_clip)) / rate +
          _INPOINT (source_clip);

      ges_timeline_element_set_max_duration (GES_TIMELINE_ELEMENT (source_clip),
          new_max_duration);
    }

    new_duration = source_clip->priv->input_duration / rate;
    SET_FLAG (source_clip, GES_SOURCE_CLIP_IS_SPEEDING);
    _set_duration0 (GES_TIMELINE_ELEMENT (source_clip), new_duration);
    UNSET_FLAG (source_clip, GES_SOURCE_CLIP_IS_SPEEDING);

    source_clip->priv->rate = rate;
    /* Notify rate change */
    g_object_notify_by_pspec (G_OBJECT (source_clip), properties[PROP_RATE]);
  }
}

/**
 * ges_source_clip_get_rate:
 * @source_clip: the #GESSourceClip
 *
 * Get the current rate of the @source_clip.
 *
 * Returns: The rate of the @source_clip.
 */
gdouble
ges_source_clip_get_rate (GESSourceClip * source_clip)
{
  return source_clip->priv->rate;
}
