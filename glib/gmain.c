/* GLIB - Library of useful routines for C programming
 * Copyright (C) 1995-1997  Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * gmain.c: Main loop abstraction, timeouts, and idle functions
 * Copyright 1998 Owen Taylor
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*
 * Modified by the GLib Team and others 1997-2000.  See the AUTHORS
 * file for a list of people on the GLib Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GLib at ftp://ftp.gtk.org/pub/gtk/. 
 */

/* 
 * MT safe
 */

#include "config.h"

/* uncomment the next line to get poll() debugging info */
/* #define G_MAIN_POLL_DEBUG */

#include "glib.h"
#include <sys/types.h>
#include <time.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif /* HAVE_SYS_TIME_H */
#ifdef GLIB_HAVE_SYS_POLL_H
#  include <sys/poll.h>
#  undef events	 /* AIX 4.1.5 & 4.3.2 define this for SVR3,4 compatibility */
#  undef revents /* AIX 4.1.5 & 4.3.2 define this for SVR3,4 compatibility */
#endif /* GLIB_HAVE_SYS_POLL_H */
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#include <errno.h>

#ifdef G_OS_WIN32
#define STRICT
#include <windows.h>
#endif /* G_OS_WIN32 */

#ifdef G_OS_BEOS
#include <net/socket.h>
#endif /* G_OS_BEOS */

/* Types */

typedef struct _GTimeoutSource GTimeoutSource;
typedef struct _GPollRec GPollRec;
typedef struct _GSourceCallback GSourceCallback;

typedef enum
{
  G_SOURCE_READY = 1 << G_HOOK_FLAG_USER_SHIFT,
  G_SOURCE_CAN_RECURSE = 1 << (G_HOOK_FLAG_USER_SHIFT + 1)
} GSourceFlags;

struct _GMainContext
{
#ifdef G_THREADS_ENABLED
  /* The following lock is used for both the list of sources
   * and the list of poll records
   */
  GMutex *mutex;
  GThread *thread;
#endif  

  GPtrArray *pending_dispatches;
  gint timeout;			/* Timeout for current iteration */

  guint next_id;
  GSource *source_list;
  gint in_check_or_prepare;

  GPollRec *poll_records;
  GPollRec *poll_free_list;
  GMemChunk *poll_chunk;
  guint n_poll_records;
  GPollFD *cached_poll_array;
  gint cached_poll_array_size;

#ifdef G_THREADS_ENABLED  
#ifndef G_OS_WIN32
/* this pipe is used to wake up the main loop when a source is added.
 */
  gint wake_up_pipe[2];
#else /* G_OS_WIN32 */
  HANDLE wake_up_semaphore;
#endif /* G_OS_WIN32 */

  GPollFD wake_up_rec;
  gboolean poll_waiting;

/* Flag indicating whether the set of fd's changed during a poll */
  gboolean poll_changed;
#endif /* G_THREADS_ENABLED */

  GPollFunc poll_func;

  GTimeVal current_time;
  gboolean time_is_current;
};

struct _GSourceCallback
{
  guint ref_count;
  GSourceFunc func;
  gpointer    data;
  GDestroyNotify notify;
};

struct _GMainLoop
{
  GMainContext *context;
  gboolean is_running;

#ifdef G_THREADS_ENABLED
  GMutex *mutex;
  GCond *sem_cond;
#endif /* G_THREADS_ENABLED */
};

struct _GTimeoutSource
{
  GSource     source;
  GTimeVal    expiration;
  gint        interval;
};

struct _GPollRec
{
  gint priority;
  GPollFD *fd;
  GPollRec *next;
};

#ifdef G_THREADS_ENABLED
#define LOCK_CONTEXT(context) g_mutex_lock(context->mutex)
#define UNLOCK_CONTEXT(context) g_mutex_unlock(context->mutex)
#define LOCK_LOOP(loop) g_mutex_lock(loop->mutex)
#define UNLOCK_LOOP(loop) g_mutex_unlock(loop->mutex)
#else
#define LOCK_CONTEXT(context) (void)0
#define UNLOCK_CONTEXT(context) (void)0
#define LOCK_LOOP(context) (void)0
#define UNLOCK_LOOP(context) (void)0
#endif

#define SOURCE_DESTROYED(source) (((source)->flags & G_HOOK_FLAG_ACTIVE) == 0)

#define SOURCE_UNREF(source, context)                       \
   G_STMT_START {                                           \
    if ((source)->ref_count > 1)                            \
      (source)->ref_count--;                                \
    else                                                    \
      g_source_unref_internal ((source), (context), TRUE);  \
   } G_STMT_END


/* Forward declarations */

static void g_source_unref_internal             (GSource      *source,
						 GMainContext *context,
						 gboolean      have_lock);
static void g_source_destroy_internal           (GSource      *source,
						 GMainContext *context,
						 gboolean      have_lock);
static void g_main_context_poll                 (GMainContext *context,
						 gint          timeout,
						 gint          priority,
						 GPollFD      *fds,
						 gint          n_fds);
static void g_main_context_add_poll_unlocked    (GMainContext *context,
						 gint          priority,
						 GPollFD      *fd);
static void g_main_context_remove_poll_unlocked (GMainContext *context,
						 GPollFD      *fd);
static void g_main_context_wakeup               (GMainContext *context);

static gboolean g_timeout_prepare  (GSource     *source,
				    gint        *timeout);
static gboolean g_timeout_check    (GSource     *source);
static gboolean g_timeout_dispatch (GSource     *source,
				    GSourceFunc  callback,
				    gpointer     user_data);
static gboolean g_idle_prepare     (GSource     *source,
				    gint        *timeout);
static gboolean g_idle_check       (GSource     *source);
static gboolean g_idle_dispatch    (GSource     *source,
				    GSourceFunc  callback,
				    gpointer     user_data);

G_LOCK_DEFINE_STATIC (main_loop);
static GMainContext *default_main_context;

static GSourceFuncs timeout_funcs =
{
  g_timeout_prepare,
  g_timeout_check,
  g_timeout_dispatch,
  NULL
};

static GSourceFuncs idle_funcs =
{
  g_idle_prepare,
  g_idle_check,
  g_idle_dispatch,
  NULL
};

#ifdef HAVE_POLL
/* SunOS has poll, but doesn't provide a prototype. */
#  if defined (sun) && !defined (__SVR4)
extern gint poll (GPollFD *ufds, guint nfsd, gint timeout);
#  endif  /* !sun */
#else	/* !HAVE_POLL */

#ifdef G_OS_WIN32

static gint
g_poll (GPollFD *fds,
	guint    nfds,
	gint     timeout)
{
  HANDLE handles[MAXIMUM_WAIT_OBJECTS];
  gboolean poll_msgs = FALSE;
  GPollFD *f;
  DWORD ready;
  MSG msg;
  UINT timer;
  gint nhandles = 0;

  for (f = fds; f < &fds[nfds]; ++f)
    if (f->fd >= 0)
      {
	if (f->events & G_IO_IN)
	  {
	    if (f->fd == G_WIN32_MSG_HANDLE)
	      poll_msgs = TRUE;
	    else
	      {
#ifdef G_MAIN_POLL_DEBUG
		g_print ("g_poll: waiting for %#x\n", f->fd);
#endif
		handles[nhandles++] = (HANDLE) f->fd;
	      }
	  }
      }

  if (timeout == -1)
    timeout = INFINITE;

  if (poll_msgs)
    {
      /* Waiting for messages, and maybe events */
      if (nhandles == 0)
	{
	  if (timeout == INFINITE)
	    {
	      /* Waiting just for messages, infinite timeout
	       * -> Use PeekMessage, then WaitMessage
	       */
#ifdef G_MAIN_POLL_DEBUG
	      g_print ("PeekMessage, then WaitMessage\n");
#endif
	      if (PeekMessage (&msg, NULL, 0, 0, PM_NOREMOVE))
		ready = WAIT_OBJECT_0;
	      else if (!WaitMessage ())
		g_warning ("g_poll: WaitMessage failed");
	      ready = WAIT_OBJECT_0;
	    }
	  else if (timeout == 0)
	    {
	      /* Waiting just for messages, zero timeout
	       * -> Use PeekMessage
	       */
#ifdef G_MAIN_POLL_DEBUG
	      g_print ("PeekMessage\n");
#endif
	      if (PeekMessage (&msg, NULL, 0, 0, PM_NOREMOVE))
		ready = WAIT_OBJECT_0;
	      else
		ready = WAIT_TIMEOUT;
	    }
	  else
	    {
	      /* Waiting just for messages, some timeout
	       * -> First try PeekMessage, then set a timer, wait for message,
	       * kill timer, use PeekMessage
	       */
#ifdef G_MAIN_POLL_DEBUG
	      g_print ("PeekMessage\n");
#endif
	      if (PeekMessage (&msg, NULL, 0, 0, PM_NOREMOVE))
		ready = WAIT_OBJECT_0;
	      else if ((timer = SetTimer (NULL, 0, timeout, NULL)) == 0)
		g_warning ("g_poll: SetTimer failed");
	      else
		{
#ifdef G_MAIN_POLL_DEBUG
		  g_print ("WaitMessage\n");
#endif
		  WaitMessage ();
		  KillTimer (NULL, timer);
		  if (PeekMessage (&msg, NULL, 0, 0, PM_NOREMOVE)
		      && msg.message != WM_TIMER)
		    ready = WAIT_OBJECT_0;
		  else
		    ready = WAIT_TIMEOUT;
		}
	    }
	}
      else
	{
	  /* Wait for either message or event
	   * -> Use MsgWaitForMultipleObjects
	   */
#ifdef G_MAIN_POLL_DEBUG
	  g_print ("MsgWaitForMultipleObjects(%d, %d)\n", nhandles, timeout);
#endif
	  ready = MsgWaitForMultipleObjects (nhandles, handles, FALSE,
					     timeout, QS_ALLINPUT);

	  if (ready == WAIT_FAILED)
	    g_warning ("g_poll: MsgWaitForMultipleObjects failed");
	}
    }
  else if (nhandles == 0)
    {
      /* Wait for nothing (huh?) */
      return 0;
    }
  else
    {
      /* Wait for just events
       * -> Use WaitForMultipleObjects
       */
#ifdef G_MAIN_POLL_DEBUG
      g_print ("WaitForMultipleObjects(%d, %d)\n", nhandles, timeout);
#endif
      ready = WaitForMultipleObjects (nhandles, handles, FALSE, timeout);
      if (ready == WAIT_FAILED)
	g_warning ("g_poll: WaitForMultipleObjects failed");
    }

  for (f = fds; f < &fds[nfds]; ++f)
    f->revents = 0;

  if (ready == WAIT_FAILED)
    return -1;
  else if (ready == WAIT_TIMEOUT)
    return 0;
  else if (poll_msgs && ready == WAIT_OBJECT_0 + nhandles)
    {
      for (f = fds; f < &fds[nfds]; ++f)
	if (f->fd >= 0)
	  {
	    if (f->events & G_IO_IN)
	      if (f->fd == G_WIN32_MSG_HANDLE)
		f->revents |= G_IO_IN;
	  }
    }
  else if (ready >= WAIT_OBJECT_0 && ready < WAIT_OBJECT_0 + nhandles)
    for (f = fds; f < &fds[nfds]; ++f)
      {
	if ((f->events & G_IO_IN)
	    && f->fd == (gint) handles[ready - WAIT_OBJECT_0])
	  {
	    f->revents |= G_IO_IN;
#ifdef G_MAIN_POLL_DEBUG
	    g_print ("g_poll: got event %#x\n", f->fd);
#endif
#if 0
	    ResetEvent ((HANDLE) f->fd);
#endif
	  }
      }
    
  if (ready >= WAIT_OBJECT_0 && ready < WAIT_OBJECT_0 + nhandles)
    return ready - WAIT_OBJECT_0 + 1;
  else
    return 0;
}

