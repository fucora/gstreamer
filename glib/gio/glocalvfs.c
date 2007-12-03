/* GIO - GLib Input, Output and Streaming Library
 * 
 * Copyright (C) 2006-2007 Red Hat, Inc.
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
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Alexander Larsson <alexl@redhat.com>
 */

#include <config.h>
#include "glocalvfs.h"
#include "glocalfile.h"
#include <gio/gdummyfile.h>
#include <sys/types.h>
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif

#include "gioalias.h"

struct _GLocalVfs
{
  GVfs parent;
};

struct _GLocalVfsClass
{
  GVfsClass parent_class;
  
};

#define g_local_vfs_get_type _g_local_vfs_get_type
G_DEFINE_TYPE (GLocalVfs, g_local_vfs, G_TYPE_VFS)
 
static void
g_local_vfs_finalize (GObject *object)
{
  /* must chain up */
  G_OBJECT_CLASS (g_local_vfs_parent_class)->finalize (object);
}

static void
g_local_vfs_init (GLocalVfs *vfs)
{
}

/**
 * g_local_vfs_new:
 *
 * Returns a new #GVfs handle for a local vfs.
 *
 * Returns: a new #GVfs handle.
 **/
GVfs *
_g_local_vfs_new (void)
{
  return g_object_new (G_TYPE_LOCAL_VFS, NULL);
}

static GFile *
g_local_vfs_get_file_for_path (GVfs       *vfs,
                               const char *path)
{
  return _g_local_file_new (path);
}

static GFile *
g_local_vfs_get_file_for_uri (GVfs       *vfs,
                              const char *uri)
{
  char *path;
  GFile *file;

  path = g_filename_from_uri (uri, NULL, NULL);

  if (path != NULL)
    file = _g_local_file_new (path);
  else
    file = _g_dummy_file_new (uri);

  g_free (path);

  return file;
}

static const gchar * const *
g_local_vfs_get_supported_uri_schemes (GVfs *vfs)
{
  static const gchar * uri_schemes[] = { "file", NULL };

  return uri_schemes;
}

static GFile *
g_local_vfs_parse_name (GVfs       *vfs,
                        const char *parse_name)
{
  GFile *file;
  char *filename;
  char *user_name;
  char *user_prefix;
  const char *user_start, *user_end;
  char *rest;
  struct passwd *passwd_file_entry;
  
  g_return_val_if_fail (G_IS_VFS (vfs), NULL);
  g_return_val_if_fail (parse_name != NULL, NULL);

  if (g_ascii_strncasecmp ("file:", parse_name, 5) == 0)
    filename = g_filename_from_uri (parse_name, NULL, NULL);
  else
    {
      if (*parse_name == '~')
	{
	  parse_name ++;
	  user_start = parse_name;
	  
	  while (*parse_name != 0 && *parse_name != '/')
	    parse_name++;
	  
	  user_end = parse_name;

	  if (user_end == user_start)
	    user_prefix = g_strdup (g_get_home_dir());
	  else
	    {
#ifdef HAVE_PWD_H
	      user_name = g_strndup (user_start, user_end - user_start);
	      passwd_file_entry = getpwnam (user_name);
	      g_free (user_name);
	      
	      if (passwd_file_entry != NULL &&
		  passwd_file_entry->pw_dir != NULL)
		user_prefix = g_strdup (passwd_file_entry->pw_dir);
	      else
#endif
		user_prefix = g_strdup (g_get_home_dir ());
	    }

	  rest = NULL;
	  if (*user_end != 0)
	    rest = g_filename_from_utf8 (user_end, -1, NULL, NULL, NULL);
	  
	  filename = g_build_filename (user_prefix, rest, NULL);
	  g_free (rest);
	  g_free (user_prefix);
	}
      else
	filename = g_filename_from_utf8 (parse_name, -1, NULL, NULL, NULL);
    }
  
  if (filename == NULL)
    filename = g_strdup (parse_name);
    
  file = _g_local_file_new (filename);
  g_free (filename);

  return file;
}

static gboolean
g_local_vfs_is_active (GVfs *vfs)
{
  return TRUE;
}

static void
g_local_vfs_class_init (GLocalVfsClass *class)
{
  GObjectClass *object_class;
  GVfsClass *vfs_class;
  
  object_class = (GObjectClass *) class;

  object_class->finalize = g_local_vfs_finalize;

  vfs_class = G_VFS_CLASS (class);

  vfs_class->name = "local";
  vfs_class->priority = 0;
  
  vfs_class->is_active = g_local_vfs_is_active;
  vfs_class->get_file_for_path = g_local_vfs_get_file_for_path;
  vfs_class->get_file_for_uri = g_local_vfs_get_file_for_uri;
  vfs_class->get_supported_uri_schemes = g_local_vfs_get_supported_uri_schemes;
  vfs_class->parse_name = g_local_vfs_parse_name;
}
