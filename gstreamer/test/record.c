#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <glib.h>
#include <gst/gst.h>

int main(int argc,char *argv[]) {
  int fd;
  GstElement *pipeline, *osssrc, *fdsink;
  GstElementFactory *osssrcfactory, *fdsinkfactory;

  gst_init(&argc,&argv);

  pipeline = GST_ELEMENT(gst_pipeline_new("pipeline"));

  osssrcfactory = gst_elementfactory_find("osssrc");
  osssrc = gst_elementfactory_create(osssrcfactory,"osssrc");

  fd = open(argv[1],O_CREAT|O_RDWR);

  fdsinkfactory = gst_elementfactory_find("fdsink");
  fdsink = gst_elementfactory_create(fdsinkfactory,"fdsink");
  g_object_set(G_OBJECT(fdsink),"fd",fd);

  /* add objects to the main pipeline */
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(osssrc));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(fdsink));

  /* connect src to sink */
  gst_pad_connect(gst_element_get_pad(osssrc,"src"),
                  gst_element_get_pad(fdsink,"sink"));

  g_print("\nok, runnable, hitting 'play'...\n");
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PLAYING);

  while(1) {
    gst_bin_iterate(GST_BIN(pipeline));
  }
}

