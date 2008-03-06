/* GStreamer EBML I/O
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * ebml-read.c: read EBML data from file/stream
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "ebml-read.h"
#include "ebml-ids.h"

GST_DEBUG_CATEGORY_STATIC (ebmlread_debug);
#define GST_CAT_DEFAULT ebmlread_debug

static void gst_ebml_read_class_init (GstEbmlReadClass * klass);
static void gst_ebml_read_init (GstEbmlRead * ebml);
static GstStateChangeReturn gst_ebml_read_change_state (GstElement * element,
    GstStateChange transition);

/* convenience functions */
static gboolean gst_ebml_read_peek_bytes (GstEbmlRead * ebml, guint size,
    GstBuffer ** p_buf, guint8 ** bytes);
static gboolean gst_ebml_read_pull_bytes (GstEbmlRead * ebml, guint size,
    GstBuffer ** p_buf, guint8 ** bytes);


static GstElementClass *parent_class;   /* NULL */

GType
gst_ebml_read_get_type (void)
{
  static GType gst_ebml_read_type;      /* 0 */

  if (!gst_ebml_read_type) {
    static const GTypeInfo gst_ebml_read_info = {
      sizeof (GstEbmlReadClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_ebml_read_class_init,
      NULL,
      NULL,
      sizeof (GstEbmlRead),
      0,
      (GInstanceInitFunc) gst_ebml_read_init,
    };

    gst_ebml_read_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstEbmlRead",
        &gst_ebml_read_info, 0);
  }

  return gst_ebml_read_type;
}

static void
gst_ebml_read_class_init (GstEbmlReadClass * klass)
{
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  GST_DEBUG_CATEGORY_INIT (ebmlread_debug, "ebmlread",
      0, "EBML stream helper class");

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_ebml_read_change_state);
}

static void
gst_ebml_read_init (GstEbmlRead * ebml)
{
  ebml->sinkpad = NULL;
  ebml->level = NULL;
}

static GstStateChangeReturn
gst_ebml_read_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstEbmlRead *ebml = GST_EBML_READ (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!ebml->sinkpad) {
        g_return_val_if_reached (GST_STATE_CHANGE_FAILURE);
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    {
      g_list_foreach (ebml->level, (GFunc) g_free, NULL);
      g_list_free (ebml->level);
      ebml->level = NULL;
      if (ebml->cached_buffer) {
        gst_buffer_unref (ebml->cached_buffer);
        ebml->cached_buffer = NULL;
      }
      ebml->offset = 0;
      break;
    }
    default:
      break;
  }

  return ret;
}

/*
 * Return: the amount of levels in the hierarchy that the
 * current element lies higher than the previous one.
 * The opposite isn't done - that's auto-done using master
 * element reading.
 */

static guint
gst_ebml_read_element_level_up (GstEbmlRead * ebml)
{
  guint num = 0;
  guint64 pos = ebml->offset;

  while (ebml->level != NULL) {
    GList *last = g_list_last (ebml->level);
    GstEbmlLevel *level = last->data;

    if (pos >= level->start + level->length) {
      ebml->level = g_list_remove (ebml->level, level);
      g_free (level);
      num++;
    } else {
      break;
    }
  }

  return num;
}

/*
 * Calls pull_range for (offset,size) without advancing our offset
 */
