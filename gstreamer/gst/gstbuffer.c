/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstbuffer.c: Buffer operations
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

#include "gst_private.h"

#include "gstbuffer.h"
#include "gstinfo.h"
#include "gstutils.h"
#include "gstminiobject.h"

#ifndef GST_DISABLE_TRACE
/* #define GST_WITH_ALLOC_TRACE  */
#include "gsttrace.h"

static GstAllocTrace *_gst_buffer_trace;
#endif

static void gst_buffer_init (GTypeInstance * instance, gpointer g_class);
static void gst_buffer_class_init (gpointer g_class, gpointer class_data);
static void gst_buffer_finalize (GstBuffer * buffer);
static GstBuffer *_gst_buffer_copy (GstBuffer * buffer);


void
_gst_buffer_initialize (void)
{
  gst_buffer_get_type ();

#ifndef GST_DISABLE_TRACE
  _gst_buffer_trace = gst_alloc_trace_register (GST_BUFFER_TRACE_NAME);
#endif
}

GType
gst_buffer_get_type (void)
{
  static GType _gst_buffer_type;

  if (G_UNLIKELY (_gst_buffer_type == 0)) {
    static const GTypeInfo buffer_info = {
      sizeof (GstBufferClass),
      NULL,
      NULL,
      gst_buffer_class_init,
      NULL,
      NULL,
      sizeof (GstBuffer),
      0,
      gst_buffer_init,
      NULL
    };

    _gst_buffer_type = g_type_register_static (GST_TYPE_MINI_OBJECT,
        "GstBuffer", &buffer_info, 0);
  }
  return _gst_buffer_type;
}

static void
gst_buffer_class_init (gpointer g_class, gpointer class_data)
{
  GstBufferClass *buffer_class = GST_BUFFER_CLASS (g_class);

  buffer_class->mini_object_class.copy =
      (GstMiniObjectCopyFunction) _gst_buffer_copy;
  buffer_class->mini_object_class.finalize =
      (GstMiniObjectFinalizeFunction) gst_buffer_finalize;

}

static void
gst_buffer_finalize (GstBuffer * buffer)
{
  g_return_if_fail (buffer != NULL);

  /* free our data */
  if (buffer->malloc_data) {
    g_free (buffer->malloc_data);
  }

  gst_caps_replace (&GST_BUFFER_CAPS (buffer), NULL);
}

static GstBuffer *
_gst_buffer_copy (GstBuffer * buffer)
{
  GstBuffer *copy;
  guint mask;

  g_return_val_if_fail (buffer != NULL, NULL);

  /* create a fresh new buffer */
  copy = gst_buffer_new ();

  GST_CAT_LOG (GST_CAT_BUFFER, "copy %p to %p", buffer, copy);

  /* copy relevant flags */
  mask = GST_BUFFER_FLAG_PREROLL | GST_BUFFER_FLAG_IN_CAPS |
      GST_BUFFER_FLAG_DELTA_UNIT;
  GST_MINI_OBJECT (copy)->flags |= GST_MINI_OBJECT (buffer)->flags & mask;

  /* we simply copy everything from our parent */
  if (buffer->malloc_data) {
    copy->malloc_data = g_memdup (buffer->data, buffer->size);
    copy->data = copy->malloc_data;
  }

  copy->size = buffer->size;

  GST_BUFFER_TIMESTAMP (copy) = GST_BUFFER_TIMESTAMP (buffer);
  GST_BUFFER_DURATION (copy) = GST_BUFFER_DURATION (buffer);
  GST_BUFFER_OFFSET (copy) = GST_BUFFER_OFFSET (buffer);
  GST_BUFFER_OFFSET_END (copy) = GST_BUFFER_OFFSET_END (buffer);

  if (GST_BUFFER_CAPS (buffer))
    GST_BUFFER_CAPS (copy) = gst_caps_ref (GST_BUFFER_CAPS (buffer));
  else
    GST_BUFFER_CAPS (copy) = NULL;

  return copy;
}

