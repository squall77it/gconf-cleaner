/* 
 * main.c
 * Copyright (C) 2007 Akira TAGOH
 * 
 * Authors:
 *   Akira TAGOH  <akira@tagoh.org>
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

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
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
	gchar        *name;
	guint         n_unknown_pairs;
	/* page 2 */
	GtkWidget    *label_progress;
	GtkWidget    *progressbar;
	/* page 3 */
	GtkWidget    *label_n_dirs;
	GtkWidget    *label_n_pairs;
	GtkWidget    *label_n_unknown_pairs;
	GtkWidget    *expander;
	GtkWidget    *treeview;
	GtkWidget    *hbox;
	GtkWidget    *label_message;
	/* page 4 */
	GtkWidget    *progressbar2;
	/* page 5 */
	GtkWidget    *label_cleaned_pairs;
} GConfCleanerInstance;
typedef struct _GConfCleanerPageCallback {
	GtkWidget *widget;
	GSourceFunc func;
} GConfCleanerPageCallback;


static gchar *_gconf_cleaner_value_to_string(GConfValue *value,
					     gint        indent);

static GQuark quark_question_response = 0;

/*
 * Private Functions
 */
static void
_gconf_cleaner_error_dialog(GConfCleanerInstance *inst,
			    const gchar          *primary_text,
			    const gchar          *secondary_text,
			    gboolean              is_quit)
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

	if (is_quit)
		gtk_main_quit();
}

static void
_gconf_cleaner_question_dialog_on_response(GtkDialog *dialog,
					   gint       response_id,
					   gpointer   data)
{
	if (!quark_question_response)
		quark_question_response = g_quark_from_static_string("gconf-cleaner-question-response");

	if (response_id == GTK_RESPONSE_YES)
		g_object_set_qdata(G_OBJECT (dialog), quark_question_response, (gpointer)TRUE);
	else
		g_object_set_qdata(G_OBJECT (dialog), quark_question_response, (gpointer)FALSE);
}

static gboolean
_gconf_cleaner_question_dialog(GConfCleanerInstance *inst,
			       const gchar          *primary_text,
			       const gchar          *secondary_text)
{
	GtkWidget *dialog = gtk_message_dialog_new_with_markup(GTK_WINDOW (inst->window),
							       GTK_DIALOG_MODAL,
							       GTK_MESSAGE_WARNING,
							       GTK_BUTTONS_YES_NO,
							       primary_text);
	gboolean retval;

	gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG (dialog),
						 secondary_text);
	g_signal_connect(dialog, "response",
			 G_CALLBACK (_gconf_cleaner_question_dialog_on_response), NULL);

	while (g_main_context_pending(NULL))
		g_main_context_iteration(NULL, TRUE);

	gtk_dialog_run(GTK_DIALOG (dialog));

	retval = (gboolean)GPOINTER_TO_UINT (g_object_get_qdata(G_OBJECT (dialog), quark_question_response));
	gtk_widget_destroy(dialog);

	return retval;
}

static const gchar *
_gconf_cleaner_value_type_to_string(GConfValueType type)
{
	switch (type) {
	    case GCONF_VALUE_INT:
		    return "int";
	    case GCONF_VALUE_STRING:
		    return "string";
	    case GCONF_VALUE_FLOAT:
		    return "float";
	    case GCONF_VALUE_BOOL:
		    return "bool";
	    case GCONF_VALUE_LIST:
		    return "list";
	    case GCONF_VALUE_PAIR:
		    return "pair";
	    default:
		    g_assert_not_reached();
		    return NULL;
	}
}