#else  /* !G_OS_WIN32 */

/* The following implementation of poll() comes from the GNU C Library.
 * Copyright (C) 1994, 1996, 1997 Free Software Foundation, Inc.
 */

#include <string.h> /* for bzero on BSD systems */

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif /* HAVE_SYS_SELECT_H */

#ifdef G_OS_BEOS
#undef NO_FD_SET
#endif /* G_OS_BEOS */

#ifndef NO_FD_SET
#  define SELECT_MASK fd_set
#else /* !NO_FD_SET */
#  ifndef _AIX
typedef long fd_mask;
#  endif /* _AIX */
#  ifdef _IBMR2
#    define SELECT_MASK void
#  else /* !_IBMR2 */
#    define SELECT_MASK int
#  endif /* !_IBMR2 */
#endif /* !NO_FD_SET */

static gint 
g_poll (GPollFD *fds,
	guint    nfds,
	gint     timeout)
{
  struct timeval tv;
  SELECT_MASK rset, wset, xset;
  GPollFD *f;
  int ready;
  int maxfd = 0;

  FD_ZERO (&rset);
  FD_ZERO (&wset);
  FD_ZERO (&xset);

  for (f = fds; f < &fds[nfds]; ++f)
    if (f->fd >= 0)
      {
	if (f->events & G_IO_IN)
	  FD_SET (f->fd, &rset);
	if (f->events & G_IO_OUT)
	  FD_SET (f->fd, &wset);
	if (f->events & G_IO_PRI)
	  FD_SET (f->fd, &xset);
	if (f->fd > maxfd && (f->events & (G_IO_IN|G_IO_OUT|G_IO_PRI)))
	  maxfd = f->fd;
      }

  tv.tv_sec = timeout / 1000;
  tv.tv_usec = (timeout % 1000) * 1000;

  ready = select (maxfd + 1, &rset, &wset, &xset,
		  timeout == -1 ? NULL : &tv);
  if (ready > 0)
    for (f = fds; f < &fds[nfds]; ++f)
      {
	f->revents = 0;
	if (f->fd >= 0)
	  {
	    if (FD_ISSET (f->fd, &rset))
	      f->revents |= G_IO_IN;
	    if (FD_ISSET (f->fd, &wset))
	      f->revents |= G_IO_OUT;
	    if (FD_ISSET (f->fd, &xset))
	      f->revents |= G_IO_PRI;
	  }
      }

  return ready;
}

#endif /* !G_OS_WIN32 */

#endif	/* !HAVE_POLL */

/* Called to clean up when a thread terminates
 */
static void
g_main_context_destroy (GMainContext *context)
{
  GSource *source;

  /* We need the lock here only because g_source_destroy expects
   * to be able to unlock when destroying the source's data
   */
  LOCK_CONTEXT (context);
  source = context->source_list;
  while (source)
    {
      GSource *next = source->next;
      g_source_destroy_internal (source, context, TRUE);
      source = next;
    }
  UNLOCK_CONTEXT (context);

#ifdef G_THREADS_ENABLED  
  g_mutex_free (context->mutex);
#endif

  g_ptr_array_free (context->pending_dispatches, TRUE);
  g_free (context->cached_poll_array);
  
  g_mem_chunk_destroy (context->poll_chunk);

#ifdef G_THREADS_ENABLED
  if (g_thread_supported())
    {
#ifndef G_OS_WIN32
      close (context->wake_up_pipe[0]);
      close (context->wake_up_pipe[1]);
#else
      CloseHandle (context->wake_up_semaphore);
#endif
    }
#endif
  
  g_free (context);
}

/**
 * g_main_context_get:
 * @thread: a #GThread
 * 
 * Retrieves the main loop context for a particular thread. This
 * will create the main context for the thread if none previously
 * existed. The context will exist until the thread terminates.
 * 
 * Return value: the main loop context for @thread.
 **/
GMainContext *
g_main_context_get (GThread *thread)
{
  static GStaticPrivate private_key = G_STATIC_PRIVATE_INIT;
  GMainContext *context;

  g_return_val_if_fail (thread != NULL, NULL);

  if (g_thread_supported ())
    context = g_static_private_get_for_thread (&private_key, thread);
  else
    context = default_main_context;

  if (!context)
    {
      context = g_new0 (GMainContext, 1);

#ifdef G_THREADS_ENABLED
      if (g_thread_supported ())
	context->mutex = g_mutex_new();

      context->thread = thread;
#endif
      
      context->next_id = 1;
      
      context->source_list = NULL;

#if HAVE_POLL
      context->poll_func = (GPollFunc)poll;
#else
      context->poll_func = g_poll;
#endif

      context->cached_poll_array = NULL;
      context->cached_poll_array_size = 0;
      
      context->pending_dispatches = g_ptr_array_new ();
      
      context->time_is_current = FALSE;

#ifdef G_THREADS_ENABLED
      if (g_thread_supported ())
	{
#ifndef G_OS_WIN32
	  if (pipe (context->wake_up_pipe) < 0)
	    g_error ("Cannot create pipe main loop wake-up: %s\n",
		     g_strerror (errno));
	  
	  context->wake_up_rec.fd = context->wake_up_pipe[0];
	  context->wake_up_rec.events = G_IO_IN;
	  g_main_context_add_poll_unlocked (context, 0, &context->wake_up_rec);
#else
	  if ((context->wake_up_semaphore = CreateSemaphore (NULL, 0, 100, NULL)) == NULL)
	    g_error ("Cannot create wake-up semaphore: %s", g_win32_error_message (GetLastError ()));
	  context->wake_up_rec.fd = (gint) context->wake_up_semaphore;
	  context->wake_up_rec.events = G_IO_IN;
#ifdef G_MAIN_POLL_DEBUG
	  g_print ("wake-up semaphore: %#x\n", (guint) context->wake_up_semaphore);
#endif
	  g_main_context_add_poll_unlocked (context, 0, &context->wake_up_rec);
#endif
	}
#endif

      if (g_thread_supported ())
	g_static_private_set_for_thread (&private_key, thread,
					 context,
					 (GDestroyNotify)g_main_context_destroy);
      else
	default_main_context = context;
    }

  return context;
}

/**
 * g_main_context_default:
 * 
 * Return the default main context. This is the main context used
 * for main loop functions when a main loop is not explicitly
 * specified.
 * 
 * Return value: the default main context.
 **/
GMainContext *
g_main_context_default (void)
{
  /* Slow, but safe */
  
  G_LOCK (main_loop);

  if (!default_main_context)
    default_main_context = g_main_context_get (g_thread_self ());

  G_UNLOCK (main_loop);

  return default_main_context;
}

