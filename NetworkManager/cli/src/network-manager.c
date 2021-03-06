/* nmcli - command-line tool to control NetworkManager
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
 * (C) Copyright 2010 - 2012 Red Hat, Inc.
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <nm-client.h>
#include <nm-setting-connection.h>

#include "utils.h"
#include "network-manager.h"


/* Available fields for 'general status' */
static NmcOutputField nmc_fields_nm_status[] = {
	{"RUNNING",    N_("RUNNING"),    15},  /* 0 */
	{"VERSION",    N_("VERSION"),    10},  /* 1 */
	{"STATE",      N_("STATE"),      15},  /* 2 */
	{"NETWORKING", N_("NETWORKING"), 13},  /* 3 */
	{"WIFI-HW",    N_("WIFI-HW"),    15},  /* 4 */
	{"WIFI",       N_("WIFI"),       10},  /* 5 */
	{"WWAN-HW",    N_("WWAN-HW"),    15},  /* 6 */
	{"WWAN",       N_("WWAN"),       10},  /* 7 */
	{"WIMAX-HW",   N_("WIMAX-HW"),   15},  /* 8 */
	{"WIMAX",      N_("WIMAX"),      10},  /* 9 */
	{NULL,         NULL,              0}
};
#if WITH_WIMAX
#define NMC_FIELDS_NM_STATUS_ALL     "RUNNING,VERSION,STATE,NETWORKING,WIFI-HW,WIFI,WWAN-HW,WWAN,WIMAX-HW,WIMAX"
#define NMC_FIELDS_NM_STATUS_SWITCH  "NETWORKING,WIFI-HW,WIFI,WWAN-HW,WWAN,WIMAX-HW,WIMAX"
#define NMC_FIELDS_NM_STATUS_RADIO   "WIFI-HW,WIFI,WWAN-HW,WWAN,WIMAX-HW,WIMAX"
#else
#define NMC_FIELDS_NM_STATUS_ALL     "RUNNING,VERSION,STATE,NETWORKING,WIFI-HW,WIFI,WWAN-HW,WWAN"
#define NMC_FIELDS_NM_STATUS_SWITCH  "NETWORKING,WIFI-HW,WIFI,WWAN-HW,WWAN"
#define NMC_FIELDS_NM_STATUS_RADIO   "WIFI-HW,WIFI,WWAN-HW,WWAN"
#endif
#define NMC_FIELDS_NM_STATUS_COMMON  "STATE,WIFI-HW,WIFI,WWAN-HW,WWAN"
#define NMC_FIELDS_NM_NETWORKING     "NETWORKING"
#define NMC_FIELDS_NM_WIFI           "WIFI"
#define NMC_FIELDS_NM_WWAN           "WWAN"
#define NMC_FIELDS_NM_WIMAX          "WIMAX"


/* Available fields for 'general permissions' */
static NmcOutputField nmc_fields_nm_permissions[] = {
	{"PERMISSION", N_("PERMISSION"), 57},  /* 0 */
	{"VALUE",      N_("VALUE"),      10},  /* 1 */
	{NULL,         NULL,              0}
};
#define NMC_FIELDS_NM_PERMISSIONS_ALL     "PERMISSION,VALUE"
#define NMC_FIELDS_NM_PERMISSIONS_COMMON  "PERMISSION,VALUE"

/* Available fields for 'general logging' */
static NmcOutputField nmc_fields_nm_logging[] = {
	{"LEVEL",   N_("LEVEL"),   10},  /* 0 */
	{"DOMAINS", N_("DOMAINS"), 70},  /* 1 */
	{NULL,      NULL,           0}
};
#define NMC_FIELDS_NM_LOGGING_ALL     "LEVEL,DOMAINS"
#define NMC_FIELDS_NM_LOGGING_COMMON  "LEVEL,DOMAINS"


/* glib main loop variable - defined in nmcli.c */
extern GMainLoop *loop;


static void
usage_general (void)
{
	fprintf (stderr,
	         _("Usage: nmcli general { COMMAND | help }\n\n"
	         "  COMMAND := { status | permissions | logging }\n\n"
	         "  status\n\n"
	         "  permissions\n\n"
	         "  logging [level <log level>] [domains <log domains>]\n\n"
	         ));
}

