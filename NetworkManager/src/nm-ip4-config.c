/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager -- Network link manager
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2005 - 2008 Red Hat, Inc.
 * Copyright (C) 2006 - 2008 Novell, Inc.
 */

#include <glib.h>
#include <stdio.h>
#include <string.h>
#include "nm-ip4-config.h"
#include "nm-dbus-manager.h"
#include "NetworkManager.h"
#include "NetworkManagerUtils.h"
#include "nm-setting-ip4-config.h"
#include "nm-platform.h"
#include "nm-utils.h"
#include "nm-ip4-config-glue.h"
#include "nm-dbus-glib-types.h"


G_DEFINE_TYPE (NMIP4Config, nm_ip4_config, G_TYPE_OBJECT)

#define NM_IP4_CONFIG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_IP4_CONFIG, NMIP4ConfigPrivate))

typedef struct {
	char *path;

	GSList *addresses;
	guint32	ptp_address;

	guint32	mtu;	/* Maximum Transmission Unit of the interface */
	guint32	mss;	/* Maximum Segment Size of the route */

	GArray *nameservers;
	GPtrArray *domains;
	GPtrArray *searches;

	GArray *wins;

	GArray *nis;
	char * nis_domain;

	GSList *routes;

	gboolean never_default;
} NMIP4ConfigPrivate;


enum {
	PROP_0,
	PROP_ADDRESSES,
	PROP_NAMESERVERS,
	PROP_DOMAINS,
	PROP_ROUTES,
	PROP_WINS_SERVERS,

	LAST_PROP
};


NMIP4Config *
nm_ip4_config_new (void)
{
	return (NMIP4Config *) g_object_new (NM_TYPE_IP4_CONFIG, NULL);
}

NMIP4Config *
nm_ip4_config_new_for_interface (int ifindex)
{
	NMIP4Config *ip4;
	GArray *addrs_array, *routes_array;
	NMPlatformIP4Address *addrs;
	NMPlatformIP4Route *routes;
	NMIP4Address *addr;
	NMIP4Route *route;
	int i;

	addrs_array = nm_platform_ip4_address_get_all (ifindex);
	if (addrs_array->len == 0) {
		g_array_unref (addrs_array);
		return NULL;
	}

	ip4 = nm_ip4_config_new ();

	addrs = (NMPlatformIP4Address *)addrs_array->data;
	for (i = 0; i < addrs_array->len; i++) {
		addr = nm_ip4_address_new ();
		nm_ip4_address_set_address (addr, addrs[i].address);
		nm_ip4_address_set_prefix (addr, addrs[i].plen);
		nm_ip4_config_take_address (ip4, addr);
	}
	g_array_unref (addrs_array);

	routes_array = nm_platform_ip4_route_get_all (ifindex);
	routes = (NMPlatformIP4Route *)routes_array->data;
	for (i = 0; i < routes_array->len; i++) {
		/* Default route ignored; it's handled internally by NM and not
		 * tracked in the device's IP config.
		 */
		if (routes[i].plen == 0)
			continue;

		route = nm_ip4_route_new ();
		nm_ip4_route_set_dest (route, routes[i].network);
		nm_ip4_route_set_prefix (route, routes[i].plen);
		nm_ip4_route_set_next_hop (route, routes[i].gateway);
		nm_ip4_route_set_metric (route, routes[i].metric);
		nm_ip4_config_take_route (ip4, route);
	}
	g_array_unref (routes_array);

	return ip4;
}

void
nm_ip4_config_export (NMIP4Config *config)
{
	NMIP4ConfigPrivate *priv;
	static guint32 counter = 0;

	g_return_if_fail (NM_IS_IP4_CONFIG (config));

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	g_return_if_fail (priv->path == NULL);

	priv->path = g_strdup_printf (NM_DBUS_PATH "/IP4Config/%d", counter++);
	nm_dbus_manager_register_object (nm_dbus_manager_get (), priv->path, config);
}

const char *
nm_ip4_config_get_dbus_path (NMIP4Config *config)
{
	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), NULL);

	return NM_IP4_CONFIG_GET_PRIVATE (config)->path;
}

