/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *
 * gsttask.c: Streaming tasks
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
 * SECTION:gsttask
 * @short_description: Abstraction of GStreamer streaming threads.
 * @see_also: #GstElement, #GstPad
 *
 * #GstTask is used by #GstElement and #GstPad to provide the data passing
 * threads in a #GstPipeline.
 *
 * A #GstPad will typically start a #GstTask to push or pull data to/from the
 * peer pads. Most source elements start a #GstTask to push data. In some cases
 * a demuxer element can start a #GstTask to pull data from a peer element. This
 * is typically done when the demuxer can perform random access on the upstream
 * peer element for improved performance.
 *
 * Although convenience functions exist on #GstPad to start/pause/stop tasks, it 
 * might sometimes be needed to create a #GstTask manually if it is not related to
 * a #GstPad.
 *
 * Before the #GstTask can be run, it needs a #GStaticRecMutex that can be set with
 * gst_task_set_lock().
 *
 * The task can be started, paused and stopped with gst_task_start(), gst_task_pause()
 * and gst_task_stop() respectively or with the gst_task_set_state() function.
 *
 * A #GstTask will repeatedly call the #GstTaskFunction with the user data
 * that was provided when creating the task with gst_task_create(). While calling
 * the function it will acquire the provided lock. The provided lock is released
 * when the task pauses or stops.
 *
 * Stopping a task with gst_task_stop() will not immediately make sure the task is
 * not running anymore. Use gst_task_join() to make sure the task is completely
 * stopped and the thread is stopped.
 *
 * After creating a #GstTask, use gst_object_unref() to free its resources. This can
 * only be done it the task is not running anymore.
 *
 * Last reviewed on 2006-02-13 (0.10.4)
 */

#include "gst_private.h"

#include "gstinfo.h"
#include "gsttask.h"

GST_DEBUG_CATEGORY_STATIC (task_debug);
#define GST_CAT_DEFAULT (task_debug)

#define GST_TASK_GET_PRIVATE(obj)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_TASK, GstTaskPrivate))

struct _GstTaskPrivate
{
  /* callbacks for managing the thread of this task */
  GstTaskThreadCallbacks thr_callbacks;
  gpointer thr_user_data;
  GDestroyNotify thr_notify;
};

static void gst_task_class_init (GstTaskClass * klass);
static void gst_task_init (GstTask * task);
static void gst_task_finalize (GObject * object);

static void gst_task_func (GstTask * task, GstTaskClass * tclass);

static GStaticMutex pool_lock = G_STATIC_MUTEX_INIT;

#define _do_init \
{ \
  GST_DEBUG_CATEGORY_INIT (task_debug, "task", 0, "Processing tasks"); \
}

G_DEFINE_TYPE_WITH_CODE (GstTask, gst_task, GST_TYPE_OBJECT, _do_init);

static void
gst_task_class_init (GstTaskClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  g_type_class_add_private (klass, sizeof (GstTaskPrivate));

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_task_finalize);

  klass->pool = g_thread_pool_new (
      (GFunc) gst_task_func, klass, -1, FALSE, NULL);
}

static void
gst_task_init (GstTask * task)
{
  task->priv = GST_TASK_GET_PRIVATE (task);
  task->running = FALSE;
  task->abidata.ABI.thread = NULL;
  task->lock = NULL;
  task->cond = g_cond_new ();
  task->state = GST_TASK_STOPPED;
}

static void
gst_task_finalize (GObject * object)
{
  GstTask *task = GST_TASK (object);
  GstTaskPrivate *priv = task->priv;

  GST_DEBUG ("task %p finalize", task);

  if (priv->thr_notify)
    priv->thr_notify (priv->thr_user_data);
  priv->thr_notify = NULL;
  priv->thr_user_data = NULL;

  /* task thread cannot be running here since it holds a ref
   * to the task so that the finalize could not have happened */
  g_cond_free (task->cond);
  task->cond = NULL;

  G_OBJECT_CLASS (gst_task_parent_class)->finalize (object);
}

