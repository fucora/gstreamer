/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#include <gnome.h>
#include <gst/gst.h>

#include "gsteditor.h"
#include "gsteditorcreate.h"

/* signals and args */
enum {
  LAST_SIGNAL
};

enum {
  ARG_0,
};

static void 	gst_editor_bin_class_init	(GstEditorBinClass *klass);
static void 	gst_editor_bin_init		(GstEditorBin *bin);

//static void gst_editor_bin_set_arg(GtkObject *object,GtkArg *arg,guint id);
//static void gst_editor_bin_get_arg(GtkObject *object,GtkArg *arg,guint id);

static void 	gst_editor_bin_realize 		(GstEditorElement *bin);
static void 	gst_editor_bin_repack 		(GstEditorBin *bin);

static void 	gst_editor_bin_object_added 	(GstEditorBin *editorbin, GstObject *bin, GstObject *child);

static gint 	gst_editor_bin_event		(GnomeCanvasItem *item,
                                 		 GdkEvent *event,
                                 		 GstEditorElement *element);
static gint 	gst_editor_bin_button_event	(GnomeCanvasItem *item,
                                        	 GdkEvent *event,
                                        	 GstEditorElement *element);
void 		gst_editor_bin_connection_drag 	(GstEditorBin *bin,
                                    		 gdouble wx,gdouble wy);

static GstEditorElementClass *parent_class = NULL;

GtkType 
gst_editor_bin_get_type (void) 
{
  static GtkType bin_type = 0;

  if (!bin_type) {
    static const GtkTypeInfo bin_info = {
      "GstEditorBin",
      sizeof(GstEditorBin),
      sizeof(GstEditorBinClass),
      (GtkClassInitFunc)gst_editor_bin_class_init,
      (GtkObjectInitFunc)gst_editor_bin_init,
      NULL,
      NULL,
      (GtkClassInitFunc)NULL,
    };
    bin_type = gtk_type_unique(gst_editor_element_get_type(),&bin_info);
  }
  return bin_type;
}

static void 
gst_editor_bin_class_init (GstEditorBinClass *klass) 
{
  GstEditorElementClass *element_class;

  element_class = (GstEditorElementClass*)klass;

  parent_class = gtk_type_class(gst_editor_element_get_type());

  element_class->event = gst_editor_bin_event;
  element_class->button_event = gst_editor_bin_button_event;
  element_class->realize = gst_editor_bin_realize;
}

static void 
gst_editor_bin_init (GstEditorBin *bin) 
{
  GstEditorElement *element = GST_EDITOR_ELEMENT(bin);

  element->insidewidth = 200;
  element->insideheight = 100;
}

GstEditorBin*
gst_editor_bin_new (GstBin *bin, const gchar *first_arg_name,...) 
{
  GstEditorBin *editorbin;
  GList *children;
  va_list args;
  gdouble xpos;

  g_return_val_if_fail(bin != NULL, NULL);
  g_return_val_if_fail(GST_IS_BIN(bin), NULL);

  editorbin = GST_EDITOR_BIN(gtk_type_new(GST_TYPE_EDITOR_BIN));
  GST_EDITOR_ELEMENT(editorbin)->element = GST_ELEMENT(bin);
  GST_EDITOR_SET_OBJECT(bin, editorbin);

  gtk_signal_connect_object (GTK_OBJECT (bin), "object_added", 
		             gst_editor_bin_object_added, GTK_OBJECT (editorbin));

  va_start(args,first_arg_name);
  gst_editor_element_construct(GST_EDITOR_ELEMENT(editorbin), first_arg_name,args);
  va_end(args);

  children = gst_bin_get_list (bin);
  xpos = 50.0;

  while (children) {
    GstElement *child = (GstElement *)children->data;

    if (GST_IS_BIN (child)) {
      GstEditorBin *childbin = gst_editor_bin_new (GST_BIN (child), "x", xpos+60.0, "y", 80.0, NULL);
      
      gst_editor_bin_add (editorbin, GST_EDITOR_ELEMENT (childbin));
      xpos += 120.0;
    }
    else {
      GstEditorElement *childelement = gst_editor_element_new (child, "x", xpos, "y", 50.0, NULL);
      
      gst_editor_bin_add (editorbin, childelement);
    }
	
    xpos += 100.0;
    children = g_list_next (children);
  }

  gtk_object_set (GTK_OBJECT (editorbin), "width", xpos-50.0, NULL);

  return editorbin;
}