void
nm_ip4_config_take_address (NMIP4Config *config, NMIP4Address *address)
{
	NMIP4ConfigPrivate *priv;

	g_return_if_fail (NM_IS_IP4_CONFIG (config));
	g_return_if_fail (address != NULL);

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	priv->addresses = g_slist_append (priv->addresses, address);
}

void
nm_ip4_config_add_address (NMIP4Config *config,
                           NMIP4Address *address)
{
	NMIP4ConfigPrivate *priv;
	GSList *iter;

	g_return_if_fail (NM_IS_IP4_CONFIG (config));
	g_return_if_fail (address != NULL);

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	for (iter = priv->addresses; iter; iter = g_slist_next (iter)) {
		if (nm_ip4_address_compare ((NMIP4Address *) iter->data, address))
			return;
	}

	priv->addresses = g_slist_append (priv->addresses, nm_ip4_address_dup (address));
}

void
nm_ip4_config_replace_address (NMIP4Config *config,
                               guint i,
                               NMIP4Address *new_address)
{
	NMIP4ConfigPrivate *priv;
	GSList *old;

	g_return_if_fail (NM_IS_IP4_CONFIG (config));

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	old = g_slist_nth (priv->addresses, i);
	g_return_if_fail (old != NULL);
	nm_ip4_address_unref ((NMIP4Address *) old->data);

	old->data = nm_ip4_address_dup (new_address);
}

NMIP4Address *nm_ip4_config_get_address (NMIP4Config *config, guint i)
{
	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), NULL);

	return (NMIP4Address *) g_slist_nth_data (NM_IP4_CONFIG_GET_PRIVATE (config)->addresses, i);
}

guint32 nm_ip4_config_get_num_addresses (NMIP4Config *config)
{
	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), 0);

	return g_slist_length (NM_IP4_CONFIG_GET_PRIVATE (config)->addresses);
}

/* Return the first gateway we find */
guint32
nm_ip4_config_get_gateway (NMIP4Config *config)
{
	GSList *iter;
	guint32 gw;

	for (iter = NM_IP4_CONFIG_GET_PRIVATE (config)->addresses; iter; iter = iter->next) {
		NMIP4Address *addr = iter->data;

		gw = nm_ip4_address_get_gateway (addr);
		if (gw)
			return gw;
	}

	return 0;
}

guint32 nm_ip4_config_get_ptp_address (NMIP4Config *config)
{
	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), 0);

	return NM_IP4_CONFIG_GET_PRIVATE (config)->ptp_address;
}

void nm_ip4_config_set_ptp_address (NMIP4Config *config, guint32 ptp_addr)
{
	g_return_if_fail (NM_IS_IP4_CONFIG (config));

	NM_IP4_CONFIG_GET_PRIVATE (config)->ptp_address = ptp_addr;
}

void nm_ip4_config_add_nameserver (NMIP4Config *config, guint32 nameserver)
{
	NMIP4ConfigPrivate *priv;
	int i;

	g_return_if_fail (NM_IS_IP4_CONFIG (config));
	g_return_if_fail (nameserver > 0);

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	for (i = 0; i < priv->nameservers->len; i++) {
		guint32 s = g_array_index (priv->nameservers, guint32, i);

		/* No dupes */
		if (nameserver == s)
			return;
	}

	g_array_append_val (priv->nameservers, nameserver);
}

guint32 nm_ip4_config_get_nameserver (NMIP4Config *config, guint i)
{
	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), 0);

	return g_array_index (NM_IP4_CONFIG_GET_PRIVATE (config)->nameservers, guint32, i);
}

guint32 nm_ip4_config_get_num_nameservers (NMIP4Config *config)
{
	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), 0);

	return NM_IP4_CONFIG_GET_PRIVATE (config)->nameservers->len;
}

void nm_ip4_config_reset_nameservers (NMIP4Config *config)
{
	NMIP4ConfigPrivate *priv;

	g_return_if_fail (NM_IS_IP4_CONFIG (config));

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	if (priv->nameservers->len)
		g_array_remove_range (priv->nameservers, 0, priv->nameservers->len);
}

void nm_ip4_config_add_wins (NMIP4Config *config, guint32 wins)
{
	NMIP4ConfigPrivate *priv;
	int i;

	g_return_if_fail (NM_IS_IP4_CONFIG (config));
	g_return_if_fail (wins > 0);

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	for (i = 0; i < priv->wins->len; i++) {
		guint32 s = g_array_index (priv->wins, guint32, i);

		/* No dupes */
		if (wins == s)
			return;
	}

	g_array_append_val (priv->wins, wins);
}

