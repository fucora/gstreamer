#include <gst/gst.h>

int main (int argc, char *argv[]) 
{
    GstElement *fakesrc, *fakesink1, *fakesink2, *pipe1, *pipe2;
    
    gst_init(&argc, &argv);
    
    if (argc!=1) {
        g_print("usage: %s\n", argv[0]);
        exit(-1);
    }
    
    fakesrc = gst_elementfactory_make("fakesrc", "fakesrc");
    fakesink1 = gst_elementfactory_make("fakesink", "fakesink1");
    fakesink2 = gst_elementfactory_make("fakesink", "fakesink2");
    pipe1 = gst_pipeline_new("pipe1");
    pipe2 = gst_pipeline_new("pipe2");
    
    // make the first pipeline
    gst_bin_add (GST_BIN(pipe1), fakesrc);
    gst_bin_add (GST_BIN(pipe1), fakesink1);
    gst_element_connect(fakesrc, "src", fakesink1, "sink");
    
    // initialize cothreads
    gst_element_set_state(pipe1, GST_STATE_PLAYING);
    gst_element_set_state(pipe1, GST_STATE_READY);
    
    // destroy the fakesink, but keep fakesrc (its state is GST_STATE_READY)
    gst_element_disconnect(fakesrc, "src", fakesink1, "sink");
    gst_object_ref(GST_OBJECT(fakesrc));
    gst_bin_remove(GST_BIN(pipe1), fakesrc);
    gst_bin_remove(GST_BIN(pipe1), fakesink1);
    
    // make a new pipeline
    gst_bin_add (GST_BIN(pipe2), fakesink2);
    
    // don't change the new pipeline's state, it should change on the bin_add
    gst_bin_add (GST_BIN(pipe2), fakesrc);
    gst_element_connect(fakesrc, "src", fakesink2, "sink");
    
    // show the pipeline state
    gst_xml_write_file (GST_ELEMENT (pipe2), stdout);
    
    // try to iterate the pipeline
    gst_element_set_state(pipe2, GST_STATE_PLAYING);
    gst_bin_iterate(GST_BIN(pipe2));
    gst_element_set_state(pipe2, GST_STATE_NULL);
    
    return 0;
}

                    
