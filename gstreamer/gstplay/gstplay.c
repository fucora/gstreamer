/*
 * Initial main.c file generated by Glade. Edit as required.
 * Glade will not overwrite this file.
 */

#include <config.h>

//#define DEBUG_ENABLED

#include <glade/glade.h>
#include <gst/gstclock.h>

#include "gstplay.h"

#include "callbacks.h"
#include "interface.h"

#include "codecs.h"

#define MUTEX_STATUS() (g_mutex_trylock(gdk_threads_mutex)? g_mutex_unlock(gdk_threads_mutex), "was not locked" : "was locked")


#define BUFFER 20

extern gboolean _gst_plugin_spew;
gboolean idle_func(gpointer data);
GstElement *show, *video_render_queue;
GstElement *audio_play, *audio_render_queue;
GstElement *src;
GstElement *pipeline;
GstElement *parse = NULL;
GstElement *typefind;
GstElement *video_render_thread;
GstElement *audio_render_thread;
GstPlayState state;
gboolean picture_shown = FALSE;
guchar statusline[200];
guchar *statustext = "stopped";
GtkWidget *status_area;
GtkAdjustment *adjustment;
GtkWidget *play_button;
GtkWidget *pause_button;
GtkWidget *stop_button;
GtkFileSelection *open_file_selection;

gint start_from_file(guchar *filename); 

static void frame_displayed(GstSrc *asrc) 
{
  int size, time, frame_time = 0, src_pos;
  guint mux_rate;
  static int prev_time = -1;

  if (!parse) return;
  DEBUG("gstplay: frame displayed %s\n", MUTEX_STATUS());

  mux_rate = gst_util_get_int_arg(GTK_OBJECT(parse),"mux_rate");
  size = gst_util_get_int_arg(GTK_OBJECT(src),"size");
  time = (size*8)/mux_rate;
  frame_time = gst_util_get_int_arg(GTK_OBJECT(show),"frame_time");
  src_pos = gst_util_get_int_arg(GTK_OBJECT(src),"offset");
  frame_time = (src_pos*8)/mux_rate;

  if (frame_time >= prev_time)  {
    
    g_snprintf(statusline, 200, "%02d:%02d / %02d:%02d\n", 
		  frame_time/60, frame_time%60,
		  time/60, time%60);

    //printf("%d %d %g\n", frame_time, size, frame_time*100.0/size);

    update_status_area(status_area);
    if (state == GSTPLAY_PLAYING)
      update_slider(adjustment, src_pos*100.0/size);
  }
  picture_shown = TRUE;

  prev_time = frame_time;
  DEBUG("gstplay: frame displayed end %s\n", MUTEX_STATUS());
}

gboolean idle_func(gpointer data) {
  DEBUG("idle start %s\n",MUTEX_STATUS());
  //gst_src_push(GST_SRC(data));
  gst_bin_iterate(GST_BIN(data));
  DEBUG("idle stop %s\n",MUTEX_STATUS());
  return TRUE;
}

static void eof(GstSrc *src) {
  change_state(GSTPLAY_PAUSE);
  picture_shown = TRUE;
}

void show_next_picture() {
  picture_shown = FALSE;
  DEBUG("gstplay: next picture %s\n", MUTEX_STATUS());
  while (!picture_shown) {
    gdk_threads_leave();
    gst_src_push(GST_SRC(src));
    gdk_threads_enter();
  }
  DEBUG("gstplay: next found %s\n", MUTEX_STATUS());
}

void mute_audio(gboolean mute) {
  gtk_object_set(GTK_OBJECT(audio_play),"mute",mute,NULL);
}

static void gstplay_tear_down() 
{
  g_print("setting to NULL state\n");
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_NULL);
}

static void  
target_drag_data_received  (GtkWidget          *widget,
                            GdkDragContext     *context,
                            gint                x,
                            gint                y,
                            GtkSelectionData   *data,
                            guint               info,
                            guint               time)
{
  if (strstr(data->data, "file:")) {
    g_print("Got: %s\n",data->data);
    start_from_file(g_strchomp(&data->data[5]));
  }
}

void
on_exit_menu_activate                 (GtkMenuItem     *menuitem,
                                       gpointer         user_data)
{
  gdk_threads_leave();
  gstplay_tear_down();
  gdk_threads_enter();
  gtk_main_quit();
}

void on_ok_button1_clicked             (GtkButton     *button,
                                        GtkFileSelection *sel)
{
  gchar *selected_filename;

  selected_filename = gtk_file_selection_get_filename (GTK_FILE_SELECTION(open_file_selection));
  start_from_file(selected_filename);
}

gint on_gstplay_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data)
{
  gdk_threads_leave();
  gstplay_tear_down();
  gdk_threads_enter();
  return FALSE;
}

void gstplay_parse_pads_created(GstElement *element, gpointer data)
{
  printf("gstplay: element \"%s\" is ready\n", gst_element_get_name(element));
  gst_clock_reset(gst_clock_get_system());
}