static void
gst_buffer_init (GTypeInstance * instance, gpointer g_class)
{
  GstBuffer *buffer;

  buffer = (GstBuffer *) instance;

  GST_CAT_LOG (GST_CAT_BUFFER, "init %p", buffer);

  //GST_BUFFER_DATA (buffer) = NULL;
  //GST_BUFFER_SIZE (buffer) = 0;
  GST_BUFFER_TIMESTAMP (buffer) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_DURATION (buffer) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_OFFSET (buffer) = GST_BUFFER_OFFSET_NONE;
  GST_BUFFER_OFFSET_END (buffer) = GST_BUFFER_OFFSET_NONE;
  //GST_BUFFER_CAPS (buffer) = NULL;
}

/**
 * gst_buffer_new:
 *
 * Creates a newly allocated buffer without any data.
 *
 * Returns: the new #GstBuffer.
 * 
 * MT safe.
 */
GstBuffer *
gst_buffer_new (void)
{
  GstBuffer *newbuf;

  newbuf = (GstBuffer *) gst_mini_object_new (GST_TYPE_BUFFER);

  GST_CAT_LOG (GST_CAT_BUFFER, "new %p", newbuf);

  return newbuf;
}

/**
 * gst_buffer_new_and_alloc:
 * @size: the size of the new buffer's data.
 *
 * Creates a newly allocated buffer with data of the given size.
 *
 * Returns: the new #GstBuffer.
 * 
 * MT safe.
 */
GstBuffer *
gst_buffer_new_and_alloc (guint size)
{
  GstBuffer *newbuf;

  newbuf = gst_buffer_new ();

  newbuf->malloc_data = g_malloc (size);
  GST_BUFFER_DATA (newbuf) = newbuf->malloc_data;
  GST_BUFFER_SIZE (newbuf) = size;

  return newbuf;
}


/**
 * gst_buffer_get_caps:
 * @buffer: a #GstBuffer to get the caps of.
 *
 * Gets the media type of the buffer. This can be NULL if there
 * is not media type attached to this buffer or when the media
 * type is the same as the previous received buffer.
 *
 * This function does not increment the refcount of the caps. The
 * caps pointer will therefore remain valid until the buffer is 
 * unreffed.
 *
 * Returns: the #GstCaps, or NULL if there was an error or there
 * were no caps on this buffer.
 */
/* FIXME can we make this threadsafe without a lock on the buffer?
 * We can use compare and swap and atomic reads. */
GstCaps *
gst_buffer_get_caps (GstBuffer * buffer)
{
  g_return_val_if_fail (buffer != NULL, NULL);

  return GST_BUFFER_CAPS (buffer);
}

/**
 * gst_buffer_set_caps:
 * @buffer: a #GstBuffer to set the caps of.
 * @caps: a #GstCaps to set.
 *
 * Sets the media type on the buffer. The caps' refcount will
 * be increased and any previous caps on the buffer will be
 * unreffed.
 */
/* FIXME can we make this threadsafe without a lock on the buffer? 
 * We can use compare and swap and atomic reads. Another idea is to
 * not attach the caps to the buffer but use an event to signal a caps
 * change. */
void
gst_buffer_set_caps (GstBuffer * buffer, GstCaps * caps)
{
  g_return_if_fail (buffer != NULL);

  gst_caps_replace (&GST_BUFFER_CAPS (buffer), caps);
}


typedef struct _GstSubBuffer GstSubBuffer;
typedef struct _GstSubBufferClass GstSubBufferClass;

#define GST_TYPE_SUBBUFFER                         (gst_subbuffer_get_type())

#define GST_IS_SUBBUFFER(obj)  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_BUFFER))
#define GST_SUBBUFFER(obj)     (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_SUBBUFFER, GstSubBuffer))

struct _GstSubBuffer
{
  GstBuffer buffer;

  GstBuffer *parent;
};

struct _GstSubBufferClass
{
  GstBufferClass buffer_class;
};

static void gst_subbuffer_init (GTypeInstance * instance, gpointer g_class);

