/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <bilboed@bilboed.com>
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

#include "test-utils.h"
#include <ges/ges.h>
#include <gst/check/gstcheck.h>
#include <plugins/nle/nleobject.h>

/* This test uri will eventually have to be fixed */
#define TEST_URI "http://nowhere/blahblahblah"


static gchar *av_uri;
static gchar *image_uri;
GMainLoop *mainloop;

typedef struct _AssetUri
{
  const gchar *uri;
  GESAsset *asset;
} AssetUri;

static void
asset_created_cb (GObject * source, GAsyncResult * res, gpointer udata)
{
  GList *tracks, *tmp;
  GESAsset *asset;
  GESLayer *layer;
  GESUriClip *tlfs;

  GError *error = NULL;

  asset = ges_asset_request_finish (res, &error);
  ASSERT_OBJECT_REFCOUNT (asset, "1 for us + for the cache + 1 taken "
      "by g_task", 3);
  fail_unless (error == NULL);
  fail_if (asset == NULL);
  fail_if (g_strcmp0 (ges_asset_get_id (asset), av_uri));

  layer = GES_LAYER (g_async_result_get_user_data (res));
  tlfs = GES_URI_CLIP (ges_layer_add_asset (layer,
          asset, 0, 0, GST_CLOCK_TIME_NONE, GES_TRACK_TYPE_UNKNOWN));
  fail_unless (GES_IS_URI_CLIP (tlfs));
  fail_if (g_strcmp0 (ges_uri_clip_get_uri (tlfs), av_uri));
  assert_equals_uint64 (_DURATION (tlfs), GST_SECOND);

  fail_unless (ges_clip_get_supported_formats
      (GES_CLIP (tlfs)) & GES_TRACK_TYPE_VIDEO);
  fail_unless (ges_clip_get_supported_formats
      (GES_CLIP (tlfs)) & GES_TRACK_TYPE_AUDIO);

  tracks = ges_timeline_get_tracks (ges_layer_get_timeline (layer));
  for (tmp = tracks; tmp; tmp = tmp->next) {
    GList *trackelements = ges_track_get_elements (GES_TRACK (tmp->data));

    assert_equals_int (g_list_length (trackelements), 1);
    fail_unless (GES_IS_VIDEO_URI_SOURCE (trackelements->data)
        || GES_IS_AUDIO_URI_SOURCE (trackelements->data));
    g_list_free_full (trackelements, gst_object_unref);
  }
  g_list_free_full (tracks, gst_object_unref);

  gst_object_unref (asset);
  g_main_loop_quit (mainloop);
}

static void
_timeline_element_check (GESTimelineElement * element, GstClockTime start,
    GstClockTime duration, GstClockTime inpoint, GstClockTime max_duration)
{
  GstClockTime track_max_duration;

  assert_equals_uint64 (_START (element), start);
  assert_equals_uint64 (_DURATION (element), duration);
  assert_equals_uint64 (_INPOINT (element), inpoint);
  g_object_get (element, "max-duration", &track_max_duration, NULL);
  assert_equals_uint64 (track_max_duration, max_duration);
}

GST_START_TEST (test_filesource_basic)
{
  GESTimeline *timeline;
  GESLayer *layer;

  mainloop = g_main_loop_new (NULL, FALSE);

  timeline = ges_timeline_new_audio_video ();
  fail_unless (timeline != NULL);

  layer = ges_layer_new ();
  fail_unless (layer != NULL);
  fail_unless (ges_timeline_add_layer (timeline, layer));

  ges_asset_request_async (GES_TYPE_URI_CLIP,
      av_uri, NULL, asset_created_cb, layer);

  g_main_loop_run (mainloop);
  g_main_loop_unref (mainloop);
  gst_object_unref (timeline);
}

GST_END_TEST;

static gboolean
create_asset (AssetUri * asset_uri)
{
  asset_uri->asset =
      GES_ASSET (ges_uri_clip_asset_request_sync (asset_uri->uri, NULL));
  g_main_loop_quit (mainloop);

  return FALSE;
}