static GstFlowReturn
gst_ebml_read_peek_bytes (GstEbmlRead * ebml, guint size, GstBuffer ** p_buf,
    guint8 ** bytes)
{
  GstFlowReturn ret;

  /* Caching here actually makes much less difference than one would expect.
   * We do it mainly to avoid pulling buffers of 1 byte all the time */
  if (ebml->cached_buffer) {
    guint64 cache_offset = GST_BUFFER_OFFSET (ebml->cached_buffer);
    guint cache_size = GST_BUFFER_SIZE (ebml->cached_buffer);

    if (cache_offset <= ebml->offset &&
        (ebml->offset + size) < (cache_offset + cache_size)) {
      if (p_buf)
        *p_buf = gst_buffer_create_sub (ebml->cached_buffer,
            ebml->offset - cache_offset, size);
      if (bytes)
        *bytes =
            GST_BUFFER_DATA (ebml->cached_buffer) + ebml->offset - cache_offset;
      return GST_FLOW_OK;
    }
    /* not enough data in the cache, free cache and get a new one */
    gst_buffer_unref (ebml->cached_buffer);
    ebml->cached_buffer = NULL;
  }

  /* refill the cache */
  ret = gst_pad_pull_range (ebml->sinkpad, ebml->offset, MAX (size, 64 * 1024),
      &ebml->cached_buffer);
  if (ret != GST_FLOW_OK) {
    ebml->cached_buffer = NULL;
    return ret;
  }

  if (GST_BUFFER_SIZE (ebml->cached_buffer) >= size) {
    if (p_buf)
      *p_buf = gst_buffer_create_sub (ebml->cached_buffer, 0, size);
    if (bytes)
      *bytes = GST_BUFFER_DATA (ebml->cached_buffer);
    return GST_FLOW_OK;
  }

  /* FIXME, seems silly to require this */
  if (!p_buf)
    return GST_FLOW_ERROR;

  ret = gst_pad_pull_range (ebml->sinkpad, ebml->offset, size, p_buf);
  if (ret != GST_FLOW_OK) {
    GST_DEBUG ("pull_range returned %d", ret);
    return ret;
  }

  if (GST_BUFFER_SIZE (*p_buf) < size) {
    GST_WARNING_OBJECT (ebml, "Dropping short buffer at offset %"
        G_GUINT64_FORMAT ": wanted %u bytes, got %u bytes", ebml->offset,
        size, GST_BUFFER_SIZE (*p_buf));
    gst_buffer_unref (*p_buf);
    *p_buf = NULL;
    if (bytes)
      *bytes = NULL;
    return GST_FLOW_ERROR;
  }

  if (bytes)
    *bytes = GST_BUFFER_DATA (*p_buf);

  return GST_FLOW_OK;
}

/*
 * Calls pull_range for (offset,size) and advances our offset by size
 */
static GstFlowReturn
gst_ebml_read_pull_bytes (GstEbmlRead * ebml, guint size, GstBuffer ** p_buf,
    guint8 ** bytes)
{
  GstFlowReturn ret;

  ret = gst_ebml_read_peek_bytes (ebml, size, p_buf, bytes);
  if (ret != GST_FLOW_OK)
    return ret;

  ebml->offset += size;
  return GST_FLOW_OK;
}

/*
 * Read: the element content data ID.
 * Return: FALSE on error.
 */

static GstFlowReturn
gst_ebml_read_element_id (GstEbmlRead * ebml, guint32 * id, guint * level_up)
{
  guint8 *buf;
  gint len_mask = 0x80, read = 1, n = 1;
  guint32 total;
  guint8 b;
  GstFlowReturn ret;

  ret = gst_ebml_read_peek_bytes (ebml, 1, NULL, &buf);
  if (ret != GST_FLOW_OK)
    return ret;

  b = GST_READ_UINT8 (buf);

  total = (guint32) b;

  while (read <= 4 && !(total & len_mask)) {
    read++;
    len_mask >>= 1;
  }
  if (read > 4) {
    guint64 pos = ebml->offset;

    GST_ELEMENT_ERROR (ebml, STREAM, DEMUX, (NULL),
        ("Invalid EBML ID size tag (0x%x) at position %" G_GUINT64_FORMAT
            " (0x%" G_GINT64_MODIFIER "x)", (guint) b, pos, pos));
    return GST_FLOW_ERROR;
  }

  ret = gst_ebml_read_peek_bytes (ebml, read, NULL, &buf);
  if (ret != GST_FLOW_OK)
    return ret;

  while (n < read) {
    b = GST_READ_UINT8 (buf + n);
    total = (total << 8) | b;
    ++n;
  }

  *id = total;

  /* level */
  if (level_up)
    *level_up = gst_ebml_read_element_level_up (ebml);

  ebml->offset += read;
  return GST_FLOW_OK;
}