guint32 nm_ip4_config_get_wins (NMIP4Config *config, guint i)
{
	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), 0);

	return g_array_index (NM_IP4_CONFIG_GET_PRIVATE (config)->wins, guint32, i);
}

guint32 nm_ip4_config_get_num_wins (NMIP4Config *config)
{
	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), 0);

	return NM_IP4_CONFIG_GET_PRIVATE (config)->wins->len;
}

void nm_ip4_config_reset_wins (NMIP4Config *config)
{
	NMIP4ConfigPrivate *priv;

	g_return_if_fail (NM_IS_IP4_CONFIG (config));

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	if (priv->wins->len)
		g_array_remove_range (priv->wins, 0, priv->wins->len);
}

void
nm_ip4_config_take_route (NMIP4Config *config, NMIP4Route *route)
{
	NMIP4ConfigPrivate *priv;

	g_return_if_fail (NM_IS_IP4_CONFIG (config));
	g_return_if_fail (route != NULL);

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	priv->routes = g_slist_append (priv->routes, route);
}

void
nm_ip4_config_add_route (NMIP4Config *config, NMIP4Route *route)
{
	NMIP4ConfigPrivate *priv;
	GSList *iter;

	g_return_if_fail (NM_IS_IP4_CONFIG (config));
	g_return_if_fail (route != NULL);

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	for (iter = priv->routes; iter; iter = g_slist_next (iter)) {
		if (nm_ip4_route_compare ((NMIP4Route *) iter->data, route))
			return;
	}

	priv->routes = g_slist_append (priv->routes, nm_ip4_route_dup (route));
}

void
nm_ip4_config_replace_route (NMIP4Config *config,
							 guint i,
							 NMIP4Route *new_route)
{
	NMIP4ConfigPrivate *priv;
	GSList *old;

	g_return_if_fail (NM_IS_IP4_CONFIG (config));

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	old = g_slist_nth (priv->routes, i);
	g_return_if_fail (old != NULL);
	nm_ip4_route_unref ((NMIP4Route *) old->data);

	old->data = nm_ip4_route_dup (new_route);
}

NMIP4Route *
nm_ip4_config_get_route (NMIP4Config *config, guint i)
{
	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), NULL);

	return (NMIP4Route *) g_slist_nth_data (NM_IP4_CONFIG_GET_PRIVATE (config)->routes, i);
}

guint32 nm_ip4_config_get_num_routes (NMIP4Config *config)
{
	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), 0);

	return g_slist_length (NM_IP4_CONFIG_GET_PRIVATE (config)->routes);
}

void nm_ip4_config_reset_routes (NMIP4Config *config)
{
	NMIP4ConfigPrivate *priv;

	g_return_if_fail (NM_IS_IP4_CONFIG (config));

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	g_slist_foreach (priv->routes, (GFunc) g_free, NULL);
	priv->routes = NULL;
}

void nm_ip4_config_add_domain (NMIP4Config *config, const char *domain)
{
	NMIP4ConfigPrivate *priv;
	int i;

	g_return_if_fail (NM_IS_IP4_CONFIG (config));
	g_return_if_fail (domain != NULL);
	g_return_if_fail (strlen (domain) > 0);

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);

	for (i = 0; i < priv->domains->len; i++) {
		if (!strcmp (g_ptr_array_index (priv->domains, i), domain))
			return;
	}

	g_ptr_array_add (priv->domains, g_strdup (domain));
}

const char *nm_ip4_config_get_domain (NMIP4Config *config, guint i)
{
	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), NULL);

	return (const char *) g_ptr_array_index (NM_IP4_CONFIG_GET_PRIVATE (config)->domains, i);
}

guint32 nm_ip4_config_get_num_domains (NMIP4Config *config)
{
	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), 0);

	return NM_IP4_CONFIG_GET_PRIVATE (config)->domains->len;
}

