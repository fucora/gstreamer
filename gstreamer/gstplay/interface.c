/*
 * DO NOT EDIT THIS FILE - it is generated by Glade.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#include <gnome.h>

#include "callbacks.h"
#include "interface.h"
#include "support.h"

static GnomeUIInfo file1_menu_uiinfo[] =
{
  {
    GNOME_APP_UI_ITEM, N_("_Open..."),
    NULL,
    on_open1_activate, NULL, NULL,
    GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_OPEN,
    0, 0, NULL
  },
  GNOMEUIINFO_SEPARATOR,
  {
    GNOME_APP_UI_ITEM, N_("_Exit"),
    NULL,
    on_close1_activate, NULL, NULL,
    GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_EXIT,
    0, 0, NULL
  },
  GNOMEUIINFO_END
};

static GnomeUIInfo view2_menu_uiinfo[] =
{
  {
    GNOME_APP_UI_ITEM, N_("_Media..."),
    NULL,
    on_media1_activate, NULL, NULL,
    GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_PROP,
    0, 0, NULL
  },
  GNOMEUIINFO_END
};

static GnomeUIInfo play1_menu_uiinfo[] =
{
  {
    GNOME_APP_UI_ITEM, N_("_Play"),
    NULL,
    on_play2_activate, NULL, NULL,
    GNOME_APP_PIXMAP_FILENAME, "pixmaps/play.xpm",
    0, 0, NULL
  },
  {
    GNOME_APP_UI_ITEM, N_("P_ause"),
    NULL,
    on_pause1_activate, NULL, NULL,
    GNOME_APP_PIXMAP_FILENAME, "pixmaps/pause.xpm",
    0, 0, NULL
  },
  {
    GNOME_APP_UI_ITEM, N_("_Stop"),
    NULL,
    on_stop1_activate, NULL, NULL,
    GNOME_APP_PIXMAP_FILENAME, "pixmaps/stop.xpm",
    0, 0, NULL
  },
  GNOMEUIINFO_END
};

static GnomeUIInfo help1_menu_uiinfo[] =
{
  {
    GNOME_APP_UI_ITEM, N_("_About"),
    NULL,
    on_about1_activate, NULL, NULL,
    GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_ABOUT,
    0, 0, NULL
  },
  GNOMEUIINFO_END
};

static GnomeUIInfo menubar1_uiinfo[] =
{
  {
    GNOME_APP_UI_SUBTREE, N_("_File"),
    NULL,
    file1_menu_uiinfo, NULL, NULL,
    GNOME_APP_PIXMAP_NONE, NULL,
    0, 0, NULL
  },
  {
    GNOME_APP_UI_SUBTREE, N_("_View"),
    NULL,
    view2_menu_uiinfo, NULL, NULL,
    GNOME_APP_PIXMAP_NONE, NULL,
    0, 0, NULL
  },
  {
    GNOME_APP_UI_SUBTREE, N_("_Play"),
    NULL,
    play1_menu_uiinfo, NULL, NULL,
    GNOME_APP_PIXMAP_NONE, NULL,
    0, 0, NULL
  },
  {
    GNOME_APP_UI_SUBTREE, N_("_Help"),
    NULL,
    help1_menu_uiinfo, NULL, NULL,
    GNOME_APP_PIXMAP_NONE, NULL,
    0, 0, NULL
  },
  GNOMEUIINFO_END
};

GtkWidget*
create_window1 (GtkWidget *video_element)
{
  GtkWidget *window1;
  GtkWidget *vbox1;
  GtkWidget *handlebox2;
  GtkWidget *menubar1;
  GtkWidget *vbox2;
  GtkWidget *hscale1;
  GtkWidget *handlebox1;
  GtkWidget *toolbar1;
  GtkWidget *tmp_toolbar_icon;
  GtkWidget *button6;
  GtkWidget *button7;
  GtkWidget *button8;
  GtkWidget *vseparator1;
  GtkObject *adjustment;

  window1 = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_object_set_data (GTK_OBJECT (window1), "window1", window1);
  gtk_window_set_title (GTK_WINDOW (window1), _("GStreamer Media Player"));
  gtk_window_set_policy(GTK_WINDOW(window1), TRUE, TRUE, FALSE);
  
  vbox1 = gtk_vbox_new (FALSE, 0);
  gtk_widget_ref (vbox1);
  gtk_object_set_data_full (GTK_OBJECT (window1), "vbox1", vbox1,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (vbox1);
  gtk_container_add (GTK_CONTAINER (window1), vbox1);

  handlebox2 = gtk_handle_box_new ();
  gtk_widget_ref (handlebox2);
  gtk_object_set_data_full (GTK_OBJECT (window1), "handlebox2", handlebox2,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (handlebox2);
  gtk_box_pack_start (GTK_BOX (vbox1), handlebox2, FALSE, FALSE, 0);

  menubar1 = gtk_menu_bar_new ();
  gtk_widget_ref (menubar1);
  gtk_object_set_data_full (GTK_OBJECT (window1), "menubar1", menubar1,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (menubar1);
  gtk_container_add (GTK_CONTAINER (handlebox2), menubar1);
  gnome_app_fill_menu (GTK_MENU_SHELL (menubar1), menubar1_uiinfo,
                       NULL, FALSE, 0);

  gtk_widget_ref (menubar1_uiinfo[0].widget);
  gtk_object_set_data_full (GTK_OBJECT (window1), "file1",
                            menubar1_uiinfo[0].widget,
                            (GtkDestroyNotify) gtk_widget_unref);

  gtk_widget_ref (file1_menu_uiinfo[0].widget);
  gtk_object_set_data_full (GTK_OBJECT (window1), "open1",
                            file1_menu_uiinfo[0].widget,
                            (GtkDestroyNotify) gtk_widget_unref);

  gtk_widget_ref (file1_menu_uiinfo[1].widget);
  gtk_object_set_data_full (GTK_OBJECT (window1), "separator1",
                            file1_menu_uiinfo[1].widget,
                            (GtkDestroyNotify) gtk_widget_unref);

  gtk_widget_ref (file1_menu_uiinfo[2].widget);
  gtk_object_set_data_full (GTK_OBJECT (window1), "close1",
                            file1_menu_uiinfo[2].widget,
                            (GtkDestroyNotify) gtk_widget_unref);

  gtk_widget_ref (menubar1_uiinfo[1].widget);
  gtk_object_set_data_full (GTK_OBJECT (window1), "view2",
                            menubar1_uiinfo[1].widget,
                            (GtkDestroyNotify) gtk_widget_unref);

  gtk_widget_ref (view2_menu_uiinfo[0].widget);
  gtk_object_set_data_full (GTK_OBJECT (window1), "media1",
                            view2_menu_uiinfo[0].widget,
                            (GtkDestroyNotify) gtk_widget_unref);

  gtk_widget_ref (menubar1_uiinfo[2].widget);
  gtk_object_set_data_full (GTK_OBJECT (window1), "play1",
                            menubar1_uiinfo[2].widget,
                            (GtkDestroyNotify) gtk_widget_unref);

  gtk_widget_ref (play1_menu_uiinfo[0].widget);
  gtk_object_set_data_full (GTK_OBJECT (window1), "play2",
                            play1_menu_uiinfo[0].widget,
                            (GtkDestroyNotify) gtk_widget_unref);

  gtk_widget_ref (play1_menu_uiinfo[1].widget);
  gtk_object_set_data_full (GTK_OBJECT (window1), "pause1",
                            play1_menu_uiinfo[1].widget,
                            (GtkDestroyNotify) gtk_widget_unref);

  gtk_widget_ref (play1_menu_uiinfo[2].widget);
  gtk_object_set_data_full (GTK_OBJECT (window1), "stop1",
                            play1_menu_uiinfo[2].widget,
                            (GtkDestroyNotify) gtk_widget_unref);

  gtk_widget_ref (menubar1_uiinfo[3].widget);
  gtk_object_set_data_full (GTK_OBJECT (window1), "help1",
                            menubar1_uiinfo[3].widget,
                            (GtkDestroyNotify) gtk_widget_unref);

  gtk_widget_ref (help1_menu_uiinfo[0].widget);
  gtk_object_set_data_full (GTK_OBJECT (window1), "about1",
                            help1_menu_uiinfo[0].widget,
                            (GtkDestroyNotify) gtk_widget_unref);

  gtk_box_pack_start (GTK_BOX (vbox1), video_element, TRUE, TRUE, 0);

  vbox2 = gtk_vbox_new (FALSE, 0);
  gtk_widget_ref (vbox2);
  gtk_object_set_data_full (GTK_OBJECT (window1), "vbox2", vbox2,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (vbox2);
  gtk_box_pack_start (GTK_BOX (vbox1), vbox2, FALSE, TRUE, 0);

  adjustment = gtk_adjustment_new (0, 0, 100, 1, 10, 10);
  hscale1 = gtk_hscale_new (GTK_ADJUSTMENT (adjustment));
  gtk_widget_ref (hscale1);
  gtk_object_set_data_full (GTK_OBJECT (window1), "hscale1", hscale1,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (hscale1);
  gtk_box_pack_start (GTK_BOX (vbox2), hscale1, TRUE, TRUE, 3);
  gtk_scale_set_draw_value (GTK_SCALE (hscale1), FALSE);
  gtk_scale_set_value_pos (GTK_SCALE (hscale1), GTK_POS_LEFT);

  gtk_signal_connect (GTK_OBJECT (adjustment), "value_changed",
                         GTK_SIGNAL_FUNC (on_hscale1_value_changed),
	                        NULL);

  handlebox1 = gtk_handle_box_new ();
  gtk_widget_ref (handlebox1);
  gtk_object_set_data_full (GTK_OBJECT (window1), "handlebox1", handlebox1,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (handlebox1);
  gtk_box_pack_start (GTK_BOX (vbox2), handlebox1, TRUE, TRUE, 1);
  gtk_handle_box_set_shadow_type (GTK_HANDLE_BOX (handlebox1), GTK_SHADOW_NONE);
  gtk_handle_box_set_snap_edge (GTK_HANDLE_BOX (handlebox1), GTK_POS_BOTTOM);

  toolbar1 = gtk_toolbar_new (GTK_ORIENTATION_HORIZONTAL, GTK_TOOLBAR_ICONS);
  gtk_widget_ref (toolbar1);
  gtk_object_set_data_full (GTK_OBJECT (window1), "toolbar1", toolbar1,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (toolbar1);
  gtk_container_add (GTK_CONTAINER (handlebox1), toolbar1);
  gtk_container_set_border_width (GTK_CONTAINER (toolbar1), 3);
  gtk_toolbar_set_space_size (GTK_TOOLBAR (toolbar1), 0);
  gtk_toolbar_set_button_relief (GTK_TOOLBAR (toolbar1), GTK_RELIEF_NONE);

  tmp_toolbar_icon = create_pixmap (window1, "pixmaps/play.xpm", TRUE);
  button6 = gtk_toolbar_append_element (GTK_TOOLBAR (toolbar1),
                                GTK_TOOLBAR_CHILD_BUTTON,
                                NULL,
                                _("button6"),
                                NULL, NULL,
                                tmp_toolbar_icon, NULL, NULL);
  gtk_widget_ref (button6);
  gtk_object_set_data_full (GTK_OBJECT (window1), "button6", button6,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (button6);

  tmp_toolbar_icon = create_pixmap (window1, "pixmaps/pause.xpm", TRUE);
  button7 = gtk_toolbar_append_element (GTK_TOOLBAR (toolbar1),
                                GTK_TOOLBAR_CHILD_BUTTON,
                                NULL,
                                _("button7"),
                                NULL, NULL,
                                tmp_toolbar_icon, NULL, NULL);
  gtk_widget_ref (button7);
  gtk_object_set_data_full (GTK_OBJECT (window1), "button7", button7,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (button7);

  tmp_toolbar_icon = create_pixmap (window1, "pixmaps/stop.xpm", TRUE);
  button8 = gtk_toolbar_append_element (GTK_TOOLBAR (toolbar1),
                                GTK_TOOLBAR_CHILD_BUTTON,
                                NULL,
                                _("button8"),
                                NULL, NULL,
                                tmp_toolbar_icon, NULL, NULL);
  gtk_widget_ref (button8);
  gtk_object_set_data_full (GTK_OBJECT (window1), "button8", button8,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (button8);

  vseparator1 = gtk_vseparator_new ();
  gtk_widget_ref (vseparator1);
  gtk_object_set_data_full (GTK_OBJECT (window1), "vseparator1", vseparator1,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (vseparator1);
  gtk_toolbar_append_widget (GTK_TOOLBAR (toolbar1), vseparator1, NULL, NULL);
  gtk_widget_set_usize (vseparator1, 8, 21);


  return window1;
}

