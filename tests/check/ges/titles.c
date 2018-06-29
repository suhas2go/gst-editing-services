/* GStreamer Editing Services
 * Copyright (C) 2010 Brandon Lewis <brandon.lewis@collabora.co.uk>
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

GST_START_TEST (test_title_source_basic)
{
  GESTitleClip *source;

  source = ges_title_clip_new ();
  fail_unless (source != NULL);

  gst_object_unref (source);
}

GST_END_TEST;

GST_START_TEST (test_title_source_properties)
{
  GESClip *clip;
  GESTrack *track;
  GESTimeline *timeline;
  GESLayer *layer;
  GESTrackElement *trackelement;

  track = GES_TRACK (ges_video_track_new ());
  fail_unless (track != NULL);
  layer = ges_layer_new ();
  fail_unless (layer != NULL);
  timeline = ges_timeline_new ();
  fail_unless (timeline != NULL);
  fail_unless (ges_timeline_add_layer (timeline, layer));
  fail_unless (ges_timeline_add_track (timeline, track));
  ASSERT_OBJECT_REFCOUNT (timeline, "timeline", 1);

  clip = (GESClip *) ges_title_clip_new ();
  fail_unless (clip != NULL);

  /* Set some properties */
  g_object_set (clip, "start", (guint64) 42, "duration", (guint64) 51,
      "in-point", (guint64) 12, NULL);
  assert_equals_uint64 (_START (clip), 42);
  assert_equals_uint64 (_DURATION (clip), 51);
  assert_equals_uint64 (_INPOINT (clip), 0);

  ges_layer_add_clip (layer, GES_CLIP (clip));
  ges_timeline_commit (timeline);
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip)), 1);
  trackelement = GES_CONTAINER_CHILDREN (clip)->data;
  fail_unless (trackelement != NULL);
  fail_unless (GES_TIMELINE_ELEMENT_PARENT (trackelement) ==
      GES_TIMELINE_ELEMENT (clip));
  fail_unless (ges_track_element_get_track (trackelement) == track);

  /* Check that trackelement has the same properties */
  assert_equals_uint64 (_START (trackelement), 42);
  assert_equals_uint64 (_DURATION (trackelement), 51);
  assert_equals_uint64 (_INPOINT (trackelement), 0);

  /* And let's also check that it propagated correctly to GNonLin */
  nle_object_check (ges_track_element_get_nleobject (trackelement), 42, 51, 0,
      51, MIN_NLE_PRIO + TRANSITIONS_HEIGHT, TRUE);

  /* Change more properties, see if they propagate */
  g_object_set (clip, "start", (guint64) 420, "duration", (guint64) 510,
      "in-point", (guint64) 120, NULL);
  ges_timeline_commit (timeline);
  assert_equals_uint64 (_START (clip), 420);
  assert_equals_uint64 (_DURATION (clip), 510);
  assert_equals_uint64 (_INPOINT (clip), 0);
  assert_equals_uint64 (_START (trackelement), 420);
  assert_equals_uint64 (_DURATION (trackelement), 510);
  assert_equals_uint64 (_INPOINT (trackelement), 0);

  /* And let's also check that it propagated correctly to GNonLin */
  nle_object_check (ges_track_element_get_nleobject (trackelement), 420, 510,
      0, 510, MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 0, TRUE);

  ges_container_remove (GES_CONTAINER (clip),
      GES_TIMELINE_ELEMENT (trackelement));
  gst_object_unref (clip);
}

GST_END_TEST;