static void
gst_task_func (GstTask * task, GstTaskClass * tclass)
{
  GStaticRecMutex *lock;
  GThread *tself;
  GstTaskPrivate *priv;

  priv = task->priv;

  tself = g_thread_self ();

  GST_DEBUG ("Entering task %p, thread %p", task, tself);

  /* we have to grab the lock to get the mutex. We also
   * mark our state running so that nobody can mess with
   * the mutex. */
  GST_OBJECT_LOCK (task);
  if (task->state == GST_TASK_STOPPED)
    goto exit;
  lock = GST_TASK_GET_LOCK (task);
  if (G_UNLIKELY (lock == NULL))
    goto no_lock;
  task->abidata.ABI.thread = tself;
  GST_OBJECT_UNLOCK (task);

  /* fire the enter_thread callback when we need to */
  if (priv->thr_callbacks.enter_thread)
    priv->thr_callbacks.enter_thread (task, tself, priv->thr_user_data);

  /* locking order is TASK_LOCK, LOCK */
  g_static_rec_mutex_lock (lock);
  GST_OBJECT_LOCK (task);
  while (G_LIKELY (task->state != GST_TASK_STOPPED)) {
    while (G_UNLIKELY (task->state == GST_TASK_PAUSED)) {
      gint t;

      t = g_static_rec_mutex_unlock_full (lock);
      if (t <= 0) {
        g_warning ("wrong STREAM_LOCK count %d", t);
      }
      GST_TASK_SIGNAL (task);
      GST_TASK_WAIT (task);
      GST_OBJECT_UNLOCK (task);
      /* locking order.. */
      if (t > 0)
        g_static_rec_mutex_lock_full (lock, t);

      GST_OBJECT_LOCK (task);
      if (G_UNLIKELY (task->state == GST_TASK_STOPPED))
        goto done;
    }
    GST_OBJECT_UNLOCK (task);

    task->func (task->data);

    GST_OBJECT_LOCK (task);
  }
done:
  GST_OBJECT_UNLOCK (task);
  g_static_rec_mutex_unlock (lock);

  GST_OBJECT_LOCK (task);
  task->abidata.ABI.thread = NULL;

exit:
  /* now we allow messing with the lock again by setting the running flag to
   * FALSE. Together with the SIGNAL this is the sign for the _join() to 
   * complete. 
   * Note that we still have not dropped the final ref on the task. We could
   * check here if there is a pending join() going on and drop the last ref
   * before releasing the lock as we can be sure that a ref is held by the
   * caller of the join(). */
  task->running = FALSE;
  if (priv->thr_callbacks.leave_thread) {
    /* fire the leave_thread callback when we need to. We need to do this before
     * we signal the task and with the task lock released. */
    GST_OBJECT_UNLOCK (task);
    priv->thr_callbacks.leave_thread (task, tself, priv->thr_user_data);
    GST_OBJECT_LOCK (task);
  }
  GST_TASK_SIGNAL (task);
  GST_OBJECT_UNLOCK (task);

  GST_DEBUG ("Exit task %p, thread %p", task, g_thread_self ());

  gst_object_unref (task);
  return;

no_lock:
  {
    g_warning ("starting task without a lock");
    goto exit;
  }
}

/**
 * gst_task_cleanup_all:
 *
 * Wait for all tasks to be stopped. This is mainly used internally
 * to ensure proper cleanup of internal data structures in test suites.
 *
 * MT safe.
 */
void
gst_task_cleanup_all (void)
{
  GstTaskClass *klass;

  if ((klass = g_type_class_peek (GST_TYPE_TASK))) {
    g_static_mutex_lock (&pool_lock);
    if (klass->pool) {
      /* Shut down all the threads, we still process the ones scheduled
       * because the unref happens in the thread function.
       * Also wait for currently running ones to finish. */
      g_thread_pool_free (klass->pool, FALSE, TRUE);
      /* create new pool, so we can still do something after this
       * call. */
      klass->pool = g_thread_pool_new (
          (GFunc) gst_task_func, klass, -1, FALSE, NULL);
    }
    g_static_mutex_unlock (&pool_lock);
  }
}

/**
 * gst_task_create:
 * @func: The #GstTaskFunction to use
 * @data: User data to pass to @func
 *
 * Create a new Task that will repeatedly call the provided @func
 * with @data as a parameter. Typically the task will run in
 * a new thread.
 *
 * The function cannot be changed after the task has been created. You
 * must create a new #GstTask to change the function.
 *
 * This function will not yet create and start a thread. Use gst_task_start() or
 * gst_task_pause() to create and start the GThread.
 *
 * Before the task can be used, a #GStaticRecMutex must be configured using the
 * gst_task_set_lock() function. This lock will always be acquired while
 * @func is called.
 *
 * Returns: A new #GstTask.
 *
 * MT safe.
 */
GstTask *
gst_task_create (GstTaskFunction func, gpointer data)
{
  GstTask *task;

  task = g_object_new (GST_TYPE_TASK, NULL);
  task->func = func;
  task->data = data;

  GST_DEBUG ("Created task %p", task);

  return task;
}