/*
 * Read: element content length.
 * Return: the number of bytes read or -1 on error.
 */

static GstFlowReturn
gst_ebml_read_element_length (GstEbmlRead * ebml, guint64 * length,
    gint * rread)
{
  GstFlowReturn ret;
  guint8 *buf;
  gint len_mask = 0x80, read = 1, n = 1, num_ffs = 0;
  guint64 total;
  guint8 b;

  ret = gst_ebml_read_peek_bytes (ebml, 1, NULL, &buf);
  if (ret != GST_FLOW_OK)
    return ret;

  b = GST_READ_UINT8 (buf);

  total = (guint64) b;

  while (read <= 8 && !(total & len_mask)) {
    read++;
    len_mask >>= 1;
  }
  if (read > 8) {
    guint64 pos = ebml->offset;

    GST_ELEMENT_ERROR (ebml, STREAM, DEMUX, (NULL),
        ("Invalid EBML length size tag (0x%x) at position %" G_GUINT64_FORMAT
            " (0x%" G_GINT64_MODIFIER "x)", (guint) b, pos, pos));
    return GST_FLOW_ERROR;
  }

  if ((total &= (len_mask - 1)) == len_mask - 1)
    num_ffs++;

  ret = gst_ebml_read_peek_bytes (ebml, read, NULL, &buf);
  if (ret != GST_FLOW_OK)
    return ret;

  while (n < read) {
    guint8 b = GST_READ_UINT8 (buf + n);

    if (b == 0xff)
      num_ffs++;
    total = (total << 8) | b;
    ++n;
  }

  if (read == num_ffs)
    *length = G_MAXUINT64;
  else
    *length = total;

  if (rread)
    *rread = read;

  ebml->offset += read;

  return GST_FLOW_OK;
}

/*
 * Return: the ID of the next element.
 * Level_up contains the amount of levels that this
 * next element lies higher than the previous one.
 */

GstFlowReturn
gst_ebml_peek_id (GstEbmlRead * ebml, guint * level_up, guint32 * id)
{
  guint64 off;
  GstFlowReturn ret;

  g_assert (level_up);

  off = ebml->offset;           /* save offset */

  if ((ret = gst_ebml_read_element_id (ebml, id, level_up)) != GST_FLOW_OK)
    return ret;

  ebml->offset = off;           /* restore offset */
  return ret;
}

/*
 * Return the length of the stream in bytes
 */

gint64
gst_ebml_read_get_length (GstEbmlRead * ebml)
{
  GstFormat fmt = GST_FORMAT_BYTES;
  gint64 end;

  if (!gst_pad_query_duration (GST_PAD_PEER (ebml->sinkpad), &fmt, &end))
    g_return_val_if_reached (0);        ///// FIXME /////////

  if (fmt != GST_FORMAT_BYTES || end < 0)
    g_return_val_if_reached (0);        ///// FIXME /////////

  return end;
}

/*
 * Seek to a given offset.
 */

gboolean
gst_ebml_read_seek (GstEbmlRead * ebml, guint64 offset)
{
  if (offset >= gst_ebml_read_get_length (ebml))
    return FALSE;

  ebml->offset = offset;

  return TRUE;
}

/*
 * Skip the next element.
 */

GstFlowReturn
gst_ebml_read_skip (GstEbmlRead * ebml)
{
  guint64 length;
  guint32 id;
  GstFlowReturn ret;

  ret = gst_ebml_read_element_id (ebml, &id, NULL);
  if (ret != GST_FLOW_OK)
    return ret;

  ret = gst_ebml_read_element_length (ebml, &length, NULL);
  if (ret != GST_FLOW_OK)
    return ret;

  ebml->offset += length;
  return ret;
}

/*
 * Read the next element as a GstBuffer (binary).
 */

