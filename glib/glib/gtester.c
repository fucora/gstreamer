/* GLib testing framework runner
 * Copyright (C) 2007 Sven Herzberg
 * Copyright (C) 2007 Tim Janik
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#include <glib.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

/* the read buffer size in bytes */
#define READ_BUFFER_SIZE 4096

/* --- prototypes --- */
static void     parse_args      (gint           *argc_p,
                                 gchar        ***argv_p);

/* --- variables --- */
static GIOChannel  *ioc_report = NULL;
static gboolean     subtest_running = FALSE;
static gboolean     subtest_io_pending = FALSE;
static gboolean     gtester_quiet = TRUE;
static gboolean     gtester_verbose = FALSE;
static gboolean     gtester_list_tests = FALSE;
static gboolean     subtest_mode_fatal = FALSE;
static gboolean     subtest_mode_perf = FALSE;
static gboolean     subtest_mode_quick = TRUE;
static const gchar *subtest_seedstr = NULL;
static GSList      *subtest_paths = NULL;
static const gchar *outpu_filename = NULL;

/* --- functions --- */
static gboolean
child_report_cb (GIOChannel  *source,
                 GIOCondition condition,
                 gpointer     data)
{
  GTestLogBuffer *tlb = data;
  GIOStatus status = G_IO_STATUS_NORMAL;
  gsize length = 0;
  do
    {
      guint8 buffer[READ_BUFFER_SIZE];
      GError *error = NULL;
      status = g_io_channel_read_chars (source, (gchar*) buffer, sizeof (buffer), &length, &error);
      if (length)
        {
          GTestLogMsg *msg;
          g_test_log_buffer_push (tlb, length, buffer);
          do
            {
              msg = g_test_log_buffer_pop (tlb);
              if (msg)
                {
                  guint ui;
                  /* print message, this should be written to an XML log file */
                  g_printerr ("{*GTLOG(%s)", g_test_log_type_name (msg->log_type));
                  for (ui = 0; ui < msg->n_strings; ui++)
                    g_printerr (":{%s}", msg->strings[ui]);
                  if (msg->n_nums)
                    {
                      g_printerr (":(");
                      for (ui = 0; ui < msg->n_nums; ui++)
                        g_printerr ("%s%.16Lg", ui ? ";" : "", msg->nums[ui]);
                      g_printerr (")");
                    }
                  g_printerr (":GTLOG*}\n");
                  g_test_log_msg_free (msg);
                }
            }
          while (msg);
        }
      g_clear_error (&error);
      /* ignore the io channel status, which seems to be bogus especially for non blocking fds */
      (void) status;
    }
  while (length > 0);
  if (condition & (G_IO_ERR | G_IO_HUP))
    {
      /* if there's no data to read and select() reports an error or hangup,
       * the fd must have been closed remotely
       */
      subtest_io_pending = FALSE;
      return FALSE;
    }
  return TRUE; /* keep polling */
}

static void
child_watch_cb (GPid     pid,
		gint     status,
		gpointer data)
{
  g_spawn_close_pid (pid);
  subtest_running = FALSE;
}

static gchar*
queue_gfree (GSList **slistp,
             gchar   *string)
{
  *slistp = g_slist_prepend (*slistp, string);
  return string;
}

static void
unset_cloexec_fdp (gpointer fdp_data)
{
  int r, *fdp = fdp_data;
  do
    r = fcntl (*fdp, F_SETFD, 0 /* FD_CLOEXEC */);
  while (r < 0 && errno == EINTR);
}