GST_START_TEST (test_title_source_in_layer)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESTrack *a, *v;
  GESTrackElement *track_element;
  GESTitleClip *source;
  gchar *text;
  gint halign, valign;
  guint32 color;
  gdouble xpos;
  gdouble ypos;

  timeline = ges_timeline_new ();
  layer = ges_layer_new ();
  a = GES_TRACK (ges_audio_track_new ());
  v = GES_TRACK (ges_video_track_new ());

  ges_timeline_add_track (timeline, a);
  ges_timeline_add_track (timeline, v);
  ges_timeline_add_layer (timeline, layer);

  source = ges_title_clip_new ();

  g_object_set (source, "duration", (guint64) GST_SECOND, NULL);

  ges_layer_add_clip (layer, (GESClip *) source);

  /* specifically test the text property */
  g_object_set (source, "text", (gchar *) "some text", NULL);
  g_object_get (source, "text", &text, NULL);
  assert_equals_string ("some text", text);
  g_free (text);

  track_element =
      ges_clip_find_track_element (GES_CLIP (source), v, GES_TYPE_TITLE_SOURCE);

  /* Check the text is still the same */
  assert_equals_string (ges_title_source_get_text (GES_TITLE_SOURCE
          (track_element)), "some text");

  /* test the font-desc property */
  g_object_set (source, "font-desc", (gchar *) "sans 72", NULL);
  g_object_get (source, "font-desc", &text, NULL);
  assert_equals_string ("sans 72", text);
  g_free (text);

  assert_equals_string ("sans 72",
      ges_title_source_get_font_desc (GES_TITLE_SOURCE (track_element)));

  /* test halign and valign */
  g_object_set (source, "halignment", (gint)
      GES_TEXT_HALIGN_LEFT, "valignment", (gint) GES_TEXT_VALIGN_TOP, NULL);
  g_object_get (source, "halignment", &halign, "valignment", &valign, NULL);
  assert_equals_int (halign, GES_TEXT_HALIGN_LEFT);
  assert_equals_int (valign, GES_TEXT_VALIGN_TOP);

  assert_equals_int (ges_title_source_get_halignment
      (GES_TITLE_SOURCE (track_element)), GES_TEXT_HALIGN_LEFT);
  assert_equals_int (ges_title_source_get_valignment
      (GES_TITLE_SOURCE (track_element)), GES_TEXT_VALIGN_TOP);

  /* test color */
  g_object_set (source, "color", (gint) 2147483647, NULL);
  g_object_get (source, "color", &color, NULL);
  assert_equals_int (color, 2147483647);

  color = ges_title_source_get_text_color (GES_TITLE_SOURCE (track_element));
  assert_equals_int (color, 2147483647);

  /* test xpos */
  g_object_set (source, "xpos", (gdouble) 0.25, NULL);
  g_object_get (source, "xpos", &xpos, NULL);
  assert_equals_float (xpos, 0.25);

  xpos = ges_title_source_get_xpos (GES_TITLE_SOURCE (track_element));
  assert_equals_float (xpos, 0.25);

  /* test ypos */
  g_object_set (source, "ypos", (gdouble) 0.66, NULL);
  g_object_get (source, "ypos", &ypos, NULL);
  assert_equals_float (ypos, 0.66);

  xpos = ges_title_source_get_xpos (GES_TITLE_SOURCE (track_element));
  assert_equals_float (ypos, 0.66);

  GST_DEBUG ("removing the source");

  ges_layer_remove_clip (layer, (GESClip *) source);

  GST_DEBUG ("removing the layer");

  gst_object_unref (track_element);
  gst_object_unref (timeline);
}

GST_END_TEST;