GstFlowReturn
gst_ebml_read_buffer (GstEbmlRead * ebml, guint32 * id, GstBuffer ** buf)
{
  guint64 length;
  GstFlowReturn ret;

  ret = gst_ebml_read_element_id (ebml, id, NULL);
  if (ret != GST_FLOW_OK)
    return ret;

  ret = gst_ebml_read_element_length (ebml, &length, NULL);
  if (ret != GST_FLOW_OK)
    return ret;

  if (length == 0) {
    *buf = gst_buffer_new ();
    return GST_FLOW_OK;
  }

  *buf = NULL;
  ret = gst_ebml_read_pull_bytes (ebml, (guint) length, buf, NULL);

  return ret;
}

/*
 * Read the next element, return a pointer to it and its size.
 */

static GstFlowReturn
gst_ebml_read_bytes (GstEbmlRead * ebml, guint32 * id, guint8 ** data,
    guint * size)
{
  guint64 length;
  GstFlowReturn ret;

  *size = 0;

  ret = gst_ebml_read_element_id (ebml, id, NULL);
  if (ret != GST_FLOW_OK)
    return ret;

  ret = gst_ebml_read_element_length (ebml, &length, NULL);
  if (ret != GST_FLOW_OK)
    return ret;

  if (length == 0) {
    *data = NULL;
    return ret;
  }

  *data = NULL;
  ret = gst_ebml_read_pull_bytes (ebml, (guint) length, NULL, data);
  if (ret != GST_FLOW_OK)
    return ret;

  *size = (guint) length;

  return ret;
}

/*
 * Read the next element as an unsigned int.
 */

GstFlowReturn
gst_ebml_read_uint (GstEbmlRead * ebml, guint32 * id, guint64 * num)
{
  guint8 *data;
  guint size;
  GstFlowReturn ret;

  ret = gst_ebml_read_bytes (ebml, id, &data, &size);
  if (ret != GST_FLOW_OK)
    return ret;

  if (size < 1 || size > 8) {
    GST_ELEMENT_ERROR (ebml, STREAM, DEMUX, (NULL),
        ("Invalid integer element size %d at position %" G_GUINT64_FORMAT
            " (0x%" G_GINT64_MODIFIER "x)",
            size, ebml->offset - size, ebml->offset - size));
    return GST_FLOW_ERROR;
  }
  *num = 0;
  while (size > 0) {
    *num = (*num << 8) | *data;
    size--;
    data++;
  }

  return ret;
}

/*
 * Read the next element as a signed int.
 */

GstFlowReturn
gst_ebml_read_sint (GstEbmlRead * ebml, guint32 * id, gint64 * num)
{
  guint8 *data;
  guint size;
  gboolean negative = 0;
  GstFlowReturn ret;

  ret = gst_ebml_read_bytes (ebml, id, &data, &size);
  if (ret != GST_FLOW_OK)
    return ret;

  if (size < 1 || size > 8) {
    GST_ELEMENT_ERROR (ebml, STREAM, DEMUX, (NULL),
        ("Invalid integer element size %d at position %" G_GUINT64_FORMAT
            " (0x%" G_GINT64_MODIFIER "x)", size, ebml->offset - size,
            ebml->offset - size));
    return GST_FLOW_ERROR;
  }

  *num = 0;
  if (*data & 0x80) {
    negative = 1;
    *num = *data & ~0x80;
    size--;
    data++;
  }

  while (size > 0) {
    *num = (*num << 8) | *data;
    size--;
    data++;
  }

  /* make signed */
  if (negative) {
    *num = 0 - *num;
  }

  return ret;
}

/*
 * Read the next element as a float.
 */

