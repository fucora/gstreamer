/* GStreamer
 *
 * unit test for GstObject
 *
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
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

#include "../gstcheck.h"

/*
  Create a fake subclass
 */
typedef struct _GstFakeObjectClass GstFakeObjectClass;
typedef struct _GstFakeObject GstFakeObject;

struct _GstFakeObject
{
  GstObject object;
};

struct _GstFakeObjectClass
{
  GstObjectClass parent_class;
};

GType _gst_fake_object_type = 0;

//static GstObjectClass *parent_class = NULL;
//static guint gst_fake_object_signals[LAST_SIGNAL] = { 0 };

GType
gst_fake_object_get_type (void)
{
  if (!_gst_fake_object_type) {
    static const GTypeInfo fake_object_info = {
      sizeof (GstFakeObjectClass),
      NULL,                     //gst_fake_object_base_class_init,
      NULL,                     //gst_fake_object_base_class_finalize,
      NULL,                     //(GClassInitFunc) gst_fake_object_class_init,
      NULL,
      NULL,
      sizeof (GstFakeObject),
      0,
      NULL,                     //(GInstanceInitFunc) gst_fake_object_init,
      NULL
    };

    _gst_fake_object_type = g_type_register_static (GST_TYPE_OBJECT,
        "GstFakeObject", &fake_object_info, 0);
  }
  return _gst_fake_object_type;
}

/* g_object_new on abstract GstObject should fail */
START_TEST (test_fail_abstract_new)
{
  GstObject *object;

  ASSERT_CRITICAL (object = g_object_new (gst_object_get_type (), NULL));
  fail_unless (object == NULL, "Created an instance of abstract GstObject");
}

END_TEST
/* g_object_new on GstFakeObject should succeed */
START_TEST (test_fake_object_new)
{
  GstObject *object;

  object = g_object_new (gst_fake_object_get_type (), NULL);
  fail_if (object == NULL, "Failed to create instance of GstFakeObject");
  fail_unless (GST_IS_OBJECT (object),
      "GstFakeObject instance is not a GstObject");
}

END_TEST
/* GstFakeObject name tests */
START_TEST (test_fake_object_name)
{
  GstObject *object;
  gchar *name;
  gchar *name2;

  object = g_object_new (gst_fake_object_get_type (), NULL);

  name = gst_object_get_name (object);
  fail_if (name == NULL, "Newly created object has no name");
  fail_if (strncmp (name, "fakeobject", 10) != 0,
      "Random name %s does not start with Gst", name);

  /* give a random name by setting with NULL;
   * GstFakeObject class -> fakeobject%d */
  gst_object_set_name (object, NULL);
  name = gst_object_get_name (object);
  fail_if (name == NULL, "Random name was not assigned");
  fail_if (strncmp (name, "fakeobject", 10) != 0,
      "Random name %s does not start with Gst", name);
  g_free (name);

  gst_object_set_name (object, "fake");
  name = gst_object_get_name (object);
  fail_if (name == NULL, "Failed to get name of GstFakeObject");
  fail_if (strcmp (name, "fake") != 0, "Name of GstFakeObject is not 'fake'");

  /* change the gotten name to see that it's a copy and not the original */
  name[0] = 'm';
  name2 = gst_object_get_name (object);
  fail_if (strcmp (name2, "fake") != 0,
      "Copy of object name affected actual object name");
  g_free (name);
  g_free (name2);
}

END_TEST
/* thread function for threaded name change test */
    gpointer thread_name_object (GstObject * object)
{
  gchar *thread_id = g_strdup_printf ("%p", g_thread_self ());

  THREAD_START ();

  /* give main thread a head start */
  g_usleep (100000);

  /* write our name repeatedly */
  g_message ("THREAD %s: starting loop\n", thread_id);
  while (THREAD_TEST_RUNNING ()) {
    gst_object_set_name (object, thread_id);
    /* a minimal sleep invokes a thread switch */
    THREAD_SWITCH ();
  }

  /* thread is done, so let's return */
  g_message ("THREAD %s: set name\n", thread_id);
  g_free (thread_id);

  return NULL;
}

/*
 * main thread sets and gets name while other threads set the name
 * constantly; fails because lock is released inbetween set and get
 */

START_TEST (test_fake_object_name_threaded_wrong)
{
  GstObject *object;
  gchar *name;
  gint i;
  gboolean expected_failure = FALSE;

  g_message ("\nTEST: set/get without lock\n");

  object = g_object_new (gst_fake_object_get_type (), NULL);
  gst_object_set_name (object, "main");

  MAIN_START_THREADS (5, thread_name_object, object);

  /* start looping and set/get name repeatedly */
  for (i = 0; i < 1000; ++i) {
    gst_object_set_name (object, "main");
    THREAD_SWITCH ();
    name = gst_object_get_name (object);
    if (strcmp (name, "main") != 0) {
      g_message ("MAIN: expected failure during run %d\n", i);
      expected_failure = TRUE;
      g_free (name);
      break;
    }
    g_free (name);
  }
  MAIN_STOP_THREADS ();

  fail_unless (expected_failure, "name did not get changed");
}