void nm_ip4_config_reset_domains (NMIP4Config *config)
{
	NMIP4ConfigPrivate *priv;
	int i;

	g_return_if_fail (NM_IS_IP4_CONFIG (config));

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	for (i = 0; i < priv->domains->len; i++)
		g_free (g_ptr_array_index (priv->domains, i));
	g_ptr_array_free (priv->domains, TRUE);
	priv->domains = g_ptr_array_sized_new (3);
}

void nm_ip4_config_add_search (NMIP4Config *config, const char *search)
{
	NMIP4ConfigPrivate *priv;
	int i;

	g_return_if_fail (config != NULL);
	g_return_if_fail (search != NULL);
	g_return_if_fail (strlen (search) > 0);

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);

	for (i = 0; i < priv->searches->len; i++) {
		if (!strcmp (g_ptr_array_index (priv->searches, i), search))
			return;
	}

	g_ptr_array_add (priv->searches, g_strdup (search));
}

const char *nm_ip4_config_get_search (NMIP4Config *config, guint i)
{
	g_return_val_if_fail (config != NULL, NULL);

	return (const char *) g_ptr_array_index (NM_IP4_CONFIG_GET_PRIVATE (config)->searches, i);
}

guint32 nm_ip4_config_get_num_searches (NMIP4Config *config)
{
	g_return_val_if_fail (config != NULL, 0);

	return NM_IP4_CONFIG_GET_PRIVATE (config)->searches->len;
}

void nm_ip4_config_reset_searches (NMIP4Config *config)
{
	NMIP4ConfigPrivate *priv;
	int i;

	g_return_if_fail (NM_IS_IP4_CONFIG (config));

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	for (i = 0; i < priv->searches->len; i++)
		g_free (g_ptr_array_index (priv->searches, i));
	g_ptr_array_free (priv->searches, TRUE);
	priv->searches = g_ptr_array_sized_new (3);
}

guint32 nm_ip4_config_get_mtu (NMIP4Config *config)
{
	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), 0);

	return NM_IP4_CONFIG_GET_PRIVATE (config)->mtu;
}

void nm_ip4_config_set_mtu (NMIP4Config *config, guint32 mtu)
{
	g_return_if_fail (NM_IS_IP4_CONFIG (config));

	NM_IP4_CONFIG_GET_PRIVATE (config)->mtu = mtu;
}

guint32 nm_ip4_config_get_mss (NMIP4Config *config)
{
	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), 0);

	return NM_IP4_CONFIG_GET_PRIVATE (config)->mss;
}

void nm_ip4_config_set_mss (NMIP4Config *config, guint32 mss)
{
	g_return_if_fail (NM_IS_IP4_CONFIG (config));

	NM_IP4_CONFIG_GET_PRIVATE (config)->mss = mss;
}

gboolean
nm_ip4_config_get_never_default (NMIP4Config *config)
{
	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), FALSE);

	return NM_IP4_CONFIG_GET_PRIVATE (config)->never_default;
}

void
nm_ip4_config_set_never_default (NMIP4Config *config, gboolean never_default)
{
	g_return_if_fail (NM_IS_IP4_CONFIG (config));

	NM_IP4_CONFIG_GET_PRIVATE (config)->never_default = never_default;
}

void nm_ip4_config_add_nis_server (NMIP4Config *config, guint32 nis)
{
	NMIP4ConfigPrivate *priv;
	int i;

	g_return_if_fail (NM_IS_IP4_CONFIG (config));
	g_return_if_fail (nis > 0);

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	for (i = 0; i < priv->nis->len; i++) {
		guint32 s = g_array_index (priv->nis, guint32, i);

		/* No dupes */
		g_return_if_fail (nis != s);
	}

	g_array_append_val (priv->nis, nis);
}

guint32 nm_ip4_config_get_nis_server (NMIP4Config *config, guint i)
{
	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), 0);

	return g_array_index (NM_IP4_CONFIG_GET_PRIVATE (config)->nis, guint32, i);
}

guint32 nm_ip4_config_get_num_nis_servers (NMIP4Config *config)
{
	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), 0);

	return NM_IP4_CONFIG_GET_PRIVATE (config)->nis->len;
}

void nm_ip4_config_reset_nis_servers (NMIP4Config *config)
{
	NMIP4ConfigPrivate *priv;

	g_return_if_fail (NM_IS_IP4_CONFIG (config));

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	if (priv->nis->len)
		g_array_remove_range (priv->nis, 0, priv->nis->len);
}