static void
usage_networking (void)
{
	fprintf (stderr,
	         _("Usage: nmcli networking { COMMAND | help }\n\n"
	         "  COMMAND := { [ on | off ] }\n\n"
	         ));
}

static void
usage_radio (void)
{
	fprintf (stderr,
	         _("Usage: nmcli radio { COMMAND | help }\n\n"
#if WITH_WIMAX
	         "  COMMAND := { all | wifi | wwan | wimax }\n\n"
	         "  all | wifi | wwan | wimax [ on | off ]\n\n"
#else
	         "  COMMAND := { all | wifi | wwan }\n\n"
	         "  all | wifi | wwan [ on | off ]\n\n"
#endif
	         ));
}

/* quit main loop */
static void
quit (void)
{
	g_main_loop_quit (loop);  /* quit main loop */
}

static const char *
nm_state_to_string (NMState state)
{
	switch (state) {
	case NM_STATE_ASLEEP:
		return _("asleep");
	case NM_STATE_CONNECTING:
		return _("connecting");
	case NM_STATE_CONNECTED_LOCAL:
		return _("connected (local only)");
	case NM_STATE_CONNECTED_SITE:
		return _("connected (site only)");
	case NM_STATE_CONNECTED_GLOBAL:
		return _("connected");
	case NM_STATE_DISCONNECTING:
		return _("disconnecting");
	case NM_STATE_DISCONNECTED:
		return _("disconnected");
	case NM_STATE_UNKNOWN:
	default:
		return _("unknown");
	}
}

static gboolean
show_nm_status (NmCli *nmc, const char *pretty_header_name, const char *print_flds)
{
	gboolean nm_running;
	NMState state = NM_STATE_UNKNOWN;
	const char *net_enabled_str;
	const char *wireless_hw_enabled_str, *wireless_enabled_str;
	const char *wwan_hw_enabled_str, *wwan_enabled_str;
#if WITH_WIMAX
	const char *wimax_hw_enabled_str, *wimax_enabled_str;
#endif
	GError *error = NULL;
	const char *fields_str;
	const char *fields_all =    print_flds ? print_flds : NMC_FIELDS_NM_STATUS_ALL;
	const char *fields_common = print_flds ? print_flds : NMC_FIELDS_NM_STATUS_COMMON;
	NmcOutputField *tmpl, *arr;
	size_t tmpl_len;

	if (!nmc->required_fields || strcasecmp (nmc->required_fields, "common") == 0)
		fields_str = fields_common;
	else if (!nmc->required_fields || strcasecmp (nmc->required_fields, "all") == 0)
		fields_str = fields_all;
	else
		fields_str = nmc->required_fields;

	tmpl = nmc_fields_nm_status;
	tmpl_len = sizeof (nmc_fields_nm_status);
	nmc->print_fields.indices = parse_output_fields (fields_str, tmpl, &error);

	if (error) {
		if (error->code == 0)
			g_string_printf (nmc->return_text, _("Error: %s"), error->message);
		else
			g_string_printf (nmc->return_text, _("Error: %s (allowed fields: %s)"), error->message, fields_all);
		g_error_free (error);
		nmc->return_value = NMC_RESULT_ERROR_USER_INPUT;
		return FALSE;
	}

	nmc->get_client (nmc); /* create NMClient */

	nm_running = nm_client_get_manager_running (nmc->client);
	if (nm_running) {
		if (!nmc_versions_match (nmc))
			return FALSE;

		state = nm_client_get_state (nmc->client);
		net_enabled_str = nm_client_networking_get_enabled (nmc->client) ? _("enabled") : _("disabled");
		wireless_hw_enabled_str = nm_client_wireless_hardware_get_enabled (nmc->client) ? _("enabled") : _("disabled");
		wireless_enabled_str = nm_client_wireless_get_enabled (nmc->client) ? _("enabled") : _("disabled");
		wwan_hw_enabled_str = nm_client_wwan_hardware_get_enabled (nmc->client) ? _("enabled") : _("disabled");
		wwan_enabled_str = nm_client_wwan_get_enabled (nmc->client) ? _("enabled") : _("disabled");
#if WITH_WIMAX
		wimax_hw_enabled_str = nm_client_wimax_hardware_get_enabled (nmc->client) ? _("enabled") : _("disabled");
		wimax_enabled_str = nm_client_wimax_get_enabled (nmc->client) ? _("enabled") : _("disabled");
#endif
	} else {
#if WITH_WIMAX
		net_enabled_str = wireless_hw_enabled_str = wireless_enabled_str =
		wwan_hw_enabled_str = wwan_enabled_str = wimax_hw_enabled_str = wimax_enabled_str = _("unknown");
#else
		net_enabled_str = wireless_hw_enabled_str = wireless_enabled_str =
		wwan_hw_enabled_str = wwan_enabled_str = _("unknown");
#endif
	}

	nmc->print_fields.header_name = pretty_header_name ? (char *) pretty_header_name : _("NetworkManager status");
	arr = nmc_dup_fields_array (tmpl, tmpl_len, NMC_OF_FLAG_MAIN_HEADER_ADD | NMC_OF_FLAG_FIELD_NAMES);
	g_ptr_array_add (nmc->output_data, arr);

	arr = nmc_dup_fields_array (tmpl, tmpl_len, 0);
	set_val_strc (arr, 0, nm_running ? _("running") : _("not running"));
	set_val_strc (arr, 1, nm_running ? nm_client_get_version (nmc->client) : _("unknown"));
	set_val_strc (arr, 2, nm_state_to_string (state));
	set_val_strc (arr, 3, net_enabled_str);
	set_val_strc (arr, 4, wireless_hw_enabled_str);
	set_val_strc (arr, 5, wireless_enabled_str);
	set_val_strc (arr, 6, wwan_hw_enabled_str);
	set_val_strc (arr, 7, wwan_enabled_str);
#if WITH_WIMAX
	set_val_strc (arr, 8, wimax_hw_enabled_str);
	set_val_strc (arr, 9, wimax_enabled_str);
#endif
	g_ptr_array_add (nmc->output_data, arr);

	print_data (nmc);  /* Print all data */

	return TRUE;
}