static gchar *
_gconf_cleaner_value_list_to_string(GConfValue *value,
				    gint        indent)
{
	gchar *whitespace, *tmp;
	GConfValueType list_type;
	GString *retval;
	GSList *l;

	g_return_val_if_fail (value->type == GCONF_VALUE_LIST, NULL);

	retval = g_string_new(NULL);
	whitespace = g_strnfill(indent, ' ');
	list_type = gconf_value_get_list_type(value);
	g_string_append_printf(retval, "%s<list type=\"%s\">\n",
			       whitespace, _gconf_cleaner_value_type_to_string(list_type));
	l = gconf_value_get_list(value);
	while (l) {
		tmp = _gconf_cleaner_value_to_string(l->data, indent + 4);
		g_string_append(retval, tmp);
		g_free(tmp);
		l = g_slist_next(l);
	}
	g_string_append_printf(retval, "%s</list>\n", whitespace);

	g_free(whitespace);

	return g_string_free(retval, FALSE);
}

static gchar *
_gconf_cleaner_value_pair_to_string(GConfValue *value,
				    gint        indent)
{
	gchar *whitespace, *tmp;
	GString *retval;

	g_return_val_if_fail (value->type == GCONF_VALUE_PAIR, NULL);

	retval = g_string_new(NULL);
	whitespace = g_strnfill(indent, ' ');
	g_string_append_printf(retval, "%s<pair>\n", whitespace);

	g_string_append_printf(retval, "%s  <car>\n", whitespace);
	tmp = _gconf_cleaner_value_to_string(gconf_value_get_car(value), indent + 4);
	g_string_append(retval, tmp);
	g_free(tmp);
	g_string_append_printf(retval, "%s  </car>\n", whitespace);

	g_string_append_printf(retval, "%s  <cdr>\n", whitespace);
	tmp = _gconf_cleaner_value_to_string(gconf_value_get_cdr(value), indent + 4);
	g_string_append(retval, tmp);
	g_free(tmp);
	g_string_append_printf(retval, "%s  </cdr>\n", whitespace);

	g_string_append_printf(retval, "%s</pair>\n", whitespace);

	g_free(whitespace);

	return g_string_free(retval, FALSE);
}

static gchar *
_gconf_cleaner_value_to_string(GConfValue *value,
			       gint        indent)
{
	GString *retval = g_string_new(NULL);
	gchar *whitespace = g_strnfill(indent, ' '), *tmp;

	g_string_append_printf(retval, "%s<value>\n", whitespace);

	switch (value->type) {
	    case GCONF_VALUE_INT:
		    g_string_append_printf(retval, "%s  <int>%d</int>\n",
					   whitespace, gconf_value_get_int(value));
		    break;
	    case GCONF_VALUE_FLOAT:
		    g_string_append_printf(retval, "%s  <float>%.17g</float>\n",
					   whitespace, gconf_value_get_float(value));
		    break;
	    case GCONF_VALUE_STRING:
		    tmp = g_markup_escape_text(gconf_value_get_string(value), -1);
		    g_string_append_printf(retval, "%s  <string>%s</string>\n",
					   whitespace, (tmp[0] == ' ' && tmp[1] == '\0') ? "" : tmp);
		    g_free(tmp);
		    break;
	    case GCONF_VALUE_BOOL:
		    g_string_append_printf(retval, "%s  <bool>%s</bool>\n",
					   whitespace, gconf_value_get_bool(value) ? "true" : "false");
		    break;
	    case GCONF_VALUE_LIST:
		    tmp = _gconf_cleaner_value_list_to_string(value, indent + 2);
		    g_string_append(retval, tmp);
		    g_free(tmp);
		    break;
	    case GCONF_VALUE_PAIR:
		    tmp = _gconf_cleaner_value_pair_to_string(value, indent + 2);
		    g_string_append(retval, tmp);
		    g_free(tmp);
		    break;
	    default:
		    g_assert_not_reached();
		    break;
	}
	g_string_append_printf(retval, "%s</value>\n", whitespace);

	g_free(whitespace);

	return g_string_free(retval, FALSE);
}

static void
_gconf_cleaner_about_url_cb(GtkAboutDialog *about,
			    const gchar    *link,
			    gpointer        data)
{
}

