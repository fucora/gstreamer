/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstprops.h: Header for properties subsystem
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


#ifndef __GST_PROPS_H__
#define __GST_PROPS_H__

#include <gst/gstconfig.h>

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _GstProps GstProps;
extern GType _gst_props_type;

#define	GST_PROPS_TRACE_NAME "GstProps"

#define GST_TYPE_PROPS	(_gst_props_type)

typedef enum {
   GST_PROPS_END_TYPE = 0,

   GST_PROPS_INVALID_TYPE,

   GST_PROPS_INT_TYPE,
   GST_PROPS_FLOAT_TYPE,
   GST_PROPS_FOURCC_TYPE,
   GST_PROPS_BOOLEAN_TYPE,
   GST_PROPS_STRING_TYPE,

   GST_PROPS_VAR_TYPE,   /* after this marker start the variable properties */

   GST_PROPS_LIST_TYPE,
   GST_PROPS_GLIST_TYPE,
   GST_PROPS_FLOAT_RANGE_TYPE,
   GST_PROPS_INT_RANGE_TYPE,

   GST_PROPS_LAST_TYPE = GST_PROPS_END_TYPE + 16
} GstPropsType;

#define GST_MAKE_FOURCC(a,b,c,d) 	(guint32)((a)|(b)<<8|(c)<<16|(d)<<24)
#define GST_STR_FOURCC(f)		(guint32)(((f)[0])|((f)[1]<<8)|((f)[2]<<16)|((f)[3]<<24))

#define GST_FOURCC_FORMAT "%c%c%c%c"
#define GST_FOURCC_ARGS(fourcc) \
	((gchar) ((fourcc)     &0xff)), \
	((gchar) (((fourcc)>>8 )&0xff)), \
	((gchar) (((fourcc)>>16)&0xff)), \
	((gchar) (((fourcc)>>24)&0xff))

#ifdef G_HAVE_ISO_VARARGS
#  define GST_PROPS_LIST(...)	    GST_PROPS_LIST_TYPE,__VA_ARGS__,NULL
#elif defined(G_HAVE_GNUC_VARARGS)
#  define GST_PROPS_LIST(a...)	    GST_PROPS_LIST_TYPE,a,NULL
#endif

#define GST_PROPS_GLIST(a) 		GST_PROPS_GLIST_TYPE,(a)
#define GST_PROPS_INT(a) 		GST_PROPS_INT_TYPE,(a)
#define GST_PROPS_INT_RANGE(a,b) 	GST_PROPS_INT_RANGE_TYPE,(a),(b)
#define GST_PROPS_FLOAT(a) 		GST_PROPS_FLOAT_TYPE,((float)(a))
#define GST_PROPS_FLOAT_RANGE(a,b) 	GST_PROPS_FLOAT_RANGE_TYPE,((float)(a)),((float)(b))
#define GST_PROPS_FOURCC(a) 		GST_PROPS_FOURCC_TYPE,(a)
#define GST_PROPS_BOOLEAN(a) 		GST_PROPS_BOOLEAN_TYPE,(a)
#define GST_PROPS_STRING(a) 		GST_PROPS_STRING_TYPE,(a)

#define GST_PROPS_INT_POSITIVE		GST_PROPS_INT_RANGE(0,G_MAXINT)
#define GST_PROPS_INT_NEGATIVE		GST_PROPS_INT_RANGE(G_MININT,0)
#define GST_PROPS_INT_ANY		GST_PROPS_INT_RANGE(G_MININT,G_MAXINT)

/* propsentries are private */
typedef struct _GstPropsEntry GstPropsEntry;
extern GType _gst_props_entry_type;

#define	GST_PROPS_ENTRY_TRACE_NAME "GstPropsEntry"

#define GST_TYPE_PROPS_ENTRY	(_gst_props_entry_type)

typedef enum {
  GST_PROPS_FIXED        = (1 << 0),	/* props has no variable entries */
  GST_PROPS_FLOATING     = (1 << 1)	/* props is floating */
} GstPropsFlags;

#define GST_PROPS_FLAGS(props)            ((props)->flags)
#define GST_PROPS_FLAG_IS_SET(props,flag) (GST_PROPS_FLAGS (props) & flag)
#define GST_PROPS_FLAG_SET(props,flag)    (GST_PROPS_FLAGS (props) |= (flag))
#define GST_PROPS_FLAG_UNSET(props,flag)  (GST_PROPS_FLAGS (props) &= ~(flag))

#define GST_PROPS_REFCOUNT(props)         ((props)->refcount)
#define GST_PROPS_PROPERTIES(props)       ((props)->properties)