GstFlowReturn
gst_ebml_read_float (GstEbmlRead * ebml, guint32 * id, gdouble * num)
{
  guint8 *data;
  guint size;
  GstFlowReturn ret;

  ret = gst_ebml_read_bytes (ebml, id, &data, &size);
  if (ret != GST_FLOW_OK)
    return ret;

  if (size != 4 && size != 8 && size != 10) {
    GST_ELEMENT_ERROR (ebml, STREAM, DEMUX, (NULL),
        ("Invalid float element size %d at position %" G_GUINT64_FORMAT
            " (0x%" G_GINT64_MODIFIER "x)", size, ebml->offset - size,
            ebml->offset - size));
    return GST_FLOW_ERROR;
  }

  if (size == 10) {
    GST_ELEMENT_ERROR (ebml, CORE, NOT_IMPLEMENTED, (NULL),
        ("FIXME! 10-byte floats unimplemented"));
    return GST_FLOW_NOT_SUPPORTED;
  }

  if (size == 4) {
    gfloat f;

#if (G_BYTE_ORDER == G_BIG_ENDIAN)
    f = *(gfloat *) data;
#else
    while (size > 0) {
      ((guint8 *) & f)[size - 1] = data[4 - size];
      size--;
    }
#endif

    *num = f;
  } else {
    gdouble d;

#if (G_BYTE_ORDER == G_BIG_ENDIAN)
    d = *(gdouble *) data;
#else
    while (size > 0) {
      ((guint8 *) & d)[size - 1] = data[8 - size];
      size--;
    }
#endif

    *num = d;
  }

  return ret;
}

/*
 * Read the next element as an ASCII string.
 */

GstFlowReturn
gst_ebml_read_ascii (GstEbmlRead * ebml, guint32 * id, gchar ** str)
{
  guint8 *data;
  guint size;
  GstFlowReturn ret;

  ret = gst_ebml_read_bytes (ebml, id, &data, &size);
  if (ret != GST_FLOW_OK)
    return ret;

  *str = g_malloc (size + 1);
  memcpy (*str, data, size);
  (*str)[size] = '\0';

  return ret;
}

/*
 * Read the next element as a UTF-8 string.
 */

GstFlowReturn
gst_ebml_read_utf8 (GstEbmlRead * ebml, guint32 * id, gchar ** str)
{
  GstFlowReturn ret;

#ifndef GST_DISABLE_GST_DEBUG
  guint64 oldoff = ebml->offset;
#endif

  ret = gst_ebml_read_ascii (ebml, id, str);
  if (ret != GST_FLOW_OK)
    return ret;

  if (str != NULL && *str != NULL && **str != '\0' &&
      !g_utf8_validate (*str, -1, NULL)) {
    GST_WARNING ("Invalid UTF-8 string at offset %" G_GUINT64_FORMAT, oldoff);
  }

  return ret;
}

/*
 * Read the next element as a date.
 * Returns the seconds since the unix epoch.
 */

GstFlowReturn
gst_ebml_read_date (GstEbmlRead * ebml, guint32 * id, gint64 * date)
{
  gint64 ebml_date;
  GstFlowReturn ret;

  ret = gst_ebml_read_sint (ebml, id, &ebml_date);
  if (ret != GST_FLOW_OK)
    return ret;

  *date = (ebml_date / GST_SECOND) + GST_EBML_DATE_OFFSET;

  return ret;
}

/*
 * Read the next element, but only the header. The contents
 * are supposed to be sub-elements which can be read separately.
 */

GstFlowReturn
gst_ebml_read_master (GstEbmlRead * ebml, guint32 * id)
{
  GstEbmlLevel *level;
  guint64 length;
  GstFlowReturn ret;

  ret = gst_ebml_read_element_id (ebml, id, NULL);
  if (ret != GST_FLOW_OK)
    return ret;

  ret = gst_ebml_read_element_length (ebml, &length, NULL);
  if (ret != GST_FLOW_OK)
    return ret;

  /* remember level */
  level = g_new (GstEbmlLevel, 1);
  level->start = ebml->offset;
  level->length = length;
  ebml->level = g_list_append (ebml->level, level);

  return GST_FLOW_OK;
}

/*
 * Read the next element as binary data.
 */

GstFlowReturn
gst_ebml_read_binary (GstEbmlRead * ebml,
    guint32 * id, guint8 ** binary, guint64 * length)
{
  guint8 *data;
  guint size;
  GstFlowReturn ret;

  ret = gst_ebml_read_bytes (ebml, id, &data, &size);
  if (ret != GST_FLOW_OK)
    return ret;

  *length = size;
  *binary = g_memdup (data, size);

  return GST_FLOW_OK;
}

/*
 * Read an EBML header.
 */