GST_START_TEST (test_filesource_properties)
{
  GESClip *clip;
  GESTrack *track;
  AssetUri asset_uri;
  GESTimeline *timeline;
  GESUriClipAsset *asset;
  GESLayer *layer;
  GESTrackElement *trackelement;

  track = ges_track_new (GES_TRACK_TYPE_AUDIO, gst_caps_ref (GST_CAPS_ANY));
  fail_unless (track != NULL);

  layer = ges_layer_new ();
  fail_unless (layer != NULL);
  timeline = ges_timeline_new ();
  fail_unless (GES_IS_TIMELINE (timeline));
  fail_unless (ges_timeline_add_layer (timeline, layer));
  fail_unless (ges_timeline_add_track (timeline, track));
  ASSERT_OBJECT_REFCOUNT (timeline, "timeline", 1);

  mainloop = g_main_loop_new (NULL, FALSE);
  asset_uri.uri = av_uri;
  /* Right away request the asset synchronously */
  g_timeout_add (1, (GSourceFunc) create_asset, &asset_uri);
  g_main_loop_run (mainloop);

  asset = GES_URI_CLIP_ASSET (asset_uri.asset);
  fail_unless (GES_IS_ASSET (asset));
  clip = ges_layer_add_asset (layer, GES_ASSET (asset),
      42, 12, 51, GES_TRACK_TYPE_AUDIO);
  ges_timeline_commit (timeline);
  assert_is_type (clip, GES_TYPE_URI_CLIP);
  assert_equals_uint64 (_START (clip), 42);
  assert_equals_uint64 (_DURATION (clip), 51);
  assert_equals_uint64 (_INPOINT (clip), 12);

  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip)), 1);
  trackelement = GES_CONTAINER_CHILDREN (clip)->data;
  fail_unless (trackelement != NULL);
  fail_unless (GES_TIMELINE_ELEMENT_PARENT (trackelement) ==
      GES_TIMELINE_ELEMENT (clip));
  fail_unless (ges_track_element_get_track (trackelement) == track);

  /* Check that trackelement has the same properties */
  assert_equals_uint64 (_START (trackelement), 42);
  assert_equals_uint64 (_DURATION (trackelement), 51);
  assert_equals_uint64 (_INPOINT (trackelement), 12);

  /* And let's also check that it propagated correctly to GNonLin */
  nle_object_check (ges_track_element_get_nleobject (trackelement), 42, 51, 12,
      51, MIN_NLE_PRIO + TRANSITIONS_HEIGHT, TRUE);

  /* Change more properties, see if they propagate */
  g_object_set (clip, "start", (guint64) 420, "duration", (guint64) 510,
      "in-point", (guint64) 120, NULL);
  ges_timeline_commit (timeline);
  assert_equals_uint64 (_START (clip), 420);
  assert_equals_uint64 (_DURATION (clip), 510);
  assert_equals_uint64 (_INPOINT (clip), 120);
  assert_equals_uint64 (_START (trackelement), 420);
  assert_equals_uint64 (_DURATION (trackelement), 510);
  assert_equals_uint64 (_INPOINT (trackelement), 120);

  /* And let's also check that it propagated correctly to GNonLin */
  nle_object_check (ges_track_element_get_nleobject (trackelement), 420, 510,
      120, 510, MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 0, TRUE);

  /* Test mute support */
  g_object_set (clip, "mute", TRUE, NULL);
  ges_timeline_commit (timeline);
  nle_object_check (ges_track_element_get_nleobject (trackelement), 420, 510,
      120, 510, MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 0, FALSE);
  g_object_set (clip, "mute", FALSE, NULL);
  ges_timeline_commit (timeline);
  nle_object_check (ges_track_element_get_nleobject (trackelement), 420, 510,
      120, 510, MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 0, TRUE);

  ges_container_remove (GES_CONTAINER (clip),
      GES_TIMELINE_ELEMENT (trackelement));

  gst_object_unref (timeline);
}

