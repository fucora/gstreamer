/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2004 Wim Taymans <wim@fluendo.com>
 *
 * gstclock.c: Clock subsystem for maintaining time sync
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

/**
 * SECTION:gstclock
 * @short_description: Abstract class for global clocks
 * @see_also: #GstSystemClock
 *
 * GStreamer uses a global clock to synchronize the plugins in a pipeline.
 * Different clock implementations are possible by implementing this abstract
 * base class.
 *
 * The #GstClock returns a monotonically increasing time with the method
 * gst_clock_get_time(). Its accuracy and base time depends on the specific
 * clock implementation but time is always expessed in nanoseconds. Since the
 * baseline of the clock is undefined, the clock time returned is not
 * meaningful in itself, what matters are the deltas between two clock times.
 *
 * The pipeline uses the clock to calculate the stream time.  Usually all
 * renderers synchronize to the global clock using the buffer timestamps, the
 * newsegment events and the element's base time.
 *
 * The time of the clock in itself is not very useful for an application.
 *
 * A clock implementation can support periodic and single shot clock
 * notifications both synchronous and asynchronous.
 *
 * One first needs to create a #GstClockID for the periodic or single shot
 * notification using gst_clock_new_single_shot_id() or
 * gst_clock_new_periodic_id().
 *
 * To perform a blocking wait for the specific time of the #GstClockID use the
 * gst_clock_id_wait(). To receive a callback when the specific time is reached
 * in the clock use gst_clock_id_wait_async(). Both these calls can be
 * interrupted with the gst_clock_id_unschedule() call. If the blocking wait is
 * unscheduled a return value of GST_CLOCK_UNSCHEDULED is returned.
 *
 * Periodic callbacks scheduled async will be repeadedly called automatically
 * until it is unscheduled. To schedule an async periodic callback,
 * gst_clock_id_wait() should be called repeadedly.
 *
 * The async callbacks can happen from any thread, either provided by the core
 * or from a streaming thread. The application should be prepared for this.
 *
 * A #GstClockID that has been unscheduled cannot be used again for any wait
 * operation.
 *
 * It is possible to perform a blocking wait on the same #GstClockID from
 * multiple threads. However, registering the same #GstClockID for multiple
 * async notifications is not possible, the callback will only be called once.
 *
 * None of the wait operations unref the #GstClockID, the owner is responsible
 * for unreffing the ids itself. This holds for both periodic and single shot
 * notifications. The reason being that the owner of the #GstClockID has to
 * keep a handle to the #GstClockID to unblock the wait on FLUSHING events or
 * state changes and if we unref it automatically, the handle might be invalid.
 *
 * These clock operations do not operate on the stream time, so the callbacks
 * will also occur when not in PLAYING state as if the clock just keeps on
 * running. Some clocks however do not progress when the element that provided
 * the clock is not PLAYING.
 *
 * When a clock has the GST_CLOCK_FLAG_CAN_SET_MASTER flag set, it can be
 * slaved to another #GstClock with the gst_clock_set_master(). The clock will
 * then automatically be synchronized to this master clock by repeadedly
 * sampling the master clock and the slave clock and recalibrating the slave
 * clock with gst_clock_set_calibration(). This feature is mostly usefull for
 * plugins that have an internal clock but must operate with another clock
 * selected by the #GstPipeline.  They can track the offset and rate difference
 * of their internal clock relative to the master clock by using the
 * gst_clock_get_calibration() function.
 *
 * Last reviewed on 2005-10-28 (0.9.4)
 */

#include <time.h>

#include "gst_private.h"

#include "gstclock.h"
#include "gstinfo.h"
#include "gstutils.h"

#ifndef GST_DISABLE_TRACE
/* #define GST_WITH_ALLOC_TRACE */
#include "gsttrace.h"
static GstAllocTrace *_gst_clock_entry_trace;
#endif

/* #define DEBUGGING_ENABLED */

#define DEFAULT_STATS                   FALSE
#define DEFAULT_WINDOW_SIZE             32
#define DEFAULT_WINDOW_THRESHOLD        4
#define DEFAULT_TIMEOUT                 GST_SECOND / 10

enum
{
  PROP_0,
  PROP_STATS,
  PROP_WINDOW_SIZE,
  PROP_WINDOW_THRESHOLD,
  PROP_TIMEOUT
};

static void gst_clock_class_init (GstClockClass * klass);
static void gst_clock_init (GstClock * clock);
static void gst_clock_dispose (GObject * object);
static void gst_clock_finalize (GObject * object);

static void gst_clock_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_clock_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_clock_update_stats (GstClock * clock);


static GstObjectClass *parent_class = NULL;

/* static guint gst_clock_signals[LAST_SIGNAL] = { 0 }; */

