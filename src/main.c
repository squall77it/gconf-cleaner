/* 
 * main.c
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

#include <string.h>
#include <glib/gi18n.h>
#include <glib/gmain.h>
#include <gtk/gtk.h>
#include <gconf/gconf.h>
#include "gconf-cleaner.h"


typedef struct _GConfCleanerInstance {
	GConfCleaner *cleaner;
	GtkWidget    *window;
	GPtrArray    *pages;
	GSList       *pairs;
	guint         n_unknown_pairs;
	/* page 2 */
	GtkWidget    *label_progress;
	GtkWidget    *progressbar;
	/* page 3 */
	GtkWidget    *label_n_dirs;
	GtkWidget    *label_n_pairs;
	GtkWidget    *label_n_unknown_pairs;
	GtkWidget    *treeview;
	/* page 4 */
	GtkWidget    *progressbar2;
	/* page 5 */
	GtkWidget    *label_cleaned_pairs;
} GConfCleanerInstance;
typedef struct _GConfCleanerPageCallback {
	GtkWidget *widget;
	GSourceFunc func;
} GConfCleanerPageCallback;

/*
 * Private Functions
 */
static void
_gconf_cleaner_error_dialog(GConfCleanerInstance *inst,
			    const gchar          *primary_text,
			    const gchar          *secondary_text)
{
	GtkWidget *dialog = gtk_message_dialog_new_with_markup(GTK_WINDOW (inst->window),
							       GTK_DIALOG_MODAL,
							       GTK_MESSAGE_ERROR,
							       GTK_BUTTONS_OK,
							       primary_text);

	gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG (dialog),
						 secondary_text);
	g_signal_connect(dialog, "response",
			 G_CALLBACK (gtk_widget_destroy), NULL);

	while (g_main_context_pending(NULL))
		g_main_context_iteration(NULL, TRUE);

	gtk_dialog_run(GTK_DIALOG (dialog));
	gtk_main_quit();
}

static void
_gconf_cleaner_cancel_confirm_on_response(GtkDialog *dialog,
					  gint       response_id,
					  gpointer   data)
{
	if (response_id == GTK_RESPONSE_YES)
		gtk_main_quit();
	gtk_widget_destroy(GTK_WIDGET (dialog));
}

static void
_gconf_cleaner_on_assistant_cancel(GtkWidget *widget,
				   gpointer   data)
{
	GtkWidget *dialog;
	GConfCleanerInstance *inst = data;

	dialog = gtk_message_dialog_new_with_markup(GTK_WINDOW (inst->window),
						    GTK_DIALOG_MODAL,
						    GTK_MESSAGE_WARNING,
						    GTK_BUTTONS_YES_NO,
						    _("<span weight=\"bold\" size=\"larger\">Do you want to leave GConf Cleaner?</span>"));
	gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG (dialog),
						 _("If you leave here, any GConf keys will not be cleaned up."));
	g_signal_connect(dialog, "response",
			 G_CALLBACK (_gconf_cleaner_cancel_confirm_on_response),
			 NULL);

	while (g_main_context_pending(NULL))
		g_main_context_iteration(NULL, TRUE);

	gtk_dialog_run(GTK_DIALOG (dialog));
}

static void
_gconf_cleaner_on_assistant_prepare(GtkWidget *widget,
				    GtkWidget *page,
				    gpointer   data)
{
	gint current_page, n_pages;
	gchar *title;
	GConfCleanerPageCallback *cb;
	GConfCleanerInstance *inst = data;

	current_page = gtk_assistant_get_current_page(GTK_ASSISTANT (widget));
	n_pages = gtk_assistant_get_n_pages(GTK_ASSISTANT (widget));

	title = g_strdup_printf(_("GConf Cleaner - (%d of %d)"), current_page + 1, n_pages);
	gtk_window_set_title(GTK_WINDOW (widget), title);
	g_free(title);

	cb = g_ptr_array_index(inst->pages, current_page);
	g_return_if_fail (cb != NULL);
	if (cb->func)
		g_timeout_add(100, cb->func, inst);
}