static GType
gst_subbuffer_get_type (void)
{
  static GType _gst_subbuffer_type;

  if (G_UNLIKELY (_gst_subbuffer_type == 0)) {
    static const GTypeInfo subbuffer_info = {
      sizeof (GstSubBufferClass),
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      sizeof (GstSubBuffer),
      0,
      gst_subbuffer_init,
      NULL
    };

    _gst_subbuffer_type = g_type_register_static (GST_TYPE_BUFFER,
        "GstSubBuffer", &subbuffer_info, 0);
  }
  return _gst_subbuffer_type;
}

static void
gst_subbuffer_init (GTypeInstance * instance, gpointer g_class)
{

}

/**
 * gst_buffer_create_subbuffer:
 * @parent: a parent #GstBuffer to create a subbuffer from.
 * @offset: the offset into parent #GstBuffer.
 * @size: the size of the new #GstBuffer sub-buffer (with size > 0).
 *
 * Creates a sub-buffer from the parent at a given offset.
 * This sub-buffer uses the actual memory space of the parent buffer.
 * This function will copy the offset and timestamp field when the 
 * offset is 0, else they are set to _NONE.
 * The duration field of the new buffer are set to GST_CLOCK_TIME_NONE.
 *
 * Returns: the new #GstBuffer, or NULL if there was an error.
 * 
 * MT safe.
 */
GstBuffer *
gst_buffer_create_sub (GstBuffer * buffer, guint offset, guint size)
{
  GstSubBuffer *subbuffer;
  GstBuffer *parent;

  g_return_val_if_fail (buffer != NULL, NULL);
  g_return_val_if_fail (buffer->mini_object.refcount > 0, NULL);
  g_return_val_if_fail (size > 0, NULL);
  g_return_val_if_fail (buffer->size >= offset + size, NULL);

  /* find real parent */
  if (GST_IS_SUBBUFFER (buffer)) {
    parent = GST_SUBBUFFER (buffer)->parent;
  } else {
    parent = buffer;
  }
  gst_buffer_ref (parent);

  /* create the new buffer */
  subbuffer = (GstSubBuffer *) gst_mini_object_new (GST_TYPE_SUBBUFFER);
  subbuffer->parent = parent;

  GST_CAT_LOG (GST_CAT_BUFFER, "new subbuffer %p (parent %p)", subbuffer,
      parent);

  /* set the right values in the child */
  GST_BUFFER_DATA (GST_BUFFER (subbuffer)) = buffer->data + offset;
  GST_BUFFER_SIZE (GST_BUFFER (subbuffer)) = size;

  /* we can copy the timestamp and offset if the new buffer starts at
   * offset 0 */
  if (offset == 0) {
    GST_BUFFER_TIMESTAMP (subbuffer) = GST_BUFFER_TIMESTAMP (buffer);
    GST_BUFFER_OFFSET (subbuffer) = GST_BUFFER_OFFSET (buffer);
  } else {
    GST_BUFFER_TIMESTAMP (subbuffer) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_OFFSET (subbuffer) = GST_BUFFER_OFFSET_NONE;
  }

  GST_BUFFER_DURATION (subbuffer) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_OFFSET_END (subbuffer) = GST_BUFFER_OFFSET_NONE;

  GST_BUFFER_CAPS (subbuffer) = NULL;

  return GST_BUFFER (subbuffer);
}

/**
 * gst_buffer_is_span_fast:
 * @buf1: a first source #GstBuffer.
 * @buf2: the second source #GstBuffer.
 *
 * Determines whether a gst_buffer_span() can be done without copying
 * the contents, that is, whether the data areas are contiguous.
 *
 * Returns: TRUE if the buffers are contiguous, 
 * FALSE if a copy would be required.
 * 
 * MT safe.
 */