static GstClockID
gst_clock_entry_new (GstClock * clock, GstClockTime time,
    GstClockTime interval, GstClockEntryType type)
{
  GstClockEntry *entry;

  entry = g_malloc0 (sizeof (GstClockEntry));
#ifndef GST_DISABLE_TRACE
  gst_alloc_trace_new (_gst_clock_entry_trace, entry);
#endif
  GST_CAT_DEBUG_OBJECT (GST_CAT_CLOCK, clock,
      "created entry %p, time %" GST_TIME_FORMAT, entry, GST_TIME_ARGS (time));

  gst_atomic_int_set (&entry->refcount, 1);
  entry->clock = clock;
  entry->time = time;
  entry->interval = interval;
  entry->type = type;
  entry->status = GST_CLOCK_BUSY;

  return (GstClockID) entry;
}

/**
 * gst_clock_id_ref:
 * @id: The clockid to ref
 *
 * Increase the refcount of the given clockid.
 *
 * Returns: The same #GstClockID with increased refcount.
 *
 * MT safe.
 */
GstClockID
gst_clock_id_ref (GstClockID id)
{
  g_return_val_if_fail (id != NULL, NULL);

  g_atomic_int_inc (&((GstClockEntry *) id)->refcount);

  return id;
}

static void
_gst_clock_id_free (GstClockID id)
{
  g_return_if_fail (id != NULL);

  GST_CAT_DEBUG (GST_CAT_CLOCK, "freed entry %p", id);

#ifndef GST_DISABLE_TRACE
  gst_alloc_trace_free (_gst_clock_entry_trace, id);
#endif
  g_free (id);
}

/**
 * gst_clock_id_unref:
 * @id: The clockid to unref
 *
 * Unref the given clockid. When the refcount reaches 0 the
 * #GstClockID will be freed.
 *
 * MT safe.
 */
void
gst_clock_id_unref (GstClockID id)
{
  gint zero;

  g_return_if_fail (id != NULL);

  zero = g_atomic_int_dec_and_test (&((GstClockEntry *) id)->refcount);
  /* if we ended up with the refcount at zero, free the id */
  if (zero) {
    _gst_clock_id_free (id);
  }
}

/**
 * gst_clock_new_single_shot_id
 * @clock: The clockid to get a single shot notification from
 * @time: the requested time
 *
 * Get an ID from the given clock to trigger a single shot
 * notification at the requested time. The single shot id should be
 * unreffed after usage.
 *
 * Returns: An id that can be used to request the time notification.
 *
 * MT safe.
 */
GstClockID
gst_clock_new_single_shot_id (GstClock * clock, GstClockTime time)
{
  g_return_val_if_fail (GST_IS_CLOCK (clock), NULL);

  return gst_clock_entry_new (clock,
      time, GST_CLOCK_TIME_NONE, GST_CLOCK_ENTRY_SINGLE);
}

/**
 * gst_clock_new_periodic_id
 * @clock: The clockid to get a periodic notification id from
 * @start_time: the requested start time
 * @interval: the requested interval
 *
 * Get an ID from the given clock to trigger a periodic notification.
 * The periodeic notifications will be start at time start_time and
 * will then be fired with the given interval. The id should be unreffed
 * after usage.
 *
 * Returns: An id that can be used to request the time notification.
 *
 * MT safe.
 */
GstClockID
gst_clock_new_periodic_id (GstClock * clock, GstClockTime start_time,
    GstClockTime interval)
{
  g_return_val_if_fail (GST_IS_CLOCK (clock), NULL);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (start_time), NULL);
  g_return_val_if_fail (interval != 0, NULL);

  return gst_clock_entry_new (clock,
      start_time, interval, GST_CLOCK_ENTRY_PERIODIC);
}

/**
 * gst_clock_id_compare_func
 * @id1: A clockid
 * @id2: A clockid to compare with
 *
 * Compares the two GstClockID instances. This function can be used
 * as a GCompareFunc when sorting ids.
 *
 * Returns: negative value if a < b; zero if a = b; positive value if a > b
 *
 * MT safe.
 */
gint
gst_clock_id_compare_func (gconstpointer id1, gconstpointer id2)
{
  GstClockEntry *entry1, *entry2;

  entry1 = (GstClockEntry *) id1;
  entry2 = (GstClockEntry *) id2;

  if (GST_CLOCK_ENTRY_TIME (entry1) > GST_CLOCK_ENTRY_TIME (entry2)) {
    return 1;
  }
  if (GST_CLOCK_ENTRY_TIME (entry1) < GST_CLOCK_ENTRY_TIME (entry2)) {
    return -1;
  }

  return entry1 - entry2;
}

/**
 * gst_clock_id_get_time
 * @id: The clockid to query
 *
 * Get the time of the clock ID
 *
 * Returns: the time of the given clock id.
 *
 * MT safe.
 */
