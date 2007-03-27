/* 
 * gconf-cleaner.c
 * Copyright (C) 2007 Akira TAGOH
 * 
 * Authors:
 *   Akira TAGOH  <at@gclab.org>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib/gi18n.h>
#include <gconf/gconf.h>
#include "gconf-cleaner.h"


struct _GConfCleaner {
	GConfEngine *gconf;
	GSList      *dirs;
	GSList      *current_dir;
	guint        n_dirs;
	guint        n_pairs;
	guint        n_unknown_pairs;
	gboolean     initialized;
};

/*
 * Private Functions
 */
static GSList *
_gconf_cleaner_all_dirs_recursively(GConfCleaner      *gcleaner,
				    const gchar       *path,
				    GError           **error)
{
	GSList *subdirs, *l, *retval = NULL;
	GError *err = NULL;

	subdirs = gconf_engine_all_dirs(gcleaner->gconf, path, &err);
	if (err != NULL) {
		if (error)
			g_set_error(error, 0, 0,
				    N_("Failed to get the directories in `%s': %s"),
				    path, err->message);
		g_error_free(err);
		return NULL;
	}
	for (l = subdirs; l != NULL; l = g_slist_next(l)) {
		gcleaner->n_dirs++;
		retval = g_slist_append(retval, l->data);
		retval = g_slist_concat(retval,
					_gconf_cleaner_all_dirs_recursively(gcleaner,
									    l->data,
									    error));
		if (*error) {
			g_slist_free(retval);
			return NULL;
		}
	}

	return retval;
}

/*
 * Public Functions
 */
GConfCleaner *
gconf_cleaner_new(void)
{
	GConfCleaner *retval = g_new0(GConfCleaner, 1);

	g_return_val_if_fail (retval != NULL, NULL);
	retval->gconf = gconf_engine_get_default();
	g_return_val_if_fail (retval->gconf != NULL, NULL);

	return retval;
}

void
gconf_cleaner_free(GConfCleaner *gcleaner)
{
	g_return_if_fail (gcleaner != NULL);

	gconf_engine_unref(gcleaner->gconf);
	if (gcleaner->dirs)
		g_slist_free(gcleaner->dirs);
	g_free(gcleaner);
}

gboolean
gconf_cleaner_is_initialized(GConfCleaner *gcleaner)
{
	return gcleaner->initialized;
}

gboolean
gconf_cleaner_update(GConfCleaner  *gcleaner,
		     GError       **error)
{
	g_return_val_if_fail (gcleaner != NULL, FALSE);
	g_return_val_if_fail (error != NULL, FALSE);

	if (*error != NULL) {
		g_error_free(*error);
		*error = NULL;
	}
	gcleaner->n_dirs = gcleaner->n_pairs = gcleaner->n_unknown_pairs = 0;
	gcleaner->dirs = _gconf_cleaner_all_dirs_recursively(gcleaner, "/", error);
	gcleaner->current_dir = gcleaner->dirs;
	gcleaner->initialized = TRUE;

	return *error == NULL;
}

guint
gconf_cleaner_n_dirs(GConfCleaner *gcleaner)
{
	g_return_val_if_fail (gcleaner != NULL, 0);

	return gcleaner->n_dirs;
}

guint
gconf_cleaner_n_pairs(GConfCleaner *gcleaner)
{
	g_return_val_if_fail (gcleaner != NULL, 0);

	return gcleaner->n_pairs;
}

guint
gconf_cleaner_n_unknown_pairs(GConfCleaner *gcleaner)
{
	g_return_val_if_fail (gcleaner != NULL, 0);

	return gcleaner->n_unknown_pairs;
}

GSList *
gconf_cleaner_get_unknown_pairs_at_current_dir(GConfCleaner  *gcleaner,
					       GError       **error)
{
	GSList *pairs, *l, *retval = NULL;
	const gchar *path;
	GError *err = NULL;

	g_return_val_if_fail (gcleaner != NULL, NULL);
	g_return_val_if_fail (error != NULL, NULL);

	if (*error != NULL) {
		g_error_free(*error);
		*error = NULL;
	}
	path = gcleaner->current_dir->data;
	gcleaner->current_dir = g_slist_next(gcleaner->current_dir);
	pairs = gconf_engine_all_entries(gcleaner->gconf, path, &err);
	if (err != NULL) {
		g_set_error(error, 0, 0,
			    N_("Failed to get the entries in `%s': %s"),
			    path, err->message);
		g_error_free(err);
		return NULL;
	}
	for (l = pairs; l != NULL; l = g_slist_next(l)) {
		GConfEntry *pair = l->data;
		const gchar *schema = gconf_entry_get_schema_name(pair);

		gcleaner->n_pairs++;
		if (!schema) {
			gcleaner->n_unknown_pairs++;
			retval = g_slist_append(retval, g_strdup(gconf_entry_get_key(pair)));
			retval = g_slist_append(retval, gconf_value_copy(gconf_entry_get_value(pair)));
		}
		gconf_entry_free(pair);
	}
	if (pairs)
		g_slist_free(pairs);

	return retval;
}

const gchar *
gconf_cleaner_get_current_dir(GConfCleaner *gcleaner)
{
	g_return_val_if_fail (gcleaner != NULL, NULL);
	g_return_val_if_fail (gcleaner->current_dir != NULL, NULL);

	return gcleaner->current_dir->data;
}

void
gconf_cleaner_pairs_free(GSList *list)
{
	GSList *l;

	g_return_if_fail (list != NULL);

	for (l = list; l != NULL; l = g_slist_next(l)) {
		g_free(l->data);
		l = g_slist_next(l);
		gconf_value_free(l->data);
	}
	g_slist_free(list);
}