#define NM_AUTH_PERMISSION_ENABLE_DISABLE_NETWORK     "org.freedesktop.NetworkManager.enable-disable-network"
#define NM_AUTH_PERMISSION_ENABLE_DISABLE_WIFI        "org.freedesktop.NetworkManager.enable-disable-wifi"
#define NM_AUTH_PERMISSION_ENABLE_DISABLE_WWAN        "org.freedesktop.NetworkManager.enable-disable-wwan"
#define NM_AUTH_PERMISSION_ENABLE_DISABLE_WIMAX       "org.freedesktop.NetworkManager.enable-disable-wimax"
#define NM_AUTH_PERMISSION_SLEEP_WAKE                 "org.freedesktop.NetworkManager.sleep-wake"
#define NM_AUTH_PERMISSION_NETWORK_CONTROL            "org.freedesktop.NetworkManager.network-control"
#define NM_AUTH_PERMISSION_WIFI_SHARE_PROTECTED       "org.freedesktop.NetworkManager.wifi.share.protected"
#define NM_AUTH_PERMISSION_WIFI_SHARE_OPEN            "org.freedesktop.NetworkManager.wifi.share.open"
#define NM_AUTH_PERMISSION_SETTINGS_MODIFY_SYSTEM     "org.freedesktop.NetworkManager.settings.modify.system"
#define NM_AUTH_PERMISSION_SETTINGS_MODIFY_OWN        "org.freedesktop.NetworkManager.settings.modify.own"
#define NM_AUTH_PERMISSION_SETTINGS_MODIFY_HOSTNAME   "org.freedesktop.NetworkManager.settings.modify.hostname"