/* Hooks for adding to the main loop */

/**
 * g_source_new:
 * @source_funcs: structure containing functions that implement
 *                the sources behavior.
 * @struct_size: size of the #GSource structure to create
 * 
 * Create a new GSource structure. The size is specified to
 * allow creating structures derived from GSource that contain
 * additional data. The size passed in must be at least
 * sizeof(GSource).
 * 
 * The source will not initially be associated with any #GMainContext
 * and must be added to one with g_source_add() before it will be
 * executed.
 * 
 * Return value: the newly create #GSource
 **/
GSource *
g_source_new (GSourceFuncs *source_funcs,
	      guint         struct_size)
{
  GSource *source;

  g_return_val_if_fail (source_funcs != NULL, NULL);
  g_return_val_if_fail (struct_size >= sizeof (GSource), NULL);
  
  source = (GSource*) g_malloc0 (struct_size);

  source->source_funcs = source_funcs;
  source->ref_count = 1;
  
  source->priority = G_PRIORITY_DEFAULT;

  source->flags = G_HOOK_FLAG_ACTIVE;

  /* NULL/0 initialization for all other fields */
  
  return source;
}

/* Holds context's lock
 */
static void
g_source_list_add (GSource      *source,
		   GMainContext *context)
{
  GSource *tmp_source, *last_source;
  
  last_source = NULL;
  tmp_source = context->source_list;
  while (tmp_source && tmp_source->priority <= source->priority)
    {
      last_source = tmp_source;
      tmp_source = tmp_source->next;
    }

  source->next = tmp_source;
  if (tmp_source)
    tmp_source->prev = source;
  
  source->prev = last_source;
  if (last_source)
    last_source->next = source;
  else
    context->source_list = source;
}

/* Holds context's lock
 */
static void
g_source_list_remove (GSource      *source,
		      GMainContext *context)
{
  if (source->prev)
    source->prev->next = source->next;
  else
    context->source_list = source->next;

  if (source->next)
    source->next->prev = source->prev;

  source->prev = NULL;
  source->next = NULL;
}

/**
 * g_source_attach:
 * @source: a #GSource
 * @context: a #GMainContext (if %NULL, the default context will be used)
 * 
 * Adds a #GSource to a @context so that it will be executed within
 * that context.
 *
 * Return value: the ID for the source within the #GMainContext
 **/
guint
g_source_attach (GSource      *source,
		 GMainContext *context)
{
  guint result = 0;
  GSList *tmp_list;

  g_return_val_if_fail (source->context == NULL, 0);
  g_return_val_if_fail (!SOURCE_DESTROYED (source), 0);
  
  if (!context)
    context = g_main_context_default ();

  LOCK_CONTEXT (context);

  source->context = context;
  result = source->id = context->next_id++;

  source->ref_count++;
  g_source_list_add (source, context);

  tmp_list = source->poll_fds;
  while (tmp_list)
    {
      g_main_context_add_poll_unlocked (context, source->priority, tmp_list->data);
      tmp_list = tmp_list->next;
    }

#ifdef G_THREADS_ENABLED
  /* Now wake up the main loop if it is waiting in the poll() */
  g_main_context_wakeup (context);
#endif

  UNLOCK_CONTEXT (context);

  return result;
}

static void
g_source_destroy_internal (GSource      *source,
			   GMainContext *context,
			   gboolean      have_lock)
{
  if (!have_lock)
    LOCK_CONTEXT (context);
  
  if (!SOURCE_DESTROYED (source))
    {
      GSList *tmp_list;
      gpointer old_cb_data;
      GSourceCallbackFuncs *old_cb_funcs;
      
      source->flags &= ~G_HOOK_FLAG_ACTIVE;

      old_cb_data = source->callback_data;
      old_cb_funcs = source->callback_funcs;

      source->callback_data = NULL;
      source->callback_funcs = NULL;

      if (old_cb_funcs)
	{
	  UNLOCK_CONTEXT (context);
	  old_cb_funcs->unref (old_cb_data);
	  LOCK_CONTEXT (context);
	}
      
      tmp_list = source->poll_fds;
      while (tmp_list)
	{
	  g_main_context_remove_poll_unlocked (context, tmp_list->data);
	  tmp_list = tmp_list->next;
	}
      
      g_source_unref_internal (source, context, TRUE);
    }

  if (!have_lock)
    UNLOCK_CONTEXT (context);
}

/**
 * g_source_destroy:
 * @source: a #GSource
 * 
 * Remove a source from its #GMainContext, if any, and mark it as
 * destroyed.  The source cannot be subsequently added to another
 * context.
 **/
void
g_source_destroy (GSource *source)
{
  GMainContext *context;
  
  g_return_if_fail (source != NULL);
  
  context = source->context;
  
  if (context)
    g_source_destroy_internal (source, context, FALSE);
  else
    source->flags &= ~G_HOOK_FLAG_ACTIVE;
}

/**
 * g_source_get_id:
 * @source: a #GSource
 * 
 * Return the numeric ID for a particular source. The ID of a source
 * is unique within a particular main loop context. The reverse
 * mapping from ID to source is done by g_main_context_find_source_by_id().
 *
 * Return value: the ID for the source
 **/
guint
g_source_get_id (GSource *source)
{
  guint result;
  
  g_return_val_if_fail (source != NULL, 0);
  g_return_val_if_fail (source->context != NULL, 0);

  LOCK_CONTEXT (source->context);
  result = source->id;
  UNLOCK_CONTEXT (source->context);
  
  return result;
}

/**
 * g_source_get_context:
 * @source: a #GSource
 * 
 * Get the #GMainContext with which the source is associated.
 * Calling this function on a destroyed source is an error.
 * 
 * Return value: the #GMainContext with which the source is associated,
 *               or %NULL if the context has not yet been added
 *               to a source.
 **/
GMainContext *
g_source_get_context (GSource *source)
{
  g_return_val_if_fail (!SOURCE_DESTROYED (source), NULL);

  return source->context;
}

/**
 * g_main_source_add_poll:
 * @source:a #GSource 
 * @fd: a #GPollFD structure holding information about a file
 *      descriptor to watch.
 * 
 * Add a file descriptor to the set of file descriptors polled * for
 * this source. This is usually combined with g_source_new() to add an
 * event source. The event source's check function will typically test
 * the revents field in the #GPollFD struct and return %TRUE if events need
 * to be processed.
 **/
void
g_source_add_poll (GSource *source,
		   GPollFD *fd)
{
  GMainContext *context;
  
  g_return_if_fail (source != NULL);
  g_return_if_fail (fd != NULL);
  g_return_val_if_fail (!SOURCE_DESTROYED (source), 0);
  
  context = source->context;

  if (context)
    LOCK_CONTEXT (context);
  
  source->poll_fds = g_slist_prepend (source->poll_fds, fd);

  if (context)
    {
      g_main_context_add_poll_unlocked (context, source->priority, fd);
      UNLOCK_CONTEXT (context);
    }
}

/**
 * g_source_set_callback_indirect:
 * @source: the source
 * @callback_data: pointer to callback data "object"
 * @callback_funcs: functions for reference counting callback_data
 *                  and getting the callback and data
 * 
 * Set the callback function storing the data as a refcounted callback
 * "object". This is used to implement g_source_set_callback_closure()
 * and internally. Note that calling g_source_set_callback_indirect() assumes
 * an initial reference count on @callback_data, and thus
 * @callback_funcs->unref will eventually be called once more
 * than @callback_funcs->ref.
 **/
void
g_source_set_callback_indirect (GSource              *source,
				gpointer              callback_data,
				GSourceCallbackFuncs *callback_funcs)
{
  GMainContext *context;
  gpointer old_cb_data;
  GSourceCallbackFuncs *old_cb_funcs;
  
  g_return_if_fail (source != NULL);
  g_return_if_fail (callback_funcs != NULL || callback_data == NULL);

  context = source->context;

  if (context)
    LOCK_CONTEXT (context);

  old_cb_data = source->callback_data;
  old_cb_funcs = source->callback_funcs;

  source->callback_data = callback_data;
  source->callback_funcs = callback_funcs;
  
  if (context)
    UNLOCK_CONTEXT (context);
  
  if (old_cb_funcs)
    old_cb_funcs->unref (old_cb_data);
}

static void
g_source_callback_ref (gpointer cb_data)
{
  GSourceCallback *callback = cb_data;

  callback->ref_count++;
}


static void
g_source_callback_unref (gpointer cb_data)
{
  GSourceCallback *callback = cb_data;

  callback->ref_count--;
  if (callback->ref_count == 0)
    {
      if (callback->notify)
	callback->notify (callback->data);
    }
}

static void
g_source_callback_get (gpointer     cb_data,
		       GSourceFunc *func,
		       gpointer    *data)
{
  GSourceCallback *callback = cb_data;

  *func = callback->func;
  *data = callback->data;
}

static GSourceCallbackFuncs g_source_callback_funcs = {
  g_source_callback_ref,
  g_source_callback_unref,
  g_source_callback_get,
};