static void
_gconf_cleaner_cell_renderer_on_toggled(GtkCellRendererToggle *cell,
					gchar                 *path_str,
					gpointer               data)
{
	GConfCleanerInstance *inst = data;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreePath *path = gtk_tree_path_new_from_string(path_str);
	gboolean flag;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW (inst->treeview));
	gtk_tree_model_get_iter(model, &iter, path);
	gtk_tree_model_get(model, &iter, 0, &flag, -1);

	flag ^= 1;
	if (flag)
		inst->n_unknown_pairs++;
	else
		inst->n_unknown_pairs--;

	gtk_list_store_set(GTK_LIST_STORE (model), &iter, 0, flag, -1);

	gtk_tree_path_free(path);
}

static gboolean
_gconf_cleaner_run_analysis_cb(gpointer data)
{
	GConfCleanerInstance *inst = data;
	GError *error = NULL;
	guint n_dirs, i;
	const gchar *text;
	GSList *l;
	GtkWidget *page;

	if (inst->pairs) {
		gconf_cleaner_pairs_free(inst->pairs);
		inst->pairs = NULL;
	}
	gtk_label_set_text(GTK_LABEL (inst->label_progress),
			   _("Retrieving the GConf directories..."));
	while (g_main_context_pending(NULL))
		g_main_context_iteration(NULL, TRUE);

	gconf_cleaner_update(inst->cleaner, &error);
	if (error != NULL) {
		_gconf_cleaner_error_dialog(inst,
					    _("<span weight=\"bold\" size=\"larger\">Failed during the initialization</span>"),
					    error->message);
		return FALSE;
	}

	gtk_label_set_text(GTK_LABEL (inst->label_progress),
			   _("Analyzing the GConf directories..."));

	while (g_main_context_pending(NULL))
		g_main_context_iteration(NULL, TRUE);

	n_dirs = gconf_cleaner_n_dirs(inst->cleaner);

	gtk_progress_bar_set_orientation(GTK_PROGRESS_BAR (inst->progressbar),
					 GTK_PROGRESS_LEFT_TO_RIGHT);
	gtk_progress_bar_set_ellipsize(GTK_PROGRESS_BAR (inst->progressbar),
				       PANGO_ELLIPSIZE_MIDDLE);
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR (inst->progressbar), 0.0);

	while (g_main_context_pending(NULL))
		g_main_context_iteration(NULL, TRUE);

	for (i = 0; i < n_dirs; i++) {
		text = gconf_cleaner_get_current_dir(inst->cleaner);
		gtk_progress_bar_set_text(GTK_PROGRESS_BAR (inst->progressbar), text);
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR (inst->progressbar), (gdouble)i / (gdouble)n_dirs);
		while (g_main_context_pending(NULL))
			g_main_context_iteration(NULL, TRUE);

		l = gconf_cleaner_get_unknown_pairs_at_current_dir(inst->cleaner, &error);
		if (error != NULL) {
			_gconf_cleaner_error_dialog(inst,
						    _("<span weight=\"bold\" size=\"larger\">Failed during analyzing the GConf key</span>"),
						    error->message);
			return FALSE;
		} else {
			inst->pairs = g_slist_concat(inst->pairs, l);
		}
	}
	page = gtk_assistant_get_nth_page(GTK_ASSISTANT (inst->window),
					  gtk_assistant_get_current_page(GTK_ASSISTANT (inst->window)));
	gtk_assistant_set_page_complete(GTK_ASSISTANT (inst->window),
					page, TRUE);
	gtk_widget_set_sensitive(GTK_ASSISTANT (inst->window)->back, FALSE);
	g_signal_emit_by_name(GTK_ASSISTANT (inst->window)->forward, "clicked");

	return FALSE;
}