static void
gst_editor_bin_realize (GstEditorElement *element)
{
  GList *children;
  GstEditorBin *bin = GST_EDITOR_BIN(element);

  children = bin->elements;

  if (GST_EDITOR_ELEMENT_CLASS(parent_class)->realize) {
    GST_EDITOR_ELEMENT_CLASS(parent_class)->realize(GST_EDITOR_ELEMENT(bin));
  }

  while (children) {
    GstEditorElement *child = (GstEditorElement *)children->data;
    GstEditorElementClass *elementclass;

    elementclass = GST_EDITOR_ELEMENT_CLASS(GTK_OBJECT(child)->klass);

    if (elementclass->realize)
      (elementclass->realize) (child);

    children = g_list_next (children);
  }

  gst_editor_bin_repack (bin);
}

static void
gst_editor_bin_repack (GstEditorBin *bin)
{
}

static gint 
gst_editor_bin_event(GnomeCanvasItem *item,
                     GdkEvent *event,
                     GstEditorElement *element) 
{
  GstEditorBin *bin = GST_EDITOR_BIN(element);

//  g_print("bin got %d event at %.2fx%.2f\n",event->type,
//          event->button.x,event->button.y);

  switch (event->type) {
    case GDK_BUTTON_RELEASE:
      if (bin->connecting) {
//        g_print("bin got release event during drag\n");
        gnome_canvas_item_ungrab(
          GNOME_CANVAS_ITEM(element->group),
          event->button.time);
        if (bin->connection->topad)
          gst_editor_connection_connect(bin->connection);
        else {
          bin->connection->frompad->connection = NULL;
          g_list_remove(bin->connections,bin->connection);
          gtk_object_destroy(GTK_OBJECT(bin->connection));
          bin->connection = NULL;
        }
        bin->connecting = FALSE;
//g_print("in bin, setting inchild for button release\n");
        //element->canvas->inchild = TRUE;
        return TRUE;
      }
      break;
    case GDK_MOTION_NOTIFY:
      if (bin->connecting) {
        gdouble x,y;
        x = event->button.x;y = event->button.y;
//        g_print("bin has motion during connection draw at %.2fx%.2f\n",
//                x,y);
        gst_editor_bin_connection_drag(bin,x,y);
        return TRUE;
      }
      break;
    default:
      break;
  }

  if (GST_EDITOR_ELEMENT_CLASS(parent_class)->event)
    return (*GST_EDITOR_ELEMENT_CLASS(parent_class)->event)(item,event,element);
 
  return TRUE;
}


static gint 
gst_editor_bin_button_event(GnomeCanvasItem *item,
                            GdkEvent *event,
                            GstEditorElement *element) 
{
  GstEditorBin *bin = GST_EDITOR_BIN(element);
  GstEditorElement *newelement;
  GdkEventButton *buttonevent;

//  g_print("bin got button event\n");

  if (event->type != GDK_BUTTON_RELEASE) return FALSE;

  buttonevent = (GdkEventButton *) event;

  if (buttonevent->button != 1) return FALSE;

  gnome_canvas_item_w2i(item,&event->button.x,&event->button.y);
//  g_print("calling gst_editor_create_item(,%.2f,%.2f)\n",
//          event->button.x,event->button.y);
  newelement = gst_editor_create_item(event->button.x,event->button.y);
  if (newelement != NULL) {
    GstEditorElementClass *elementclass;

    gst_editor_bin_add (bin, newelement);

    elementclass = GST_EDITOR_ELEMENT_CLASS(GTK_OBJECT(newelement)->klass);
    if (elementclass->realize)
      (elementclass->realize)(newelement);

    return TRUE;
  }
  return FALSE;
}