static void
_gconf_cleaner_button_about_on_clicked(GtkButton *button,
				       gpointer   data)
{
	GConfCleanerInstance *inst = data;
	const gchar *authors[] = {
		"Akira TAGOH",
		NULL
	};
	const gchar *license =
		"This program is free software; you can redistribute it and/or modify\n"
		"it under the terms of the GNU General Public License as published by\n"
		"the Free Software Foundation; either version 2 of the License, or\n"
		"(at your option) any later version.\n"
		"\n"
		"This program is distributed in the hope that it will be useful,\n"
		"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
		"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
		"GNU General Public License for more details.\n"
		"\n"
		"You should have received a copy of the GNU General Public License\n"
		"along with this program; if not, write to the Free Software\n"
		"Foundation, Inc., 59 Temple Place - Suite 330,\n"
		"Boston, MA 02111-1307, USA.\n";

	gtk_about_dialog_set_url_hook(_gconf_cleaner_about_url_cb, NULL, NULL);
	gtk_show_about_dialog(GTK_WINDOW (inst->window),
			      "name", _("GConf Cleaner"),
			      "comments", _("A Cleaning tool for GConf"),
			      "copyright", "(C) 2007 Akira TAGOH",
			      "version", PACKAGE_VERSION,
			      "website", "http://code.google.com/p/gconf-cleaner/",
			      "authors", authors,
			      "translator-credits", _("translator-credits"),
			      "license", license,
			      NULL);
}

static void
_gconf_cleaner_on_assistant_cancel(GtkWidget *widget,
				   gpointer   data)
{
	GConfCleanerInstance *inst = data;

	if (_gconf_cleaner_question_dialog(inst,
					   _("<span weight=\"bold\" size=\"larger\">Do you want to leave GConf Cleaner?</span>"),
					   _("If you leave here, any GConf keys will not be cleaned up."))) {
		gtk_main_quit();
	}
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

static void
_gconf_cleaner_save_on_response(GtkDialog *dialog,
				gint       response_id,
				gpointer   data)
{
	if (response_id == GTK_RESPONSE_OK) {
		GConfCleanerInstance *inst = data;
		GString *dump = g_string_new(NULL);
		GSList *l;
		FILE *fp;
		struct stat st;
		gchar *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER (dialog));

		g_string_append_printf(dump,
				       "<gconfentryfile>\n"
				       "  <entrylist base=\"/\">\n");
				       
		for (l = inst->pairs; l != NULL; l = g_slist_next(l)) {
			gchar *key;
			GConfValue *val;
			gchar *tmp;

			key = l->data;
			l = g_slist_next(l);
			val = l->data;
			tmp = _gconf_cleaner_value_to_string(val, 6);
			g_string_append_printf(dump,
					       "    <entry>\n"
					       "      <key>%s</key>\n"
					       "%s"
					       "    </entry>\n", key, tmp);
			g_free(tmp);
		}

		g_string_append_printf(dump,
				       "  </entrylist>\n"
				       "</gconfentryfile>\n");

		if (stat(filename, &st) == 0) {
			gchar *msg = g_strdup_printf(_("If you save the data as %s, original data will be lost."), filename);
			gboolean retval;

			retval = _gconf_cleaner_question_dialog(inst,
								_("<span weight=\"bold\" size=\"larger\">Do you want to overwrite a file?</span>"),
								msg);
			g_free(msg);
			if (!retval)
				return;
		}
		if ((fp = fopen(filename, "wb")) == NULL) {
			gchar *msg = g_strdup_printf(_("Failed during opening %s"),
						     filename);
			_gconf_cleaner_error_dialog(inst,
						    msg,
						    strerror(errno),
						    FALSE);
			g_free(msg);
		} else {
			size_t retval = fwrite(dump->str, sizeof (gchar), dump->len, fp);

			if (retval != dump->len) {
				_gconf_cleaner_error_dialog(inst,
							    _("Failed during saving GConf keys"),
							    strerror(errno),
							    FALSE);
			}
			fclose(fp);
		}

		g_string_free(dump, TRUE);
	}
	gtk_widget_destroy(GTK_WIDGET (dialog));
}