static const char *
permission_to_string (NMClientPermission perm)
{
	switch (perm) {
	case NM_CLIENT_PERMISSION_ENABLE_DISABLE_NETWORK:
		return NM_AUTH_PERMISSION_ENABLE_DISABLE_NETWORK;
	case NM_CLIENT_PERMISSION_ENABLE_DISABLE_WIFI:
		return NM_AUTH_PERMISSION_ENABLE_DISABLE_WIFI;
	case NM_CLIENT_PERMISSION_ENABLE_DISABLE_WWAN:
		return NM_AUTH_PERMISSION_ENABLE_DISABLE_WWAN;
	case NM_CLIENT_PERMISSION_ENABLE_DISABLE_WIMAX:
		return NM_AUTH_PERMISSION_ENABLE_DISABLE_WIMAX;
	case NM_CLIENT_PERMISSION_SLEEP_WAKE:
		return NM_AUTH_PERMISSION_SLEEP_WAKE;
	case NM_CLIENT_PERMISSION_NETWORK_CONTROL:
		return NM_AUTH_PERMISSION_NETWORK_CONTROL;
	case NM_CLIENT_PERMISSION_WIFI_SHARE_PROTECTED:
		return NM_AUTH_PERMISSION_WIFI_SHARE_PROTECTED;
	case NM_CLIENT_PERMISSION_WIFI_SHARE_OPEN:
		return NM_AUTH_PERMISSION_WIFI_SHARE_OPEN;
	case NM_CLIENT_PERMISSION_SETTINGS_MODIFY_SYSTEM:
		return NM_AUTH_PERMISSION_SETTINGS_MODIFY_SYSTEM;
	case NM_CLIENT_PERMISSION_SETTINGS_MODIFY_OWN:
		return NM_AUTH_PERMISSION_SETTINGS_MODIFY_OWN;
	case NM_CLIENT_PERMISSION_SETTINGS_MODIFY_HOSTNAME:
		return NM_AUTH_PERMISSION_SETTINGS_MODIFY_HOSTNAME;
	default:
		return _("unknown");
	}
}

static const char *
permission_result_to_string (NMClientPermissionResult perm_result)
{
	
	switch (perm_result) {
	case NM_CLIENT_PERMISSION_RESULT_YES:
		return _("yes");
	case NM_CLIENT_PERMISSION_RESULT_NO:
		return _("no");
	case NM_CLIENT_PERMISSION_RESULT_AUTH:
		return _("auth");
	default:
		return _("unknown");
	}
}

static gboolean
show_nm_permissions (NmCli *nmc)
{
	NMClientPermission perm;
	GError *error = NULL;
	const char *fields_str;
	const char *fields_all =    NMC_FIELDS_NM_PERMISSIONS_ALL;
	const char *fields_common = NMC_FIELDS_NM_PERMISSIONS_COMMON;
	NmcOutputField *tmpl, *arr;
	size_t tmpl_len;

	if (!nmc->required_fields || strcasecmp (nmc->required_fields, "common") == 0)
		fields_str = fields_common;
	else if (!nmc->required_fields || strcasecmp (nmc->required_fields, "all") == 0)
		fields_str = fields_all;
	else
		fields_str = nmc->required_fields;

	tmpl = nmc_fields_nm_permissions;
	tmpl_len = sizeof (nmc_fields_nm_permissions);
	nmc->print_fields.indices = parse_output_fields (fields_str, tmpl, &error);

	if (error) {
		if (error->code == 0)
			g_string_printf (nmc->return_text, _("Error: 'general permissions': %s"), error->message);
		else
			g_string_printf (nmc->return_text, _("Error: 'general permissions': %s; allowed fields: %s"),
			                 error->message, NMC_FIELDS_NM_PERMISSIONS_ALL);
		g_error_free (error);
		nmc->return_value = NMC_RESULT_ERROR_USER_INPUT;
		return FALSE;
	}

	nmc->get_client (nmc); /* create NMClient */

	if (!nm_client_get_manager_running (nmc->client)) {
		g_string_printf (nmc->return_text, _("Error: NetworkManager is not running."));
		nmc->return_value = NMC_RESULT_ERROR_NM_NOT_RUNNING;
		return FALSE;
	}

	nmc->print_fields.header_name = _("NetworkManager permissions");
	arr = nmc_dup_fields_array (tmpl, tmpl_len, NMC_OF_FLAG_MAIN_HEADER_ADD | NMC_OF_FLAG_FIELD_NAMES);
	g_ptr_array_add (nmc->output_data, arr);

	for (perm = NM_CLIENT_PERMISSION_NONE + 1; perm <= NM_CLIENT_PERMISSION_LAST; perm++) {
		NMClientPermissionResult perm_result = nm_client_get_permission_result (nmc->client, perm);

		arr = nmc_dup_fields_array (tmpl, tmpl_len, 0);
		set_val_strc (arr, 0, permission_to_string (perm));
		set_val_strc (arr, 1, permission_result_to_string (perm_result));
		g_ptr_array_add (nmc->output_data, arr);
	}
	print_data (nmc);  /* Print all data */

	return TRUE;
}