void change_state(GstPlayState new_state) {

  if (new_state == state) return;
  switch (new_state) { 
    case GSTPLAY_PLAYING:
      mute_audio(FALSE);
      statustext = "playing";
      update_status_area(status_area);
      gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PLAYING);
      gtk_idle_add(idle_func, pipeline);
      state = GSTPLAY_PLAYING;
      update_buttons(0);
      break;
    case GSTPLAY_PAUSE:
      statustext = "paused";
      update_status_area(status_area);
      if (state != GSTPLAY_STOPPED) gtk_idle_remove_by_data(pipeline);
      mute_audio(TRUE);
      state = GSTPLAY_PAUSE;
      update_buttons(1);
      break;
    case GSTPLAY_STOPPED:
      if (state != GSTPLAY_PAUSE) gtk_idle_remove_by_data(pipeline);
      statustext = "stopped";
      update_status_area(status_area);
      mute_audio(TRUE);
      state = GSTPLAY_STOPPED;
      gtk_object_set(GTK_OBJECT(src),"offset",0,NULL);
      update_buttons(2);
      update_slider(adjustment, 0.0);
      show_next_picture();
      break;
  }
}

static void have_type(GstSink *sink) {
  gint type;
  GstType *gsttype;
  
  type = gst_util_get_int_arg(GTK_OBJECT(sink),"type");
  gsttype = gst_type_find_by_id(type);

  g_print("have type %d:%s\n", type, gsttype->mime);

  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_NULL);
  gst_bin_remove(GST_BIN(pipeline), GST_ELEMENT(sink));

  gst_pad_disconnect(gst_element_get_pad(src,"src"),
                  gst_element_get_pad(GST_ELEMENT(sink),"sink"));
   
  if (strstr(gsttype->mime, "mpeg1-system")) {
    parse = gst_elementfactory_make("mpeg1parse","mpeg1_system_parse");
    gtk_signal_connect(GTK_OBJECT(parse),"new_pad",
                       GTK_SIGNAL_FUNC(mpeg1_new_pad_created),pipeline);
    gtk_signal_connect(GTK_OBJECT(show),"frame_displayed",
                       GTK_SIGNAL_FUNC(frame_displayed),NULL);
  }
  else if (strstr(gsttype->mime, "mpeg2-system")) {
    parse = gst_elementfactory_make("mpeg2parse","mpeg2_system_parse");
    gtk_signal_connect(GTK_OBJECT(parse),"new_pad",
                       GTK_SIGNAL_FUNC(mpeg2_new_pad_created),pipeline);
    gtk_signal_connect(GTK_OBJECT(show),"frame_displayed",
                       GTK_SIGNAL_FUNC(frame_displayed),NULL);
  }
  else if (strstr(gsttype->mime, "avi")) {
    parse = gst_elementfactory_make("parseavi","parse");
    gtk_signal_connect(GTK_OBJECT(parse),"new_pad",
                       GTK_SIGNAL_FUNC(avi_new_pad_created),pipeline);
  }
  else if (strstr(gsttype->mime, "mpeg1")) {
    mpeg1_setup_video_thread(gst_element_get_pad(src,"src"), video_render_queue, GST_ELEMENT(pipeline));
    gst_clock_reset(gst_clock_get_system());
    gtk_signal_connect(GTK_OBJECT(show),"frame_displayed",
                       GTK_SIGNAL_FUNC(frame_displayed),NULL);
  }
  else if (strstr(gsttype->mime, "mp3")) {
    mpeg1_setup_audio_thread(gst_element_get_pad(src,"src"), audio_render_queue, GST_ELEMENT(pipeline));
    gst_clock_reset(gst_clock_get_system());
  }
  else {
    g_print("unknown media type\n");
    exit(0);
  }

  if (parse) {
    gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(parse));
    gst_pad_connect(gst_element_get_pad(src,"src"),
                  gst_element_get_pad(parse,"sink"));
    gtk_signal_connect(GTK_OBJECT(parse),"pads_created",
                       GTK_SIGNAL_FUNC(gstplay_parse_pads_created),pipeline);
  }
  gtk_object_set(GTK_OBJECT(src),"offset",0,NULL);

  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(video_render_thread));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(audio_render_thread));

  g_print("setting to READY state\n");
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_READY);
  g_print("setting to PLAYING state\n");
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PLAYING);
  g_print("set to PLAYING state\n");

}