#define GST_PROPS_IS_FIXED(props)         (GST_PROPS_FLAGS (props) & GST_PROPS_FIXED)
#define GST_PROPS_IS_FLOATING(props)      (GST_PROPS_FLAGS (props) & GST_PROPS_FLOATING)

struct _GstProps {
  gint   refcount;
  gint   flags;

  GList *properties;		/* real property entries for this property */
};

/* initialize the subsystem */
void 			_gst_props_initialize		(void);

/* creating new properties */
GType			gst_props_get_type		(void);
GstProps*		gst_props_new			(const gchar *firstname, ...);
GstProps*		gst_props_newv			(const gchar *firstname, va_list var_args);
GstProps*		gst_props_empty_new		(void);

/* replace pointer to props, doing proper refcounting */
void                    gst_props_replace               (GstProps **oldprops, GstProps *newprops);
void                    gst_props_replace_sink          (GstProps **oldprops, GstProps *newprops);

/* lifecycle management */
GstProps*            	gst_props_unref			(GstProps *props);
GstProps*            	gst_props_ref			(GstProps *props);
void            	gst_props_sink			(GstProps *props);
void            	gst_props_destroy		(GstProps *props);

/* dump property debug info to the log */
void            	gst_props_debug 		(GstProps *props);

/* copy */
GstProps*       	gst_props_copy                  (GstProps *props);
GstProps*       	gst_props_copy_on_write         (GstProps *props);

/* check if fromprops is subset of toprops */
gboolean 		gst_props_check_compatibility 	(GstProps *fromprops, GstProps *toprops);

/* operation on props */
GstProps*		gst_props_merge			(GstProps *props, GstProps *tomerge);
GstProps* 		gst_props_intersect	 	(GstProps *props1, GstProps *props2);
GList* 			gst_props_normalize	 	(GstProps *props);

/* modify entries */
GstProps*		gst_props_set			(GstProps *props, const gchar *name, ...);
gboolean		gst_props_get			(GstProps *props, gchar *first_name, ...);
gboolean		gst_props_get_safe		(GstProps *props, gchar *first_name, ...);

/* query entries */
gboolean 		gst_props_has_property		(GstProps *props, const gchar *name);
gboolean 		gst_props_has_property_typed 	(GstProps *props, const gchar *name, GstPropsType type);
gboolean 		gst_props_has_fixed_property	(GstProps *props, const gchar *name);

/* add/get entries */
const GstPropsEntry* 	gst_props_get_entry		(GstProps *props, const gchar *name);
void			gst_props_add_entry		(GstProps *props, GstPropsEntry *entry);
void		 	gst_props_remove_entry		(GstProps *props, GstPropsEntry *entry);
void		 	gst_props_remove_entry_by_name	(GstProps *props, const gchar *name);

/* working with props entries */
GType			gst_props_entry_get_type	(void);
GstPropsEntry*		gst_props_entry_new		(const gchar *name, ...);

void            	gst_props_entry_destroy		(GstPropsEntry *entry);
GstPropsEntry*       	gst_props_entry_copy		(const GstPropsEntry *entry);
GstPropsType		gst_props_entry_get_props_type	(const GstPropsEntry *entry);
const gchar*		gst_props_entry_get_name	(const GstPropsEntry *entry);
gboolean		gst_props_entry_is_fixed	(const GstPropsEntry *entry);

gboolean		gst_props_entry_get		(const GstPropsEntry *entry, ...);

gboolean		gst_props_entry_get_int		(const GstPropsEntry *entry, gint *val);
gboolean		gst_props_entry_get_float	(const GstPropsEntry *entry, gfloat *val);
gboolean		gst_props_entry_get_fourcc_int	(const GstPropsEntry *entry, guint32 *val);
gboolean		gst_props_entry_get_boolean	(const GstPropsEntry *entry, gboolean *val);
gboolean		gst_props_entry_get_string	(const GstPropsEntry *entry, const gchar **val);
gboolean		gst_props_entry_get_int_range	(const GstPropsEntry *entry, gint *min, gint *max);
gboolean		gst_props_entry_get_float_range	(const GstPropsEntry *entry, gfloat *min, gfloat *max);
gboolean		gst_props_entry_get_list	(const GstPropsEntry *entry, const GList **val);

/* for debugging purposes */
gchar *			gst_props_to_string		(GstProps *props);
GstProps *		gst_props_from_string		(gchar *str);

#ifndef GST_DISABLE_LOADSAVE
xmlNodePtr 		gst_props_save_thyself 		(GstProps *props, xmlNodePtr parent);
GstProps* 		gst_props_load_thyself 		(xmlNodePtr parent);
#endif


G_END_DECLS

#endif /* __GST_PROPS_H__ */