/**
 * g_source_set_callback:
 * @source: the source
 * @func: a callback function
 * @data: the data to pass to callback function
 * @notify: a function to call when @data is no longer in use, or %NULL.
 * 
 * Set the callback function for a source.
 **/
void
g_source_set_callback (GSource        *source,
		       GSourceFunc     func,
		       gpointer        data,
		       GDestroyNotify  notify)
{
  GSourceCallback *new_callback;

  g_return_if_fail (source != NULL);

  new_callback = g_new (GSourceCallback, 1);
  
  new_callback->func = func;
  new_callback->data = data;
  new_callback->notify = notify;

  g_source_set_callback_indirect (source, new_callback, &g_source_callback_funcs);
}

/**
 * g_source_set_priority:
 * @source: a #GSource
 * @priority: the new priority.
 * 
 * Set the priority of a source. While the main loop is being
 * run, a source will 
 **/
void
g_source_set_priority (GSource  *source,
		       gint      priority)
{
  GSList *tmp_list;
  GMainContext *context;
  
  g_return_if_fail (source != NULL);

  context = source->context;

  if (context)
    LOCK_CONTEXT (context);
  
  source->priority = priority;

  if (context)
    {
      source->next = NULL;
      source->prev = NULL;
      
      tmp_list = source->poll_fds;
      while (tmp_list)
	{
	  g_main_context_remove_poll_unlocked (context, tmp_list->data);
	  g_main_context_add_poll_unlocked (context, priority, tmp_list->data);
      
	  tmp_list = tmp_list->next;
	}
      
      UNLOCK_CONTEXT (source->context);
    }
}

/**
 * g_source_get_priority:
 * @source: a #GSource
 * 
 * Gets the priority of a surce
 * 
 * Return value: the priority of the source
 **/
gint
g_source_get_priority (GSource *source)
{
  g_return_val_if_fail (source != NULL, 0);

  return source->priority;
}

/**
 * g_source_set_can_recurse:
 * @source: a #GSource
 * @can_recurse: whether recursion is allowed for this source
 * 
 * Sets whether a source can be called recursively. If @can_recurse is
 * %TRUE, then while the source is being dispatched then this source
 * will be processed normally. Otherwise, all processing of this
 * source is blocked until the dispatch function returns.
 **/
void
g_source_set_can_recurse (GSource  *source,
			  gboolean  can_recurse)
{
  GMainContext *context;
  
  g_return_if_fail (source != NULL);

  context = source->context;

  if (context)
    LOCK_CONTEXT (context);
  
  if (can_recurse)
    source->flags |= G_SOURCE_CAN_RECURSE;
  else
    source->flags &= ~G_SOURCE_CAN_RECURSE;

  if (context)
    UNLOCK_CONTEXT (context);
}

/**
 * g_source_get_can_recurse:
 * @source: a #GSource
 * 
 * Checks whether a source is allowed to be called recursively.
 * see g_source_set_can_recurse.
 * 
 * Return value: whether recursion is allowed.
 **/
gboolean
g_source_get_can_recurse (GSource  *source)
{
  g_return_val_if_fail (source != NULL, FALSE);
  
  return (source->flags & G_SOURCE_CAN_RECURSE) != 0;
}

/**
 * g_source_ref:
 * @source: a #GSource
 * 
 * Increases the reference count on a source by one.
 * 
 * Return value: @source
 **/
GSource *
g_source_ref (GSource *source)
{
  GMainContext *context;
  
  g_return_val_if_fail (source != NULL, NULL);

  context = source->context;

  if (context)
    LOCK_CONTEXT (context);

  source->ref_count++;

  if (context)
    UNLOCK_CONTEXT (context);

  return source;
}

/* g_source_unref() but possible to call within context lock
 */
static void
g_source_unref_internal (GSource      *source,
			 GMainContext *context,
			 gboolean      have_lock)
{
  gpointer cb_data = NULL;
  GSourceCallbackFuncs *cb_funcs = NULL;
  GSList *tmp_list;

  g_return_if_fail (source != NULL);
  
  if (!have_lock && context)
    LOCK_CONTEXT (context);

  source->ref_count--;
  if (source->ref_count == 0)
    {
      if (context && !SOURCE_DESTROYED (source))
	{
	  g_warning (G_STRLOC ": ref_count == 0, but source is still attached to a context!");
	  source->ref_count++;
	}
      else if (context)
	g_source_list_remove (source, context);

      if (source->source_funcs->destroy)
	source->source_funcs->destroy (source);
      
      g_slist_free (source->poll_fds);
      source->poll_fds = NULL;
      g_free (source);
    }
  
  if (!have_lock && context)
    UNLOCK_CONTEXT (context);

  if (cb_data)
    {
      if (have_lock)
	UNLOCK_CONTEXT (context);
      
      cb_funcs->unref (cb_data);

      if (have_lock)
	LOCK_CONTEXT (context);
    }
}

/**
 * g_source_unref:
 * @source: a #GSource
 * 
 * Decreases the reference count of a source by one. If the
 * resulting reference count is zero the source and associated
 * memory will be destroyed. 
 **/
void
g_source_unref (GSource *source)
{
  g_return_if_fail (source != NULL);

  g_source_unref_internal (source, source->context, FALSE);
}

/**
 * g_main_context_find_source_by_id:
 * @context: a #GMainContext (if %NULL, the default context will be used)
 * @id: the source ID, as returned by g_source_get_id()
 * 
 * Finds a #GSource given a pair of context and ID
 * 
 * Return value: the #GSource if found, otherwise, %NULL
 **/
GSource *
g_main_context_find_source_by_id (GMainContext *context,
				  guint         id)
{
  GSource *source;
  
  g_return_val_if_fail (id > 0, FALSE);

  if (context == NULL)
    context = g_main_context_default ();
  
  LOCK_CONTEXT (context);
  
  source = context->source_list;
  while (source)
    {
      if (!SOURCE_DESTROYED (source) &&
	  source->id == id)
	break;
      source = source->next;
    }

  UNLOCK_CONTEXT (context);

  return source;
}

/**
 * g_main_context_find_source_by_funcs_user_data:
 * @context: a #GMainContext (if %NULL, the default context will be used).
 * @funcs: the @source_funcs passed to g_source_new().
 * @user_data: the user data from the callback.
 * 
 * Finds a source with the given source functions and user data.  If
 * multiple sources exist with the same source function and user data,
 * the first one found will be returned.
 * 
 * Return value: the source, if one was found, otherwise %NULL
 **/
GSource *
g_main_context_find_source_by_funcs_user_data (GMainContext *context,
					       GSourceFuncs *funcs,
					       gpointer      user_data)
{
  GSource *source;
  
  g_return_val_if_fail (funcs != NULL, FALSE);

  if (context == NULL)
    context = g_main_context_default ();
  
  LOCK_CONTEXT (context);

  source = context->source_list;
  while (source)
    {
      if (!SOURCE_DESTROYED (source) &&
	  source->source_funcs == funcs &&
	  source->callback_data == user_data)
	break;
      source = source->next;
    }

  UNLOCK_CONTEXT (context);

  return source;
}

/**
 * g_main_context_find_source_by_user_data:
 * @context: a #GMainContext
 * @user_data: the user_data for the callback.
 * 
 * Finds a source with the given user data for the callback.  If
 * multiple sources exist with the same user data, the first
 * one found will be returned.
 * 
 * Return value: the source, if one was found, otherwise %NULL
 **/
GSource *
g_main_context_find_source_by_user_data (GMainContext *context,
					 gpointer      user_data)
{
  GSource *source;
  
  if (context == NULL)
    context = g_main_context_default ();
  
  LOCK_CONTEXT (context);

  source = context->source_list;
  while (source)
    {
      if (!SOURCE_DESTROYED (source) &&
	  source->callback_data == user_data)
	break;
      source = source->next;
    }

  UNLOCK_CONTEXT (context);

  return source;
}

/**
 * g_source_remove:
 * @tag: the id of the source to remove.
 * 
 * Removes the source with the given id from the default main
 * context. The id of a #GSource is given by g_source_get_id(),
 * or will be returned by the functions g_source_attach(),
 * g_idle_add(), g_idle_add_full(), g_timeout_add(),
 * g_timeout_add_full(), g_io_add_watch, and g_io_add_watch_full().
 *
 * See also g_source_destroy().
 *
 * Return value: %TRUE if the source was found and removed.
 **/
gboolean
g_source_remove (guint tag)
{
  GSource *source;
  
  g_return_val_if_fail (tag > 0, FALSE);

  source = g_main_context_find_source_by_id (NULL, tag);
  if (source)
    g_source_destroy (source);

  return source != NULL;
}

/**
 * g_source_remove_by_user_data:
 * @user_data: the user_data for the callback.
 * 
 * Removes a source from the default main loop context given the user
 * data for the callback. If multiple sources exist with the same user
 * data, only one will be destroyed.
 * 
 * Return value: %TRUE if a source was found and removed. 
 **/
gboolean
g_source_remove_by_user_data (gpointer user_data)
{
  GSource *source;
  
  source = g_main_context_find_source_by_user_data (NULL, user_data);
  if (source)
    {
      g_source_destroy (source);
      return TRUE;
    }
  else
    return FALSE;
}