GST_END_TEST;

GST_START_TEST (test_filesource_images)
{
  GESClip *clip;
  GESAsset *asset;
  GESTrack *a, *v;
  GESUriClip *uriclip;
  AssetUri asset_uri;
  GESTimeline *timeline;
  GESLayer *layer;
  GESTrackElement *track_element;

  a = GES_TRACK (ges_audio_track_new ());
  v = GES_TRACK (ges_video_track_new ());

  layer = ges_layer_new ();
  fail_unless (layer != NULL);
  timeline = ges_timeline_new ();
  fail_unless (timeline != NULL);
  fail_unless (ges_timeline_add_layer (timeline, layer));
  fail_unless (ges_timeline_add_track (timeline, a));
  fail_unless (ges_timeline_add_track (timeline, v));
  ASSERT_OBJECT_REFCOUNT (timeline, "timeline", 1);

  mainloop = g_main_loop_new (NULL, FALSE);
  /* Right away request the asset synchronously */
  asset_uri.uri = image_uri;
  g_timeout_add (1, (GSourceFunc) create_asset, &asset_uri);
  g_main_loop_run (mainloop);

  asset = asset_uri.asset;
  fail_unless (GES_IS_ASSET (asset));
  fail_unless (ges_uri_clip_asset_is_image (GES_URI_CLIP_ASSET (asset)));
  uriclip = GES_URI_CLIP (ges_asset_extract (asset, NULL));
  fail_unless (GES_IS_URI_CLIP (uriclip));
  fail_unless (ges_clip_get_supported_formats (GES_CLIP (uriclip)) ==
      GES_TRACK_TYPE_VIDEO);
  clip = GES_CLIP (uriclip);
  fail_unless (ges_uri_clip_is_image (uriclip));
  ges_timeline_element_set_duration (GES_TIMELINE_ELEMENT (clip),
      1 * GST_SECOND);

  /* the returned track element should be an image source */
  /* the clip should not create any TrackElement in the audio track */
  ges_layer_add_clip (layer, GES_CLIP (clip));
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip)), 1);
  track_element = GES_CONTAINER_CHILDREN (clip)->data;
  fail_unless (track_element != NULL);
  fail_unless (GES_TIMELINE_ELEMENT_PARENT (track_element) ==
      GES_TIMELINE_ELEMENT (clip));
  fail_unless (ges_track_element_get_track (track_element) == v);
  fail_unless (GES_IS_IMAGE_SOURCE (track_element));

  ASSERT_OBJECT_REFCOUNT (track_element, "1 in track, 1 in clip 2 in timeline",
      4);

  gst_object_unref (timeline);
}

GST_END_TEST;