END_TEST
/*
 * main thread sets and gets name directly on struct inside the object lock
 * succeed because lock is held during set/get, and threads are locked out
 */
START_TEST (test_fake_object_name_threaded_right)
{
  GstObject *object;
  gchar *name;
  gint i;

  g_message ("\nTEST: set/get inside lock\n");

  object = g_object_new (gst_fake_object_get_type (), NULL);
  gst_object_set_name (object, "main");

  MAIN_START_THREADS (5, thread_name_object, object);

  /* start looping and set/get name repeatedly */
  for (i = 0; i < 1000; ++i) {
    GST_LOCK (object);
    g_free (GST_OBJECT_NAME (object));
    GST_OBJECT_NAME (object) = g_strdup ("main");
    THREAD_SWITCH ();
    name = g_strdup (GST_OBJECT_NAME (object));
    GST_UNLOCK (object);

    fail_unless (strcmp (name, "main") == 0,
        "Name got changed while lock held during run %d", i);
    g_free (name);
  }
  MAIN_STOP_THREADS ();
}

END_TEST
/*
 * main thread creates lots of objects
 * child threads sets default names on objects
 * then main thread checks uniqueness of object names
 */
    GList * object_list = NULL;
gint num_objects = 1000;
gint num_threads = 5;

/* thread function for threaded default name change test */
gpointer
thread_name_object_default (int *i)
{
  int j;

  THREAD_START ();

  for (j = *i; j < num_objects; j += num_threads) {
    GstObject *o = GST_OBJECT (g_list_nth_data (object_list, j));

    /* g_message ("THREAD %p: setting default name on object %d\n",
       g_thread_self (), j); */
    gst_object_set_name (o, NULL);
    THREAD_SWITCH ();
  }

  /* thread is done, so let's return */
  g_message ("THREAD %p: set name\n", g_thread_self ());
  g_free (i);

  return NULL;
}

static gint
gst_object_name_compare (GstObject * o, GstObject * p)
{
  gint result;

  GST_LOCK (o);
  GST_LOCK (p);

  if (o->name == NULL && p->name == NULL) {
    result = 0;
  } else if (o->name == NULL) {
    result = -1;
  } else if (p->name == NULL) {
    result = 1;
  } else {
    result = strcmp (o->name, p->name);
  }

  GST_UNLOCK (p);
  GST_UNLOCK (o);

  return result;
}

START_TEST (test_fake_object_name_threaded_unique)
{
  GstObject *object;
  gint i;
  gint *ip;
  gchar *name1, *name2;
  GList *l;

  g_message ("\nTEST: uniqueness of default names\n");

  for (i = 0; i < num_objects; ++i) {
    object = g_object_new (gst_fake_object_get_type (), NULL);
    object_list = g_list_append (object_list, object);
  }

  MAIN_INIT ();

  mark_point ();
  for (i = 0; i < num_threads; ++i) {
    ip = g_new (gint, 1);
    *ip = i;
    MAIN_START_THREAD_FUNCTION (i, thread_name_object_default, ip);
  }

  mark_point ();
  MAIN_SYNCHRONIZE ();
  mark_point ();
  MAIN_STOP_THREADS ();

  /* sort GList based on object name */
  /* FIXME: sort and test */
  g_list_sort (object_list, (GCompareFunc) gst_object_name_compare);

  name1 = gst_object_get_name (GST_OBJECT (object_list->data));
  for (l = object_list->next; l->next; l = l->next) {
    g_message ("object with name %s\n", name1);
    name2 = gst_object_get_name (GST_OBJECT (l->data));
    fail_if (strcmp (name1, name2) == 0, "Two objects with name %s", name2);
    g_free (name1);
    name1 = name2;
  }

  /* free stuff */
  g_list_foreach (object_list, (GFunc) g_object_unref, NULL);
}

END_TEST
/* test: try renaming a parented object, make sure it fails */
    Suite * gst_object_suite (void)
{
  Suite *s = suite_create ("GstObject");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_fake_object_new);
  tcase_add_test (tc_chain, test_fake_object_name);
  tcase_add_test (tc_chain, test_fake_object_name_threaded_wrong);
  tcase_add_test (tc_chain, test_fake_object_name_threaded_right);
  tcase_add_test (tc_chain, test_fake_object_name_threaded_unique);
  //tcase_add_checked_fixture (tc_chain, setup, teardown);

  /* SEGV tests go last so we can debug the others */
  tcase_add_test_raise_signal (tc_chain, test_fail_abstract_new, SIGSEGV);
  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = gst_object_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