/**
 * gst_task_set_lock:
 * @task: The #GstTask to use
 * @mutex: The #GMutex to use
 *
 * Set the mutex used by the task. The mutex will be acquired before
 * calling the #GstTaskFunction.
 *
 * This function has to be called before calling gst_task_pause() or
 * gst_task_start().
 *
 * MT safe.
 */
void
gst_task_set_lock (GstTask * task, GStaticRecMutex * mutex)
{
  GST_OBJECT_LOCK (task);
  if (G_UNLIKELY (task->running))
    goto is_running;
  GST_TASK_GET_LOCK (task) = mutex;
  GST_OBJECT_UNLOCK (task);

  return;

  /* ERRORS */
is_running:
  {
    GST_OBJECT_UNLOCK (task);
    g_warning ("cannot call set_lock on a running task");
  }
}

/**
 * gst_task_set_thread_callbacks:
 * @task: The #GstTask to use
 * @callbacks: a #GstTaskThreadCallbacks pointer
 * @user_data: user data passed to the callbacks
 * @notify: called when @user_data is no longer referenced
 *
 * Set callbacks which will be executed when a new thread is needed, the thread
 * function is entered and left and when the thread is joined.
 *
 * By default a thread for @task will be created from a default thread pool.
 *
 * Objects can use custom GThreads or can perform additional configuration of
 * the threads (such as changing the thread priority) by installing callbacks.
 *
 * Since: 0.10.24
 *
 * MT safe.
 */
void
gst_task_set_thread_callbacks (GstTask * task,
    GstTaskThreadCallbacks * callbacks, gpointer user_data,
    GDestroyNotify notify)
{
  GDestroyNotify old_notify;

  g_return_if_fail (task != NULL);
  g_return_if_fail (GST_IS_TASK (task));
  g_return_if_fail (callbacks != NULL);

  GST_OBJECT_LOCK (task);
  old_notify = task->priv->thr_notify;

  if (old_notify) {
    gpointer old_data;

    old_data = task->priv->thr_user_data;

    task->priv->thr_user_data = NULL;
    task->priv->thr_notify = NULL;
    GST_OBJECT_UNLOCK (task);

    old_notify (old_data);

    GST_OBJECT_LOCK (task);
  }
  task->priv->thr_callbacks = *callbacks;
  task->priv->thr_user_data = user_data;
  task->priv->thr_notify = notify;
  GST_OBJECT_UNLOCK (task);
}

/**
 * gst_task_get_state:
 * @task: The #GstTask to query
 *
 * Get the current state of the task.
 *
 * Returns: The #GstTaskState of the task
 *
 * MT safe.
 */
GstTaskState
gst_task_get_state (GstTask * task)
{
  GstTaskState result;

  g_return_val_if_fail (GST_IS_TASK (task), GST_TASK_STOPPED);

  GST_OBJECT_LOCK (task);
  result = task->state;
  GST_OBJECT_UNLOCK (task);

  return result;
}

/* make sure the task is running and start a thread if it's not.
 * This function must be called with the task LOCK. */
static gboolean
start_task (GstTask * task)
{
  gboolean res = TRUE;
  GstTaskClass *tclass;
  GError *error = NULL;

  /* new task, We ref before so that it remains alive while
   * the thread is running. */
  gst_object_ref (task);
  /* mark task as running so that a join will wait until we schedule
   * and exit the task function. */
  task->running = TRUE;

  tclass = GST_TASK_GET_CLASS (task);

  /* push on the thread pool */
  g_static_mutex_lock (&pool_lock);
  g_thread_pool_push (tclass->pool, task, &error);
  g_static_mutex_unlock (&pool_lock);

  if (error != NULL) {
    g_warning ("failed to create thread: %s", error->message);
    g_error_free (error);
    res = FALSE;
  }
  return res;
}


/**
 * gst_task_set_state:
 * @task: a #GstTask
 * @state: the new task state
 *
 * Sets the state of @task to @state.
 *
 * The @task must have a lock associated with it using
 * gst_task_set_lock() when going to GST_TASK_STARTED or GST_TASK_PAUSED or
 * this function will return %FALSE.
 *
 * Returns: %TRUE if the state could be changed.
 *
 * Since: 0.10.24
 *
 * MT safe.
 */
