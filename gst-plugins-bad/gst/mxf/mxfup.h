/* GStreamer
 * Copyright (C) 2008 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

/* Implementation of SMPTE 384M - Mapping of Uncompressed Pictures into the MXF
 * Generic Container
 */

#ifndef __MXF_UP_H__
#define __MXF_UP_H__

#include <gst/gst.h>

#include "mxfparse.h"

gboolean mxf_is_up_essence_track (const MXFMetadataTrack *track);

GstCaps *
mxf_up_create_caps (MXFMetadataGenericPackage *package, MXFMetadataTrack *track, GstTagList **tags, MXFEssenceElementHandler *handler, gpointer *mapping_data);

#endif /* __MXF_UP_H__ */