void
nm_ip4_config_set_nis_domain (NMIP4Config *config, const char *domain)
{
	NMIP4ConfigPrivate *priv;

	g_return_if_fail (NM_IS_IP4_CONFIG (config));

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	g_free (priv->nis_domain);
	priv->nis_domain = g_strdup (domain);
}

const char *
nm_ip4_config_get_nis_domain (NMIP4Config *config)
{
	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), 0);

	return NM_IP4_CONFIG_GET_PRIVATE (config)->nis_domain;
}

static gboolean
addr_slist_compare (GSList *a, GSList *b)
{
	GSList *iter_a, *iter_b;
	gboolean found = FALSE;

	for (iter_a = a; iter_a; iter_a = g_slist_next (iter_a)) {
		NMIP4Address *addr_a = (NMIP4Address *) iter_a->data;

		for (iter_b = b, found = FALSE; iter_b; iter_b = g_slist_next (iter_b)) {
			NMIP4Address *addr_b = (NMIP4Address *) iter_b->data;

			if (nm_ip4_address_compare (addr_a, addr_b)) {
				found = TRUE;
				break;
			}
		}

		if (!found)
			return FALSE;
	}
	return TRUE;
}

static gboolean
route_slist_compare (GSList *a, GSList *b)
{
	GSList *iter_a, *iter_b;
	gboolean found = FALSE;

	for (iter_a = a; iter_a; iter_a = g_slist_next (iter_a)) {
		NMIP4Route *route_a = (NMIP4Route *) iter_a->data;

		for (iter_b = b, found = FALSE; iter_b; iter_b = g_slist_next (iter_b)) {
			NMIP4Route *route_b = (NMIP4Route *) iter_b->data;

			if (nm_ip4_route_compare (route_a, route_b)) {
				found = TRUE;
				break;
			}
		}

		if (!found)
			return FALSE;
	}
	return TRUE;
}

static gboolean
string_array_compare (GPtrArray *a, GPtrArray *b)
{
	int i, j;
	gboolean found = FALSE;

	for (i = 0; i < a->len; i++) {
		for (j = 0, found = FALSE; j < b->len; j++) {
			const char *item_a = g_ptr_array_index (a, i);
			const char *item_b = g_ptr_array_index (b, j);

			if ((!item_a && !item_b) || (item_a && item_b && !strcmp (item_a, item_b))) {
				found = TRUE;
				break;
			}
		}

		if (!found)
			return FALSE;
	}
	return TRUE;
}

static gboolean
addr_array_compare (GArray *a, GArray *b)
{
	int i, j;
	gboolean found = FALSE;

	for (i = 0; i < a->len; i++) {
		for (j = 0, found = FALSE; j < b->len; j++) {
			if (g_array_index (a, guint32, i) == g_array_index (b, guint32, j)) {
				found = TRUE;
				break;
			}
		}

		if (!found)
			return FALSE;
	}
	return TRUE;
}