static gboolean
show_general_logging (NmCli *nmc)
{
	char *level = NULL;
	char *domains = NULL;
	GError *error = NULL;
	const char *fields_str;
	const char *fields_all =    NMC_FIELDS_NM_LOGGING_ALL;
	const char *fields_common = NMC_FIELDS_NM_LOGGING_COMMON;
	NmcOutputField *tmpl, *arr;
	size_t tmpl_len;

	if (!nmc->required_fields || strcasecmp (nmc->required_fields, "common") == 0)
		fields_str = fields_common;
	else if (!nmc->required_fields || strcasecmp (nmc->required_fields, "all") == 0)
		fields_str = fields_all;
	else
		fields_str = nmc->required_fields;

	tmpl = nmc_fields_nm_logging;
	tmpl_len = sizeof (nmc_fields_nm_logging);
	nmc->print_fields.indices = parse_output_fields (fields_str, tmpl, &error);

	if (error) {
		if (error->code == 0)
			g_string_printf (nmc->return_text, _("Error: 'general logging': %s"), error->message);
		else
			g_string_printf (nmc->return_text, _("Error: 'general logging': %s; allowed fields: %s"),
			                 error->message, NMC_FIELDS_NM_LOGGING_ALL);
		g_error_free (error);
		nmc->return_value = NMC_RESULT_ERROR_USER_INPUT;
		return FALSE;
	}

	nmc->get_client (nmc); /* create NMClient */
	nm_client_get_logging (nmc->client, &level, &domains, &error);
	if (error) {
		g_string_printf (nmc->return_text, _("Error: %s."), error->message);
		nmc->return_value = NMC_RESULT_ERROR_USER_INPUT;
		g_error_free (error);
		return FALSE;
	}

	nmc->print_fields.header_name = _("NetworkManager logging");
	arr = nmc_dup_fields_array (tmpl, tmpl_len, NMC_OF_FLAG_MAIN_HEADER_ADD | NMC_OF_FLAG_FIELD_NAMES);
	g_ptr_array_add (nmc->output_data, arr);

	arr = nmc_dup_fields_array (tmpl, tmpl_len, 0);
	set_val_str (arr, 0, level);
	set_val_str (arr, 1, domains);
	g_ptr_array_add (nmc->output_data, arr);

	print_data (nmc);  /* Print all data */

	return TRUE;
}

/*
 * Entry point function for general operations 'nmcli general'
 */