static void
launch_test (const char *binary)
{
  GTestLogBuffer *tlb;
  GSList *slist, *free_list = NULL;
  GError *error = NULL;
  const gchar *argv[20 + g_slist_length (subtest_paths)];
  GPid pid = 0;
  gint report_pipe[2] = { -1, -1 };
  gint i = 0;

  if (pipe (report_pipe) < 0)
    {
      if (subtest_mode_fatal)
        g_error ("Failed to open pipe for test binary: %s: %s", binary, g_strerror (errno));
      else
        g_warning ("Failed to open pipe for test binary: %s: %s", binary, g_strerror (errno));
      return;
    }

  /* setup argv */
  argv[i++] = binary;
  if (gtester_quiet)
    argv[i++] = "--quiet";
  if (gtester_verbose)
    argv[i++] = "--verbose";
  // argv[i++] = "--debug-log";
  argv[i++] = queue_gfree (&free_list, g_strdup_printf ("--GTestLogFD=%u", report_pipe[1]));
  if (!subtest_mode_fatal)
    argv[i++] = "--keep-going";
  if (subtest_mode_quick)
    argv[i++] = "-m=quick";
  else
    argv[i++] = "-m=slow";
  if (subtest_mode_perf)
    argv[i++] = "-m=perf";
  if (subtest_seedstr)
    argv[i++] = queue_gfree (&free_list, g_strdup_printf ("--seed=%s", subtest_seedstr));
  for (slist = subtest_paths; slist; slist = slist->next)
    argv[i++] = queue_gfree (&free_list, g_strdup_printf ("-p=%s", (gchar*) slist->data));
  if (gtester_list_tests)
    argv[i++] = "-l";
  argv[i++] = NULL;

  g_spawn_async_with_pipes (NULL, /* g_get_current_dir() */
                            (gchar**) argv,
                            NULL, /* envp */
                            G_SPAWN_DO_NOT_REAP_CHILD, /* G_SPAWN_SEARCH_PATH */
                            unset_cloexec_fdp, &report_pipe[1], /* pre-exec callback */
                            &pid,
                            NULL,       /* standard_input */
                            NULL,       /* standard_output */
                            NULL,       /* standard_error */
                            &error);
  g_slist_foreach (free_list, (void(*)(void*,void*)) g_free, NULL);
  g_slist_free (free_list);
  free_list = NULL;
  close (report_pipe[1]);

  if (error)
    {
      close (report_pipe[0]);
      if (subtest_mode_fatal)
        g_error ("Failed to execute test binary: %s: %s", argv[0], error->message);
      else
        g_warning ("Failed to execute test binary: %s: %s", argv[0], error->message);
      g_clear_error (&error);
      return;
    }

  subtest_running = TRUE;
  subtest_io_pending = TRUE;
  tlb = g_test_log_buffer_new();
  if (report_pipe[0] >= 0)
    {
      ioc_report = g_io_channel_unix_new (report_pipe[0]);
      g_io_channel_set_flags (ioc_report, G_IO_FLAG_NONBLOCK, NULL);
      g_io_channel_set_encoding (ioc_report, NULL, NULL);
      g_io_channel_set_buffered (ioc_report, FALSE);
      g_io_add_watch_full (ioc_report, G_PRIORITY_DEFAULT - 1, G_IO_IN | G_IO_ERR | G_IO_HUP, child_report_cb, tlb, NULL);
      g_io_channel_unref (ioc_report);
    }
  g_child_watch_add_full (G_PRIORITY_DEFAULT + 1, pid, child_watch_cb, NULL, NULL);

  while (subtest_running ||             /* FALSE once child exits */
         subtest_io_pending ||          /* FALSE once ioc_report closes */
         g_main_context_pending (NULL)) /* TRUE while idler, etc are running */
    g_main_context_iteration (NULL, TRUE);

  close (report_pipe[0]);
  g_test_log_buffer_free (tlb);
}

static void
usage (gboolean just_version)
{
  if (just_version)
    {
      g_print ("gtester version %d.%d.%d\n", GLIB_MAJOR_VERSION, GLIB_MINOR_VERSION, GLIB_MICRO_VERSION);
      return;
    }
  g_print ("Usage: gtester [OPTIONS] testprogram...\n");
  /*        12345678901234567890123456789012345678901234567890123456789012345678901234567890 */
  g_print ("Options:\n");
  g_print ("  -h, --help                  show this help message\n");
  g_print ("  -v, --version               print version informations\n");
  g_print ("  --g-fatal-warnings          make warnings fatal (abort)\n");
  g_print ("  -k, --keep-going            continue running after tests failed\n");
  g_print ("  -l                          list paths of available test cases\n");
  g_print ("  -m=perf, -m=slow, -m=quick  run test cases in mode perf, slow or quick (default)\n");
  g_print ("  -p=TESTPATH                 only start test cases matching TESTPATH\n");
  g_print ("  --seed=SEEDSTRING           start all tests with random number seed SEEDSTRING\n");
  g_print ("  -o=LOGFILE                  write the test log to LOGFILE\n");
  g_print ("  -q, --quiet                 suppress unnecessary output\n");
  g_print ("  --verbose                   produce additional output\n");
}

