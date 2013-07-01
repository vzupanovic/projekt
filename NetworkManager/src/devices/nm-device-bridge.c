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
 * Copyright 2011 - 2012 Red Hat, Inc.
 */

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>

#include <netinet/ether.h>

#include "nm-device-bridge.h"
#include "nm-logging.h"
#include "nm-utils.h"
#include "NetworkManagerUtils.h"
#include "nm-device-private.h"
#include "nm-dbus-glib-types.h"
#include "nm-dbus-manager.h"
#include "nm-enum-types.h"
#include "nm-platform.h"
#include "nm-system.h"

#include "nm-device-bridge-glue.h"


G_DEFINE_TYPE (NMDeviceBridge, nm_device_bridge, NM_TYPE_DEVICE)

#define NM_DEVICE_BRIDGE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_DEVICE_BRIDGE, NMDeviceBridgePrivate))

#define NM_BRIDGE_ERROR (nm_bridge_error_quark ())

typedef struct {
	int dummy;
} NMDeviceBridgePrivate;

enum {
	PROP_0,
	PROP_SLAVES,

	LAST_PROP
};

/******************************************************************/

static GQuark
nm_bridge_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("nm-bridge-error");
	return quark;
}

/******************************************************************/

static guint32
get_generic_capabilities (NMDevice *dev)
{
	return NM_DEVICE_CAP_CARRIER_DETECT;
}

static gboolean
is_available (NMDevice *dev)
{
	if (NM_DEVICE_GET_CLASS (dev)->is_up)
		return NM_DEVICE_GET_CLASS (dev)->is_up (dev);
	return FALSE;
}

static gboolean
check_connection_compatible (NMDevice *device,
                             NMConnection *connection,
                             GError **error)
{
	const char *iface;
	NMSettingBridge *s_bridge;

	if (!NM_DEVICE_CLASS (nm_device_bridge_parent_class)->check_connection_compatible (device, connection, error))
		return FALSE;

	s_bridge = nm_connection_get_setting_bridge (connection);
	if (!s_bridge || !nm_connection_is_type (connection, NM_SETTING_BRIDGE_SETTING_NAME)) {
		g_set_error (error, NM_BRIDGE_ERROR, NM_BRIDGE_ERROR_CONNECTION_NOT_BRIDGE,
		             "The connection was not a bridge connection.");
		return FALSE;
	}

	/* Bridge connections must specify the virtual interface name */
	iface = nm_connection_get_virtual_iface_name (connection);
	if (!iface || strcmp (nm_device_get_iface (device), iface)) {
		g_set_error (error, NM_BRIDGE_ERROR, NM_BRIDGE_ERROR_CONNECTION_INVALID,
		             "The bridge connection virtual interface name did not match.");
		return FALSE;
	}

	return TRUE;
}

static gboolean
complete_connection (NMDevice *device,
                     NMConnection *connection,
                     const char *specific_object,
                     const GSList *existing_connections,
                     GError **error)
{
	NMSettingBridge *s_bridge, *tmp;
	guint32 i = 0;
	char *name;
	const GSList *iter;
	gboolean found;

	nm_utils_complete_generic (connection,
	                           NM_SETTING_BRIDGE_SETTING_NAME,
	                           existing_connections,
	                           _("Bridge connection %d"),
	                           NULL,
	                           TRUE);

	s_bridge = nm_connection_get_setting_bridge (connection);
	if (!s_bridge) {
		s_bridge = (NMSettingBridge *) nm_setting_bridge_new ();
		nm_connection_add_setting (connection, NM_SETTING (s_bridge));
	}

	/* Grab the first name that doesn't exist in either our connections
	 * or a device on the system.
	 */
	while (i < 500 && !nm_setting_bridge_get_interface_name (s_bridge)) {
		name = g_strdup_printf ("br%u", i);
		/* check interface names */
		if (!nm_platform_link_exists (name)) {
			/* check existing bridge connections */
			for (iter = existing_connections, found = FALSE; iter; iter = g_slist_next (iter)) {
				NMConnection *candidate = iter->data;

				tmp = nm_connection_get_setting_bridge (candidate);
				if (tmp && nm_connection_is_type (candidate, NM_SETTING_BRIDGE_SETTING_NAME)) {
					if (g_strcmp0 (nm_setting_bridge_get_interface_name (tmp), name) == 0) {
						found = TRUE;
						break;
					}
				}
			}

			if (!found)
				g_object_set (G_OBJECT (s_bridge), NM_SETTING_BRIDGE_INTERFACE_NAME, name, NULL);
		}

		g_free (name);
		i++;
	}

	return TRUE;
}

static gboolean
match_l2_config (NMDevice *self, NMConnection *connection)
{
	/* FIXME */
	return TRUE;
}

