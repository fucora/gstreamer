#include <gst/gst.h>

int
main (int argc, char *argv[])
{
  GstElement *pipeline, *queue, *src, *sink;

  gst_init (&argc, &argv);

  free (malloc (8));            /* -lefence */

  pipeline = gst_pipeline_new ("pipeline");

  src = gst_element_factory_make ("fakesrc", "src");

  queue = gst_element_factory_make ("queue", "queue");
  sink = gst_element_factory_make ("fakesink", "sink");

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), queue);
  gst_bin_add (GST_BIN (pipeline), sink);

  gst_element_link_pads (src, "src", queue, "sink");
  gst_element_link_pads (queue, "src", sink, "sink");

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_usleep (G_USEC_PER_SEC);
  gst_element_set_state (pipeline, GST_STATE_PAUSED);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_usleep (G_USEC_PER_SEC);
  gst_element_set_state (pipeline, GST_STATE_PAUSED);

  return 0;
}