gboolean
gst_buffer_is_span_fast (GstBuffer * buf1, GstBuffer * buf2)
{
  return FALSE;
#if 0
  g_return_val_if_fail (buf1 != NULL && buf2 != NULL, FALSE);
  g_return_val_if_fail (GST_BUFFER_REFCOUNT_VALUE (buf1) > 0, FALSE);
  g_return_val_if_fail (GST_BUFFER_REFCOUNT_VALUE (buf2) > 0, FALSE);

  /* it's only fast if we have subbuffers of the same parent */
  return ((GST_BUFFER_FLAG_IS_SET (buf1, GST_BUFFER_SUBBUFFER)) &&
      (GST_BUFFER_FLAG_IS_SET (buf2, GST_BUFFER_SUBBUFFER)) &&
      (buf1->buffer_private == buf2->buffer_private) &&
      ((buf1->data + buf1->size) == buf2->data));
#endif
}

/**
 * gst_buffer_span:
 * @buf1: a first source #GstBuffer to merge.
 * @offset: the offset in the first buffer from where the new
 * buffer should start.
 * @buf2: the second source #GstBuffer to merge.
 * @len: the total length of the new buffer.
 *
 * Creates a new buffer that consists of part of buf1 and buf2.
 * Logically, buf1 and buf2 are concatenated into a single larger
 * buffer, and a new buffer is created at the given offset inside
 * this space, with a given length.
 *
 * If the two source buffers are children of the same larger buffer,
 * and are contiguous, the new buffer will be a child of the shared
 * parent, and thus no copying is necessary. you can use 
 * gst_buffer_is_span_fast() to determine if a memcpy will be needed.
 *
 * Returns: the new #GstBuffer that spans the two source buffers.
 * 
 * MT safe.
 */
GstBuffer *
gst_buffer_span (GstBuffer * buf1, guint32 offset, GstBuffer * buf2,
    guint32 len)
{
  return NULL;
#if 0
  GstBuffer *newbuf;

  g_return_val_if_fail (buf1 != NULL && buf2 != NULL, NULL);
  g_return_val_if_fail (GST_BUFFER_REFCOUNT_VALUE (buf1) > 0, NULL);
  g_return_val_if_fail (GST_BUFFER_REFCOUNT_VALUE (buf2) > 0, NULL);
  g_return_val_if_fail (len > 0, NULL);
  g_return_val_if_fail (len <= buf1->size + buf2->size - offset, NULL);

  /* if the two buffers have the same parent and are adjacent */
  if (gst_buffer_is_span_fast (buf1, buf2)) {
    GstBuffer *parent = GST_BUFFER (buf1->buffer_private);

    /* we simply create a subbuffer of the common parent */
    newbuf = gst_buffer_create_sub (parent,
        buf1->data - parent->data + offset, len);
  } else {
    GST_CAT_DEBUG (GST_CAT_BUFFER,
        "slow path taken while spanning buffers %p and %p", buf1, buf2);
    /* otherwise we simply have to brute-force copy the buffers */
    newbuf = gst_buffer_new_and_alloc (len);

    /* copy the first buffer's data across */
    memcpy (newbuf->data, buf1->data + offset, buf1->size - offset);
    /* copy the second buffer's data across */
    memcpy (newbuf->data + (buf1->size - offset), buf2->data,
        len - (buf1->size - offset));
    /* if the offset is 0, the new buffer has the same timestamp as buf1 */
    if (offset == 0) {
      GST_BUFFER_OFFSET (newbuf) = GST_BUFFER_OFFSET (buf1);
      GST_BUFFER_TIMESTAMP (newbuf) = GST_BUFFER_TIMESTAMP (buf1);
    }
  }
  /* if we completely merged the two buffers (appended), we can
   * calculate the duration too. Also make sure we's not messing with
   * invalid DURATIONS */
  if (offset == 0 && buf1->size + buf2->size == len) {
    if (GST_BUFFER_DURATION_IS_VALID (buf1) &&
        GST_BUFFER_DURATION_IS_VALID (buf2)) {
      /* add duration */
      GST_BUFFER_DURATION (newbuf) = GST_BUFFER_DURATION (buf1) +
          GST_BUFFER_DURATION (buf2);
    }
    if (GST_BUFFER_OFFSET_END_IS_VALID (buf2)) {
      /* add offset_end */
      GST_BUFFER_OFFSET_END (newbuf) = GST_BUFFER_OFFSET_END (buf2);
    }
  }

  return newbuf;
#endif
}