/**
 * g_source_remove_by_funcs_user_data:
 * @funcs: The @source_funcs passed to g_source_new()
 * @user_data: the user data for the callback
 * 
 * Removes a source from the default main loop context given the
 * source functions and user data. If multiple sources exist with the
 * same source functions and user data, only one will be destroyed.
 * 
 * Return value: %TRUE if a source was found and removed. 
 **/
gboolean
g_source_remove_by_funcs_user_data (GSourceFuncs *funcs,
				    gpointer      user_data)
{
  GSource *source;

  g_return_val_if_fail (funcs != NULL, FALSE);

  source = g_main_context_find_source_by_funcs_user_data (NULL, funcs, user_data);
  if (source)
    {
      g_source_destroy (source);
      return TRUE;
    }
  else
    return FALSE;
}

/**
 * g_get_current_time:
 * @result: #GTimeVal structure in which to store current time.
 * 
 * Equivalent to Unix's <function>gettimeofday()</function>, but portable
 **/
void
g_get_current_time (GTimeVal *result)
{
#ifndef G_OS_WIN32
  struct timeval r;

  g_return_if_fail (result != NULL);

  /*this is required on alpha, there the timeval structs are int's
    not longs and a cast only would fail horribly*/
  gettimeofday (&r, NULL);
  result->tv_sec = r.tv_sec;
  result->tv_usec = r.tv_usec;
#else
  /* Avoid calling time() except for the first time.
   * GetTickCount() should be pretty fast and low-level?
   * I could also use ftime() but it seems unnecessarily overheady.
   */
  static DWORD start_tick = 0;
  static time_t start_time;
  DWORD tick;

  g_return_if_fail (result != NULL);
 
  if (start_tick == 0)
    {
      start_tick = GetTickCount ();
      time (&start_time);
    }

  tick = GetTickCount ();

  result->tv_sec = (tick - start_tick) / 1000 + start_time;
  result->tv_usec = ((tick - start_tick) % 1000) * 1000;
#endif
}

/* Running the main loop */

/* HOLDS: context's lock */
static void
g_main_dispatch (GMainContext *context)
{
  gint i;

  for (i = 0; i < context->pending_dispatches->len; i++)
    {
      GSource *source = context->pending_dispatches->pdata[i];

      context->pending_dispatches->pdata[i] = NULL;
      g_assert (source);

      source->flags &= ~G_SOURCE_READY;

      if (!SOURCE_DESTROYED (source))
	{
	  gboolean was_in_call;
	  gpointer user_data = NULL;
	  GSourceFunc callback = NULL;
	  GSourceCallbackFuncs *cb_funcs;
	  gpointer cb_data;
	  gboolean need_destroy;

	  gboolean (*dispatch) (GSource *,
				GSourceFunc,
				gpointer);

	  dispatch = source->source_funcs->dispatch;
	  cb_funcs = source->callback_funcs;
	  cb_data = source->callback_data;

	  if (cb_funcs)
	    cb_funcs->ref (cb_data);
	  
	  was_in_call = source->flags & G_HOOK_FLAG_IN_CALL;
	  source->flags |= G_HOOK_FLAG_IN_CALL;

	  UNLOCK_CONTEXT (context);

	  if (cb_funcs)
	    cb_funcs->get (cb_data, &callback, &user_data);

	  need_destroy = ! dispatch (source,
				     callback,
				     user_data);
	  LOCK_CONTEXT (context);

	  if (cb_funcs)
	    cb_funcs->unref (cb_data);

	 if (!was_in_call)
	    source->flags &= ~G_HOOK_FLAG_IN_CALL;

	  /* Note: this depends on the fact that we can't switch
	   * sources from one main context to another
	   */
	  if (need_destroy && !SOURCE_DESTROYED (source))
	    {
	      g_assert (source->context == context);
	      g_source_destroy_internal (source, context, TRUE);
	    }
	}
      
      SOURCE_UNREF (source, context);
    }

  g_ptr_array_set_size (context->pending_dispatches, 0);
}

/* Holds context's lock */
static inline GSource *
next_valid_source (GMainContext *context,
		   GSource      *source)
{
  GSource *new_source = source ? source->next : context->source_list;

  while (new_source)
    {
      if (!SOURCE_DESTROYED (new_source))
	{
	  new_source->ref_count++;
	  break;
	}
      
      new_source = new_source->next;
    }

  if (source)
    SOURCE_UNREF (source, context);
	  
  return new_source;
}


/**
 * g_main_context_prepare:
 * @context: a #GMainContext
 * @priority: location to store priority of highest priority
 *            source already ready.
 * 
 * Prepares to poll sources within a main loop. The resulting information
 * for polling is determined by calling g_main_context_query ().
 * 
 * Return value: %TRUE if some source is ready to be dispatched
 *               prior to polling.
 **/
gboolean
g_main_context_prepare (GMainContext *context,
			gint         *priority)
{
  gint n_ready = 0;
  gint current_priority = G_MAXINT;
  GSource *source;

  if (context == NULL)
    context = g_main_context_default ();
  
  LOCK_CONTEXT (context);

  context->time_is_current = FALSE;

  if (context->in_check_or_prepare)
    {
      g_warning ("g_main_context_prepare() called recursively from within a source's check() or "
		 "prepare() member.");
      return FALSE;
    }

#ifdef G_THREADS_ENABLED
  if (context->poll_waiting)
    {
      g_warning("g_main_context_prepare(): main loop already active in another thread");
      UNLOCK_CONTEXT (context);
      return FALSE;
    }
  
  context->poll_waiting = TRUE;
#endif /* G_THREADS_ENABLED */

#if 0
  /* If recursing, finish up current dispatch, before starting over */
  if (context->pending_dispatches)
    {
      if (dispatch)
	g_main_dispatch (context, &current_time);
      
      UNLOCK_CONTEXT (context);
      return TRUE;
    }
#endif

  /* If recursing, clear list of pending dispatches */
  g_ptr_array_set_size (context->pending_dispatches, 0);
  
  /* Prepare all sources */

  context->timeout = -1;
  
  source = next_valid_source (context, NULL);
  while (source)
    {
      gint source_timeout = -1;

      if ((n_ready > 0) && (source->priority > current_priority))
	{
	  SOURCE_UNREF (source, context);
	  break;
	}
      if ((source->flags & G_HOOK_FLAG_IN_CALL) && !(source->flags & G_SOURCE_CAN_RECURSE))
	goto next;

      if (!(source->flags & G_SOURCE_READY))
	{
	  gboolean result;
	  gboolean (*prepare)  (GSource  *source, 
				gint     *timeout);

	  prepare = source->source_funcs->prepare;
	  context->in_check_or_prepare++;
	  UNLOCK_CONTEXT (context);

	  result = (*prepare) (source, &source_timeout);

	  LOCK_CONTEXT (context);
	  context->in_check_or_prepare--;

	  if (result)
	    source->flags |= G_SOURCE_READY;
	}

      if (source->flags & G_SOURCE_READY)
	{
	  n_ready++;
	  current_priority = source->priority;
	  context->timeout = 0;
	}
      
      if (source_timeout >= 0)
	{
	  if (context->timeout < 0)
	    context->timeout = source_timeout;
	  else
	    context->timeout = MIN (context->timeout, source_timeout);
	}

    next:
      source = next_valid_source (context, source);
    }

  UNLOCK_CONTEXT (context);
  
  if (priority)
    *priority = current_priority;
  
  return (n_ready > 0);
}

/**
 * g_main_context_query:
 * @context: a #GMainContext
 * @max_priority: maximum priority source to check
 * @timeout: location to store timeout to be used in polling
 * @fds: location to store #GPollFD records that need to be polled.
 * @n_fds: length of @fds.
 * 
 * Determines information necessary to poll this main loop.
 * 
 * Return value: 
 **/
gint
g_main_context_query (GMainContext *context,
		      gint          max_priority,
		      gint         *timeout,
		      GPollFD      *fds,
		      gint          n_fds)
{
  gint n_poll;
  GPollRec *pollrec;
  
  LOCK_CONTEXT (context);

  pollrec = context->poll_records;
  n_poll = 0;
  while (pollrec && max_priority >= pollrec->priority)
    {
      if (pollrec->fd->events)
	{
	  if (n_poll < n_fds)
	    {
	      fds[n_poll].fd = pollrec->fd->fd;
	      /* In direct contradiction to the Unix98 spec, IRIX runs into
	       * difficulty if you pass in POLLERR, POLLHUP or POLLNVAL
	       * flags in the events field of the pollfd while it should
	       * just ignoring them. So we mask them out here.
	       */
	      fds[n_poll].events = pollrec->fd->events & ~(G_IO_ERR|G_IO_HUP|G_IO_NVAL);
	      fds[n_poll].revents = 0;
	    }
	  n_poll++;
	}
      
      pollrec = pollrec->next;
    }

#ifdef G_THREADS_ENABLED
  context->poll_changed = FALSE;
#endif
  
  if (timeout)
    {
      *timeout = context->timeout;
      if (timeout != 0)
	context->time_is_current = FALSE;
    }
  
  UNLOCK_CONTEXT (context);

  return n_poll;
}

