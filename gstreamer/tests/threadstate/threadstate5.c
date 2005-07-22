#include <stdlib.h>
#include <gst/gst.h>

/* this pipeline is:
 * { fakesrc ! { queue ! fakesink } }
 */

int
main (int argc, char *argv[])
{
  GstElement *fakesrc, *fakesink;
  GstElement *pipeline, *pipeline2;
  GstElement *queue;
  gint x;

  gst_init (&argc, &argv);

  pipeline = gst_pipeline_new ("pipeline");
  g_assert (pipeline != NULL);

  pipeline2 = gst_pipeline_new ("pipeline");
  g_assert (pipeline2 != NULL);

  queue = gst_element_factory_make ("queue", "the_queue");
  g_assert (queue != NULL);

  fakesrc = gst_element_factory_make ("fakesrc", "fake_source");
  g_assert (fakesrc != NULL);

  fakesink = gst_element_factory_make ("fakesink", "fake_sink");
  g_assert (fakesink != NULL);

  gst_bin_add_many (GST_BIN (pipeline), fakesrc, pipeline2, NULL);
  gst_bin_add_many (GST_BIN (pipeline2), queue, fakesink, NULL);

  gst_element_add_pad (pipeline2, gst_ghost_pad_new ("sink",
          gst_element_get_pad (queue, "sink")));
  gst_element_link_many (queue, fakesink, NULL);
  gst_element_link_many (fakesrc, pipeline2, NULL);

  for (x = 0; x < 10; x++) {
    g_print ("playing %d\n", x);
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    g_usleep (G_USEC_PER_SEC);

    g_print ("nulling %d\n", x);
    gst_element_set_state (pipeline, GST_STATE_NULL);
    g_usleep (G_USEC_PER_SEC);
  }

  exit (0);
}