GstClockTime
gst_clock_id_get_time (GstClockID id)
{
  g_return_val_if_fail (id != NULL, GST_CLOCK_TIME_NONE);

  return GST_CLOCK_ENTRY_TIME ((GstClockEntry *) id);
}


/**
 * gst_clock_id_wait
 * @id: The clockid to wait on
 * @jitter: A pointer that will contain the jitter
 *
 * Perform a blocking wait on the given ID. The jitter arg can be
 * NULL.
 *
 * Returns: the result of the blocking wait.
 *
 * MT safe.
 */
GstClockReturn
gst_clock_id_wait (GstClockID id, GstClockTimeDiff * jitter)
{
  GstClockEntry *entry;
  GstClock *clock;
  GstClockReturn res;
  GstClockTime requested;
  GstClockClass *cclass;

  g_return_val_if_fail (id != NULL, GST_CLOCK_ERROR);

  entry = (GstClockEntry *) id;
  requested = GST_CLOCK_ENTRY_TIME (entry);

  clock = GST_CLOCK_ENTRY_CLOCK (entry);

  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (requested)))
    goto invalid_time;

  if (G_UNLIKELY (entry->status == GST_CLOCK_UNSCHEDULED))
    goto unscheduled;

  cclass = GST_CLOCK_GET_CLASS (clock);

  if (G_LIKELY (cclass->wait)) {

    GST_CAT_DEBUG_OBJECT (GST_CAT_CLOCK, clock, "waiting on clock entry %p",
        id);
    res = cclass->wait (clock, entry);
    GST_CAT_DEBUG_OBJECT (GST_CAT_CLOCK, clock,
        "done waiting entry %p, res: %d", id, res);

    if (jitter) {
      GstClockTime now = gst_clock_get_time (clock);

      *jitter = now - requested;
    }
    if (entry->type == GST_CLOCK_ENTRY_PERIODIC) {
      entry->time += entry->interval;
    }

    if (clock->stats) {
      gst_clock_update_stats (clock);
    }
  } else {
    res = GST_CLOCK_UNSUPPORTED;
  }
  return res;

  /* ERRORS */
invalid_time:
  {
    GST_CAT_DEBUG_OBJECT (GST_CAT_CLOCK, clock,
        "invalid time requested, returning _BADTIME");
    return GST_CLOCK_BADTIME;
  }
unscheduled:
  {
    GST_CAT_DEBUG_OBJECT (GST_CAT_CLOCK, clock,
        "entry was unscheduled return _UNSCHEDULED");
    return GST_CLOCK_UNSCHEDULED;
  }
}

/**
 * gst_clock_id_wait_async:
 * @id: a #GstClockID to wait on
 * @func: The callback function
 * @user_data: User data passed in the calback
 *
 * Register a callback on the given clockid with the given
 * function and user_data. When passing an id with an invalid
 * time to this function, the callback will be called immediatly
 * with  a time set to GST_CLOCK_TIME_NONE. The callback will
 * be called when the time of the id has been reached.
 *
 * Returns: the result of the non blocking wait.
 *
 * MT safe.
 */
GstClockReturn
gst_clock_id_wait_async (GstClockID id,
    GstClockCallback func, gpointer user_data)
{
  GstClockEntry *entry;
  GstClock *clock;
  GstClockReturn res;
  GstClockClass *cclass;
  GstClockTime requested;

  g_return_val_if_fail (id != NULL, GST_CLOCK_ERROR);
  g_return_val_if_fail (func != NULL, GST_CLOCK_ERROR);

  entry = (GstClockEntry *) id;
  requested = GST_CLOCK_ENTRY_TIME (entry);
  clock = GST_CLOCK_ENTRY_CLOCK (entry);

  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (requested)))
    goto invalid_time;

  if (G_UNLIKELY (entry->status == GST_CLOCK_UNSCHEDULED))
    goto unscheduled;

  cclass = GST_CLOCK_GET_CLASS (clock);

  if (cclass->wait_async) {
    entry->func = func;
    entry->user_data = user_data;

    res = cclass->wait_async (clock, entry);
  } else {
    res = GST_CLOCK_UNSUPPORTED;
  }
  return res;

  /* ERRORS */
invalid_time:
  {
    (func) (clock, GST_CLOCK_TIME_NONE, id, user_data);
    GST_CAT_DEBUG_OBJECT (GST_CAT_CLOCK, clock,
        "invalid time requested, returning _BADTIME");
    return GST_CLOCK_BADTIME;
  }
unscheduled:
  {
    GST_CAT_DEBUG_OBJECT (GST_CAT_CLOCK, clock,
        "entry was unscheduled return _UNSCHEDULED");
    return GST_CLOCK_UNSCHEDULED;
  }
}