static gboolean
_gconf_cleaner_run_analyzing_result_cb(gpointer data)
{
	GConfCleanerInstance *inst = data;
	gchar *text;
	GtkListStore *list;
	GtkTreeIter iter;
	GSList *l;

	text = g_strdup_printf("%d", gconf_cleaner_n_dirs(inst->cleaner));
	gtk_label_set_text(GTK_LABEL (inst->label_n_dirs), text);
	g_free(text);
	text = g_strdup_printf("%d", gconf_cleaner_n_pairs(inst->cleaner));
	gtk_label_set_text(GTK_LABEL (inst->label_n_pairs), text);
	g_free(text);
	inst->n_unknown_pairs = gconf_cleaner_n_unknown_pairs(inst->cleaner);
	text = g_strdup_printf("%d", inst->n_unknown_pairs);
	gtk_label_set_text(GTK_LABEL (inst->label_n_unknown_pairs), text);
	g_free(text);

	list = gtk_list_store_new(3, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING);
	for (l = inst->pairs; l != NULL; l = g_slist_next(l)) {
		gchar *str;

		gtk_list_store_append(list, &iter);
		gtk_list_store_set(list, &iter, 0, TRUE, -1);
		gtk_list_store_set(list, &iter, 1, l->data, -1);
		l = g_slist_next(l);
		str = gconf_value_to_string(l->data);
		gtk_list_store_set(list, &iter, 2, str, -1);
		g_free(str);
	}
	gtk_tree_view_set_model(GTK_TREE_VIEW (inst->treeview), GTK_TREE_MODEL (list));
	g_object_unref(list);

	return FALSE;
}

static gboolean
_gconf_cleaner_run_cleaning_cb(gpointer data)
{
	GConfCleanerInstance *inst = data;
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean flag;
	gchar *key;
	guint i = 0;
	GtkWidget *page;

	gtk_progress_bar_set_orientation(GTK_PROGRESS_BAR (inst->progressbar2),
					 GTK_PROGRESS_LEFT_TO_RIGHT);
	gtk_progress_bar_set_ellipsize(GTK_PROGRESS_BAR (inst->progressbar2),
				       PANGO_ELLIPSIZE_MIDDLE);
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR (inst->progressbar2), 0.0);

	while (g_main_context_pending(NULL))
		g_main_context_iteration(NULL, TRUE);

	model = gtk_tree_view_get_model(GTK_TREE_VIEW (inst->treeview));
	if (gtk_tree_model_get_iter_first(model, &iter)) {
		do {
			gtk_tree_model_get(model, &iter, 0, &flag, 1, &key, -1);
			if (flag) {
				GError *error = NULL;

				i++;
				gtk_progress_bar_set_text(GTK_PROGRESS_BAR (inst->progressbar2), key);
				gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR (inst->progressbar2),
							      (gdouble)i / (gdouble)inst->n_unknown_pairs);

				while (g_main_context_pending(NULL))
					g_main_context_iteration(NULL, TRUE);

				gconf_cleaner_unset_key(inst->cleaner, key, &error);
				if (error) {
					_gconf_cleaner_error_dialog(inst,
								    _("<span weight=\"bold\" size=\"larger\">Failed during cleaning GConf key up."),
								    error->message);
					return FALSE;
				}
			}
		} while (gtk_tree_model_iter_next(model, &iter));
	}
	page = gtk_assistant_get_nth_page(GTK_ASSISTANT (inst->window),
					  gtk_assistant_get_current_page(GTK_ASSISTANT (inst->window)));
	gtk_assistant_set_page_complete(GTK_ASSISTANT (inst->window),
					page, TRUE);
	gtk_widget_set_sensitive(GTK_ASSISTANT (inst->window)->back, FALSE);
	g_signal_emit_by_name(GTK_ASSISTANT (inst->window)->forward, "clicked");

	return FALSE;
}

static gboolean
_gconf_cleaner_run_finish_cb(gpointer data)
{
	GConfCleanerInstance *inst = data;
	gchar *text;

	text = g_strdup_printf(_("%d of %d GConf keys has been cleaned up successfully."),
			       inst->n_unknown_pairs,
			       gconf_cleaner_n_unknown_pairs(inst->cleaner));
	gtk_label_set_text(GTK_LABEL (inst->label_cleaned_pairs), text);
	g_free(text);

	return FALSE;
}

