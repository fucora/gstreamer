/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * filename:
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

#ifndef __GST_PARSE_H__
#define __GST_PARSE_H__

#include <gst/gstbin.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef GST_DISABLE_PARSE

gint	gst_parse_launch	(const gchar *cmdline, GstBin *parent);

#else // GST_DISABLE_PARSE

#pragma GCC poison gst_parse_launch

#endif // GST_DISABLE_PARSE

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_PARSE_H__ */