/******************************************************************/

static void
set_sysfs_uint (const char *iface,
                GObject *obj,
                const char *obj_prop,
                const char *dir,
                const char *sysfs_prop,
                gboolean default_if_zero,
                gboolean user_hz_compensate)
{
	char *path, *s;
	GParamSpec *pspec;
	GValue val = G_VALUE_INIT;
	guint32 uval = 0;

	pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (obj), obj_prop);
	g_return_if_fail (pspec != NULL);

	/* Get the property's value */
	g_value_init (&val, G_PARAM_SPEC_VALUE_TYPE (pspec));
	g_object_get_property (obj, obj_prop, &val);
	if (G_VALUE_HOLDS_BOOLEAN (&val))
		uval = g_value_get_boolean (&val) ? 1 : 0;
	else if (G_VALUE_HOLDS_UINT (&val)) {
		uval = g_value_get_uint (&val);

		/* zero means "unspecified" for some NM properties but isn't in the
		 * allowed kernel range, so reset the property to the default value.
		 */
		if (default_if_zero && uval == 0) {
			g_value_unset (&val);
			g_value_init (&val, G_PARAM_SPEC_VALUE_TYPE (pspec));
			g_param_value_set_default (pspec, &val);
			uval = g_value_get_uint (&val);
		}
	} else
		g_assert_not_reached ();

	g_value_unset (&val);

	/* Linux kernel bridge interfaces use 'centiseconds' for time-based values.
	 * In reality it's not centiseconds, but depends on HZ and USER_HZ, which
	 * is almost always works out to be a multiplier of 100, so we can assume
	 * centiseconds.  See clock_t_to_jiffies().
	 */
	if (user_hz_compensate)
		uval *= 100;

	path = g_strdup_printf ("/sys/class/net/%s/%s/%s", iface, dir, sysfs_prop);
	s = g_strdup_printf ("%u", uval);
	/* FIXME: how should failure be handled? */
	nm_utils_do_sysctl (path, s);
	g_free (path);
	g_free (s);
}

static NMActStageReturn
act_stage1_prepare (NMDevice *dev, NMDeviceStateReason *reason)
{
	NMActStageReturn ret = NM_ACT_STAGE_RETURN_SUCCESS;
	NMConnection *connection;
	NMSettingBridge *s_bridge;
	const char *iface;

	g_return_val_if_fail (reason != NULL, NM_ACT_STAGE_RETURN_FAILURE);

	ret = NM_DEVICE_CLASS (nm_device_bridge_parent_class)->act_stage1_prepare (dev, reason);
	if (ret == NM_ACT_STAGE_RETURN_SUCCESS) {
		connection = nm_device_get_connection (dev);
		g_assert (connection);

		s_bridge = nm_connection_get_setting_bridge (connection);
		g_assert (s_bridge);

		iface = nm_device_get_ip_iface (dev);
		g_assert (iface);

		set_sysfs_uint (iface, G_OBJECT (s_bridge), NM_SETTING_BRIDGE_STP, "bridge", "stp_state", FALSE, FALSE);
		set_sysfs_uint (iface, G_OBJECT (s_bridge), NM_SETTING_BRIDGE_PRIORITY, "bridge", "priority", TRUE, FALSE);
		set_sysfs_uint (iface, G_OBJECT (s_bridge), NM_SETTING_BRIDGE_FORWARD_DELAY, "bridge", "forward_delay", TRUE, TRUE);
		set_sysfs_uint (iface, G_OBJECT (s_bridge), NM_SETTING_BRIDGE_HELLO_TIME, "bridge", "hello_time", TRUE, TRUE);
		set_sysfs_uint (iface, G_OBJECT (s_bridge), NM_SETTING_BRIDGE_MAX_AGE, "bridge", "max_age", TRUE, TRUE);
		set_sysfs_uint (iface, G_OBJECT (s_bridge), NM_SETTING_BRIDGE_AGEING_TIME, "bridge", "ageing_time", TRUE, TRUE);
	}
	return ret;
}