gboolean
gst_task_set_state (GstTask * task, GstTaskState state)
{
  GstTaskState old;
  gboolean res = TRUE;

  g_return_val_if_fail (GST_IS_TASK (task), FALSE);

  GST_DEBUG_OBJECT (task, "Changing task %p to state %d", task, state);

  GST_OBJECT_LOCK (task);
  if (state != GST_TASK_STOPPED)
    if (G_UNLIKELY (GST_TASK_GET_LOCK (task) == NULL))
      goto no_lock;

  /* if the state changed, do our thing */
  old = task->state;
  if (old != state) {
    task->state = state;
    switch (old) {
      case GST_TASK_STOPPED:
        /* If the task already has a thread scheduled we don't have to do
         * anything. */
        if (G_UNLIKELY (!task->running))
          res = start_task (task);
        break;
      case GST_TASK_PAUSED:
        /* when we are paused, signal to go to the new state */
        GST_TASK_SIGNAL (task);
        break;
      case GST_TASK_STARTED:
        /* if we were started, we'll go to the new state after the next
         * iteration. */
        break;
    }
  }
  GST_OBJECT_UNLOCK (task);

  return res;

  /* ERRORS */
no_lock:
  {
    GST_WARNING_OBJECT (task, "state %d set on task without a lock", state);
    GST_OBJECT_UNLOCK (task);
    g_warning ("task without a lock can't be set to state %d", state);
    return FALSE;
  }
}

/**
 * gst_task_start:
 * @task: The #GstTask to start
 *
 * Starts @task. The @task must have a lock associated with it using
 * gst_task_set_lock() or this function will return %FALSE.
 *
 * Returns: %TRUE if the task could be started.
 *
 * MT safe.
 */
gboolean
gst_task_start (GstTask * task)
{
  return gst_task_set_state (task, GST_TASK_STARTED);
}

/**
 * gst_task_stop:
 * @task: The #GstTask to stop
 *
 * Stops @task. This method merely schedules the task to stop and
 * will not wait for the task to have completely stopped. Use
 * gst_task_join() to stop and wait for completion.
 *
 * Returns: %TRUE if the task could be stopped.
 *
 * MT safe.
 */
gboolean
gst_task_stop (GstTask * task)
{
  return gst_task_set_state (task, GST_TASK_STOPPED);
}

/**
 * gst_task_pause:
 * @task: The #GstTask to pause
 *
 * Pauses @task. This method can also be called on a task in the
 * stopped state, in which case a thread will be started and will remain
 * in the paused state. This function does not wait for the task to complete
 * the paused state.
 *
 * Returns: %TRUE if the task could be paused.
 *
 * MT safe.
 */
gboolean
gst_task_pause (GstTask * task)
{
  return gst_task_set_state (task, GST_TASK_PAUSED);
}

/**
 * gst_task_join:
 * @task: The #GstTask to join
 *
 * Joins @task. After this call, it is safe to unref the task
 * and clean up the lock set with gst_task_set_lock().
 *
 * The task will automatically be stopped with this call.
 *
 * This function cannot be called from within a task function as this
 * would cause a deadlock. The function will detect this and print a 
 * g_warning.
 *
 * Returns: %TRUE if the task could be joined.
 *
 * MT safe.
 */
gboolean
gst_task_join (GstTask * task)
{
  GThread *tself;

  g_return_val_if_fail (GST_IS_TASK (task), FALSE);

  tself = g_thread_self ();

  GST_DEBUG_OBJECT (task, "Joining task %p, thread %p", task, tself);

  /* we don't use a real thread join here because we are using
   * thread pools */
  GST_OBJECT_LOCK (task);
  if (G_UNLIKELY (tself == task->abidata.ABI.thread))
    goto joining_self;
  task->state = GST_TASK_STOPPED;
  /* signal the state change for when it was blocked in PAUSED. */
  GST_TASK_SIGNAL (task);
  /* we set the running flag when pushing the task on the thread pool.
   * This means that the task function might not be called when we try
   * to join it here. */
  while (G_LIKELY (task->running))
    GST_TASK_WAIT (task);
  GST_OBJECT_UNLOCK (task);

  GST_DEBUG_OBJECT (task, "Joined task %p", task);

  return TRUE;

  /* ERRORS */
joining_self:
  {
    GST_WARNING_OBJECT (task, "trying to join task from its thread");
    GST_OBJECT_UNLOCK (task);
    g_warning ("\nTrying to join task %p from its thread would deadlock.\n"
        "You cannot change the state of an element from its streaming\n"
        "thread. Use g_idle_add() or post a GstMessage on the bus to\n"
        "schedule the state change from the main thread.\n", task);
    return FALSE;
  }
}