/**
 * g_main_context_check:
 * @context: a #GMainContext
 * @max_priority: the maximum numerical priority of sources to check
 * @fds: array of #GPollFD's that was passed to the last call to
 *       g_main_context_query()
 * @n_fds: return value of g_main_context_query()
 * 
 * Pass the results of polling back to the main loop.
 * 
 * Return value: %TRUE if some sources are ready to be dispatched.
 **/
gboolean
g_main_context_check (GMainContext *context,
		      gint          max_priority,
		      GPollFD      *fds,
		      gint          n_fds)
{
  GSource *source;
  GPollRec *pollrec;
  gint n_ready = 0;
  gint i;
  
  LOCK_CONTEXT (context);

  if (context->in_check_or_prepare)
    {
      g_warning ("g_main_context_check() called recursively from within a source's check() or "
		 "prepare() member.");
      return FALSE;
    }
  
#ifdef G_THREADS_ENABLED
  if (!context->poll_waiting)
    {
#ifndef G_OS_WIN32
      gchar c;
      read (context->wake_up_pipe[0], &c, 1);
#endif
    }
  else
    context->poll_waiting = FALSE;

  /* If the set of poll file descriptors changed, bail out
   * and let the main loop rerun
   */
  if (context->poll_changed)
    return 0;
#endif /* G_THREADS_ENABLED */
  
  pollrec = context->poll_records;
  i = 0;
  while (i < n_fds)
    {
      if (pollrec->fd->events)
	{
	  pollrec->fd->revents = fds[i].revents;
	  i++;
	}
      pollrec = pollrec->next;
    }

  source = next_valid_source (context, NULL);
  while (source)
    {
      if ((n_ready > 0) && (source->priority > max_priority))
	{
	  SOURCE_UNREF (source, context);
	  break;
	}
      if ((source->flags & G_HOOK_FLAG_IN_CALL) && !(source->flags & G_SOURCE_CAN_RECURSE))
	goto next;

      if (!(source->flags & G_SOURCE_READY))
	{
	  gboolean result;
	  gboolean (*check) (GSource  *source);

	  check = source->source_funcs->check;
	  
	  context->in_check_or_prepare++;
	  UNLOCK_CONTEXT (context);
	  
	  result = (*check) (source);
	  
	  LOCK_CONTEXT (context);
	  context->in_check_or_prepare--;
	  
	  if (result)
	    source->flags |= G_SOURCE_READY;
	}

      if (source->flags & G_SOURCE_READY)
	{
	  source->ref_count++;
	  g_ptr_array_add (context->pending_dispatches, source);

	  n_ready++;
	}

    next:
      source = next_valid_source (context, source);
    }

  UNLOCK_CONTEXT (context);

  return n_ready > 0;
}

/**
 * g_main_context_dispatch:
 * @context: a #GMainContext
 * 
 * Dispatch all pending sources()
 **/
void
g_main_context_dispatch (GMainContext *context)
{
  LOCK_CONTEXT (context);

  if (context->pending_dispatches->len > 0)
    {
      g_main_dispatch (context);
    }

  UNLOCK_CONTEXT (context);
}

static gboolean
g_main_context_iterate (GMainContext *context,
			gboolean      block,
			gboolean      dispatch)
{
  gint max_priority;
  gint timeout;
  gboolean some_ready;
  gint nfds, new_nfds;
  GPollFD *fds;
  
  some_ready = g_main_context_prepare (context, &max_priority);

  do
    {
      LOCK_CONTEXT (context);

      if (context->cached_poll_array)
	{
	  nfds = context->cached_poll_array_size;
	  fds = context->cached_poll_array;
	  context->cached_poll_array = NULL;
	}
      else
	{
	  nfds = context->cached_poll_array_size = context->n_poll_records;
	  fds = g_new (GPollFD, nfds);
	}

      UNLOCK_CONTEXT (context);
  
      new_nfds = g_main_context_query (context, max_priority,
				       &timeout, fds, nfds);
    }
  while (new_nfds > nfds);

  if (!block)
    timeout = 0;
  
  g_main_context_poll (context, timeout, max_priority,
		       fds, new_nfds);

  g_main_context_check (context,
			max_priority,
			fds, new_nfds);

  LOCK_CONTEXT (context);

  g_assert (!context->cached_poll_array);
  
  context->cached_poll_array = fds;
  context->cached_poll_array_size = nfds;

  UNLOCK_CONTEXT (context);
  
  if (dispatch)
    g_main_context_dispatch (context);

  return some_ready;
}

/**
 * g_main_context_pending:
 * @context: a #GMainContext (if %NULL, the default context will be used)
 *
 * Check if any sources have pending events for the given context.
 * 
 * Return value: %TRUE if events are pending.
 **/
gboolean 
g_main_context_pending (GMainContext *context)
{
  if (!context)
    context = g_main_context_default();
  
  return g_main_context_iterate (context, FALSE, FALSE);
}

/**
 * g_main_context_iteration:
 * @context: a #GMainContext (if %NULL, the default context will be used) 
 * @may_block: whether the call may block.
 * 
 * Run a single iteration for the given main loop. This involves
 * checking to see if any event sources are ready to be processed,
 * then if no events sources are ready and @may_block is %TRUE, waiting
 * for a source to become ready, then dispatching the highest priority
 * events sources that are ready. Note that even when @may_block is %TRUE,
 * it is still possible for g_main_context_iteration() to return
 * %FALSE, since the the wait may be interrupted for other
 * reasons than an event source becoming ready.
 * 
 * Return value: %TRUE if events were dispatched.
 **/
gboolean
g_main_context_iteration (GMainContext *context, gboolean may_block)
{
  if (!context)
    context = g_main_context_default();
  
  return g_main_context_iterate (context, may_block, TRUE);
}

/**
 * g_main_loop_new:
 * @context: a #GMainContext  (if %NULL, the default context will be used).
 * @is_running: set to TRUE to indicate that the loop is running. This
 * is not very important since calling g_main_run() will set this to
 * TRUE anyway.
 * 
 * Create a new #GMainLoop structure
 * 
 * Return value: 
 **/
GMainLoop *
g_main_loop_new (GMainContext *context,
		 gboolean      is_running)
{
  GMainLoop *loop;
  
  if (!context)
    context = g_main_context_default();
  
  loop = g_new0 (GMainLoop, 1);
  loop->context = context;
  loop->is_running = is_running != FALSE;

#ifdef G_THREADS_ENABLED
  if (g_thread_supported ())
    loop->mutex = g_mutex_new ();
  else
    loop->mutex = NULL;
  loop->sem_cond = NULL;
#endif /* G_THREADS_ENABLED */

  return loop;
}

/**
 * g_main_loop_run:
 * @loop: a #GMainLoop
 * 
 * Run a main loop until g_main_quit() is called on the loop.
 * If this is called for the thread of the loop's #GMainContext,
 * it will process events from the loop, otherwise it will
 * simply wait.
 **/
void 
g_main_loop_run (GMainLoop *loop)
{
  g_return_if_fail (loop != NULL);

#ifdef G_THREADS_ENABLED
  if (loop->context->thread != g_thread_self ())
    {
      LOCK_LOOP (loop);

      if (!g_thread_supported ())
	{
	  g_warning ("g_main_loop_run() was called from second thread but"
		     "g_thread_init() was never called.");
	}
      else
	{
	  if (!loop->sem_cond)
	    loop->sem_cond = g_cond_new ();
	  
	  if (!loop->is_running)
	    loop->is_running = TRUE;
	  
	  while (loop->is_running)
	    g_cond_wait (loop->sem_cond, loop->mutex);
	}
	
      UNLOCK_LOOP (loop);
    }
  else
#endif /* G_THREADS_ENABLED */    
    {
      LOCK_CONTEXT (loop->context);
      if (loop->context->in_check_or_prepare)
	{
	  g_warning ("g_main_run(): called recursively from within a source's check() or "
		     "prepare() member, iteration not possible.");
	  return;
	}
      UNLOCK_CONTEXT (loop->context);
      
      LOCK_LOOP (loop);

      loop->is_running = TRUE;
      while (loop->is_running)
	{
	  UNLOCK_LOOP (loop);
	  g_main_context_iterate (loop->context, TRUE, TRUE);
	  LOCK_LOOP (loop);
	}
      UNLOCK_LOOP (loop);
    }
}

/**
 * g_main_loop_quit:
 * @loop: a #GMainLoop
 * 
 * Stops a #GMainLoop from running. Any calls to g_main_loop_run()
 * for the loop will return.
 **/
void 
g_main_loop_quit (GMainLoop *loop)
{
  g_return_if_fail (loop != NULL);

  LOCK_LOOP (loop);
  loop->is_running = FALSE;

#ifdef G_THREADS_ENABLED
  if (loop->sem_cond)
    g_cond_broadcast (loop->sem_cond);
#endif
  
  UNLOCK_LOOP (loop);

  LOCK_CONTEXT (loop->context);
  
  g_main_context_wakeup (loop->context);
  UNLOCK_CONTEXT (loop->context);
}

/**
 * g_main_loop_destroy:
 * @loop: a #GMainLoop
 * 
 * Destroy a #GMainLoop object and free all associated memory.
 * The loop must not currently be running via g_main_run().
 **/