NMIP4ConfigCompareFlags
nm_ip4_config_diff (NMIP4Config *a, NMIP4Config *b)
{
	NMIP4ConfigPrivate *a_priv;
	NMIP4ConfigPrivate *b_priv;
	NMIP4ConfigCompareFlags flags = NM_IP4_COMPARE_FLAG_NONE;

	if ((a && !b) || (b && !a))
		return NM_IP4_COMPARE_FLAG_ALL;
	if (!a && !b)
		return NM_IP4_COMPARE_FLAG_NONE;

	a_priv = NM_IP4_CONFIG_GET_PRIVATE (a);
	b_priv = NM_IP4_CONFIG_GET_PRIVATE (b);

	if (   !addr_slist_compare (a_priv->addresses, b_priv->addresses)
	    || !addr_slist_compare (b_priv->addresses, a_priv->addresses))
		flags |= NM_IP4_COMPARE_FLAG_ADDRESSES;

	if (a_priv->ptp_address != b_priv->ptp_address)
		flags |= NM_IP4_COMPARE_FLAG_PTP_ADDRESS;

	if (   (a_priv->nameservers->len != b_priv->nameservers->len)
	    || !addr_array_compare (a_priv->nameservers, b_priv->nameservers)
	    || !addr_array_compare (b_priv->nameservers, a_priv->nameservers))
		flags |= NM_IP4_COMPARE_FLAG_NAMESERVERS;

	if (   (a_priv->wins->len != b_priv->wins->len)
	    || !addr_array_compare (a_priv->wins, b_priv->wins)
	    || !addr_array_compare (b_priv->wins, a_priv->wins))
		flags |= NM_IP4_COMPARE_FLAG_WINS_SERVERS;

	if (   (a_priv->nis->len != b_priv->nis->len)
	    || !addr_array_compare (a_priv->nis, b_priv->nis)
	    || !addr_array_compare (b_priv->nis, a_priv->nis))
		flags |= NM_IP4_COMPARE_FLAG_NIS_SERVERS;

	if (   (a_priv->nis_domain || b_priv->nis_domain)
		&& (g_strcmp0 (a_priv->nis_domain, b_priv->nis_domain) != 0))
		flags |= NM_IP4_COMPARE_FLAG_NIS_DOMAIN;

	if (   !route_slist_compare (a_priv->routes, b_priv->routes)
	    || !route_slist_compare (b_priv->routes, a_priv->routes))
		flags |= NM_IP4_COMPARE_FLAG_ROUTES;

	if (   (a_priv->domains->len != b_priv->domains->len)
	    || !string_array_compare (a_priv->domains, b_priv->domains)
	    || !string_array_compare (b_priv->domains, a_priv->domains))
		flags |= NM_IP4_COMPARE_FLAG_DOMAINS;

	if (   (a_priv->searches->len != b_priv->searches->len)
	    || !string_array_compare (a_priv->searches, b_priv->searches)
	    || !string_array_compare (b_priv->searches, a_priv->searches))
		flags |= NM_IP4_COMPARE_FLAG_SEARCHES;

	if (a_priv->mtu != b_priv->mtu)
		flags |= NM_IP4_COMPARE_FLAG_MTU;

	if (a_priv->mss != b_priv->mss)
		flags |= NM_IP4_COMPARE_FLAG_MSS;

	return flags;
}

static inline void
hash_u32 (GChecksum *sum, guint32 n)
{
	g_checksum_update (sum, (const guint8 *) &n, sizeof (n));
}

void
nm_ip4_config_hash (NMIP4Config *config, GChecksum *sum, gboolean dns_only)
{
	guint32 i, n;
	const char *s;

	g_return_if_fail (config != NULL);
	g_return_if_fail (sum != NULL);

	if (dns_only == FALSE) {
		for (i = 0; i < nm_ip4_config_get_num_addresses (config); i++) {
			NMIP4Address *a = nm_ip4_config_get_address (config, i);

			hash_u32 (sum, nm_ip4_address_get_address (a));
			hash_u32 (sum, nm_ip4_address_get_prefix (a));
			hash_u32 (sum, nm_ip4_address_get_gateway (a));
		}

		for (i = 0; i < nm_ip4_config_get_num_routes (config); i++) {
			NMIP4Route *r = nm_ip4_config_get_route (config, i);

			hash_u32 (sum, nm_ip4_route_get_dest (r));
			hash_u32 (sum, nm_ip4_route_get_prefix (r));
			hash_u32 (sum, nm_ip4_route_get_next_hop (r));
			hash_u32 (sum, nm_ip4_route_get_metric (r));
		}

		n = nm_ip4_config_get_ptp_address (config);
		if (n)
			hash_u32 (sum, n);

		for (i = 0; i < nm_ip4_config_get_num_nis_servers (config); i++)
			hash_u32 (sum, nm_ip4_config_get_nis_server (config, i));

		s = nm_ip4_config_get_nis_domain (config);
		if (s)
			g_checksum_update (sum, (const guint8 *) s, strlen (s));
	}

	for (i = 0; i < nm_ip4_config_get_num_nameservers (config); i++)
		hash_u32 (sum, nm_ip4_config_get_nameserver (config, i));

	for (i = 0; i < nm_ip4_config_get_num_wins (config); i++)
		hash_u32 (sum, nm_ip4_config_get_wins (config, i));

	for (i = 0; i < nm_ip4_config_get_num_domains (config); i++) {
		s = nm_ip4_config_get_domain (config, i);
		g_checksum_update (sum, (const guint8 *) s, strlen (s));
	}

	for (i = 0; i < nm_ip4_config_get_num_searches (config); i++) {
		s = nm_ip4_config_get_search (config, i);
		g_checksum_update (sum, (const guint8 *) s, strlen (s));
	}
}

