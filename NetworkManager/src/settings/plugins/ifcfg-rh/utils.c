/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager system settings service
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
 * (C) Copyright 2008 - 2012 Red Hat, Inc.
 */

#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"
#include "shvar.h"

/*
 * utils_single_quote_string
 *
 * Put string inside single quotes and remove CR, LF characters. If single quote
 * is present, escape it with a backslash and prepend the whole string with $
 * in order to have $'string'. That allows us to use single quote inside
 * single quotes without breaking bash syntax. (man bash, section QUOTING).
 *
 * Caller is responsible for freeing the returned string.
 */
char *
utils_single_quote_string (const char *str)
{
	static const char const drop_chars[] = "\r\n"; /* drop CR and LF */
	static const char escape_char = '\\'; /* escape char is backslash */
	static const char quote_char = '\'';  /* quote char is single quote */
	size_t i, slen, j = 0;
	size_t drop = 0, extra = 0;
	char *new_str;

	slen = strlen (str);
	for (i = 0; i < slen; i++) {
		if (str[i] == quote_char)
			extra++;
		if (strchr (drop_chars, str[i]))
			drop++;
	}
	new_str = g_malloc0 (slen + extra - drop + 4); /* 4 is for $''\0*/

	if (extra > 0)
		new_str[j++] = '$';
	new_str[j++] = quote_char;
	for (i = 0; i < slen; i++) {
		if (strchr (drop_chars, str[i]))
			continue;
		if (str[i] == quote_char)
			new_str[j++] = escape_char;
		new_str[j++] = str[i];
	}
	new_str[j] = quote_char;

	return new_str;
}

/*
 * utils_single_unquote_string
 *
 * Remove string from single (or double) quotes, and remove escaping of '.
 * Also remove first $ if the string is in the form of $'string'.
 *
 * Caller is responsible for freeing the returned string.
 */
char *
utils_single_unquote_string (const char *str)
{
	static const char escape_char = '\\'; /* escape char is backslash */
	static const char q_char = '\''; /* quote char is single quote */
	static const char dq_char = '"'; /* double quote char */
	size_t i, slen, j = 0, quote = 0, dollar = 0;
	char *new_str;

	slen = strlen (str);
	new_str = g_malloc0 (slen + 1);

	if (   (slen >= 2 && (str[0] == dq_char || str[0] == q_char) && str[0] == str[slen-1])
	    || (slen >= 3 && str[0] == '$' && str[1] == q_char && str[1] == str[slen-1])) {
		quote = 1;
		if (str[0] == '$') dollar = 1;
	}

	i = quote + dollar;
	while (i < slen - quote) {
		if (str[i] == escape_char && str[i+1] == q_char && i+1 < slen-quote)
			i++;
		new_str[j++] = str[i++];
	}
	new_str[j] = '\0';

	return new_str;
}

/*
 * Check ';[a-fA-F0-9]{8}' file suffix used for temporary files by rpm when
 * installing packages.
 *
 * Implementation taken from upstart.
 */
static gboolean
check_rpm_temp_suffix (const char *path)
{
	const char *ptr;

	g_return_val_if_fail (path != NULL, FALSE);

	/* Matches *;[a-fA-F0-9]{8}; used by rpm */
	ptr = strrchr (path, ';');
	if (ptr && (strspn (ptr + 1, "abcdefABCDEF0123456789") == 8)
	    && (! ptr[9]))
		return TRUE;
	return FALSE;
}

static gboolean
check_suffix (const char *base, const char *tag)
{
	int len, tag_len;

	g_return_val_if_fail (base != NULL, TRUE);
	g_return_val_if_fail (tag != NULL, TRUE);

	len = strlen (base);
	tag_len = strlen (tag);
	if ((len > tag_len) && !strcasecmp (base + len - tag_len, tag))
		return TRUE;
	return FALSE;
}

gboolean
utils_should_ignore_file (const char *filename, gboolean only_ifcfg)
{
	char *base;
	gboolean ignore = TRUE;
	gboolean is_ifcfg = FALSE;
	gboolean is_other = FALSE;

	g_return_val_if_fail (filename != NULL, TRUE);

	base = g_path_get_basename (filename);
	g_return_val_if_fail (base != NULL, TRUE);

	/* Only handle ifcfg, keys, and routes files */
	if (!strncmp (base, IFCFG_TAG, strlen (IFCFG_TAG)))
		is_ifcfg = TRUE;

	if (only_ifcfg == FALSE) {
		if (   !strncmp (base, KEYS_TAG, strlen (KEYS_TAG))
		    || !strncmp (base, ROUTE_TAG, strlen (ROUTE_TAG))
		    || !strncmp (base, ROUTE6_TAG, strlen (ROUTE6_TAG)))
				is_other = TRUE;
	}

	/* But not those that have certain suffixes */
	if (   (is_ifcfg || is_other)
	    && !check_suffix (base, BAK_TAG)
	    && !check_suffix (base, TILDE_TAG)
	    && !check_suffix (base, ORIG_TAG)
	    && !check_suffix (base, REJ_TAG)
	    && !check_suffix (base, RPMNEW_TAG)
	    && !check_suffix (base, AUGNEW_TAG)
	    && !check_suffix (base, AUGTMP_TAG)
	    && !check_rpm_temp_suffix (base))
		ignore = FALSE;

	g_free (base);
	return ignore;
}