/**
 * gst_clock_id_unschedule:
 * @id: The id to unschedule
 *
 * Cancel an outstanding request with the given ID. This can either
 * be an outstanding async notification or a pending sync notification.
 * After this call, the @id cannot be used anymore to receive sync or
 * async notifications, you need to create a new GstClockID.
 *
 * MT safe.
 */
void
gst_clock_id_unschedule (GstClockID id)
{
  GstClockEntry *entry;
  GstClock *clock;
  GstClockClass *cclass;

  g_return_if_fail (id != NULL);

  entry = (GstClockEntry *) id;
  clock = entry->clock;

  cclass = GST_CLOCK_GET_CLASS (clock);

  if (cclass->unschedule)
    cclass->unschedule (clock, entry);
}


/**
 * GstClock abstract base class implementation
 */
GType
gst_clock_get_type (void)
{
  static GType clock_type = 0;

  if (G_UNLIKELY (clock_type == 0)) {
    static const GTypeInfo clock_info = {
      sizeof (GstClockClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_clock_class_init,
      NULL,
      NULL,
      sizeof (GstClock),
      0,
      (GInstanceInitFunc) gst_clock_init,
      NULL
    };

    clock_type = g_type_register_static (GST_TYPE_OBJECT, "GstClock",
        &clock_info, G_TYPE_FLAG_ABSTRACT);
  }
  return clock_type;
}

static void
gst_clock_class_init (GstClockClass * klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;

  gobject_class = (GObjectClass *) klass;
  gstobject_class = (GstObjectClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_OBJECT);

  if (!g_thread_supported ())
    g_thread_init (NULL);

#ifndef GST_DISABLE_TRACE
  _gst_clock_entry_trace =
      gst_alloc_trace_register (GST_CLOCK_ENTRY_TRACE_NAME);
#endif

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_clock_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_clock_finalize);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_clock_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_clock_get_property);

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_STATS,
      g_param_spec_boolean ("stats", "Stats", "Enable clock stats",
          DEFAULT_STATS, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_WINDOW_SIZE,
      g_param_spec_int ("window-size", "Window size",
          "The size of the window used to calculate rate and offset", 2, 1024,
          DEFAULT_WINDOW_SIZE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_WINDOW_THRESHOLD, g_param_spec_int ("window-threshold",
          "Window threshold",
          "The threshold to start calculating rate and offset", 2, 1024,
          DEFAULT_WINDOW_THRESHOLD, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_TIMEOUT,
      g_param_spec_uint64 ("timeout", "Timeout",
          "The amount of time, in nanoseconds, to sample master and slave clocks",
          0, G_MAXUINT64, DEFAULT_TIMEOUT, G_PARAM_READWRITE));
}

static void
gst_clock_init (GstClock * clock)
{
  clock->last_time = 0;
  clock->entries = NULL;
  clock->entries_changed = g_cond_new ();
  clock->stats = FALSE;

  clock->internal_calibration = 0;
  clock->external_calibration = 0;
  clock->rate_numerator = 1;
  clock->rate_denominator = 1;

  clock->slave_lock = g_mutex_new ();
  clock->filling = TRUE;
  clock->window_size = DEFAULT_WINDOW_SIZE;
  clock->window_threshold = DEFAULT_WINDOW_THRESHOLD;
  clock->time_index = 0;
  clock->timeout = DEFAULT_TIMEOUT;
  clock->times = g_new0 (GstClockTime, 4 * clock->window_size);
}

static void
gst_clock_dispose (GObject * object)
{
  GstClock *clock = GST_CLOCK (object);

  GST_OBJECT_LOCK (clock);
  gst_object_replace ((GstObject **) & clock->master, NULL);
  GST_OBJECT_UNLOCK (clock);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_clock_finalize (GObject * object)
{
  GstClock *clock = GST_CLOCK (object);

  GST_CLOCK_SLAVE_LOCK (clock);
  if (clock->clockid) {
    gst_clock_id_unschedule (clock->clockid);
    gst_clock_id_unref (clock->clockid);
    clock->clockid = NULL;
  }
  g_free (clock->times);
  clock->times = NULL;
  GST_CLOCK_SLAVE_UNLOCK (clock);

  g_cond_free (clock->entries_changed);
  g_mutex_free (clock->slave_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * gst_clock_set_resolution
 * @clock: a #GstClock
 * @resolution: The resolution to set
 *
 * Set the accuracy of the clock.
 *
 * Returns: the new resolution of the clock.
 */
GstClockTime
gst_clock_set_resolution (GstClock * clock, GstClockTime resolution)
{
  GstClockClass *cclass;

  g_return_val_if_fail (GST_IS_CLOCK (clock), 0);
  g_return_val_if_fail (resolution != 0, 0);

  cclass = GST_CLOCK_GET_CLASS (clock);

  if (cclass->change_resolution)
    clock->resolution =
        cclass->change_resolution (clock, clock->resolution, resolution);

  return clock->resolution;
}

/**
 * gst_clock_get_resolution
 * @clock: a #GstClock
 *
 * Get the accuracy of the clock.
 *
 * Returns: the resolution of the clock in units of #GstClockTime.
 *
 * MT safe.
 */
GstClockTime
gst_clock_get_resolution (GstClock * clock)
{
  GstClockClass *cclass;

  g_return_val_if_fail (GST_IS_CLOCK (clock), 0);

  cclass = GST_CLOCK_GET_CLASS (clock);

  if (cclass->get_resolution)
    return cclass->get_resolution (clock);

  return 1;
}

/**
 * gst_clock_adjust_unlocked
 * @clock: a #GstClock to use
 * @internal: a clock time
 *
 * Converts the given @internal clock time to the real time, adjusting for the
 * rate and reference time set with gst_clock_set_calibration() and making sure
 * that the returned time is increasing. This function should be called with the
 * clock's OBJECT_LOCK held and is mainly used by clock subclasses.
 *
 * Returns: the converted time of the clock.
 *
 * MT safe.
 */
GstClockTime
gst_clock_adjust_unlocked (GstClock * clock, GstClockTime internal)
{
  GstClockTime ret;

  ret = gst_util_uint64_scale (internal - clock->internal_calibration,
      clock->rate_numerator, clock->rate_denominator);
  ret += clock->external_calibration;

  /* make sure the time is increasing */
  clock->last_time = MAX (ret, clock->last_time);

  return clock->last_time;
}

/**
 * gst_clock_get_internal_time
 * @clock: a #GstClock to query
 *
 * Gets the current internal time of the given clock. The time is returned
 * unadjusted for the offset and the rate.
 *
 * Returns: the internal time of the clock. Or GST_CLOCK_TIME_NONE when
 * giving wrong input.
 *
 * MT safe.
 */
GstClockTime
gst_clock_get_internal_time (GstClock * clock)
{
  GstClockTime ret;
  GstClockClass *cclass;

  g_return_val_if_fail (GST_IS_CLOCK (clock), GST_CLOCK_TIME_NONE);

  cclass = GST_CLOCK_GET_CLASS (clock);

  if (cclass->get_internal_time) {
    ret = cclass->get_internal_time (clock);
  } else {
    ret = G_GINT64_CONSTANT (0);
  }
  GST_CAT_DEBUG_OBJECT (GST_CAT_CLOCK, clock, "internal time %" GST_TIME_FORMAT,
      GST_TIME_ARGS (ret));

  return ret;
}

/**
 * gst_clock_get_time
 * @clock: a #GstClock to query
 *
 * Gets the current time of the given clock. The time is always
 * monotonically increasing and adjusted according to the current
 * offset and rate.
 *
 * Returns: the time of the clock. Or GST_CLOCK_TIME_NONE when
 * giving wrong input.
 *
 * MT safe.
 */
GstClockTime
gst_clock_get_time (GstClock * clock)
{
  GstClockTime ret;

  g_return_val_if_fail (GST_IS_CLOCK (clock), GST_CLOCK_TIME_NONE);

  ret = gst_clock_get_internal_time (clock);

  GST_OBJECT_LOCK (clock);
  /* this will scale for rate and offset */
  ret = gst_clock_adjust_unlocked (clock, ret);
  GST_OBJECT_UNLOCK (clock);

  GST_CAT_DEBUG_OBJECT (GST_CAT_CLOCK, clock, "adjusted time %" GST_TIME_FORMAT,
      GST_TIME_ARGS (ret));

  return ret;
}

/**
 * gst_clock_set_calibration
 * @clock: a #GstClock to calibrate
 * @internal: a reference internal time
 * @external: a reference external time
 * @rate_num: the numerator of the rate of the clock relative to its
 *            internal time 
 * @rate_denom: the denominator of the rate of the clock
 *
 * Adjusts the rate and time of @clock. A rate of 1/1 is the normal speed of
 * the clock. Values bigger than 1/1 make the clock go faster.
 *
 * @internal and @external are calibration parameters that arrange that
 * gst_clock_get_time() should have been @external at internal time @internal.
 * This internal time should not be in the future; that is, it should be less
 * than the value of gst_clock_get_internal_time() when this function is called.
 *
 * Subsequent calls to gst_clock_get_time() will return clock times computed as
 * follows:
 *
 * <programlisting>
 *   time = (internal_time - @internal) * @rate_num / @rate_denom + @external
 * </programlisting>
 *
 * This formula is implemented in gst_clock_adjust_unlocked(). Of course, it
 * tries to do the integer arithmetic as precisely as possible.
 *
 * Note that gst_clock_get_time() always returns increasing values so when you
 * move the clock backwards, gst_clock_get_time() will report the previous value
 * until the clock catches up.
 *
 * MT safe.
 */
void
gst_clock_set_calibration (GstClock * clock, GstClockTime internal, GstClockTime
    external, GstClockTime rate_num, GstClockTime rate_denom)
{
  g_return_if_fail (GST_IS_CLOCK (clock));
  g_return_if_fail (rate_num >= 0);
  g_return_if_fail (rate_denom > 0);
  g_return_if_fail (internal <= gst_clock_get_internal_time (clock));

  GST_OBJECT_LOCK (clock);
  clock->internal_calibration = internal;
  clock->external_calibration = external;
  clock->rate_numerator = rate_num;
  clock->rate_denominator = rate_denom;
  GST_OBJECT_UNLOCK (clock);
}

/**
 * gst_clock_get_calibration
 * @clock: a #GstClock 
 * @internal: a location to store the internal time
 * @external: a location to store the external time
 * @rate_num: a location to store the rate numerator
 * @rate_denom: a location to store the rate denominator
 *
 * Gets the internal rate and reference time of @clock. See
 * gst_clock_set_calibration() for more information.
 *
 * @internal, @external, @rate_num, and @rate_denom can be left NULL if the
 * caller is not interested in the values.
 *
 * MT safe.
 */
void
gst_clock_get_calibration (GstClock * clock, GstClockTime * internal,
    GstClockTime * external, GstClockTime * rate_num, GstClockTime * rate_denom)
{
  g_return_if_fail (GST_IS_CLOCK (clock));

  GST_OBJECT_LOCK (clock);
  if (rate_num)
    *rate_num = clock->rate_numerator;
  if (rate_denom)
    *rate_denom = clock->rate_denominator;
  if (external)
    *external = clock->external_calibration;
  if (internal)
    *internal = clock->internal_calibration;
  GST_OBJECT_UNLOCK (clock);
}

/* will be called repeadedly to sample the master and slave clock
 * to recalibrate the clock */
static gboolean
gst_clock_slave_callback (GstClock * master, GstClockTime time,
    GstClockID id, GstClock * clock)
{
  GstClockTime stime, mtime;
  gdouble r_squared;

  stime = gst_clock_get_internal_time (clock);
  mtime = gst_clock_get_time (master);

  GST_CAT_DEBUG_OBJECT (GST_CAT_CLOCK, clock,
      "master %" GST_TIME_FORMAT ", slave %" GST_TIME_FORMAT,
      GST_TIME_ARGS (mtime), GST_TIME_ARGS (stime));

  gst_clock_add_observation (clock, stime, mtime, &r_squared);

  /* FIXME, we can use the r_squared value to adjust the timeout
   * value of the clockid */

  return TRUE;
}

/**
 * gst_clock_set_master
 * @clock: a #GstClock 
 * @master: a master #GstClock 
 *
 * Set @master as the master clock for @clock. @clock will be automatically
 * calibrated so that gst_clock_get_time() reports the same time as the
 * master clock.  
 * 
 * A clock provider that slaves its clock to a master can get the current
 * calibration values with gst_clock_get_calibration().
 *
 * Returns: TRUE if the clock is capable of being slaved to a master clock.
 *
 * MT safe.
 */
gboolean
gst_clock_set_master (GstClock * clock, GstClock * master)
{
  g_return_val_if_fail (GST_IS_CLOCK (clock), FALSE);
  g_return_val_if_fail (master != clock, FALSE);

  GST_OBJECT_LOCK (clock);
  /* we always allow setting the master to NULL */
  if (master && !GST_OBJECT_FLAG_IS_SET (clock, GST_CLOCK_FLAG_CAN_SET_MASTER))
    goto not_supported;

  GST_DEBUG_OBJECT (clock, "slaving to master clock %p", master);
  gst_object_replace ((GstObject **) & clock->master, (GstObject *) master);
  GST_OBJECT_UNLOCK (clock);

  GST_CLOCK_SLAVE_LOCK (clock);
  if (clock->clockid) {
    gst_clock_id_unschedule (clock->clockid);
    gst_clock_id_unref (clock->clockid);
    clock->clockid = NULL;
  }
  if (master) {
    clock->filling = TRUE;
    clock->time_index = 0;
    /* use the master periodic id to schedule sampling and
     * clock calibration. */
    clock->clockid = gst_clock_new_periodic_id (master,
        gst_clock_get_time (master), clock->timeout);
    gst_clock_id_wait_async (clock->clockid,
        (GstClockCallback) gst_clock_slave_callback, clock);
  }
  GST_CLOCK_SLAVE_UNLOCK (clock);

  return TRUE;

not_supported:
  {
    GST_DEBUG_OBJECT (clock, "cannot be slaved to a master clock");
    GST_OBJECT_UNLOCK (clock);
    return FALSE;
  }
}

/**
 * gst_clock_get_master
 * @clock: a #GstClock 
 *
 * Get the master clock that @clock is slaved to or NULL when the clock is
 * not slaved to any master clock.
 *
 * Returns: a master #GstClock or NULL when this clock is not slaved to a master
 * clock. Unref after usage.
 *
 * MT safe.
 */
GstClock *
gst_clock_get_master (GstClock * clock)
{
  GstClock *result = NULL;

  g_return_val_if_fail (GST_IS_CLOCK (clock), NULL);

  GST_OBJECT_LOCK (clock);
  if (clock->master)
    result = gst_object_ref (clock->master);
  GST_OBJECT_UNLOCK (clock);

  return result;
}

/* http://mathworld.wolfram.com/LeastSquaresFitting.html
 * with SLAVE_LOCK
 */
static gboolean
do_linear_regression (GstClock * clock, GstClockTime * m_num,
    GstClockTime * m_denom, GstClockTime * b, GstClockTime * xbase,
    gdouble * r_squared)
{
  GstClockTime *newx, *newy;
  GstClockTime xmin, ymin, xbar, ybar, xbar4, ybar4;
  GstClockTimeDiff sxx, sxy, syy;
  GstClockTime *x, *y;
  gint i, j;
  guint n;

  xbar = ybar = sxx = syy = sxy = 0;

  x = clock->times;
  y = clock->times + 2;
  n = clock->filling ? clock->time_index : clock->window_size;

#ifdef DEBUGGING_ENABLED
  GST_DEBUG ("doing regression on:");
  for (i = j = 0; i < n; i++, j += 4)
    GST_DEBUG ("  %" G_GUINT64_FORMAT "  %" G_GUINT64_FORMAT, x[j], y[j]);
#endif

  xmin = ymin = G_MAXUINT64;
  for (i = j = 0; i < n; i++, j += 4) {
    xmin = MIN (xmin, x[j]);
    ymin = MIN (ymin, y[j]);
  }

#ifdef DEBUGGING_ENABLED
  GST_DEBUG ("min x: %" G_GUINT64_FORMAT, xmin);
  GST_DEBUG ("min y: %" G_GUINT64_FORMAT, ymin);
#endif

  newx = clock->times + 1;
  newy = clock->times + 3;

  /* strip off unnecessary bits of precision */
  for (i = j = 0; i < n; i++, j += 4) {
    newx[j] = x[j] - xmin;
    newy[j] = y[j] - ymin;
  }

#ifdef DEBUGGING_ENABLED
  GST_DEBUG ("reduced numbers:");
  for (i = j = 0; i < n; i++, j += 4)
    GST_DEBUG ("  %" G_GUINT64_FORMAT "  %" G_GUINT64_FORMAT, newx[j], newy[j]);
#endif

  /* have to do this precisely otherwise the results are pretty much useless.
   * should guarantee that none of these accumulators can overflow */

  /* quantities on the order of 1e10 -> 30 bits; window size a max of 2^10, so
     this addition could end up around 2^40 or so -- ample headroom */
  for (i = j = 0; i < n; i++, j += 4) {
    xbar += newx[j];
    ybar += newy[j];
  }
  xbar /= n;
  ybar /= n;

#ifdef DEBUGGING_ENABLED
  GST_DEBUG ("  xbar  = %" G_GUINT64_FORMAT, xbar);
  GST_DEBUG ("  ybar  = %" G_GUINT64_FORMAT, ybar);
#endif

  /* multiplying directly would give quantities on the order of 1e20 -> 60 bits;
     times the window size that's 70 which is too much. Instead we (1) subtract
     off the xbar*ybar in the loop instead of after, to avoid accumulation; (2)
     shift off 4 bits from each multiplicand, giving an expected ceiling of 52
     bits, which should be enough. Need to check the incoming range and domain
     to ensure this is an appropriate loss of precision though. */
  xbar4 = xbar >> 4;
  ybar4 = ybar >> 4;
  for (i = j = 0; i < n; i++, j += 4) {
    GstClockTime newx4, newy4;

    newx4 = newx[j] >> 4;
    newy4 = newy[j] >> 4;

    sxx += newx4 * newx4 - xbar4 * xbar4;
    syy += newy4 * newy4 - ybar4 * ybar4;
    sxy += newx4 * newy4 - xbar4 * ybar4;
  }

  if (sxx == 0)
    goto invalid;

  *m_num = sxy;
  *m_denom = sxx;
  *xbase = xmin;
  *b = (ybar + ymin) - gst_util_uint64_scale (xbar, *m_num, *m_denom);
  *r_squared = ((double) sxy * (double) sxy) / ((double) sxx * (double) syy);

#ifdef DEBUGGING_ENABLED
  GST_DEBUG ("  m      = %g", ((double) *m_num) / *m_denom);
  GST_DEBUG ("  b      = %" G_GUINT64_FORMAT, *b);
  GST_DEBUG ("  xbase  = %" G_GUINT64_FORMAT, *xbase);
  GST_DEBUG ("  r2     = %g", *r_squared);
#endif

  return TRUE;

invalid:
  {
    return FALSE;
  }
}

/**
 * gst_clock_add_observation
 * @clock: a #GstClock 
 * @slave: a time on the slave
 * @master: a time on the master
 * @r_squared: a pointer to hold the result
 *
 * The time @master of the master clock and the time @slave of the slave
 * clock are added to the list of observations. If enough observations
 * are available, a linear regression algorithm is run on the
 * observations and @clock is recalibrated.
 *
 * Returns: TRUE if enough observations were added to run the 
 * regression algorithm.
 *
 * MT safe.
 */
gboolean
gst_clock_add_observation (GstClock * clock, GstClockTime slave,
    GstClockTime master, gdouble * r_squared)
{
  GstClockTime m_num, m_denom, b, xbase;

  g_return_val_if_fail (GST_IS_CLOCK (clock), FALSE);
  g_return_val_if_fail (r_squared != NULL, FALSE);

  GST_CLOCK_SLAVE_LOCK (clock);

  clock->times[(4 * clock->time_index)] = slave;
  clock->times[(4 * clock->time_index) + 2] = master;

  clock->time_index++;
  if (clock->time_index == clock->window_size) {
    clock->filling = FALSE;
    clock->time_index = 0;
  }

  if (clock->filling && clock->time_index < clock->window_threshold)
    goto filling;

  if (!do_linear_regression (clock, &m_num, &m_denom, &b, &xbase, r_squared))
    goto invalid;

  GST_CLOCK_SLAVE_UNLOCK (clock);

  GST_CAT_LOG_OBJECT (GST_CAT_CLOCK, clock,
      "adjusting clock to m=%" G_GUINT64_FORMAT "/%" G_GUINT64_FORMAT ", b=%"
      G_GUINT64_FORMAT " (rsquared=%g)", m_num, m_denom, b, *r_squared);

  /* if we have a valid regression, adjust the clock */
  gst_clock_set_calibration (clock, xbase, b, m_num, m_denom);

  return TRUE;

filling:
  {
    GST_CLOCK_SLAVE_UNLOCK (clock);
    return FALSE;
  }
invalid:
  {
    /* no valid regression has been done, ignore the result then */
    GST_CLOCK_SLAVE_UNLOCK (clock);
    return TRUE;
  }
}

static void
gst_clock_update_stats (GstClock * clock)
{
}

static void
gst_clock_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstClock *clock;

  clock = GST_CLOCK (object);

  switch (prop_id) {
    case PROP_STATS:
      GST_OBJECT_LOCK (clock);
      clock->stats = g_value_get_boolean (value);
      GST_OBJECT_UNLOCK (clock);
      g_object_notify (object, "stats");
      break;
    case PROP_WINDOW_SIZE:
      GST_CLOCK_SLAVE_LOCK (clock);
      clock->window_size = g_value_get_int (value);
      clock->window_threshold =
          MIN (clock->window_threshold, clock->window_size);
      clock->times =
          g_renew (GstClockTime, clock->times, 4 * clock->window_size);
      GST_CLOCK_SLAVE_UNLOCK (clock);
      break;
    case PROP_WINDOW_THRESHOLD:
      GST_CLOCK_SLAVE_LOCK (clock);
      clock->window_threshold =
          MIN (g_value_get_int (value), clock->window_size);
      GST_CLOCK_SLAVE_UNLOCK (clock);
      break;
    case PROP_TIMEOUT:
      GST_CLOCK_SLAVE_LOCK (clock);
      clock->timeout = g_value_get_uint64 (value);
      GST_CLOCK_SLAVE_UNLOCK (clock);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_clock_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstClock *clock;

  clock = GST_CLOCK (object);

  switch (prop_id) {
    case PROP_STATS:
      GST_OBJECT_LOCK (clock);
      g_value_set_boolean (value, clock->stats);
      GST_OBJECT_UNLOCK (clock);
      break;
    case PROP_WINDOW_SIZE:
      GST_CLOCK_SLAVE_LOCK (clock);
      g_value_set_int (value, clock->window_size);
      GST_CLOCK_SLAVE_UNLOCK (clock);
      break;
    case PROP_WINDOW_THRESHOLD:
      GST_CLOCK_SLAVE_LOCK (clock);
      g_value_set_int (value, clock->window_threshold);
      GST_CLOCK_SLAVE_UNLOCK (clock);
      break;
    case PROP_TIMEOUT:
      GST_CLOCK_SLAVE_LOCK (clock);
      g_value_set_uint64 (value, clock->timeout);
      GST_CLOCK_SLAVE_UNLOCK (clock);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