gint start_from_file(guchar *filename) 
{
  src = gst_elementfactory_make("disksrc","disk_src");
  g_return_val_if_fail(src != NULL, -1);
  g_print("should be using file '%s'\n",filename);
  gtk_object_set(GTK_OBJECT(src),"location",filename,NULL);

  typefind = gst_elementfactory_make("typefind","typefind");
  g_return_val_if_fail(typefind != NULL, -1);

  gtk_signal_connect(GTK_OBJECT(typefind),"have_type",
                       GTK_SIGNAL_FUNC(have_type),NULL);

  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(src));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(typefind));

  gtk_signal_connect(GTK_OBJECT(src),"eos",
                       GTK_SIGNAL_FUNC(eof),NULL);

  gst_pad_connect(gst_element_get_pad(src,"src"),
                  gst_element_get_pad(typefind,"sink"));

  g_print("setting to READY state\n");

  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_READY);

  state = GSTPLAY_STOPPED;

  change_state(GSTPLAY_PLAYING);

  return 0;
}

static GtkTargetEntry target_table[] = {
   { "text/plain", 0, 0 }
};

int
main (int argc, char *argv[])
{
  GladeXML *xml;
  GtkWidget *slider, *gstplay;

  bindtextdomain (PACKAGE, PACKAGE_LOCALE_DIR);
  textdomain (PACKAGE);

  g_thread_init(NULL);
  gtk_init(&argc,&argv);
  gnome_init ("gstreamer", VERSION, argc, argv);
  glade_init();
  glade_gnome_init();
  gst_init(&argc,&argv);
  //gst_plugin_load_all();

  g_print("using %s\n", DATADIR"gstplay.glade");
  /* load the interface */
  xml = glade_xml_new(DATADIR "gstplay.glade", "gstplay");
  /* connect the signals in the interface */

  status_area = glade_xml_get_widget(xml, "status_area");
  slider = glade_xml_get_widget(xml, "slider");
  {
    GtkArg arg;
    GtkRange *range;
    arg.name = "adjustment";
    gtk_object_getv(GTK_OBJECT(slider),1,&arg);
    range = GTK_RANGE(GTK_VALUE_POINTER(arg));
    adjustment = gtk_range_get_adjustment(range);
    gtk_signal_connect(GTK_OBJECT(adjustment),"value_changed",
                    GTK_SIGNAL_FUNC(on_hscale1_value_changed),NULL);
  }
  play_button = glade_xml_get_widget(xml, "toggle_play");
  pause_button = glade_xml_get_widget(xml, "toggle_pause");
  stop_button = glade_xml_get_widget(xml, "toggle_stop");

  gstplay = glade_xml_get_widget(xml, "gstplay");
  gtk_drag_dest_set (gstplay,
                     GTK_DEST_DEFAULT_ALL,
	             target_table, 1,
	             GDK_ACTION_COPY);
  gtk_signal_connect (GTK_OBJECT (gstplay), "drag_data_received",
	              GTK_SIGNAL_FUNC (target_drag_data_received),
	              NULL);

  gst_plugin_load("videosink");

  g_snprintf(statusline, 200, "seeking"); 

  pipeline = gst_pipeline_new("main_pipeline");
  g_return_val_if_fail(pipeline != NULL, -1);

  video_render_thread = gst_thread_new("video_render_thread");
  g_return_val_if_fail(video_render_thread != NULL, -1);
  show = gst_elementfactory_make("videosink","show");
  g_return_val_if_fail(show != NULL, -1);
  gtk_object_set(GTK_OBJECT(show),"xv_enabled",FALSE,NULL);

  gnome_dock_set_client_area(GNOME_DOCK(glade_xml_get_widget(xml, "dock1")),
		  gst_util_get_widget_arg(GTK_OBJECT(show),"widget"));
  gst_bin_add(GST_BIN(video_render_thread),GST_ELEMENT(show));

  glade_xml_signal_autoconnect(xml);

  video_render_queue = gst_elementfactory_make("queue","video_render_queue");
  gtk_object_set(GTK_OBJECT(video_render_queue),"max_level",BUFFER,NULL);
  gst_pad_connect(gst_element_get_pad(video_render_queue,"src"),
                  gst_element_get_pad(show,"sink"));
  gtk_object_set(GTK_OBJECT(video_render_thread),"create_thread",TRUE,NULL);


  audio_render_thread = gst_thread_new("audio_render_thread");
  g_return_val_if_fail(audio_render_thread != NULL, -1);
  audio_play = gst_elementfactory_make("audiosink","play_audio");
  gst_bin_add(GST_BIN(audio_render_thread),GST_ELEMENT(audio_play));

  audio_render_queue = gst_elementfactory_make("queue","audio_render_queue");
  gtk_object_set(GTK_OBJECT(audio_render_queue),"max_level",BUFFER,NULL);
  gst_pad_connect(gst_element_get_pad(audio_render_queue,"src"),
                  gst_element_get_pad(audio_play,"sink"));
  gtk_object_set(GTK_OBJECT(audio_render_thread),"create_thread",TRUE,NULL);

  if (argc > 1) {
    gint ret;
    
    ret = start_from_file(argv[1]);
    if (ret < 0) exit(ret);
  }

  gdk_threads_enter();
  gtk_main();
  gdk_threads_leave();
  return 0;
}