char *
utils_cert_path (const char *parent, const char *suffix)
{
	const char *name;
	char *dir, *path;

	g_return_val_if_fail (parent != NULL, NULL);
	g_return_val_if_fail (suffix != NULL, NULL);

	name = utils_get_ifcfg_name (parent, FALSE);
	dir = g_path_get_dirname (parent);
	path = g_strdup_printf ("%s/%s-%s", dir, name, suffix);
	g_free (dir);
	return path;
}

const char *
utils_get_ifcfg_name (const char *file, gboolean only_ifcfg)
{
	const char *name = NULL, *start = NULL;
	char *base;

	g_return_val_if_fail (file != NULL, NULL);

	base = g_path_get_basename (file);
	if (!base)
		return NULL;

	/* Find the point in 'file' where 'base' starts.  We use 'file' since it's
	 * const and thus will survive after we free 'base'.
	 */
	start = file + strlen (file) - strlen (base);
	g_assert (strcmp (start, base) == 0);
	g_free (base);

	if (!strncmp (start, IFCFG_TAG, strlen (IFCFG_TAG)))
		name = start + strlen (IFCFG_TAG);
	else if (only_ifcfg == FALSE)  {
		if (!strncmp (start, KEYS_TAG, strlen (KEYS_TAG)))
			name = start + strlen (KEYS_TAG);
		else if (!strncmp (start, ROUTE_TAG, strlen (ROUTE_TAG)))
			name = start + strlen (ROUTE_TAG);
		else if (!strncmp (start, ROUTE6_TAG, strlen (ROUTE6_TAG)))
			name = start + strlen (ROUTE6_TAG);
	}

	return name;
}

/* Used to get any ifcfg/extra file path from any other ifcfg/extra path
 * in the form <tag><name>.
 */
static char *
utils_get_extra_path (const char *parent, const char *tag)
{
	char *item_path = NULL, *dirname;
	const char *name;

	g_return_val_if_fail (parent != NULL, NULL);
	g_return_val_if_fail (tag != NULL, NULL);

	dirname = g_path_get_dirname (parent);
	if (!dirname)
		return NULL;

	name = utils_get_ifcfg_name (parent, FALSE);
	if (name) {
		if (!strcmp (dirname, "."))
			item_path = g_strdup_printf ("%s%s", tag, name);
		else
			item_path = g_strdup_printf ("%s/%s%s", dirname, tag, name);
	}
	g_free (dirname);

	return item_path;
}

char *
utils_get_ifcfg_path (const char *parent)
{
	return utils_get_extra_path (parent, IFCFG_TAG);
}

char *
utils_get_keys_path (const char *parent)
{
	return utils_get_extra_path (parent, KEYS_TAG);
}

char *
utils_get_route_path (const char *parent)
{
	return utils_get_extra_path (parent, ROUTE_TAG);
}

char *
utils_get_route6_path (const char *parent)
{
	return utils_get_extra_path (parent, ROUTE6_TAG);
}

shvarFile *
utils_get_extra_ifcfg (const char *parent, const char *tag, gboolean should_create)
{
	shvarFile *ifcfg = NULL;
	char *path;

	path = utils_get_extra_path (parent, tag);
	if (!path)
		return NULL;

	if (should_create && !g_file_test (path, G_FILE_TEST_EXISTS))
		ifcfg = svCreateFile (path);

	if (!ifcfg)
		ifcfg = svNewFile (path);

	g_free (path);
	return ifcfg;
}

shvarFile *
utils_get_keys_ifcfg (const char *parent, gboolean should_create)
{
	return utils_get_extra_ifcfg (parent, KEYS_TAG, should_create);
}

shvarFile *
utils_get_route_ifcfg (const char *parent, gboolean should_create)
{
	return utils_get_extra_ifcfg (parent, ROUTE_TAG, should_create);
}

shvarFile *
utils_get_route6_ifcfg (const char *parent, gboolean should_create)
{
	return utils_get_extra_ifcfg (parent, ROUTE6_TAG, should_create);
}

/* Finds out if route file has new or older format
 * Returns TRUE  - new syntax (ADDRESS<n>=a.b.c.d ...), error opening file or empty
 *         FALSE - older syntax, i.e. argument to 'ip route add' (1.2.3.0/24 via 11.22.33.44)
 */
gboolean
utils_has_route_file_new_syntax (const char *filename)
{
	char *contents = NULL;
	gsize len = 0;
	gboolean ret = FALSE;
	const char *pattern = "^[[:space:]]*ADDRESS[0-9]+=";

	g_return_val_if_fail (filename != NULL, TRUE);

	if (!g_file_get_contents (filename, &contents, &len, NULL))
		return TRUE;

	if (len <= 0) {
		ret = TRUE;
		goto gone;
	}

	if (g_regex_match_simple (pattern, contents, G_REGEX_MULTILINE, 0))
		ret = TRUE;

gone:
	g_free (contents);
	return ret;
}

gboolean
utils_ignore_ip_config (NMConnection *connection)
{
	NMSettingConnection *s_con;

	s_con = nm_connection_get_setting_connection (connection);
	g_assert (s_con);

	/* bonding slaves have no IP configuration, and the system
	 * scripts just ignore it if it's there.
	 */
	if (   nm_setting_connection_is_slave_type (s_con, NM_SETTING_BOND_SETTING_NAME)
	    || nm_setting_connection_is_slave_type (s_con, NM_SETTING_BRIDGE_SETTING_NAME))
		return TRUE;

	return FALSE;
}