void 
g_main_loop_destroy (GMainLoop *loop)
{
  g_return_if_fail (loop != NULL);
  g_return_if_fail (!loop->is_running);

#ifdef G_THREADS_ENABLED
  g_mutex_free (loop->mutex);
  if (loop->sem_cond)
    g_cond_free (loop->sem_cond);
#endif /* G_THREADS_ENABLED */  

  g_free (loop);
}

/**
 * g_main_loop_is_running:
 * @loop: a #GMainLoop.
 * 
 * Check to see if the main loop is currently being run via g_main_run()
 * 
 * Return value: %TRUE if the mainloop is currently being run.
 **/
gboolean
g_main_loop_is_running (GMainLoop *loop)
{
  gboolean result;
  
  g_return_val_if_fail (loop != NULL, FALSE);

  LOCK_LOOP (loop);
  result = loop->is_running;
  UNLOCK_LOOP (loop);

  return result;
}

/* HOLDS: context's lock */
static void
g_main_context_poll (GMainContext *context,
		     gint          timeout,
		     gint          priority,
		     GPollFD      *fds,
		     gint          n_fds)
{
#ifdef  G_MAIN_POLL_DEBUG
  GTimer *poll_timer;
  GPollRec *pollrec;
  gint i;
#endif

  GPollFunc poll_func;

  if (n_fds || timeout != 0)
    {
#ifdef	G_MAIN_POLL_DEBUG
      g_print ("g_main_poll(%d) timeout: %d\n", n_fds, timeout);
      poll_timer = g_timer_new ();
#endif

      LOCK_CONTEXT (context);

      poll_func = context->poll_func;
      
      UNLOCK_CONTEXT (context);
      if ((*poll_func) (fds, n_fds, timeout) < 0 && errno != EINTR)
	g_warning ("poll(2) failed due to: %s.",
		   g_strerror (errno));
      
#ifdef	G_MAIN_POLL_DEBUG
      LOCK_CONTEXT (context);

      g_print ("g_main_poll(%d) timeout: %d - elapsed %12.10f seconds",
	       n_fds,
	       timeout,
	       g_timer_elapsed (poll_timer, NULL));
      g_timer_destroy (poll_timer);
      pollrec = context->poll_records;
      i = 0;
      while (i < n_fds)
	{
	  if (pollrec->fd->events)
	    {
	      if (fds[i].revents)
		{
		  g_print (" [%d:", fds[i].fd);
		  if (fds[i].revents & G_IO_IN)
		    g_print ("i");
		  if (fds[i].revents & G_IO_OUT)
		    g_print ("o");
		  if (fds[i].revents & G_IO_PRI)
		    g_print ("p");
		  if (fds[i].revents & G_IO_ERR)
		    g_print ("e");
		  if (fds[i].revents & G_IO_HUP)
		    g_print ("h");
		  if (fds[i].revents & G_IO_NVAL)
		    g_print ("n");
		  g_print ("]");
		}
	      i++;
	    }
	  pollrec = pollrec->next;
	}
      g_print ("\n");
      
      UNLOCK_CONTEXT (context);
#endif
    } /* if (n_fds || timeout != 0) */
}

/**
 * g_main_context_add_poll:
 * @context: a #GMainContext (or %NULL for the default context)
 * @fd: a #GPollFD structure holding information about a file
 *      descriptor to watch.
 * @priority: the priority for this file descriptor which should be
 *      the same as the priority used for g_source_attach() to ensure that the
 *      file descriptor is polled whenever the results may be needed.
 * 
 * Add a file descriptor to the set of file descriptors polled * for
 * this context. This will very seldom be used directly. Instead
 * a typical event source will use g_source_add_poll() instead.
 **/
void
g_main_context_add_poll (GMainContext *context,
			 GPollFD      *fd,
			 gint          priority)
{
  if (!context)
    context = g_main_context_default ();
  
  LOCK_CONTEXT (context);
  g_main_context_add_poll_unlocked (context, priority, fd);
  UNLOCK_CONTEXT (context);
}

/* HOLDS: main_loop_lock */
static void 
g_main_context_add_poll_unlocked (GMainContext *context,
				  gint          priority,
				  GPollFD      *fd)
{
  GPollRec *lastrec, *pollrec, *newrec;

  if (!context->poll_chunk)
    context->poll_chunk = g_mem_chunk_create (GPollRec, 32, G_ALLOC_ONLY);

  if (context->poll_free_list)
    {
      newrec = context->poll_free_list;
      context->poll_free_list = newrec->next;
    }
  else
    newrec = g_chunk_new (GPollRec, context->poll_chunk);

  /* This file descriptor may be checked before we ever poll */
  fd->revents = 0;
  newrec->fd = fd;
  newrec->priority = priority;

  lastrec = NULL;
  pollrec = context->poll_records;
  while (pollrec && priority >= pollrec->priority)
    {
      lastrec = pollrec;
      pollrec = pollrec->next;
    }
  
  if (lastrec)
    lastrec->next = newrec;
  else
    context->poll_records = newrec;

  newrec->next = pollrec;

  context->n_poll_records++;
  if (context->cached_poll_array &&
      context->cached_poll_array_size < context->n_poll_records)
    {
      g_free (context->cached_poll_array);
      context->cached_poll_array = NULL;
    }

#ifdef G_THREADS_ENABLED
  context->poll_changed = TRUE;

  /* Now wake up the main loop if it is waiting in the poll() */
  g_main_context_wakeup (context);
#endif
}

/**
 * g_main_context_remove_poll:
 * @context:a #GMainContext 
 * @fd: a #GPollFD descriptor previously added with g_main_context_add_poll()
 * 
 * Remove file descriptor from the set of file descriptors to be
 * polled for a particular context.
 **/
void
g_main_context_remove_poll (GMainContext *context,
			    GPollFD      *fd)
{
  if (!context)
    context = g_main_context_default ();
  
  LOCK_CONTEXT (context);

  g_main_context_remove_poll_unlocked (context, fd);
  
  UNLOCK_CONTEXT (context);
}

static void
g_main_context_remove_poll_unlocked (GMainContext *context,
				     GPollFD      *fd)
{
  GPollRec *pollrec, *lastrec;

  lastrec = NULL;
  pollrec = context->poll_records;

  while (pollrec)
    {
      if (pollrec->fd == fd)
	{
	  if (lastrec != NULL)
	    lastrec->next = pollrec->next;
	  else
	    context->poll_records = pollrec->next;

#ifdef ENABLE_GC_FRIENDLY
	  pollrec->fd = NULL;  
#endif /* ENABLE_GC_FRIENDLY */

	  pollrec->next = context->poll_free_list;
	  context->poll_free_list = pollrec;

	  context->n_poll_records--;
	  break;
	}
      lastrec = pollrec;
      pollrec = pollrec->next;
    }

#ifdef G_THREADS_ENABLED
  context->poll_changed = TRUE;
  
  /* Now wake up the main loop if it is waiting in the poll() */
  g_main_context_wakeup (context);
#endif
}

/**
 * g_source_get_current_time:
 * @source:  a #GSource
 * @timeval: #GTimeVal structure in which to store current time.
 * 
 * Gets the "current time" to be used when checking 
 * this source. The advantage of calling this function over
 * calling g_get_current_time() directly is that when 
 * checking multiple sources, GLib can cache a single value
 * instead of having to repeatedly get the system time.
 **/
void
g_source_get_current_time (GSource  *source,
			   GTimeVal *timeval)
{
  GMainContext *context;
  
  g_return_if_fail (source->context != NULL);
 
  context = source->context;

  LOCK_CONTEXT (context);

  if (!context->time_is_current)
    {
      g_get_current_time (&context->current_time);
      context->time_is_current = TRUE;
    }
  
  *timeval = context->current_time;
  
  UNLOCK_CONTEXT (context);
}

/**
 * g_main_context_set_poll_func:
 * @context: a #GMainContext
 * @func: the function to call to poll all file descriptors
 * 
 * Sets the function to use to handle polling of file descriptors. It
 * will be used instead of the poll() system call (or GLib's
 * replacement function, which is used where poll() isn't available).
 *
 * This function could possibly be used to integrate the GLib event
 * loop with an external event loop.
 **/
void
g_main_context_set_poll_func (GMainContext *context,
			      GPollFunc     func)
{
  if (!context)
    context = g_main_context_default ();
  
  LOCK_CONTEXT (context);
  
  if (func)
    context->poll_func = func;
  else
    {
#ifdef HAVE_POLL
      context->poll_func = (GPollFunc) poll;
#else
      context->poll_func = (GPollFunc) g_poll;
#endif
    }

  UNLOCK_CONTEXT (context);
}

#ifdef G_OS_WIN32

/**
 * g_main_context_get_poll_func:
 * @context: a #GMainContext
 * 
 * Gets the poll function set by g_main_context_set_poll_func()
 * 
 * Return value: the poll function
 **/
GPollFunc
g_main_context_get_poll_func (GMainContext *context)
{
  GPollFunc *result;
  
  if (!context)
    context = g_main_context_default ();
  
  LOCK_CONTEXT (context);
  result = context->poll_func;
  UNLOCK_CONTEXT (context);
}

#endif

