/*
    Copyright (C) 2002 Andy Wingo <wingo@pobox.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public
    License along with this library; if not, write to the Free
    Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <stdlib.h>
#include "gstjack.h"


static GstBinClass *parent_class = NULL;

static void gst_jack_bin_init(GstJack *this);
static void gst_jack_bin_class_init(GstJackClass *klass);

static GstElementStateReturn gst_jack_bin_change_state(GstElement *element);

/* jack callbacks */
static int process (nframes_t nframes, void *arg);
static int buffer_size (nframes_t nframes, void *arg);
static int sample_rate (nframes_t nframes, void *arg);
static void shutdown (void *arg);


GType
gst_jack_bin_get_type (void) 
{
  static GType jack_bin_type = 0;

  if (!jack_bin_type) {
    static const GTypeInfo jack_bin_info = {
      sizeof(GstJackBinClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_jack_bin_class_init,
      NULL,
      NULL,
      sizeof(GstJackBin),
      0,
      (GInstanceInitFunc)gst_jack_bin_init,
    };
    jack_bin_type = g_type_register_static (GST_TYPE_BIN, "GstJackBin", &jack_bin_info, 0);
  }
  return jack_bin_type;
}

static void
gst_jack_bin_class_init(GstJackClass *klass)
{
    GObjectClass *object_class;
    GstElementClass *element_class;
    
    object_class = (GObjectClass *)klass;
    element_class = (GstElementClass *)klass;
    
    parent_class = g_type_class_ref(GST_TYPE_BIN);

    element_class->change_state = gst_jack_bin_change_state;
}

static void
gst_jack_bin_init(GstJack *this)
{
}

static GstElementStateReturn
gst_jack_bin_change_state (GstElement *element)
{
    GstJackBin *this;
    GList *l = NULL;
    GstJackPad *pad;
    
    g_return_val_if_fail (element != NULL, FALSE);
    this = GST_JACK_BIN (element);
    
    switch (GST_STATE_PENDING (element)) {
    case GST_STATE_NULL:
        g_message ("jack: NULL state");
        if (this->client) {
            g_message ("jack: closing client");
            jack_client_close (this->client);
        }
            
        break;
        
    case GST_STATE_READY:
        g_message ("jack: READY");
        if (!this->client) {
          if (!(this->client = jack_client_new ("gst-jack"))) {
            g_warning ("jack server not running?");
            return GST_STATE_FAILURE;
          }
                
          jack_set_process_callback (this->client, process, this);
          jack_set_buffer_size_callback (this->client, buffer_size, this);
          jack_set_sample_rate_callback (this->client, sample_rate, this);
          jack_on_shutdown (this->client, shutdown, this);
        }
        
        /* fixme: there are a *lot* of problems here */
        if (GST_FLAG_IS_SET (GST_OBJECT (this), GST_JACK_OPEN)) {
            l = this->src_pads;
            while (l) {
                g_message ("jack: unregistering pad %s:%s", GST_JACK_PAD (l)->name, GST_JACK_PAD (l)->peer_name);
                jack_port_unregister (this->client, GST_JACK_PAD (l)->port);
                l = g_list_next (l);
            }
            l = this->sink_pads;
            while (l) {
                g_message ("jack: unregistering pad %s:%s", GST_JACK_PAD (l)->name, GST_JACK_PAD (l)->peer_name);
                jack_port_unregister (this->client, GST_JACK_PAD (l)->port);
                l = g_list_next (l);
            }
            GST_FLAG_UNSET (GST_OBJECT (this), GST_JACK_OPEN);
        }
        break;
        
    case GST_STATE_PAUSED:
        g_message ("jack: PAUSED");
        
        if (!GST_FLAG_IS_SET (GST_OBJECT (this), GST_JACK_OPEN)) {
            l = this->src_pads;
            while (l) {
                pad = GST_JACK_PAD (l);
                g_message ("jack: registering pad %s:%s", pad->name, pad->peer_name);
                pad->port = jack_port_register (this->client, pad->name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
                g_message ("connecting gst jack port %s to jack port %s", jack_port_name (pad->port), pad->peer_name);
                if (jack_connect (this->client, jack_port_name (pad->port), pad->peer_name)) {
                    g_warning ("could not connect %s and %s", pad->peer_name, jack_port_name (pad->port));
                    return GST_STATE_FAILURE;
                }
                l = g_list_next (l);
            }
            l = this->sink_pads;
            while (l) {
                pad = GST_JACK_PAD (l);
                g_message ("jack: registering pad %s:%s", pad->name, pad->peer_name);
                pad->port = jack_port_register (this->client, pad->name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
                g_message ("connecting gst jack port %s to jack port %s", jack_port_name (pad->port), pad->peer_name);
                if (jack_connect (this->client, jack_port_name (pad->port), pad->peer_name)) {
                    g_warning ("could not connect %s and %s", pad->peer_name, jack_port_name (pad->port));
                    return GST_STATE_FAILURE;
                }
                l = g_list_next (l);
            }
            g_message ("jack: setting OPEN flag");
            GST_FLAG_SET (GST_OBJECT (this), GST_JACK_OPEN);
        }

        if (GST_FLAG_IS_SET (GST_OBJECT (this), GST_JACK_ACTIVE)) {
            g_message ("jack: deactivating client");
            jack_deactivate (this->client);
            GST_FLAG_UNSET (GST_OBJECT (this), GST_JACK_ACTIVE);
        }
        break;
    case GST_STATE_PLAYING:
        g_message ("jack: PLAYING");
        if (!GST_FLAG_IS_SET (GST_OBJECT (this), GST_JACK_ACTIVE)) {
            g_message ("jack: activating client");
            jack_activate (this->client);
            GST_FLAG_SET (GST_OBJECT (this), GST_JACK_ACTIVE);
        }
        break;
    }
    
    g_message ("jack: state change finished");
    
    if (GST_ELEMENT_CLASS (parent_class)->change_state)
        return GST_ELEMENT_CLASS (parent_class)->change_state (element);

    return GST_STATE_SUCCESS;
}

/* jack callbacks */

/* keep in mind that these run in another thread, mm-kay? */

static int
process (nframes_t nframes, void *arg)
{
    GstJackBin *bin = (GstJackBin*) arg;
    GstJackPad *pad;
    GList *l;
    
    g_assert (bin);
    
    g_message ("jack: process()");

    l = bin->src_pads;
    while (l) {
        pad = GST_JACK_PAD (l);
        pad->data = jack_port_get_buffer (pad->port, nframes);
        l = g_list_next (l);
    }
    
    l = bin->sink_pads;
    while (l) {
        pad = GST_JACK_PAD (l);
        pad->data = jack_port_get_buffer (pad->port, nframes);
        l = g_list_next (l);
    }
    
    bin->nframes = nframes;
    
    if (!gst_bin_iterate (GST_BIN_CAST (bin))) {
        g_warning ("bin failed to iterate");
        return -1;
    }
    
    /* that's all folks */
    
    return 0;      
}

static int
buffer_size (nframes_t nframes, void *arg)
{
    printf ("the maximum buffer size is now %lu\n", nframes);
    return 0;
}

static int
sample_rate (nframes_t nframes, void *arg)
{
    GstJackBin *bin = (GstJackBin*) arg;
    printf ("the sample rate is now %lu/sec\n", nframes);
    bin->rate = nframes;
    return 0;
}

static void
shutdown (void *arg)
{
/*    GstJackClient *client = (GstJackClient*) arg; */
    printf ("shutdown %p\n", arg);
/*    gst_element_set_state (GST_ELEMENT (client->manager), GST_STATE_READY); */
}