GstFlowReturn
gst_ebml_read_header (GstEbmlRead * ebml, gchar ** doctype, guint * version)
{
  /* this function is the first to be called */
  guint32 id;
  guint level_up;
  GstFlowReturn ret;

  /* default init */
  if (doctype)
    *doctype = NULL;
  if (version)
    *version = 1;

  ret = gst_ebml_peek_id (ebml, &level_up, &id);
  if (ret != GST_FLOW_OK)
    return ret;

  GST_DEBUG_OBJECT (ebml, "id: %08x", GST_READ_UINT32_BE (&id));

  if (level_up != 0 || id != GST_EBML_ID_HEADER) {
    GST_ELEMENT_ERROR (ebml, STREAM, WRONG_TYPE, (NULL), (NULL));
    return GST_FLOW_ERROR;
  }
  ret = gst_ebml_read_master (ebml, &id);
  if (ret != GST_FLOW_OK)
    return ret;

  while (TRUE) {
    ret = gst_ebml_peek_id (ebml, &level_up, &id);
    if (ret != GST_FLOW_OK)
      return ret;

    /* end-of-header */
    if (level_up)
      break;

    switch (id) {
        /* is our read version uptodate? */
      case GST_EBML_ID_EBMLREADVERSION:{
        guint64 num;

        ret = gst_ebml_read_uint (ebml, &id, &num);
        if (ret != GST_FLOW_OK)
          return ret;
        g_assert (id == GST_EBML_ID_EBMLREADVERSION);
        if (num != GST_EBML_VERSION) {
          GST_ELEMENT_ERROR (ebml, STREAM, WRONG_TYPE, (NULL), (NULL));
          return GST_FLOW_ERROR;
        }
        break;
      }

        /* we only handle 8 byte lengths at max */
      case GST_EBML_ID_EBMLMAXSIZELENGTH:{
        guint64 num;

        ret = gst_ebml_read_uint (ebml, &id, &num);
        if (ret != GST_FLOW_OK)
          return ret;
        g_assert (id == GST_EBML_ID_EBMLMAXSIZELENGTH);
        if (num != sizeof (guint64)) {
          GST_ELEMENT_ERROR (ebml, STREAM, WRONG_TYPE, (NULL), (NULL));
          return GST_FLOW_ERROR;
        }
        break;
      }

        /* we handle 4 byte IDs at max */
      case GST_EBML_ID_EBMLMAXIDLENGTH:{
        guint64 num;

        ret = gst_ebml_read_uint (ebml, &id, &num);
        if (ret != GST_FLOW_OK)
          return ret;
        g_assert (id == GST_EBML_ID_EBMLMAXIDLENGTH);
        if (num != sizeof (guint32)) {
          GST_ELEMENT_ERROR (ebml, STREAM, WRONG_TYPE, (NULL), (NULL));
          return GST_FLOW_ERROR;
        }
        break;
      }

      case GST_EBML_ID_DOCTYPE:{
        gchar *text;

        ret = gst_ebml_read_ascii (ebml, &id, &text);
        if (ret != GST_FLOW_OK)
          return ret;
        g_assert (id == GST_EBML_ID_DOCTYPE);
        if (doctype) {
          if (doctype)
            g_free (*doctype);
          *doctype = text;
        } else
          g_free (text);
        break;
      }

      case GST_EBML_ID_DOCTYPEREADVERSION:{
        guint64 num;

        ret = gst_ebml_read_uint (ebml, &id, &num);
        if (ret != GST_FLOW_OK)
          return ret;
        g_assert (id == GST_EBML_ID_DOCTYPEREADVERSION);
        if (version)
          *version = num;
        break;
      }

      default:
        GST_WARNING ("Unknown data type 0x%x in EBML header (ignored)", id);
        /* pass-through */

        /* we ignore these two, as they don't tell us anything we care about */
      case GST_EBML_ID_VOID:
      case GST_EBML_ID_EBMLVERSION:
      case GST_EBML_ID_DOCTYPEVERSION:
        ret = gst_ebml_read_skip (ebml);
        if (ret != GST_FLOW_OK)
          return ret;
        break;
    }
  }

  return GST_FLOW_OK;
}