static void
nm_ip4_config_init (NMIP4Config *config)
{
	NMIP4ConfigPrivate *priv = NM_IP4_CONFIG_GET_PRIVATE (config);

	priv->nameservers = g_array_new (FALSE, TRUE, sizeof (guint32));
	priv->wins = g_array_new (FALSE, TRUE, sizeof (guint32));
	priv->domains = g_ptr_array_sized_new (3);
	priv->searches = g_ptr_array_sized_new (3);
	priv->nis = g_array_new (FALSE, TRUE, sizeof (guint32));
}

static void
finalize (GObject *object)
{
	NMIP4ConfigPrivate *priv = NM_IP4_CONFIG_GET_PRIVATE (object);

	g_slist_free_full (priv->addresses, (GDestroyNotify) nm_ip4_address_unref);
	g_slist_free_full (priv->routes, (GDestroyNotify) nm_ip4_route_unref);
	g_array_free (priv->wins, TRUE);
	g_array_free (priv->nameservers, TRUE);
	g_ptr_array_free (priv->domains, TRUE);
	g_ptr_array_free (priv->searches, TRUE);
	g_array_free (priv->nis, TRUE);
	g_free (priv->nis_domain);

	G_OBJECT_CLASS (nm_ip4_config_parent_class)->finalize (object);
}

static void
get_property (GObject *object, guint prop_id,
			  GValue *value, GParamSpec *pspec)
{
	NMIP4ConfigPrivate *priv = NM_IP4_CONFIG_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_ADDRESSES:
		nm_utils_ip4_addresses_to_gvalue (priv->addresses, value);
		break;
	case PROP_NAMESERVERS:
		g_value_set_boxed (value, priv->nameservers);
		break;
	case PROP_DOMAINS:
		g_value_set_boxed (value, priv->domains);
		break;
	case PROP_ROUTES:
		nm_utils_ip4_routes_to_gvalue (priv->routes, value);
		break;
	case PROP_WINS_SERVERS:
		g_value_set_boxed (value, priv->wins);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
nm_ip4_config_class_init (NMIP4ConfigClass *config_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (config_class);

	g_type_class_add_private (config_class, sizeof (NMIP4ConfigPrivate));

	/* virtual methods */
	object_class->get_property = get_property;
	object_class->finalize = finalize;

	/* properties */
	g_object_class_install_property
		(object_class, PROP_ADDRESSES,
		 g_param_spec_boxed (NM_IP4_CONFIG_ADDRESSES,
							"Addresses",
							"IP4 addresses",
							DBUS_TYPE_G_ARRAY_OF_ARRAY_OF_UINT,
							G_PARAM_READABLE));
	g_object_class_install_property
		(object_class, PROP_NAMESERVERS,
		 g_param_spec_boxed (NM_IP4_CONFIG_NAMESERVERS,
							 "Nameservers",
							 "DNS list",
							 DBUS_TYPE_G_UINT_ARRAY,
							 G_PARAM_READABLE));
	g_object_class_install_property
		(object_class, PROP_DOMAINS,
		 g_param_spec_boxed (NM_IP4_CONFIG_DOMAINS,
							 "Domains",
							 "Domains",
							 DBUS_TYPE_G_ARRAY_OF_STRING,
							 G_PARAM_READABLE));
	g_object_class_install_property
		(object_class, PROP_ROUTES,
		 g_param_spec_boxed (NM_IP4_CONFIG_ROUTES,
						 "Routes",
						 "Routes",
						 DBUS_TYPE_G_ARRAY_OF_ARRAY_OF_UINT,
						 G_PARAM_READABLE));
	g_object_class_install_property
		(object_class, PROP_WINS_SERVERS,
		 g_param_spec_boxed (NM_IP4_CONFIG_WINS_SERVERS,
							 "WinsServers",
							 "WINS server list",
							 DBUS_TYPE_G_UINT_ARRAY,
							 G_PARAM_READABLE));

	dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (config_class),
									 &dbus_glib_nm_ip4_config_object_info);
}