static gboolean
enslave_slave (NMDevice *device, NMDevice *slave, NMConnection *connection)
{
	gboolean success;
	NMSettingBridgePort *s_port;
	const char *iface = nm_device_get_ip_iface (device);
	const char *slave_iface = nm_device_get_ip_iface (slave);

	success = nm_platform_link_enslave (nm_device_get_ip_ifindex (device),
	                                    nm_device_get_ip_ifindex (slave));

	if (!success)
		return FALSE;

	/* Set port properties */
	s_port = nm_connection_get_setting_bridge_port (connection);
	if (s_port) {
		set_sysfs_uint (slave_iface, G_OBJECT (s_port), NM_SETTING_BRIDGE_PORT_PRIORITY, "brport", "priority", TRUE, FALSE);
		set_sysfs_uint (slave_iface, G_OBJECT (s_port), NM_SETTING_BRIDGE_PORT_PATH_COST, "brport", "path_cost", TRUE, FALSE);
		set_sysfs_uint (slave_iface, G_OBJECT (s_port), NM_SETTING_BRIDGE_PORT_HAIRPIN_MODE, "brport", "hairpin_mode", FALSE, FALSE);
	}

	nm_log_info (LOGD_BRIDGE, "(%s): attached bridge port %s", iface, slave_iface);

	g_object_notify (G_OBJECT (device), NM_DEVICE_BRIDGE_SLAVES);

	return TRUE;
}

static gboolean
release_slave (NMDevice *device, NMDevice *slave)
{
	gboolean success;

	success = nm_platform_link_release (nm_device_get_ip_ifindex (device),
	                                    nm_device_get_ip_ifindex (slave));

	nm_log_info (LOGD_BRIDGE, "(%s): detached bridge port %s (success %d)",
	             nm_device_get_ip_iface (device),
	             nm_device_get_ip_iface (slave),
	             success);
	g_object_notify (G_OBJECT (device), NM_DEVICE_BRIDGE_SLAVES);
	return success;
}

/******************************************************************/

NMDevice *
nm_device_bridge_new (const char *udi, const char *iface)
{
	g_return_val_if_fail (udi != NULL, NULL);
	g_return_val_if_fail (iface != NULL, NULL);

	return (NMDevice *) g_object_new (NM_TYPE_DEVICE_BRIDGE,
	                                  NM_DEVICE_UDI, udi,
	                                  NM_DEVICE_IFACE, iface,
	                                  NM_DEVICE_DRIVER, "bridge",
	                                  NM_DEVICE_TYPE_DESC, "Bridge",
	                                  NM_DEVICE_DEVICE_TYPE, NM_DEVICE_TYPE_BRIDGE,
	                                  NM_DEVICE_IS_MASTER, TRUE,
	                                  NULL);
}

static void
constructed (GObject *object)
{
	G_OBJECT_CLASS (nm_device_bridge_parent_class)->constructed (object);

	nm_log_dbg (LOGD_HW | LOGD_BRIDGE, "(%s): kernel ifindex %d",
	            nm_device_get_iface (NM_DEVICE (object)),
	            nm_device_get_ifindex (NM_DEVICE (object)));
}

static void
nm_device_bridge_init (NMDeviceBridge * self)
{
}

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
	GPtrArray *slaves;
	GSList *list, *iter;

	switch (prop_id) {
		break;
	case PROP_SLAVES:
		slaves = g_ptr_array_new ();
		list = nm_device_master_get_slaves (NM_DEVICE (object));
		for (iter = list; iter; iter = iter->next)
			g_ptr_array_add (slaves, g_strdup (nm_device_get_path (NM_DEVICE (iter->data))));
		g_slist_free (list);
		g_value_take_boxed (value, slaves);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
set_property (GObject *object, guint prop_id,
			  const GValue *value, GParamSpec *pspec)
{
	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
nm_device_bridge_class_init (NMDeviceBridgeClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	NMDeviceClass *parent_class = NM_DEVICE_CLASS (klass);

	g_type_class_add_private (object_class, sizeof (NMDeviceBridgePrivate));

	/* virtual methods */
	object_class->constructed = constructed;
	object_class->get_property = get_property;
	object_class->set_property = set_property;

	parent_class->get_generic_capabilities = get_generic_capabilities;
	parent_class->is_available = is_available;
	parent_class->check_connection_compatible = check_connection_compatible;
	parent_class->complete_connection = complete_connection;

	parent_class->match_l2_config = match_l2_config;

	parent_class->act_stage1_prepare = act_stage1_prepare;
	parent_class->enslave_slave = enslave_slave;
	parent_class->release_slave = release_slave;

	/* properties */
	g_object_class_install_property
		(object_class, PROP_SLAVES,
		 g_param_spec_boxed (NM_DEVICE_BRIDGE_SLAVES,
		                     "Slaves",
		                     "Slaves",
		                     DBUS_TYPE_G_ARRAY_OF_OBJECT_PATH,
		                     G_PARAM_READABLE));

	nm_dbus_manager_register_exported_type (nm_dbus_manager_get (),
	                                        G_TYPE_FROM_CLASS (klass),
	                                        &dbus_glib_nm_device_bridge_object_info);

	dbus_g_error_domain_register (NM_BRIDGE_ERROR, NULL, NM_TYPE_BRIDGE_ERROR);
}