GST_START_TEST (test_title_source_rate)
{
  GESClip *clip;
  GESTrack *track;
  GESTimeline *timeline;
  GESLayer *layer;
  GESTrackElement *trackelement;
  gdouble rate;

  track = GES_TRACK (ges_video_track_new ());
  fail_unless (track != NULL);
  layer = ges_layer_new ();
  fail_unless (layer != NULL);
  timeline = ges_timeline_new ();
  fail_unless (timeline != NULL);
  fail_unless (ges_timeline_add_layer (timeline, layer));
  fail_unless (ges_timeline_add_track (timeline, track));
  ASSERT_OBJECT_REFCOUNT (timeline, "timeline", 1);

  clip = (GESClip *) ges_title_clip_new ();
  fail_unless (clip != NULL);

  /* Set some properties */
  g_object_set (clip, "start", (guint64) 42, "duration", (guint64) 50,
      "in-point", (guint64) 12, NULL);

  assert_equals_uint64 (_START (clip), 42);
  assert_equals_uint64 (_DURATION (clip), 50);
  assert_equals_uint64 (_INPOINT (clip), 0);

  ges_layer_add_clip (layer, GES_CLIP (clip));
  ges_timeline_commit (timeline);
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip)), 1);
  trackelement = GES_CONTAINER_CHILDREN (clip)->data;
  fail_unless (trackelement != NULL);
  fail_unless (GES_TIMELINE_ELEMENT_PARENT (trackelement) ==
      GES_TIMELINE_ELEMENT (clip));
  fail_unless (ges_track_element_get_track (trackelement) == track);

  /* Check that trackelement has the same properties */
  assert_equals_uint64 (_START (trackelement), 42);
  assert_equals_uint64 (_DURATION (trackelement), 50);
  assert_equals_uint64 (_INPOINT (trackelement), 0);

  /* Test rate property */
  g_object_get (clip, "rate", &rate, NULL);
  assert_equals_float (rate, 1.0);

  g_object_set (clip, "rate", (gdouble) 2.0, NULL);
  g_object_get (clip, "rate", &rate, NULL);
  assert_equals_float (rate, 2.0);
  ges_timeline_commit (timeline);

  assert_equals_uint64 (_START (clip), 42);
  assert_equals_uint64 (_DURATION (clip), 25);
  assert_equals_uint64 (_INPOINT (clip), 0);

  assert_equals_uint64 (_START (trackelement), 42);
  assert_equals_uint64 (_DURATION (trackelement), 25);
  assert_equals_uint64 (_INPOINT (trackelement), 0);

  g_object_set (clip, "rate", (gdouble) 0.5, NULL);
  g_object_get (clip, "rate", &rate, NULL);
  assert_equals_float (rate, 0.5);
  ges_timeline_commit (timeline);

  assert_equals_uint64 (_START (clip), 42);
  assert_equals_uint64 (_DURATION (clip), 100);
  assert_equals_uint64 (_INPOINT (clip), 0);

  assert_equals_uint64 (_START (trackelement), 42);
  assert_equals_uint64 (_DURATION (trackelement), 100);
  assert_equals_uint64 (_INPOINT (trackelement), 0);

  ges_container_remove (GES_CONTAINER (clip),
      GES_TIMELINE_ELEMENT (trackelement));

  gst_object_unref (timeline);
}

GST_END_TEST;