void 
gst_editor_bin_start_banding (GstEditorBin *bin,GstEditorPad *pad) 
{
  GdkCursor *cursor;

//  g_print("starting to band\n");

  g_return_if_fail(GST_IS_EDITOR_PAD(pad));

  bin->connection = gst_editor_connection_new (GST_EDITOR_ELEMENT (bin), pad);
  bin->connections = g_list_prepend (bin->connections, bin->connection);
  
  cursor = gdk_cursor_new (GDK_SB_RIGHT_ARROW);
  gnome_canvas_item_grab(
    GNOME_CANVAS_ITEM(GST_EDITOR_ELEMENT(bin)->group),
    GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK,
    cursor,GDK_CURRENT_TIME);

  bin->connecting = TRUE;
}


void 
gst_editor_bin_connection_drag (GstEditorBin *bin,
                                gdouble wx,gdouble wy) 
{
  GstEditorElement *element;
  gdouble bx,by;
  GnomeCanvasItem *underitem, *under = NULL;
  GstEditorPad *destpad;

  element = GST_EDITOR_ELEMENT(bin);

  bx = wx;by = wy;
  gnome_canvas_item_w2i(GNOME_CANVAS_ITEM(GST_EDITOR_ELEMENT(bin)->group),&bx,&by);

  // first see if we're on top of an interesting pad
  underitem = gnome_canvas_get_item_at(
    &GST_EDITOR_ELEMENT(bin)->canvas->canvas,wx,wy);
  if (underitem != NULL)
    under = GST_EDITOR_GET_OBJECT(underitem);
  if ((under != NULL) && GST_IS_EDITOR_PAD(under)) {
    destpad = GST_EDITOR_PAD(under);
    if (destpad != bin->connection->frompad)
      gst_editor_connection_set_endpad(bin->connection,destpad);
  } else {
    gst_editor_connection_set_endpoint(bin->connection,bx,by);
  }

/* This code is a nightmare, it'll be fixed in the next minor version
  if (
      ((bx < element->sinkwidth) ||
       (bx > (element->width - element->srcwidth))) &&
      ((by > element->titleheight) &&
       (by < (element->height - element->stateheight)))
     ) {
    if (!bin->inpadregion) {
      GstEditorPad *ghostpad;
      g_print("I'd be creating a ghost pad right about now...\n");
      gst_element_add_ghost_pad(
        GST_EDITOR_ELEMENT(bin)->element,
        bin->connection->frompad->pad);
      ghostpad = gst_editor_element_add_pad(GST_EDITOR_ELEMENT(bin),
        bin->connection->frompad->pad);
      gst_editor_connection_set_endpad(bin->connection,ghostpad);
      gtk_object_set(GTK_OBJECT(bin->connection),"ghost",TRUE,NULL);
      bin->inpadregion = TRUE;
    } else {
      g_print("I'd be moving the ghost pad around now...\n");
    }
  } else {
    if (bin->inpadregion) {
      g_print("I'd be removing the ghost pad now...\n");
      bin->inpadregion = FALSE;
    }
  }
*/
}

static void 
gst_editor_bin_object_added (GstEditorBin *editorbin, GstObject *bin, GstObject *child) 
{
  g_print ("gsteditorbin: object added\n");
}


void 
gst_editor_bin_add (GstEditorBin *bin, GstEditorElement *element) 
{
  /* set the element's parent */
  element->parent = bin;

  /* set the canvas */
  element->canvas = GST_EDITOR_ELEMENT(bin)->canvas;

  /* add element to list of bin's children */
  bin->elements = g_list_prepend(bin->elements,element);

  /* add the real element to the real bin */
  if (!gst_object_get_parent (GST_OBJECT (element->element))) {
    gst_bin_add(GST_BIN(GST_EDITOR_ELEMENT(bin)->element),element->element);
  }
}