GST_START_TEST (test_filesource_rate)
{
  GESClip *clip;
  GESTrack *track;
  AssetUri asset_uri;
  GESTimeline *timeline;
  GESUriClipAsset *asset;
  GESLayer *layer;
  GESTrackElement *trackelement;
  gdouble rate;

  track = ges_track_new (GES_TRACK_TYPE_AUDIO, gst_caps_ref (GST_CAPS_ANY));
  fail_unless (track != NULL);

  layer = ges_layer_new ();
  fail_unless (layer != NULL);
  timeline = ges_timeline_new ();
  fail_unless (GES_IS_TIMELINE (timeline));
  fail_unless (ges_timeline_add_layer (timeline, layer));
  fail_unless (ges_timeline_add_track (timeline, track));
  ASSERT_OBJECT_REFCOUNT (timeline, "timeline", 1);

  mainloop = g_main_loop_new (NULL, FALSE);
  asset_uri.uri = av_uri;
  /* Right away request the asset synchronously */
  g_timeout_add (1, (GSourceFunc) create_asset, &asset_uri);
  g_main_loop_run (mainloop);

  asset = GES_URI_CLIP_ASSET (asset_uri.asset);
  fail_unless (GES_IS_ASSET (asset));
  clip = ges_layer_add_asset (layer, GES_ASSET (asset),
      42, 12, 50, GES_TRACK_TYPE_AUDIO);
  ges_timeline_commit (timeline);
  assert_is_type (clip, GES_TYPE_URI_CLIP);
  _timeline_element_check (GES_TIMELINE_ELEMENT (clip), 42, 50, 12, GST_SECOND);

  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip)), 1);
  trackelement = GES_CONTAINER_CHILDREN (clip)->data;
  fail_unless (trackelement != NULL);
  fail_unless (GES_TIMELINE_ELEMENT_PARENT (trackelement) ==
      GES_TIMELINE_ELEMENT (clip));
  fail_unless (ges_track_element_get_track (trackelement) == track);

  /* Check that trackelement has the same properties */
  _timeline_element_check (GES_TIMELINE_ELEMENT (trackelement), 42, 50, 12,
      GST_SECOND);

  nle_object_check (ges_track_element_get_nleobject (trackelement), 42, 50, 12,
      50, MIN_NLE_PRIO + TRANSITIONS_HEIGHT, TRUE);

  /* Test rate property */
  g_object_get (clip, "rate", &rate, NULL);
  assert_equals_float (rate, 1.0);

  g_object_set (clip, "rate", (gdouble) 2.0, NULL);
  g_object_get (clip, "rate", &rate, NULL);
  assert_equals_float (rate, 2.0);
  ges_timeline_commit (timeline);

  _timeline_element_check (GES_TIMELINE_ELEMENT (clip), 42, 25, 12,
      0.5 * (GST_SECOND - 12) + 12);

  _timeline_element_check (GES_TIMELINE_ELEMENT (trackelement), 42, 25, 12,
      0.5 * (GST_SECOND - 12) + 12);

  nle_object_check (ges_track_element_get_nleobject (trackelement), 42, 25, 12,
      25, MIN_NLE_PRIO + TRANSITIONS_HEIGHT, TRUE);

  g_object_set (clip, "rate", (gdouble) 0.5, NULL);
  g_object_get (clip, "rate", &rate, NULL);
  assert_equals_float (rate, 0.5);
  ges_timeline_commit (timeline);

  _timeline_element_check (GES_TIMELINE_ELEMENT (clip), 42, 100, 12,
      2 * (GST_SECOND - 12) + 12);

  _timeline_element_check (GES_TIMELINE_ELEMENT (trackelement), 42, 100, 12,
      2 * (GST_SECOND - 12) + 12);
  nle_object_check (ges_track_element_get_nleobject (trackelement), 42, 100, 12,
      100, MIN_NLE_PRIO + TRANSITIONS_HEIGHT, TRUE);

  ges_container_remove (GES_CONTAINER (clip),
      GES_TIMELINE_ELEMENT (trackelement));

  gst_object_unref (timeline);
}

GST_END_TEST;