GST_START_TEST (test_split_title_source_rate)
{
  GESClip *clip, *split_clip;
  GESTrack *track;
  GESTimeline *timeline;
  GESLayer *layer;
  GESTrackElement *trackelement, *split_trackelement;
  gdouble rate;
  GstElement *nle;

  track = GES_TRACK (ges_video_track_new ());
  fail_unless (track != NULL);
  layer = ges_layer_new ();
  fail_unless (layer != NULL);
  timeline = ges_timeline_new ();
  fail_unless (timeline != NULL);
  fail_unless (ges_timeline_add_layer (timeline, layer));
  fail_unless (ges_timeline_add_track (timeline, track));
  ASSERT_OBJECT_REFCOUNT (timeline, "timeline", 1);

  clip = (GESClip *) ges_title_clip_new ();
  fail_unless (clip != NULL);

  /* Set some properties */
  g_object_set (clip, "start", (guint64) 42, "duration", (guint64) 50,
      "in-point", (guint64) 12, NULL);

  assert_equals_uint64 (_START (clip), 42);
  assert_equals_uint64 (_DURATION (clip), 50);
  assert_equals_uint64 (_INPOINT (clip), 0);

  ges_layer_add_clip (layer, GES_CLIP (clip));
  ges_timeline_commit (timeline);
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip)), 1);
  trackelement = GES_CONTAINER_CHILDREN (clip)->data;
  fail_unless (trackelement != NULL);
  fail_unless (GES_TIMELINE_ELEMENT_PARENT (trackelement) ==
      GES_TIMELINE_ELEMENT (clip));
  fail_unless (ges_track_element_get_track (trackelement) == track);

  /* Check that trackelement has the same properties */
  assert_equals_uint64 (_START (trackelement), 42);
  assert_equals_uint64 (_DURATION (trackelement), 50);
  assert_equals_uint64 (_INPOINT (trackelement), 0);

  nle = ges_track_element_get_nleobject (trackelement);
  nle_object_check (nle, 42, 50, 0,
      50, MIN_NLE_PRIO + TRANSITIONS_HEIGHT, TRUE);
  assert_equals_float (((NleObject *) nle)->media_duration_factor, 1.0);

  /* Test rate property */
  g_object_get (clip, "rate", &rate, NULL);
  assert_equals_float (rate, 1.0);

  g_object_set (clip, "rate", (gdouble) 2.0, NULL);
  g_object_get (clip, "rate", &rate, NULL);
  assert_equals_float (rate, 2.0);
  ges_timeline_commit (timeline);

  assert_equals_uint64 (_START (clip), 42);
  assert_equals_uint64 (_DURATION (clip), 25);
  assert_equals_uint64 (_INPOINT (clip), 0);

  assert_equals_uint64 (_START (trackelement), 42);
  assert_equals_uint64 (_DURATION (trackelement), 25);
  assert_equals_uint64 (_INPOINT (trackelement), 0);

  nle = ges_track_element_get_nleobject (trackelement);
  nle_object_check (nle, 42, 25, 0,
      25, MIN_NLE_PRIO + TRANSITIONS_HEIGHT, TRUE);
  assert_equals_float (((NleObject *) nle)->media_duration_factor, 2.0);

  split_clip = ges_clip_split (clip, 50);
  fail_unless (GES_IS_CLIP (split_clip));
  assert_equals_uint64 (_START (split_clip), 50);
  assert_equals_uint64 (_DURATION (split_clip), 17);
  assert_equals_uint64 (_INPOINT (split_clip), 0);

  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (split_clip)), 1);
  split_trackelement = GES_CONTAINER_CHILDREN (split_clip)->data;
  fail_unless (split_trackelement != NULL);
  fail_unless (GES_TIMELINE_ELEMENT_PARENT (split_trackelement) ==
      GES_TIMELINE_ELEMENT (split_clip));
  fail_unless (ges_track_element_get_track (split_trackelement) == track);

  assert_equals_uint64 (_START (split_trackelement), 50);
  assert_equals_uint64 (_DURATION (split_trackelement), 17);
  assert_equals_uint64 (_INPOINT (split_trackelement), 0);

  nle = ges_track_element_get_nleobject (split_trackelement);
  assert_equals_float (((NleObject *) nle)->media_duration_factor, 2.0);
  nle_object_check (nle, 50, 17, 0,
      17, MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 1, TRUE);

  ges_container_remove (GES_CONTAINER (clip),
      GES_TIMELINE_ELEMENT (trackelement));
  ges_container_remove (GES_CONTAINER (split_clip),
      GES_TIMELINE_ELEMENT (split_trackelement));

  gst_object_unref (timeline);
}

GST_END_TEST;

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges-titles");
  TCase *tc_chain = tcase_create ("titles");

  ges_init ();

  if (atexit (ges_deinit) != 0) {
    GST_ERROR ("failed to set ges_deinit as exit function");
  }

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_title_source_basic);
  tcase_add_test (tc_chain, test_title_source_properties);
  tcase_add_test (tc_chain, test_title_source_in_layer);
  tcase_add_test (tc_chain, test_title_source_rate);
  tcase_add_test (tc_chain, test_split_title_source_rate);

  return s;
}

GST_CHECK_MAIN (ges);