static void
_gconf_cleaner_create_page(GConfCleanerInstance *inst)
{
	GConfCleanerPageCallback *cb;

	/* page 1 */
	G_STMT_START {
		GtkWidget *label = gtk_label_new(_("GConf Cleaner is to clean up the invalid keys or the unnecessary keys anymore in your GConf database.\n"
						   "Please proceed next step to analyze how much such keys are stored.\n"));

		gtk_label_set_line_wrap(GTK_LABEL (label), TRUE);

		gtk_assistant_append_page(GTK_ASSISTANT (inst->window),
					  label);
		gtk_assistant_set_page_title(GTK_ASSISTANT (inst->window),
					     label, _("Welcome to GConf Cleaner"));
		gtk_assistant_set_page_type(GTK_ASSISTANT (inst->window),
					    label, GTK_ASSISTANT_PAGE_INTRO);
		gtk_assistant_set_page_complete(GTK_ASSISTANT (inst->window),
						label, TRUE);

		cb = g_new0(GConfCleanerPageCallback, 1);
		cb->widget = label;
		g_ptr_array_add(inst->pages, cb);
	} G_STMT_END;
	/* page 2 */
	G_STMT_START {
		GtkWidget *vbox;

		vbox = gtk_vbox_new(FALSE, 5);
		inst->label_progress = gtk_label_new("");
		inst->progressbar = gtk_progress_bar_new();

		gtk_box_pack_start(GTK_BOX (vbox), inst->label_progress, FALSE, TRUE, 0);
		gtk_box_pack_start(GTK_BOX (vbox), inst->progressbar, FALSE, TRUE, 0);

		gtk_assistant_append_page(GTK_ASSISTANT (inst->window),
					  vbox);
		gtk_assistant_set_page_title(GTK_ASSISTANT (inst->window),
					     vbox, _("Analyzing..."));
		gtk_assistant_set_page_type(GTK_ASSISTANT (inst->window),
					    vbox, GTK_ASSISTANT_PAGE_PROGRESS);

		cb = g_new0(GConfCleanerPageCallback, 1);
		cb->widget = vbox;
		cb->func = _gconf_cleaner_run_analysis_cb;
		g_ptr_array_add(inst->pages, cb);
	} G_STMT_END;
	/* page 3 */
	G_STMT_START {
		GtkWidget *table, *vbox, *label_dirs, *label_keys, *label_pairs, *label;
		GtkWidget *expander, *scrolled;
		GtkCellRenderer *renderer;
		GtkTreeViewColumn *column;
		gint i, rows = 3;

		scrolled = gtk_scrolled_window_new(NULL, NULL);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW (scrolled),
					       GTK_POLICY_AUTOMATIC,
					       GTK_POLICY_AUTOMATIC);
		vbox = gtk_vbox_new(FALSE, 0);
		table = gtk_table_new(rows, 2, FALSE);
		label_dirs = gtk_label_new(_("<b>GConf directories:</b>"));
		label_keys = gtk_label_new(_("<b>Total GConf keys:</b>"));
		label_pairs = gtk_label_new(_("<b>Cleanable GConf keys:</b>"));
		inst->label_n_dirs = gtk_label_new("?");
		inst->label_n_pairs = gtk_label_new("?");
		inst->label_n_unknown_pairs = gtk_label_new("?");
		gtk_label_set_use_markup(GTK_LABEL (label_dirs), TRUE);
		gtk_label_set_use_markup(GTK_LABEL (label_keys), TRUE);
		gtk_label_set_use_markup(GTK_LABEL (label_pairs), TRUE);
		gtk_misc_set_alignment(GTK_MISC (label_dirs), 0, 0);
		gtk_misc_set_alignment(GTK_MISC (label_keys), 0, 0);
		gtk_misc_set_alignment(GTK_MISC (label_pairs), 0, 0);
		expander = gtk_expander_new_with_mnemonic(_("_Details"));
		gtk_expander_set_expanded(GTK_EXPANDER (expander), FALSE);
		label = gtk_label_new(_("Please press Forward button to clean up the GConf keys."));
		inst->treeview = gtk_tree_view_new();
		gtk_tree_view_set_rules_hint(GTK_TREE_VIEW (inst->treeview), TRUE);

#define TABLE_X_PADDING 10
#define TABLE_Y_PADDING 0

		i = rows;
		gtk_table_attach(GTK_TABLE (table), label_dirs,
				 0, 1, i - rows, i - rows + 1,
				 GTK_FILL, GTK_FILL, TABLE_X_PADDING, TABLE_Y_PADDING);
		gtk_table_attach(GTK_TABLE (table), inst->label_n_dirs,
				 1, 2, i - rows, i - rows + 1,
				 GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, TABLE_X_PADDING, TABLE_Y_PADDING);
		i++;
		gtk_table_attach(GTK_TABLE (table), label_keys,
				 0, 1, i - rows, i - rows + 1,
				 GTK_FILL, GTK_FILL, TABLE_X_PADDING, TABLE_Y_PADDING);
		gtk_table_attach(GTK_TABLE (table), inst->label_n_pairs,
				 1, 2, i - rows, i - rows + 1,
				 GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, TABLE_X_PADDING, TABLE_Y_PADDING);
		i++;
		gtk_table_attach(GTK_TABLE (table), label_pairs,
				 0, 1, i - rows, i - rows + 1,
				 GTK_FILL, GTK_FILL, TABLE_X_PADDING, TABLE_Y_PADDING);
		gtk_table_attach(GTK_TABLE (table), inst->label_n_unknown_pairs,
				 1, 2, i - rows, i - rows + 1,
				 GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, TABLE_X_PADDING, TABLE_Y_PADDING);
		i++;
		gtk_box_pack_start(GTK_BOX (vbox), table, FALSE, TRUE, 10);
		gtk_box_pack_start(GTK_BOX (vbox), expander, TRUE, TRUE, 0);
		gtk_box_pack_start(GTK_BOX (vbox), label, FALSE, TRUE, 0);
		gtk_container_add(GTK_CONTAINER (expander), scrolled);
		gtk_container_add(GTK_CONTAINER (scrolled), inst->treeview);

		renderer = gtk_cell_renderer_toggle_new();
		g_signal_connect(renderer, "toggled",
				 G_CALLBACK (_gconf_cleaner_cell_renderer_on_toggled), inst);
		column = gtk_tree_view_column_new_with_attributes("",
								  renderer,
								  "active", 0,
								  NULL);
		gtk_tree_view_append_column(GTK_TREE_VIEW (inst->treeview), column);
		renderer = gtk_cell_renderer_text_new();
		column = gtk_tree_view_column_new_with_attributes("Key",
								  renderer,
								  "text", 1,
								  NULL);
		gtk_tree_view_column_set_resizable(column, TRUE);
		gtk_tree_view_append_column(GTK_TREE_VIEW (inst->treeview), column);
		renderer = gtk_cell_renderer_text_new();
		column = gtk_tree_view_column_new_with_attributes("Value",
								  renderer,
								  "text", 2,
								  NULL);
		gtk_tree_view_column_set_resizable(column, TRUE);
		gtk_tree_view_append_column(GTK_TREE_VIEW (inst->treeview), column);

		gtk_assistant_append_page(GTK_ASSISTANT (inst->window),
					  vbox);
		gtk_assistant_set_page_title(GTK_ASSISTANT (inst->window),
					     vbox, _("Analyzing Result"));
		gtk_assistant_set_page_type(GTK_ASSISTANT (inst->window),
					    vbox, GTK_ASSISTANT_PAGE_CONTENT);
		gtk_assistant_set_page_complete(GTK_ASSISTANT (inst->window),
						vbox, TRUE);

		cb = g_new0(GConfCleanerPageCallback, 1);
		cb->widget = vbox;
		cb->func = _gconf_cleaner_run_analyzing_result_cb;
		g_ptr_array_add(inst->pages, cb);
	} G_STMT_END;
	/* page 4 */
	G_STMT_START {
		GtkWidget *vbox;

		vbox = gtk_vbox_new(FALSE, 5);
		inst->progressbar2 = gtk_progress_bar_new();

		gtk_box_pack_start(GTK_BOX (vbox), inst->progressbar2, TRUE, FALSE, 0);

		gtk_assistant_append_page(GTK_ASSISTANT (inst->window),
					  vbox);
		gtk_assistant_set_page_title(GTK_ASSISTANT (inst->window),
					     vbox, _("Cleaning up..."));
		gtk_assistant_set_page_type(GTK_ASSISTANT (inst->window),
					    vbox, GTK_ASSISTANT_PAGE_PROGRESS);

		cb = g_new0(GConfCleanerPageCallback, 1);
		cb->widget = vbox;
		cb->func = _gconf_cleaner_run_cleaning_cb;
		g_ptr_array_add(inst->pages, cb);
	} G_STMT_END;
	/* page 5 */
	G_STMT_START {
		inst->label_cleaned_pairs = gtk_label_new("?");
		gtk_label_set_line_wrap(GTK_LABEL (inst->label_cleaned_pairs), TRUE);

		gtk_assistant_append_page(GTK_ASSISTANT (inst->window),
					  inst->label_cleaned_pairs);
		gtk_assistant_set_page_title(GTK_ASSISTANT (inst->window),
					     inst->label_cleaned_pairs, _("Congratulation"));
		gtk_assistant_set_page_type(GTK_ASSISTANT (inst->window),
					    inst->label_cleaned_pairs, GTK_ASSISTANT_PAGE_SUMMARY);

		cb = g_new0(GConfCleanerPageCallback, 1);
		cb->widget = inst->label_cleaned_pairs;
		cb->func = _gconf_cleaner_run_finish_cb;
		g_ptr_array_add(inst->pages, cb);
	} G_STMT_END;
}