/* HOLDS: context's lock */
/* Wake the main loop up from a poll() */
static void
g_main_context_wakeup (GMainContext *context)
{
#ifdef G_THREADS_ENABLED
  if (g_thread_supported() && context->poll_waiting)
    {
      context->poll_waiting = FALSE;
#ifndef G_OS_WIN32
      write (context->wake_up_pipe[1], "A", 1);
#else
      ReleaseSemaphore (context->context->wake_up_semaphore, 1, NULL);
#endif
    }
#endif
}

/* Timeouts */

static void
g_timeout_set_expiration (GTimeoutSource *timeout_source,
			  GTimeVal       *current_time)
{
  guint seconds = timeout_source->interval / 1000;
  guint msecs = timeout_source->interval - seconds * 1000;

  timeout_source->expiration.tv_sec = current_time->tv_sec + seconds;
  timeout_source->expiration.tv_usec = current_time->tv_usec + msecs * 1000;
  if (timeout_source->expiration.tv_usec >= 1000000)
    {
      timeout_source->expiration.tv_usec -= 1000000;
      timeout_source->expiration.tv_sec++;
    }
}

static gboolean
g_timeout_prepare  (GSource  *source,
		    gint     *timeout)
{
  glong msec;
  GTimeVal current_time;
  
  GTimeoutSource *timeout_source = (GTimeoutSource *)source;

  g_source_get_current_time (source, &current_time);
  
  msec = ((timeout_source->expiration.tv_sec  - current_time.tv_sec) * 1000 +
	  (timeout_source->expiration.tv_usec - current_time.tv_usec) / 1000);

  if (msec < 0)
    msec = 0;
  else if (msec > timeout_source->interval)
    {
      /* The system time has been set backwards, so we
       * reset the expiration time to now + timeout_source->interval;
       * this at least avoids hanging for long periods of time.
       */
      g_timeout_set_expiration (timeout_source, &current_time);
      msec = timeout_source->interval;
    }
  
  *timeout = msec;
  
  return msec == 0;
}

static gboolean 
g_timeout_check (GSource  *source)
{
  GTimeVal current_time;
  GTimeoutSource *timeout_source = (GTimeoutSource *)source;

  g_source_get_current_time (source, &current_time);
  
  return ((timeout_source->expiration.tv_sec < current_time.tv_sec) ||
	  ((timeout_source->expiration.tv_sec == current_time.tv_sec) &&
	   (timeout_source->expiration.tv_usec <= current_time.tv_usec)));
}

static gboolean
g_timeout_dispatch (GSource    *source,
		    GSourceFunc callback,
		    gpointer    user_data)
{
  GTimeoutSource *timeout_source = (GTimeoutSource *)source;

  if (!callback)
    {
      g_warning ("Timeout source dispatched without callback\n"
		 "You must call g_source_set_callback().");
      return FALSE;
    }
 
  if (callback (user_data))
    {
      GTimeVal current_time;

      g_source_get_current_time (source, &current_time);
      g_timeout_set_expiration (timeout_source, &current_time);

      return TRUE;
    }
  else
    return FALSE;
}

/**
 * g_timeout_source_new:
 * @interval: the timeout interval in milliseconds.
 * 
 * Create a new timeout source.
 *
 * The source will not initially be associated with any #GMainContext
 * and must be added to one with g_source_attach() before it will be
 * executed.
 * 
 * Return value: the newly create timeout source
 **/
GSource *
g_timeout_source_new (guint interval)
{
  GSource *source = g_source_new (&timeout_funcs, sizeof (GTimeoutSource));
  GTimeoutSource *timeout_source = (GTimeoutSource *)source;
  GTimeVal current_time;

  timeout_source->interval = interval;

  g_get_current_time (&current_time);
  g_timeout_set_expiration (timeout_source, &current_time);
  
  return source;
}

/**
 * g_timeout_add_full:
 * @priority: the priority of the idle source. Typically this will be in the
 *            range btweeen #G_PRIORITY_DEFAULT_IDLE and #G_PRIORITY_HIGH_IDLE.
 * @interval: the time between calls to the function, in milliseconds
 *             (1/1000ths of a second.)
 * @function: function to call
 * @data:     data to pass to @function
 * @notify:   function to call when the idle is removed, or %NULL
 * 
 * Sets a function to be called at regular intervals, with the given
 * priority.  The function is called repeatedly until it returns
 * FALSE, at which point the timeout is automatically destroyed and
 * the function will not be called again.  The @notify function is
 * called when the timeout is destroyed.  The first call to the
 * function will be at the end of the first @interval.
 *
 * Note that timeout functions may be delayed, due to the processing of other
 * event sources. Thus they should not be relied on for precise timing.
 * After each call to the timeout function, the time of the next
 * timeout is recalculated based on the current time and the given interval
 * (it does not try to 'catch up' time lost in delays).
 * 
 * Return value: the id of event source.
 **/
guint
g_timeout_add_full (gint           priority,
		    guint          interval,
		    GSourceFunc    function,
		    gpointer       data,
		    GDestroyNotify notify)
{
  GSource *source;
  guint id;
  
  g_return_val_if_fail (function != NULL, 0);

  source = g_timeout_source_new (interval);

  if (priority != G_PRIORITY_DEFAULT)
    g_source_set_priority (source, priority);

  g_source_set_callback (source, function, data, notify);
  id = g_source_attach (source, NULL);
  g_source_unref (source);

  return id;
}

/**
 * g_timeout_add:
 * @interval: the time between calls to the function, in milliseconds
 *             (1/1000ths of a second.)
 * @function: function to call
 * @data:     data to pass to @function
 * 
 * Sets a function to be called at regular intervals, with the default
 * priority, #G_PRIORITY_DEFAULT.  The function is called repeatedly
 * until it returns FALSE, at which point the timeout is automatically
 * destroyed and the function will not be called again.  The @notify
 * function is called when the timeout is destroyed.  The first call
 * to the function will be at the end of the first @interval.
 *
 * Note that timeout functions may be delayed, due to the processing of other
 * event sources. Thus they should not be relied on for precise timing.
 * After each call to the timeout function, the time of the next
 * timeout is recalculated based on the current time and the given interval
 * (it does not try to 'catch up' time lost in delays).
 * 
 * Return value: the id of event source.
 **/
guint 
g_timeout_add (guint32        interval,
	       GSourceFunc    function,
	       gpointer       data)
{
  return g_timeout_add_full (G_PRIORITY_DEFAULT, 
			     interval, function, data, NULL);
}

/* Idle functions */

static gboolean 
g_idle_prepare  (GSource  *source,
		 gint     *timeout)
{
  *timeout = 0;

  return TRUE;
}

static gboolean 
g_idle_check    (GSource  *source)
{
  return TRUE;
}

static gboolean
g_idle_dispatch (GSource    *source, 
		 GSourceFunc callback,
		 gpointer    user_data)
{
  if (!callback)
    {
      g_warning ("Idle source dispatched without callback\n"
		 "You must call g_source_set_callback().");
      return FALSE;
    }
  
  return callback (user_data);
}

/**
 * g_idle_source_new:
 * 
 * Create a new idle source.
 *
 * The source will not initially be associated with any #GMainContext
 * and must be added to one with g_source_attach() before it will be
 * executed.
 * 
 * Return value: the newly created idle source
 **/
GSource *
g_idle_source_new (void)
{
  return g_source_new (&idle_funcs, sizeof (GSource));
}

/**
 * g_idle_add_full:
 * @priority: the priority of the idle source. Typically this will be in the
 *            range btweeen #G_PRIORITY_DEFAULT_IDLE and #G_PRIORITY_HIGH_IDLE.
 * @function: function to call
 * @data:     data to pass to @function
 * @notify:   function to call when the idle is removed, or %NULL
 * 
 * Adds a function to be called whenever there are no higher priority
 * events pending.  If the function returns FALSE it is automatically
 * removed from the list of event sources and will not be called again.
 * 
 * Return value: the id of the event source.
 **/
guint 
g_idle_add_full (gint           priority,
		 GSourceFunc    function,
		 gpointer       data,
		 GDestroyNotify notify)
{
  GSource *source;
  guint id;
  
  g_return_val_if_fail (function != NULL, 0);

  source = g_idle_source_new ();

  if (priority != G_PRIORITY_DEFAULT)
    g_source_set_priority (source, priority);

  g_source_set_callback (source, function, data, notify);
  id = g_source_attach (source, NULL);
  g_source_unref (source);

  return id;
}

/**
 * g_idle_add:
 * @function: function to call 
 * @data: data to pass to @function.
 * 
 * Adds a function to be called whenever there are no higher priority
 * events pending to the default main loop. The function is given the
 * default idle priority, #G_PRIORITY_DEFAULT_IDLE.  If the function
 * returns FALSE it is automatically removed from the list of event
 * sources and will not be called again.
 * 
 * Return value: the id of the event source.
 **/
guint 
g_idle_add (GSourceFunc    function,
	    gpointer       data)
{
  return g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, function, data, NULL);
}

/**
 * g_idle_remove_by_data:
 * @data: the data for the idle source's callback.
 * 
 * Removes the idle function with the given data.
 * 
 * Return value: %TRUE if an idle source was found and removed.
 **/
gboolean
g_idle_remove_by_data (gpointer data)
{
  return g_source_remove_by_funcs_user_data (&idle_funcs, data);
}