static void
parse_args (gint    *argc_p,
            gchar ***argv_p)
{
  guint argc = *argc_p;
  gchar **argv = *argv_p;
  guint i, e;
  /* parse known args */
  for (i = 1; i < argc; i++)
    {
      if (strcmp (argv[i], "--g-fatal-warnings") == 0)
        {
          GLogLevelFlags fatal_mask = (GLogLevelFlags) g_log_set_always_fatal ((GLogLevelFlags) G_LOG_FATAL_MASK);
          fatal_mask = (GLogLevelFlags) (fatal_mask | G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL);
          g_log_set_always_fatal (fatal_mask);
          argv[i] = NULL;
        }
      else if (strcmp (argv[i], "-h") == 0 || strcmp (argv[i], "--help") == 0)
        {
          usage (FALSE);
          exit (0);
          argv[i] = NULL;
        }
      else if (strcmp (argv[i], "-v") == 0 || strcmp (argv[i], "--version") == 0)
        {
          usage (TRUE);
          exit (0);
          argv[i] = NULL;
        }
      else if (strcmp (argv[i], "--keep-going") == 0 ||
               strcmp (argv[i], "-k") == 0)
        {
          subtest_mode_fatal = FALSE;
          argv[i] = NULL;
        }
      else if (strcmp ("-p", argv[i]) == 0 || strncmp ("-p=", argv[i], 3) == 0)
        {
          gchar *equal = argv[i] + 2;
          if (*equal == '=')
            subtest_paths = g_slist_prepend (subtest_paths, equal + 1);
          else if (i + 1 < argc)
            {
              argv[i++] = NULL;
              subtest_paths = g_slist_prepend (subtest_paths, argv[i]);
            }
          argv[i] = NULL;
        }
      else if (strcmp ("-o", argv[i]) == 0 || strncmp ("-o=", argv[i], 3) == 0)
        {
          gchar *equal = argv[i] + 2;
          if (*equal == '=')
            outpu_filename = equal + 1;
          else if (i + 1 < argc)
            {
              argv[i++] = NULL;
              outpu_filename = argv[i];
            }
          argv[i] = NULL;
        }
      else if (strcmp ("-m", argv[i]) == 0 || strncmp ("-m=", argv[i], 3) == 0)
        {
          gchar *equal = argv[i] + 2;
          const gchar *mode = "";
          if (*equal == '=')
            mode = equal + 1;
          else if (i + 1 < argc)
            {
              argv[i++] = NULL;
              mode = argv[i];
            }
          if (strcmp (mode, "perf") == 0)
            subtest_mode_perf = TRUE;
          else if (strcmp (mode, "slow") == 0)
            subtest_mode_quick = FALSE;
          else if (strcmp (mode, "quick") == 0)
            {
              subtest_mode_quick = TRUE;
              subtest_mode_perf = FALSE;
            }
          else
            g_error ("unknown test mode: -m %s", mode);
          argv[i] = NULL;
        }
      else if (strcmp ("-q", argv[i]) == 0 || strcmp ("--quiet", argv[i]) == 0)
        {
          gtester_quiet = TRUE;
          gtester_verbose = FALSE;
          argv[i] = NULL;
        }
      else if (strcmp ("--verbose", argv[i]) == 0)
        {
          gtester_quiet = FALSE;
          gtester_verbose = TRUE;
          argv[i] = NULL;
        }
      else if (strcmp ("-l", argv[i]) == 0)
        {
          gtester_list_tests = TRUE;
          argv[i] = NULL;
        }
      else if (strcmp ("--seed", argv[i]) == 0 || strncmp ("--seed=", argv[i], 7) == 0)
        {
          gchar *equal = argv[i] + 6;
          if (*equal == '=')
            subtest_seedstr = equal + 1;
          else if (i + 1 < argc)
            {
              argv[i++] = NULL;
              subtest_seedstr = argv[i];
            }
          argv[i] = NULL;
        }
    }
  /* collapse argv */
  e = 1;
  for (i = 1; i < argc; i++)
    if (argv[i])
      {
        argv[e++] = argv[i];
        if (i >= e)
          argv[i] = NULL;
      }
  *argc_p = e;
}

int
main (int    argc,
      char **argv)
{
  guint ui;

  /* some unices need SA_RESTART for SIGCHLD to return -EAGAIN for io.
   * we must fiddle with sigaction() *before* glib is used, otherwise
   * we could revoke signal hanmdler setups from glib initialization code.
   */
  if (TRUE)
    {
      struct sigaction sa;
      struct sigaction osa;
      sa.sa_handler = SIG_DFL;
      sigfillset (&sa.sa_mask);
      sa.sa_flags = SA_RESTART;
      sigaction (SIGCHLD, &sa, &osa);
    }

  g_set_prgname (argv[0]);
  parse_args (&argc, &argv);

  if (argc <= 1)
    {
      usage (FALSE);
      return 1;
    }

  for (ui = 1; ui < argc; ui++)
    {
      const char *binary = argv[ui];
      launch_test (binary);
    }

  /* we only get here on success or if !subtest_mode_fatal */
  return 0;
}
