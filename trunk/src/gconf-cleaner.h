/* 
 * gconf-cleaner.h
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
#ifndef __GCONF_CLEANER_H__
#define __GCONF_CLEANER_H__

#include <glib.h>

G_BEGIN_DECLS

typedef struct _GConfCleaner GConfCleaner;

GConfCleaner *gconf_cleaner_new                             (void);
void          gconf_cleaner_free                            (GConfCleaner  *gcleaner);
gboolean      gconf_cleaner_is_initialized                  (GConfCleaner  *gcleaner);
gboolean      gconf_cleaner_update                          (GConfCleaner  *gcleaner,
							     GError       **error);
const gchar  *gconf_cleaner_get_current_dir                 (GConfCleaner  *gcleaner);
guint         gconf_cleaner_n_dirs                          (GConfCleaner  *gcleaner);
guint         gconf_cleaner_n_pairs                         (GConfCleaner  *gcleaner);
guint         gconf_cleaner_n_unknown_pairs                 (GConfCleaner  *gcleaner);
GSList       *gconf_cleaner_get_unknown_pairs_at_current_dir(GConfCleaner  *gcleaner,
							     GError       **error);
void          gconf_cleaner_pairs_free                      (GSList        *list);

G_END_DECLS

#endif /* __GCONF_CLEANER_H__ */
