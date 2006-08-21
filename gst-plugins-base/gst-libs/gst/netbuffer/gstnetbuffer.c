/* GStreamer
 * Copyright (C) <2005> Wim Taymans <wim@fluendo.com>
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
 * SECTION:gstnetbuffer
 * @short_description: Buffer for use in network sources and sinks
 *
 * #GstNetBuffer is a subclass of a normal #GstBuffer that contains two
 * additional metadata fields of type #GstNetAddress named 'to' and 'from'. The
 * buffer can be used to store additional information about the origin of the
 * buffer data and is used in various network elements to track the to and from
 * addresses.
 *
 * Last reviewed on 2006-08-21 (0.10.10)
 */

#include <string.h>

#include "gstnetbuffer.h"

static void gst_netbuffer_init (GTypeInstance * instance, gpointer g_class);
static void gst_netbuffer_class_init (gpointer g_class, gpointer class_data);
static void gst_netbuffer_finalize (GstNetBuffer * nbuf);
static GstNetBuffer *gst_netbuffer_copy (GstNetBuffer * nbuf);

static GstBufferClass *parent_class;

GType
gst_netbuffer_get_type (void)
{
  static GType _gst_netbuffer_type = 0;

  if (G_UNLIKELY (_gst_netbuffer_type == 0)) {
    static const GTypeInfo netbuffer_info = {
      sizeof (GstNetBufferClass),
      NULL,
      NULL,
      gst_netbuffer_class_init,
      NULL,
      NULL,
      sizeof (GstNetBuffer),
      0,
      gst_netbuffer_init,
      NULL
    };

    _gst_netbuffer_type = g_type_register_static (GST_TYPE_BUFFER,
        "GstNetBuffer", &netbuffer_info, 0);
  }
  return _gst_netbuffer_type;
}

static void
gst_netbuffer_class_init (gpointer g_class, gpointer class_data)
{
  GstMiniObjectClass *mo_class = GST_MINI_OBJECT_CLASS (g_class);

  parent_class = g_type_class_peek_parent (g_class);

  mo_class->copy = (GstMiniObjectCopyFunction) gst_netbuffer_copy;
  mo_class->finalize = (GstMiniObjectFinalizeFunction) gst_netbuffer_finalize;
}

static void
gst_netbuffer_init (GTypeInstance * instance, gpointer g_class)
{
}

static void
gst_netbuffer_finalize (GstNetBuffer * nbuf)
{
  GST_MINI_OBJECT_CLASS (parent_class)->finalize (GST_MINI_OBJECT (nbuf));
}

static GstNetBuffer *
gst_netbuffer_copy (GstNetBuffer * nbuf)
{
  GstNetBuffer *copy;

  copy =
      (GstNetBuffer *) GST_MINI_OBJECT_CLASS (parent_class)->
      copy (GST_MINI_OBJECT (nbuf));

  memcpy (&copy->to, &nbuf->to, sizeof (nbuf->to));
  memcpy (&copy->from, &nbuf->from, sizeof (nbuf->from));

  return copy;
}

/**
 * gst_netbuffer_new:
 *
 * Create a new network buffer.
 *
 * Returns: a new #GstNetBuffer.
 */
GstNetBuffer *
gst_netbuffer_new (void)
{
  GstNetBuffer *buf;

  buf = (GstNetBuffer *) gst_mini_object_new (GST_TYPE_NETBUFFER);

  return buf;
}

/**
 * gst_netaddress_set_ip4_address:
 * @naddr: a network address
 * @address: an IPv4 network address.
 * @port: a port number to set.
 *
 * Set @naddr with the IPv4 @address and @port pair.
 */
void
gst_netaddress_set_ip4_address (GstNetAddress * naddr, guint32 address,
    guint16 port)
{
  g_return_if_fail (naddr != NULL);

  naddr->type = GST_NET_TYPE_IP4;
  naddr->address.ip4 = address;
  naddr->port = port;
}

/**
 * gst_netaddress_set_ip6_address:
 * @naddr: a network address
 * @address: an IPv6 network address.
 * @port: a port number to set.
 *
 * Set @naddr with the IPv6 @address and @port pair.
 */
void
gst_netaddress_set_ip6_address (GstNetAddress * naddr, guint8 address[16],
    guint16 port)
{
  g_return_if_fail (naddr != NULL);

  naddr->type = GST_NET_TYPE_IP6;
  memcpy (&naddr->address.ip6, address, 16);
  naddr->port = port;
}

/**
 * gst_netaddress_get_net_type:
 * @naddr: a network address
 *
 * Get the type of address stored in @naddr.
 *
 * Returns: the network type stored in @naddr.
 */
GstNetType
gst_netaddress_get_net_type (GstNetAddress * naddr)
{
  g_return_val_if_fail (naddr != NULL, GST_NET_TYPE_UNKNOWN);

  return naddr->type;
}

/**
 * gst_netaddress_get_ip4_address:
 * @naddr: a network address
 * @address: a location to store the address.
 * @port: a location to store the port.
 *
 * Get the IPv4 address stored in @naddr into @address.
 *
 * Returns: TRUE if the address could be retrieved.
 */
gboolean
gst_netaddress_get_ip4_address (GstNetAddress * naddr, guint32 * address,
    guint16 * port)
{
  g_return_val_if_fail (naddr != NULL, FALSE);

  if (naddr->type == GST_NET_TYPE_UNKNOWN)
    return FALSE;

  if (address)
    *address = naddr->address.ip4;
  if (port)
    *port = naddr->port;

  return TRUE;
}

/**
 * gst_netaddress_get_ip6_address:
 * @naddr: a network address
 * @address: a location to store the result.
 * @port: a location to store the port.
 *
 * Get the IPv6 address stored in @naddr into @address.
 *
 * Returns: TRUE if the address could be retrieved.
 */
gboolean
gst_netaddress_get_ip6_address (GstNetAddress * naddr, guint8 address[16],
    guint16 * port)
{
  g_return_val_if_fail (naddr != NULL, FALSE);

  if (naddr->type == GST_NET_TYPE_UNKNOWN)
    return FALSE;

  if (address)
    memcpy (address, naddr->address.ip6, 16);
  if (port)
    *port = naddr->port;

  return TRUE;
}