GST_START_TEST (test_split_filesource_rate)
{
  GESClip *clip, *split_clip;
  GESTrack *track;
  AssetUri asset_uri;
  GESTimeline *timeline;
  GESUriClipAsset *asset;
  GESLayer *layer;
  GESTrackElement *trackelement, *split_trackelement;
  gdouble rate;
  GstElement *nle;

  track = ges_track_new (GES_TRACK_TYPE_AUDIO, gst_caps_ref (GST_CAPS_ANY));
  fail_unless (track != NULL);

  layer = ges_layer_new ();
  fail_unless (layer != NULL);
  timeline = ges_timeline_new ();
  fail_unless (GES_IS_TIMELINE (timeline));
  fail_unless (ges_timeline_add_layer (timeline, layer));
  fail_unless (ges_timeline_add_track (timeline, track));
  ASSERT_OBJECT_REFCOUNT (timeline, "timeline", 1);

  mainloop = g_main_loop_new (NULL, FALSE);
  asset_uri.uri = av_uri;
  /* Right away request the asset synchronously */
  g_timeout_add (1, (GSourceFunc) create_asset, &asset_uri);
  g_main_loop_run (mainloop);

  asset = GES_URI_CLIP_ASSET (asset_uri.asset);
  fail_unless (GES_IS_ASSET (asset));
  clip = ges_layer_add_asset (layer, GES_ASSET (asset),
      42, 12, 50, GES_TRACK_TYPE_AUDIO);
  ges_timeline_commit (timeline);
  assert_is_type (clip, GES_TYPE_URI_CLIP);
  _timeline_element_check (GES_TIMELINE_ELEMENT (clip), 42, 50, 12, GST_SECOND);

  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip)), 1);
  trackelement = GES_CONTAINER_CHILDREN (clip)->data;
  fail_unless (trackelement != NULL);
  fail_unless (GES_TIMELINE_ELEMENT_PARENT (trackelement) ==
      GES_TIMELINE_ELEMENT (clip));
  fail_unless (ges_track_element_get_track (trackelement) == track);

  /* Check that trackelement has the same properties */
  _timeline_element_check (GES_TIMELINE_ELEMENT (trackelement), 42, 50, 12,
      GST_SECOND);
  nle = ges_track_element_get_nleobject (trackelement);
  nle_object_check (nle, 42, 50, 12,
      50, MIN_NLE_PRIO + TRANSITIONS_HEIGHT, TRUE);
  assert_equals_float (((NleObject *) nle)->media_duration_factor, 1.0);

  /* Test rate property */
  g_object_get (clip, "rate", &rate, NULL);
  assert_equals_float (rate, 1.0);

  g_object_set (clip, "rate", (gdouble) 2.0, NULL);
  g_object_get (clip, "rate", &rate, NULL);
  assert_equals_float (rate, 2.0);
  ges_timeline_commit (timeline);

  _timeline_element_check (GES_TIMELINE_ELEMENT (clip), 42, 25, 12,
      0.5 * (GST_SECOND - 12) + 12);

  _timeline_element_check (GES_TIMELINE_ELEMENT (trackelement), 42, 25, 12,
      0.5 * (GST_SECOND - 12) + 12);
  nle = ges_track_element_get_nleobject (trackelement);
  nle_object_check (nle, 42, 25, 12,
      25, MIN_NLE_PRIO + TRANSITIONS_HEIGHT, TRUE);
  assert_equals_float (((NleObject *) nle)->media_duration_factor, 2.0);

  split_clip = ges_clip_split (clip, 50);
  fail_unless (GES_IS_CLIP (split_clip));
  _timeline_element_check (GES_TIMELINE_ELEMENT (split_clip), 50, 17,
      28, 0.5 * (GST_SECOND - 28) + 28);

  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (split_clip)), 1);
  split_trackelement = GES_CONTAINER_CHILDREN (split_clip)->data;
  fail_unless (split_trackelement != NULL);
  fail_unless (GES_TIMELINE_ELEMENT_PARENT (split_trackelement) ==
      GES_TIMELINE_ELEMENT (split_clip));
  fail_unless (ges_track_element_get_track (split_trackelement) == track);

  _timeline_element_check (GES_TIMELINE_ELEMENT (split_trackelement), 50, 17,
      28, 0.5 * (GST_SECOND - 28) + 28);
  nle = ges_track_element_get_nleobject (split_trackelement);
  assert_equals_float (((NleObject *) nle)->media_duration_factor, 2.0);
  nle_object_check (nle, 50, 17, 28,
      17, MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 1, TRUE);

  ges_container_remove (GES_CONTAINER (clip),
      GES_TIMELINE_ELEMENT (trackelement));
  ges_container_remove (GES_CONTAINER (split_clip),
      GES_TIMELINE_ELEMENT (split_trackelement));

  gst_object_unref (timeline);
}

GST_END_TEST;