NMCResultCode
do_general (NmCli *nmc, int argc, char **argv)
{
	GError *error = NULL;

	if (argc == 0) {
		if (!nmc_terse_option_check (nmc->print_output, nmc->required_fields, &error)) {
			g_string_printf (nmc->return_text, _("Error: %s."), error->message);
			nmc->return_value = NMC_RESULT_ERROR_USER_INPUT;
			goto finish;
		}
		show_nm_status (nmc, NULL, NULL);
	}

	if (argc > 0) {
		if (matches (*argv, "status") == 0) {
			if (!nmc_terse_option_check (nmc->print_output, nmc->required_fields, &error)) {
				g_string_printf (nmc->return_text, _("Error: %s."), error->message);
				nmc->return_value = NMC_RESULT_ERROR_USER_INPUT;
				goto finish;
			}
			show_nm_status (nmc, NULL, NULL);
		}
		else if (matches (*argv, "permissions") == 0) {
			if (!nmc_terse_option_check (nmc->print_output, nmc->required_fields, &error)) {
				g_string_printf (nmc->return_text, _("Error: %s."), error->message);
				nmc->return_value = NMC_RESULT_ERROR_USER_INPUT;
				goto finish;
			}
			show_nm_permissions (nmc);
		}
		else if (matches (*argv, "logging") == 0) {
			if (next_arg (&argc, &argv) != 0) {
				/* no arguments -> get logging level and domains */
				if (!nmc_terse_option_check (nmc->print_output, nmc->required_fields, &error)) {
					g_string_printf (nmc->return_text, _("Error: %s."), error->message);
					nmc->return_value = NMC_RESULT_ERROR_USER_INPUT;
					goto finish;
				}
				show_general_logging (nmc);
			} else {
				/* arguments provided -> set logging level and domains */
				const char *level = NULL;
				const char *domains = NULL;
				nmc_arg_t exp_args[] = { {"level",   TRUE, &level,   TRUE},
				                         {"domains", TRUE, &domains, TRUE},
				                         {NULL} };

				if (!nmc_parse_args (exp_args, FALSE, &argc, &argv, &error)) {
					g_string_assign (nmc->return_text, error->message);
					nmc->return_value = error->code;
					goto finish;
				}

				nmc->get_client (nmc); /* create NMClient */
				nm_client_set_logging (nmc->client, level, domains, &error);
				if (error) {
					g_string_printf (nmc->return_text, _("Error: %s."), error->message);
					nmc->return_value = NMC_RESULT_ERROR_USER_INPUT;
					goto finish;
				}
			}
		}
		else if (nmc_arg_is_help (*argv)) {
			usage_general ();
		}
		else {
			usage_general ();
			g_string_printf (nmc->return_text, _("Error: 'general' command '%s' is not valid."), *argv);
			nmc->return_value = NMC_RESULT_ERROR_USER_INPUT;
		}
	}

finish:
	if (error)
		g_error_free (error);
	quit ();
	return nmc->return_value;
}

static gboolean
nmc_switch_show (NmCli *nmc, const char *switch_name, const char *header)
{
	g_return_val_if_fail (nmc != NULL, FALSE);
	g_return_val_if_fail (switch_name != NULL, FALSE);

	if (nmc->required_fields && strcasecmp (nmc->required_fields, switch_name) != 0) {
		g_string_printf (nmc->return_text, _("Error: '--fields' value '%s' is not valid here (allowed fields: %s)"),
		                 nmc->required_fields, switch_name);
		nmc->return_value = NMC_RESULT_ERROR_USER_INPUT;
		return FALSE;
	}
	if (nmc->print_output == NMC_PRINT_NORMAL)
		nmc->print_output = NMC_PRINT_TERSE;

	nmc->required_fields = g_strdup (switch_name);
	return show_nm_status (nmc, header, NMC_FIELDS_NM_STATUS_SWITCH);
}

static gboolean
nmc_switch_parse_on_off (NmCli *nmc, const char *arg1, const char *arg2, gboolean *res)
{
	g_return_val_if_fail (nmc != NULL, FALSE);
	g_return_val_if_fail (arg1 && arg2, FALSE);
	g_return_val_if_fail (res != NULL, FALSE);

	if (!strcmp (arg2, "on"))
		*res = TRUE;
	else if (!strcmp (arg2, "off"))
		*res = FALSE;
	else {
		g_string_printf (nmc->return_text, _("Error: invalid '%s' argument: '%s' (use on/off)."), arg1, arg2);
		nmc->return_value = NMC_RESULT_ERROR_USER_INPUT;
		return FALSE;
	}

	return TRUE;
}

/*
 * Entry point function for 'nmcli networking'
 */
NMCResultCode
do_networking (NmCli *nmc, int argc, char **argv)
{
	gboolean enable_flag;

	if (argc == 0)
		nmc_switch_show (nmc, NMC_FIELDS_NM_NETWORKING, _("Networking"));
	else if (argc > 0) {
		if (nmc_arg_is_help (*argv)) {
			usage_networking ();
		} else if (nmc_switch_parse_on_off (nmc, *(argv-1), *argv, &enable_flag)) {

			nmc->get_client (nmc); /* create NMClient */
			nm_client_networking_set_enabled (nmc->client, enable_flag);
		} else {
			usage_networking ();
			g_string_printf (nmc->return_text, _("Error: 'networking' command '%s' is not valid."), *argv);
			nmc->return_value = NMC_RESULT_ERROR_USER_INPUT;
		}
	}

	quit ();
	return nmc->return_value;
}