/*
 * Public Functions
 */
int
main(int    argc,
     char **argv)
{
	GConfCleanerInstance *inst;

#ifdef ENABLE_NLS
	bindtextdomain (GETTEXT_PACKAGE, GCLEANER_LOCALEDIR);
#ifdef HAVE_BIND_TEXTDOMAIN_CODESET
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif /* HAVE_BIND_TEXTDOMAIN_CODESET */
	textdomain (GETTEXT_PACKAGE);
#endif /* ENABLE_NLS */

	gtk_init(&argc, &argv);

	inst = g_new0(GConfCleanerInstance, 1);
	inst->cleaner = gconf_cleaner_new();
	inst->window = gtk_assistant_new();
	inst->pages = g_ptr_array_new();

	gtk_window_set_title(GTK_WINDOW (inst->window), _("GConf Cleaner"));

	_gconf_cleaner_create_page(inst);

	g_signal_connect(inst->window, "cancel",
			 G_CALLBACK (_gconf_cleaner_on_assistant_cancel),
			 inst);
	g_signal_connect(inst->window, "close",
			 G_CALLBACK (gtk_main_quit),
			 inst);
	g_signal_connect(inst->window, "prepare",
			 G_CALLBACK (_gconf_cleaner_on_assistant_prepare),
			 inst);
	g_signal_connect(inst->window, "delete-event",
			 G_CALLBACK (gtk_main_quit),
			 inst);

	gtk_widget_show_all(inst->window);

	gtk_main();

	if (inst->cleaner)
		gconf_cleaner_free(inst->cleaner);
	if (inst->pairs)
		gconf_cleaner_pairs_free(inst->pairs);
	if (inst->pages) {
		gint i;

		for (i = 0; i < inst->pages->len; i++)
			g_free(g_ptr_array_index(inst->pages, i));
		g_ptr_array_free(inst->pages, TRUE);
	}
	g_free(inst);

	return 0;
}