static void
_gconf_cleaner_save_on_clicked(GtkButton *button,
			       gpointer   data)
{
	GConfCleanerInstance *inst = data;
	GtkWidget *dialog;

	if (inst->name == NULL) {
		time_t t;
		struct tm *stm;

		t = time(NULL);
		stm = localtime(&t);
		inst->name = g_strdup_printf("%04d%02d%02d%02d%02d%02d.reg",
					     1900 + stm->tm_year, stm->tm_mon + 1, stm->tm_mday,
					     stm->tm_hour, stm->tm_min, stm->tm_sec);
	}
	dialog = gtk_file_chooser_dialog_new(_("Save GConf keys as..."),
					     GTK_WINDOW (inst->window),
					     GTK_FILE_CHOOSER_ACTION_SAVE,
					     GTK_STOCK_CANCEL,
					     GTK_RESPONSE_CANCEL,
					     GTK_STOCK_SAVE,
					     GTK_RESPONSE_OK,
					     NULL);
	gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER (dialog), inst->name);
	g_signal_connect(dialog, "response",
			 G_CALLBACK (_gconf_cleaner_save_on_response),
			 inst);

	gtk_dialog_run(GTK_DIALOG (dialog));
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
	if (G_UNLIKELY (error != NULL)) {
		_gconf_cleaner_error_dialog(inst,
					    _("<span weight=\"bold\" size=\"larger\">Failed during the initialization</span>"),
					    error->message,
					    TRUE);
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
		if (G_UNLIKELY (error != NULL)) {
			_gconf_cleaner_error_dialog(inst,
						    _("<span weight=\"bold\" size=\"larger\">Failed during analyzing the GConf key</span>"),
						    error->message,
						    TRUE);
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

	if (inst->n_unknown_pairs > 0) {
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
		gtk_widget_show(inst->expander);
		gtk_widget_show(inst->hbox);
		gtk_label_set_text(GTK_LABEL (inst->label_message),
				   _("Please press Forward button to clean up the GConf keys."));
	} else {
		gtk_widget_hide(inst->expander);
		gtk_widget_hide(inst->hbox);
		gtk_label_set_text(GTK_LABEL (inst->label_message),
				   _("Luckily, there are no GConf keys can be cleaned up."));
	}

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
	GError *error = NULL;

	if (inst->n_unknown_pairs == 0)
		goto next;
	gtk_progress_bar_set_orientation(GTK_PROGRESS_BAR (inst->progressbar2),
					 GTK_PROGRESS_LEFT_TO_RIGHT);
	gtk_progress_bar_set_ellipsize(GTK_PROGRESS_BAR (inst->progressbar2),
				       PANGO_ELLIPSIZE_MIDDLE);
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR (inst->progressbar2), 0.0);

	while (g_main_context_pending(NULL))
		g_main_context_iteration(NULL, TRUE);

	model = gtk_tree_view_get_model(GTK_TREE_VIEW (inst->treeview));
	if (G_LIKELY (gtk_tree_model_get_iter_first(model, &iter))) {
		do {
			gtk_tree_model_get(model, &iter, 0, &flag, 1, &key, -1);
			if (flag) {
				i++;
				gtk_progress_bar_set_text(GTK_PROGRESS_BAR (inst->progressbar2), key);
				gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR (inst->progressbar2),
							      (gdouble)i / (gdouble)inst->n_unknown_pairs);

				while (g_main_context_pending(NULL))
					g_main_context_iteration(NULL, TRUE);

				gconf_cleaner_unset_key(inst->cleaner, key, &error);
				if (G_UNLIKELY (error)) {
					_gconf_cleaner_error_dialog(inst,
								    _("<span weight=\"bold\" size=\"larger\">Failed during cleaning GConf key up.</span>"),
								    error->message,
								    TRUE);
					return FALSE;
				}
			}
			g_free(key);
		} while (gtk_tree_model_iter_next(model, &iter));
		gconf_cleaner_sync(inst->cleaner, &error);
	}
  next:
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
		GtkWidget *vbox, *vbox2;

		vbox = gtk_vbox_new(FALSE, 0);
		vbox2 = gtk_vbox_new(FALSE, 5);
		inst->label_progress = gtk_label_new("");
		inst->progressbar = gtk_progress_bar_new();

		gtk_box_pack_start(GTK_BOX (vbox2), inst->label_progress, FALSE, TRUE, 0);
		gtk_box_pack_start(GTK_BOX (vbox2), inst->progressbar, FALSE, TRUE, 0);
		gtk_box_pack_start(GTK_BOX (vbox), vbox2, TRUE, FALSE, 0);

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
		GtkWidget *table, *vbox, *label_dirs, *label_keys, *label_pairs;
		GtkWidget *scrolled, *label_save, *button_save;
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

		inst->expander = gtk_expander_new_with_mnemonic(_("_Details"));
		gtk_expander_set_expanded(GTK_EXPANDER (inst->expander), FALSE);

		inst->hbox = gtk_hbox_new(FALSE, 0);
		label_save = gtk_label_new(_("<small><i>Note: This cleanup may affects to your applications working. Please save here to not lose your data.</i></small>"));
		gtk_label_set_use_markup(GTK_LABEL (label_save), TRUE);
		gtk_label_set_line_wrap(GTK_LABEL (label_save), TRUE);

		button_save = gtk_button_new_from_stock(GTK_STOCK_SAVE_AS);
		g_signal_connect(button_save, "clicked",
				 G_CALLBACK (_gconf_cleaner_save_on_clicked), inst);

		inst->label_message = gtk_label_new(NULL);
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
		gtk_box_pack_start(GTK_BOX (inst->hbox), label_save, TRUE, TRUE, 10);
		gtk_box_pack_start(GTK_BOX (inst->hbox), button_save, TRUE, TRUE, 0);
		gtk_box_pack_start(GTK_BOX (vbox), table, FALSE, TRUE, 10);
		gtk_box_pack_start(GTK_BOX (vbox), inst->expander, TRUE, TRUE, 0);
		gtk_box_pack_start(GTK_BOX (vbox), inst->hbox, FALSE, TRUE, 10);
		gtk_box_pack_start(GTK_BOX (vbox), inst->label_message, FALSE, TRUE, 10);
		gtk_container_add(GTK_CONTAINER (inst->expander), scrolled);
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
		column = gtk_tree_view_column_new_with_attributes(_("Key"),
								  renderer,
								  "text", 1,
								  NULL);
		gtk_tree_view_column_set_resizable(column, TRUE);
		gtk_tree_view_append_column(GTK_TREE_VIEW (inst->treeview), column);
		renderer = gtk_cell_renderer_text_new();
		column = gtk_tree_view_column_new_with_attributes(_("Value"),
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
	GtkWidget *button;

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
	inst->name = NULL;

	gtk_window_set_title(GTK_WINDOW (inst->window), _("GConf Cleaner"));
	button = gtk_button_new_from_stock(GTK_STOCK_ABOUT);
	gtk_assistant_add_action_widget(GTK_ASSISTANT (inst->window), button);
	gtk_widget_show(button);

	_gconf_cleaner_create_page(inst);

	g_signal_connect(button, "clicked",
			 G_CALLBACK (_gconf_cleaner_button_about_on_clicked),
			 inst);
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

	if (G_LIKELY (inst->cleaner))
		gconf_cleaner_free(inst->cleaner);
	if (G_LIKELY (inst->pairs))
		gconf_cleaner_pairs_free(inst->pairs);
	if (G_LIKELY (inst->pages)) {
		gint i;

		for (i = 0; i < inst->pages->len; i++)
			g_free(g_ptr_array_index(inst->pages, i));
		g_ptr_array_free(inst->pages, TRUE);
	}
	if (G_LIKELY (inst->name))
		g_free(inst->name);
	g_free(inst);

	return 0;
}