/*
 * Entry point function for radio switch commands 'nmcli radio'
 */
NMCResultCode
do_radio (NmCli *nmc, int argc, char **argv)
{
	GError *error = NULL;
	gboolean enable_flag;

	if (argc == 0) {
		if (!nmc_terse_option_check (nmc->print_output, nmc->required_fields, &error)) {
			g_string_printf (nmc->return_text, _("Error: %s."), error->message);
			nmc->return_value = NMC_RESULT_ERROR_USER_INPUT;
			g_error_free (error);
			goto finish;
		}
		show_nm_status (nmc, _("Radio switches"), NMC_FIELDS_NM_STATUS_RADIO);
	}

	if (argc > 0) {
		if (matches (*argv, "all") == 0) {
			if (next_arg (&argc, &argv) != 0) {
				/* no argument, show all radio switches */
				if (!nmc_terse_option_check (nmc->print_output, nmc->required_fields, &error)) {
					g_string_printf (nmc->return_text, _("Error: %s."), error->message);
					nmc->return_value = NMC_RESULT_ERROR_USER_INPUT;
					g_error_free (error);
					goto finish;
				}
				show_nm_status (nmc, _("Radio switches"), NMC_FIELDS_NM_STATUS_RADIO);
			} else {
				if (!nmc_switch_parse_on_off (nmc, *(argv-1), *argv, &enable_flag))
					goto finish;

				nmc->get_client (nmc); /* create NMClient */
				nm_client_wireless_set_enabled (nmc->client, enable_flag);
				nm_client_wimax_set_enabled (nmc->client, enable_flag);
				nm_client_wwan_set_enabled (nmc->client, enable_flag);
			}
		}
		else if (matches (*argv, "wifi") == 0) {
			if (next_arg (&argc, &argv) != 0) {
				/* no argument, show current WiFi state */
				nmc_switch_show (nmc, NMC_FIELDS_NM_WIFI, _("Wi-Fi radio switch"));
			} else {
				if (!nmc_switch_parse_on_off (nmc, *(argv-1), *argv, &enable_flag))
					goto finish;
				
				nmc->get_client (nmc); /* create NMClient */
				nm_client_wireless_set_enabled (nmc->client, enable_flag);
			}
		}
		else if (matches (*argv, "wwan") == 0) {
			if (next_arg (&argc, &argv) != 0) {
				/* no argument, show current WWAN (mobile broadband) state */
				nmc_switch_show (nmc, NMC_FIELDS_NM_WWAN, _("WWAN radio switch"));
			} else {
				if (!nmc_switch_parse_on_off (nmc, *(argv-1), *argv, &enable_flag))
					goto finish;

				nmc->get_client (nmc); /* create NMClient */
				nm_client_wwan_set_enabled (nmc->client, enable_flag);
			}
		}
#if WITH_WIMAX
		else if (matches (*argv, "wimax") == 0) {
			if (next_arg (&argc, &argv) != 0) {
				/* no argument, show current WiMAX state */
				nmc_switch_show (nmc, NMC_FIELDS_NM_WIMAX, _("WiMAX radio switch"));
			} else {
				if (!nmc_switch_parse_on_off (nmc, *(argv-1), *argv, &enable_flag))
					goto finish;

				nmc->get_client (nmc); /* create NMClient */
				nm_client_wimax_set_enabled (nmc->client, enable_flag);
			}
		}
#endif
		else if (nmc_arg_is_help (*argv)) {
			usage_radio ();
		}
		else {
			usage_radio ();
			g_string_printf (nmc->return_text, _("Error: 'radio' command '%s' is not valid."), *argv);
			nmc->return_value = NMC_RESULT_ERROR_USER_INPUT;
		}
	}

finish:
	quit ();
	return nmc->return_value;
}