GST_START_TEST (test_group_ungroup_filesource_rate)
{
  GESUriClipAsset *asset;
  GESTimeline *timeline;
  GESClip *clip, *clip2;
  GList *containers, *tmp;
  GESLayer *layer;
  GESContainer *regrouped_clip;
  GESTrack *audio_track, *video_track, *track;
  AssetUri asset_uri;
  GESTrackElement *trackelement;
  gdouble rate;
  GstElement *nle;

  timeline = ges_timeline_new ();
  layer = ges_layer_new ();
  audio_track = GES_TRACK (ges_audio_track_new ());
  video_track = GES_TRACK (ges_video_track_new ());

  fail_unless (ges_timeline_add_track (timeline, audio_track));
  fail_unless (ges_timeline_add_track (timeline, video_track));
  fail_unless (ges_timeline_add_layer (timeline, layer));

  mainloop = g_main_loop_new (NULL, FALSE);
  asset_uri.uri = av_uri;
  /* Right away request the asset synchronously */
  g_timeout_add (1, (GSourceFunc) create_asset, &asset_uri);
  g_main_loop_run (mainloop);

  asset = GES_URI_CLIP_ASSET (asset_uri.asset);
  fail_unless (GES_IS_ASSET (asset));

  clip =
      ges_layer_add_asset (layer, GES_ASSET (asset), 0, 0, 10,
      GES_TRACK_TYPE_UNKNOWN);

  assert_is_type (clip, GES_TYPE_URI_CLIP);

  /* Check defaults on clip */
  _timeline_element_check (GES_TIMELINE_ELEMENT (clip), 0, 10, 0, GST_SECOND);
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip)), 2);
  g_object_get (clip, "rate", &rate, NULL);
  assert_equals_float (rate, 1.0);

  /* Check that trackelement has the same properties */
  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next) {
    trackelement = GES_CONTAINER_CHILDREN (clip)->data;
    fail_unless (trackelement != NULL);
    fail_unless (GES_TIMELINE_ELEMENT_PARENT (trackelement) ==
        GES_TIMELINE_ELEMENT (clip));
    track = ges_track_element_get_track (trackelement);
    fail_unless (track == audio_track || track == video_track);
    _timeline_element_check (GES_TIMELINE_ELEMENT (trackelement), 0, 10, 0,
        GST_SECOND);
    nle = ges_track_element_get_nleobject (trackelement);
    nle_object_check (nle, 0, 10, 0,
        10, MIN_NLE_PRIO + TRANSITIONS_HEIGHT, TRUE);
    assert_equals_float (((NleObject *) nle)->media_duration_factor, 1.0);
  }

  /* Change rate and ungroup */
  g_object_set (clip, "rate", 2.0, NULL);
  g_object_get (clip, "rate", &rate, NULL);
  assert_equals_float (rate, 2.0);

  _timeline_element_check (GES_TIMELINE_ELEMENT (clip), 0, 5, 0,
      0.5 * GST_SECOND);
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip)), 2);

  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next) {
    trackelement = GES_TRACK_ELEMENT (tmp->data);
    fail_unless (trackelement != NULL);
    fail_unless (GES_TIMELINE_ELEMENT_PARENT (trackelement) ==
        GES_TIMELINE_ELEMENT (clip));
    track = ges_track_element_get_track (trackelement);
    fail_unless (track == audio_track || track == video_track);
    _timeline_element_check (GES_TIMELINE_ELEMENT (trackelement), 0, 5, 0,
        0.5 * GST_SECOND);
    nle = ges_track_element_get_nleobject (trackelement);
    nle_object_check (nle, 0, 5, 0, 5, MIN_NLE_PRIO + TRANSITIONS_HEIGHT, TRUE);
    assert_equals_float (((NleObject *) nle)->media_duration_factor, 2.0);
  }

  containers = ges_container_ungroup (GES_CONTAINER (clip), FALSE);
  assert_equals_int (g_list_length (containers), 2);
  fail_unless (clip == containers->data);
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip)), 1);

  _timeline_element_check (GES_TIMELINE_ELEMENT (clip), 0, 5, 0,
      0.5 * GST_SECOND);
  g_object_get (clip, "rate", &rate, NULL);
  assert_equals_float (rate, 2.0);

  trackelement = GES_CONTAINER_CHILDREN (clip)->data;
  fail_unless (trackelement != NULL);
  fail_unless (GES_TIMELINE_ELEMENT_PARENT (trackelement) ==
      GES_TIMELINE_ELEMENT (clip));
  track = ges_track_element_get_track (trackelement);
  fail_unless (track == audio_track || track == video_track);

  _timeline_element_check (GES_TIMELINE_ELEMENT (trackelement), 0, 5, 0,
      0.5 * GST_SECOND);
  nle = ges_track_element_get_nleobject (trackelement);
  nle_object_check (nle, 0, 5, 0,
      5, MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 1, TRUE);
  assert_equals_float (((NleObject *) nle)->media_duration_factor, 2.0);

  clip2 = containers->next->data;
  fail_if (clip2 == clip);
  fail_unless (GES_TIMELINE_ELEMENT_TIMELINE (clip2) != NULL);
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip2)), 1);

  _timeline_element_check (GES_TIMELINE_ELEMENT (clip2), 0, 5, 0,
      0.5 * GST_SECOND);
  g_object_get (clip2, "rate", &rate, NULL);
  assert_equals_float (rate, 2.0);

  trackelement = GES_CONTAINER_CHILDREN (clip2)->data;
  fail_unless (trackelement != NULL);
  fail_unless (GES_TIMELINE_ELEMENT_PARENT (trackelement) ==
      GES_TIMELINE_ELEMENT (clip2));
  track = ges_track_element_get_track (trackelement);
  fail_unless (track == audio_track || track == video_track);
  _timeline_element_check (GES_TIMELINE_ELEMENT (trackelement), 0, 5, 0,
      0.5 * GST_SECOND);
  nle = ges_track_element_get_nleobject (trackelement);
  nle_object_check (nle, 0, 5, 0, 5, MIN_NLE_PRIO + TRANSITIONS_HEIGHT, TRUE);
  assert_equals_float (((NleObject *) nle)->media_duration_factor, 2.0);

  /* Try changing rate of clip2 */
  g_object_set (clip2, "rate", (gdouble) 1.0, NULL);
  g_object_get (clip2, "rate", &rate, NULL);
  assert_equals_float (rate, 1.0);

  _timeline_element_check (GES_TIMELINE_ELEMENT (clip2), 0, 10, 0, GST_SECOND);
  trackelement = GES_CONTAINER_CHILDREN (clip2)->data;
  fail_unless (trackelement != NULL);
  fail_unless (GES_TIMELINE_ELEMENT_PARENT (trackelement) ==
      GES_TIMELINE_ELEMENT (clip2));
  track = ges_track_element_get_track (trackelement);
  fail_unless (track == audio_track || track == video_track);
  _timeline_element_check (GES_TIMELINE_ELEMENT (trackelement), 0, 10, 0,
      GST_SECOND);
  nle = ges_track_element_get_nleobject (trackelement);
  nle_object_check (nle, 0, 10, 0, 10, MIN_NLE_PRIO + TRANSITIONS_HEIGHT, TRUE);
  assert_equals_float (((NleObject *) nle)->media_duration_factor, 1.0);

  /* Check clip is not affected */
  _timeline_element_check (GES_TIMELINE_ELEMENT (clip), 0, 5, 0,
      0.5 * GST_SECOND);
  g_object_get (clip, "rate", &rate, NULL);
  assert_equals_float (rate, 2.0);

  trackelement = GES_CONTAINER_CHILDREN (clip)->data;
  fail_unless (trackelement != NULL);
  fail_unless (GES_TIMELINE_ELEMENT_PARENT (trackelement) ==
      GES_TIMELINE_ELEMENT (clip));
  track = ges_track_element_get_track (trackelement);
  fail_unless (track == audio_track || track == video_track);

  _timeline_element_check (GES_TIMELINE_ELEMENT (trackelement), 0, 5, 0,
      0.5 * GST_SECOND);
  nle = ges_track_element_get_nleobject (trackelement);
  nle_object_check (nle, 0, 5, 0,
      5, MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 1, TRUE);
  assert_equals_float (((NleObject *) nle)->media_duration_factor, 2.0);

  /* Reset clip2 rate and regroup */

  g_object_set (clip2, "rate", (gdouble) 2.0, NULL);
  g_object_get (clip2, "rate", &rate, NULL);
  assert_equals_float (rate, 2.0);

  _timeline_element_check (GES_TIMELINE_ELEMENT (clip2), 0, 5, 0,
      0.5 * GST_SECOND);

  trackelement = GES_CONTAINER_CHILDREN (clip2)->data;
  fail_unless (trackelement != NULL);
  fail_unless (GES_TIMELINE_ELEMENT_PARENT (trackelement) ==
      GES_TIMELINE_ELEMENT (clip2));
  track = ges_track_element_get_track (trackelement);
  fail_unless (track == audio_track || track == video_track);

  _timeline_element_check (GES_TIMELINE_ELEMENT (trackelement), 0, 5, 0,
      0.5 * GST_SECOND);
  nle = ges_track_element_get_nleobject (trackelement);
  nle_object_check (nle, 0, 5, 0, 5, MIN_NLE_PRIO + TRANSITIONS_HEIGHT, TRUE);
  assert_equals_float (((NleObject *) nle)->media_duration_factor, 2.0);

  regrouped_clip = ges_container_group (containers);
  assert_is_type (regrouped_clip, GES_TYPE_CLIP);
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (regrouped_clip)),
      2);

  g_object_get (regrouped_clip, "rate", &rate, NULL);
  assert_equals_float (rate, 2.0);
  _timeline_element_check (GES_TIMELINE_ELEMENT (regrouped_clip), 0, 5, 0,
      0.5 * GST_SECOND);

  for (tmp = GES_CONTAINER_CHILDREN (regrouped_clip); tmp; tmp = tmp->next) {
    trackelement = GES_TRACK_ELEMENT (tmp->data);
    fail_unless (trackelement != NULL);
    fail_unless (GES_TIMELINE_ELEMENT_PARENT (trackelement) ==
        GES_TIMELINE_ELEMENT (regrouped_clip));
    track = ges_track_element_get_track (trackelement);
    fail_unless (track == audio_track || track == video_track);
    _timeline_element_check (GES_TIMELINE_ELEMENT (trackelement), 0, 5, 0,
        0.5 * GST_SECOND);
    nle = ges_track_element_get_nleobject (trackelement);
    nle_object_check (nle, 0, 5, 0,
        5, MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 1, TRUE);
    assert_equals_float (((NleObject *) nle)->media_duration_factor, 2.0);
  }

  g_list_free_full (containers, gst_object_unref);
  gst_object_unref (timeline);
}

GST_END_TEST;

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges-filesource");
  TCase *tc_chain = tcase_create ("filesource");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_filesource_basic);
  tcase_add_test (tc_chain, test_filesource_images);
  tcase_add_test (tc_chain, test_filesource_properties);
  tcase_add_test (tc_chain, test_filesource_rate);
  tcase_add_test (tc_chain, test_split_filesource_rate);
  tcase_add_test (tc_chain, test_group_ungroup_filesource_rate);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s;

  gst_check_init (&argc, &argv);

  ges_init ();

  if (atexit (ges_deinit) != 0) {
    GST_ERROR ("failed to set ges_deinit as exit function");
  }

  s = ges_suite ();

  av_uri = ges_test_get_audio_video_uri ();
  image_uri = ges_test_get_image_uri ();

  nf = gst_check_run_suite (s, "ges", __FILE__);

  g_free (av_uri);
  g_free (image_uri);

  return nf;
}
