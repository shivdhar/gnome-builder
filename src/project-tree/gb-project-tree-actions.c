/* gb-project-tree-actions.c
 *
 * Copyright (C) 2015 Christian Hergert <christian@hergert.me>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define G_LOG_DOMAIN "gb-project-tree-actions"

#include <glib/gi18n.h>
#include <gio/gdesktopappinfo.h>

#ifdef HAVE_VTE
# include <vte/vte.h>
#endif

#include "gb-file-manager.h"
#include "gb-new-file-popover.h"
#include "gb-project-file.h"
#include "gb-project-tree.h"
#include "gb-project-tree-actions.h"
#include "gb-project-tree-private.h"
#include "gb-rename-file-popover.h"
#include "gb-view-stack.h"
#include "gb-widget.h"
#include "gb-workbench.h"

static void
action_set (GActionGroup *group,
            const gchar  *action_name,
            const gchar  *first_param,
            ...)
{
  GAction *action;
  va_list args;

  g_assert (G_IS_ACTION_GROUP (group));
  g_assert (G_IS_ACTION_MAP (group));

  action = g_action_map_lookup_action (G_ACTION_MAP (group), action_name);
  g_assert (G_IS_SIMPLE_ACTION (action));

  va_start (args, first_param);
  g_object_set_valist (G_OBJECT (action), first_param, args);
  va_end (args);
}

static gboolean
project_file_is_directory (GObject *object)
{
  g_assert (!object || G_IS_OBJECT (object));

  return (GB_IS_PROJECT_FILE (object) &&
          gb_project_file_get_is_directory (GB_PROJECT_FILE (object)));
}

static void
gb_project_tree_actions_refresh (GSimpleAction *action,
                                 GVariant      *variant,
                                 gpointer       user_data)
{
  GbProjectTree *self = user_data;
  GbTreeNode *selected;
  GObject *item = NULL;

  g_assert (GB_IS_PROJECT_TREE (self));

  if ((selected = gb_tree_get_selected (GB_TREE (self))))
    {
      item = gb_tree_node_get_item (selected);
      if (item != NULL)
        g_object_ref (item);
    }

  gb_tree_rebuild (GB_TREE (self));

  if (item != NULL)
    {
      selected = gb_tree_find_item (GB_TREE (self), item);
      if (selected != NULL)
        {
          gb_tree_node_expand (selected, TRUE);
          gb_tree_node_select (selected);
          gb_tree_scroll_to_node (GB_TREE (self), selected);
        }
      g_object_unref (item);
    }
}

static void
gb_project_tree_actions_collapse_all_nodes (GSimpleAction *action,
                                            GVariant      *variant,
                                            gpointer       user_data)
{
  GbProjectTree *self = user_data;

  g_assert (GB_IS_PROJECT_TREE (self));

  gtk_tree_view_collapse_all (GTK_TREE_VIEW (self));
}

static void
gb_project_tree_actions_open (GSimpleAction *action,
                              GVariant      *variant,
                              gpointer       user_data)
{
  GbProjectTree *self = user_data;
  GbWorkbench *workbench;
  GbTreeNode *selected;
  GObject *item;

  g_assert (GB_IS_PROJECT_TREE (self));

  workbench = gb_widget_get_workbench (GTK_WIDGET (self));
  g_assert (GB_IS_WORKBENCH (workbench));

  if (!(selected = gb_tree_get_selected (GB_TREE (self))) ||
      !(item = gb_tree_node_get_item (selected)))
    return;

  item = gb_tree_node_get_item (selected);

  if (GB_IS_PROJECT_FILE (item))
    {
      GFileInfo *file_info;
      GFile *file;

      file_info = gb_project_file_get_file_info (GB_PROJECT_FILE (item));
      if (!file_info)
        return;

      if (g_file_info_get_file_type (file_info) == G_FILE_TYPE_DIRECTORY)
        return;

      file = gb_project_file_get_file (GB_PROJECT_FILE (item));
      if (!file)
        return;

      gb_workbench_open (workbench, file);
    }
}

static void
gb_project_tree_actions_open_with (GSimpleAction *action,
                                   GVariant      *variant,
                                   gpointer       user_data)
{
  g_autoptr(GDesktopAppInfo) app_info = NULL;
  g_autoptr(GdkAppLaunchContext) launch_context = NULL;
  GbProjectTree *self = user_data;
  GbTreeNode *selected;
  GbWorkbench *workbench;
  GdkDisplay *display;
  GFileInfo *file_info;
  GFile *file;
  const gchar *app_id;
  GObject *item;
  GList *files;

  g_assert (GB_IS_PROJECT_TREE (self));
  g_assert (g_variant_is_of_type (variant, G_VARIANT_TYPE_STRING));

  if (!(workbench = gb_widget_get_workbench (GTK_WIDGET (self))) ||
      !(selected = gb_tree_get_selected (GB_TREE (self))) ||
      !(item = gb_tree_node_get_item (selected)) ||
      !GB_IS_PROJECT_FILE (item) ||
      !(app_id = g_variant_get_string (variant, NULL)) ||
      !(file_info = gb_project_file_get_file_info (GB_PROJECT_FILE (item))) ||
      !(file = gb_project_file_get_file (GB_PROJECT_FILE (item))) ||
      !(app_info = g_desktop_app_info_new (app_id)))
    return;

  display = gtk_widget_get_display (GTK_WIDGET (self));
  launch_context = gdk_display_get_app_launch_context (display);

  files = g_list_append (NULL, file);
  g_app_info_launch (G_APP_INFO (app_info), files, G_APP_LAUNCH_CONTEXT (launch_context), NULL);
  g_list_free (files);
}

static void
gb_project_tree_actions_open_with_editor (GSimpleAction *action,
                                          GVariant      *variant,
                                          gpointer       user_data)
{
  GbWorkbench *workbench;
  GbProjectTree *self = user_data;
  GFileInfo *file_info;
  GFile *file;
  GbTreeNode *selected;
  GObject *item;

  g_assert (GB_IS_PROJECT_TREE (self));

  if (!(selected = gb_tree_get_selected (GB_TREE (self))) ||
      !(item = gb_tree_node_get_item (selected)) ||
      !GB_IS_PROJECT_FILE (item) ||
      !(file_info = gb_project_file_get_file_info (GB_PROJECT_FILE (item))) ||
      (g_file_info_get_file_type (file_info) == G_FILE_TYPE_DIRECTORY) ||
      !(file = gb_project_file_get_file (GB_PROJECT_FILE (item))) ||
      !(workbench = gb_widget_get_workbench (GTK_WIDGET (self))))
    return;

  gb_workbench_open_with_editor (workbench, file);
}

static void
gb_project_tree_actions_open_containing_folder (GSimpleAction *action,
                                                GVariant      *variant,
                                                gpointer       user_data)
{
  GbProjectTree *self = user_data;
  GbTreeNode *selected;
  GObject *item;
  GFile *file;

  g_assert (GB_IS_PROJECT_TREE (self));

  if (!(selected = gb_tree_get_selected (GB_TREE (self))) ||
      !(item = gb_tree_node_get_item (selected)) ||
      !GB_IS_PROJECT_FILE (item))
    return;

  file = gb_project_file_get_file (GB_PROJECT_FILE (item));

  gb_file_manager_show (file, NULL);
}

/* Based on gdesktopappinfo.c in GIO */
static gchar *
find_terminal_executable (void)
{
  gsize i;
  gchar *path = NULL;
  g_autoptr(GSettings) terminal_settings = NULL;
  g_autofree gchar *gsettings_terminal = NULL;
  const gchar *terminals[] = {
    NULL,                     /* GSettings */
    "x-terminal-emulator",    /* Debian's alternative system */
    "gnome-terminal",
    NULL,                     /* getenv ("TERM") */
    "nxterm", "color-xterm",
    "rxvt", "xterm", "dtterm"
  };

  /* This is deprecated, but at least the user can specify it! */
  terminal_settings = g_settings_new ("org.gnome.desktop.default-applications.terminal");
  gsettings_terminal = g_settings_get_string (terminal_settings, "exec");
  terminals[0] = gsettings_terminal;

  /* This is generally one of the fallback terminals */
  terminals[3] = g_getenv ("TERM");

  for (i = 0; i < G_N_ELEMENTS (terminals) && path == NULL; ++i)
    {
      if (terminals[i] != NULL)
        {
          G_GNUC_BEGIN_IGNORE_DEPRECATIONS
          path = g_find_program_in_path (terminals[i]);
          G_GNUC_END_IGNORE_DEPRECATIONS
        }
    }

  return path;
}

static void
gb_project_tree_actions_open_in_terminal (GSimpleAction *action,
                                          GVariant      *variant,
                                          gpointer       user_data)
{
  GbProjectTree *self = user_data;
  GbTreeNode *selected;
  GObject *item;
  GFile *file;
  g_autofree gchar *workdir = NULL;
  g_autofree gchar *terminal_executable = NULL;
  const gchar *argv[] = { NULL, NULL };
  g_auto(GStrv) env = NULL;
  GError *error = NULL;

  g_assert (GB_IS_PROJECT_TREE (self));

  if (!(selected = gb_tree_get_selected (GB_TREE (self))) ||
      !(item = gb_tree_node_get_item (selected)) ||
      !GB_IS_PROJECT_FILE (item))
    return;

  file = gb_project_file_get_file (GB_PROJECT_FILE (item));

  if (!gb_project_file_get_is_directory (GB_PROJECT_FILE (item)))
    {
      g_autoptr(GFile) parent;

      parent = g_file_get_parent (file);
      workdir = g_file_get_path (parent);
    }
  else
    {
      workdir = g_file_get_path (file);
    }

  if (workdir == NULL)
    {
      g_warning ("Cannot load non-native file in terminal.");
      return;
    }

  terminal_executable = find_terminal_executable ();
  argv[0] = terminal_executable;
  g_return_if_fail (terminal_executable != NULL);

#ifdef HAVE_VTE
  {
    /*
     * Overwrite SHELL to the users default shell.
     * Failure to do so typically results in /bin/sh being used.
     */
    gchar *shell;

    shell = vte_get_user_shell ();
    g_setenv ("SHELL", shell, TRUE);
    g_free (shell);
  }
#endif

  env = g_get_environ ();

  /* Can't use GdkAppLaunchContext as
   * we cannot set the working directory.
   */
  if (!g_spawn_async (workdir, (gchar **)argv, env,
                      G_SPAWN_STDERR_TO_DEV_NULL,
                      NULL, NULL, NULL, &error))
    {
      g_warning ("%s", error->message);
      g_clear_error (&error);
      return;
    }
}

static void
gb_project_tree_actions__make_directory_cb (GObject      *object,
                                            GAsyncResult *result,
                                            gpointer      user_data)
{
  GFile *file = (GFile *)object;
  g_autoptr(GbTreeNode) node = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (G_IS_FILE (file));
  g_assert (GB_IS_TREE_NODE (node));

  if (!g_file_make_directory_finish (file, result, &error))
    {
      /* todo: show error messsage */
      return;
    }

  gb_tree_node_invalidate (node);
  gb_tree_node_expand (node, FALSE);
  gb_tree_node_select (node);
}

static void
gb_project_tree_actions__create_cb (GObject      *object,
                                    GAsyncResult *result,
                                    gpointer      user_data)
{
  GFile *file = (GFile *)object;
  g_autoptr(GbTreeNode) node = user_data;
  g_autoptr(GError) error = NULL;
  GbProjectTree *self;
  GbWorkbench *workbench;

  g_assert (G_IS_FILE (file));
  g_assert (GB_IS_TREE_NODE (node));

  if (!g_file_create_finish (file, result, &error))
    {
      /* todo: show error messsage */
      return;
    }

  self = GB_PROJECT_TREE (gb_tree_node_get_tree (node));
  if (self == NULL)
    return;

  workbench = gb_widget_get_workbench (GTK_WIDGET (self));
  if (workbench == NULL)
    return;

  gb_workbench_open (workbench, file);

  gb_tree_node_invalidate (node);
  gb_tree_node_expand (node, FALSE);
  gb_tree_node_select (node);
}

static void
gb_project_tree_actions__popover_create_file_cb (GbProjectTree    *self,
                                                 GFile            *file,
                                                 GFileType         file_type,
                                                 GbNewFilePopover *popover)
{
  GbTreeNode *selected;

  g_assert (GB_IS_PROJECT_TREE (self));
  g_assert (G_IS_FILE (file));
  g_assert ((file_type == G_FILE_TYPE_DIRECTORY) ||
            (file_type == G_FILE_TYPE_REGULAR));
  g_assert (GB_IS_NEW_FILE_POPOVER (popover));

  selected = gb_tree_get_selected (GB_TREE (self));

  g_assert (selected != NULL);
  g_assert (GB_IS_TREE_NODE (selected));

  if (file_type == G_FILE_TYPE_DIRECTORY)
    {
      g_file_make_directory_async (file,
                                   G_PRIORITY_DEFAULT,
                                   NULL, /* cancellable */
                                   gb_project_tree_actions__make_directory_cb,
                                   g_object_ref (selected));
    }
  else if (file_type == G_FILE_TYPE_REGULAR)
    {
      g_file_create_async (file,
                           G_FILE_CREATE_NONE,
                           G_PRIORITY_DEFAULT,
                           NULL, /* cancellable */
                           gb_project_tree_actions__create_cb,
                           g_object_ref (selected));
    }
  else
    {
      g_assert_not_reached ();
    }

  self->expanded_in_new = FALSE;

  gtk_widget_hide (GTK_WIDGET (popover));
  gtk_widget_destroy (GTK_WIDGET (popover));
}

static void
gb_project_tree_actions__popover_closed_cb (GbProjectTree *self,
                                            GtkPopover    *popover)
{
  GbTreeNode *selected;

  g_assert (GB_IS_PROJECT_TREE (self));
  g_assert (GTK_IS_POPOVER (popover));

  if (!(selected = gb_tree_get_selected (GB_TREE (self))) || !self->expanded_in_new)
    return;

  gb_tree_node_collapse (selected);
}

static void
gb_project_tree_actions_new (GbProjectTree *self,
                             GFileType      file_type)
{
  GbTreeNode *selected;
  GObject *item;
  GtkPopover *popover;
  GbProjectFile *project_file;
  GFile *file = NULL;
  gboolean is_dir = FALSE;

  g_assert (GB_IS_PROJECT_TREE (self));
  g_assert ((file_type == G_FILE_TYPE_DIRECTORY) ||
            (file_type == G_FILE_TYPE_REGULAR));

again:
  if (!(selected = gb_tree_get_selected (GB_TREE (self))) ||
      !(item = gb_tree_node_get_item (selected)) ||
      !GB_IS_PROJECT_FILE (item))
    return;

  if (!(project_file = GB_PROJECT_FILE (item)) ||
      !(file = gb_project_file_get_file (project_file)))
    return;

  is_dir = project_file_is_directory (item);

  g_assert (G_IS_FILE (file));

  /*
   * If this item is an GbProjectFile and not a directory, then we really
   * want to create a sibling.
   */
  if (!is_dir)
    {
      GtkTreePath *path;

      selected = gb_tree_node_get_parent (selected);
      gb_tree_node_select (selected);
      path = gb_tree_node_get_path (selected);
      gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (self), path, NULL, FALSE, 0, 0);
      gtk_tree_path_free (path);

      goto again;
    }

  if ((self->expanded_in_new = !gb_tree_node_get_expanded (selected)))
    gb_tree_node_expand (selected, FALSE);

  popover = g_object_new (GB_TYPE_NEW_FILE_POPOVER,
                          "directory", file,
                          "file-type", file_type,
                          "position", GTK_POS_RIGHT,
                          NULL);
  g_signal_connect_object (popover,
                           "create-file",
                           G_CALLBACK (gb_project_tree_actions__popover_create_file_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (popover,
                           "closed",
                           G_CALLBACK (gb_project_tree_actions__popover_closed_cb),
                           self,
                           G_CONNECT_SWAPPED);

  gb_tree_node_show_popover (selected, popover);
}

static void
gb_project_tree_actions_new_directory (GSimpleAction *action,
                                       GVariant      *variant,
                                       gpointer       user_data)
{
  GbProjectTree *self = user_data;

  g_assert (GB_IS_PROJECT_TREE (self));

  gb_project_tree_actions_new (self, G_FILE_TYPE_DIRECTORY);
}

static void
gb_project_tree_actions_new_file (GSimpleAction *action,
                                  GVariant      *variant,
                                  gpointer       user_data)
{
  GbProjectTree *self = user_data;

  g_assert (GB_IS_PROJECT_TREE (self));

  gb_project_tree_actions_new (self, G_FILE_TYPE_REGULAR);
}

static gboolean
find_child_node (GbTree     *tree,
                 GbTreeNode *parent,
                 GbTreeNode *child,
                 gpointer    user_data)
{
  GObject *item = gb_tree_node_get_item (child);
  GFile *target = user_data;
  GFile *child_file;

  if (GB_IS_PROJECT_FILE (item) &&
      (child_file = gb_project_file_get_file (GB_PROJECT_FILE (item))) &&
      g_file_equal (child_file, target))
    return TRUE;

  return FALSE;
}

static void
gb_project_tree_actions__project_rename_file_cb (GObject      *object,
                                                 GAsyncResult *result,
                                                 gpointer      user_data)
{
  IdeProject *project = (IdeProject *)object;
  g_autoptr(GbRenameFilePopover) popover = user_data;
  g_autoptr(GError) error = NULL;
  GbTreeNode *node;
  GbTreeNode *parent;
  GFile *file;
  GbTree *tree;

  g_assert (IDE_IS_PROJECT (project));
  g_assert (GB_IS_RENAME_FILE_POPOVER (popover));

  if (!ide_project_rename_file_finish (project, result, &error))
    {
      /* todo: display error */
      g_warning ("%s", error->message);
      return;
    }

  file = g_object_get_data (G_OBJECT (popover), "G_FILE");
  tree = GB_TREE (gtk_popover_get_relative_to (GTK_POPOVER (popover)));

  g_assert (G_IS_FILE (file));
  g_assert (GB_IS_TREE (tree));

  node = gb_tree_get_selected (tree);
  if (node == NULL)
    goto cleanup;

  parent = gb_tree_node_get_parent (node);

  gb_tree_node_invalidate (parent);
  gb_tree_node_expand (parent, FALSE);

  node = gb_tree_find_child_node (tree, parent, find_child_node, file);

  if (node != NULL)
    gb_tree_node_select (node);
  else
    gb_tree_node_select (parent);

cleanup:
  gtk_widget_hide (GTK_WIDGET (popover));
  gtk_widget_destroy (GTK_WIDGET (popover));
}

static void
gb_project_tree_actions__rename_file_cb (GbProjectTree       *self,
                                         GFile               *orig_file,
                                         GFile               *new_file,
                                         GbRenameFilePopover *popover)
{
  GbWorkbench *workbench;
  IdeContext *context;
  IdeProject *project;

  g_assert (GB_IS_PROJECT_TREE (self));
  g_assert (G_IS_FILE (orig_file));
  g_assert (G_IS_FILE (new_file));
  g_assert (GTK_IS_POPOVER (popover));

  workbench = gb_widget_get_workbench (GTK_WIDGET (self));
  context = gb_workbench_get_context (workbench);
  project = ide_context_get_project (context);

  /* todo: set busy spinner in popover */

  g_object_set_data_full (G_OBJECT (popover),
                          "G_FILE",
                          g_object_ref (new_file),
                          g_object_unref);

  ide_project_rename_file_async (project, orig_file, new_file, NULL,
                                 gb_project_tree_actions__project_rename_file_cb,
                                 g_object_ref (popover));
}

static void
gb_project_tree_actions_rename_file (GSimpleAction *action,
                                     GVariant      *variant,
                                     gpointer       user_data)
{
  GbProjectTree *self = user_data;
  GbTreeNode *selected;
  GtkPopover *popover;
  GObject *item;
  GFile *file;
  GFileInfo *file_info;
  gboolean is_dir;

  g_assert (GB_IS_PROJECT_TREE (self));

  if (!(selected = gb_tree_get_selected (GB_TREE (self))) ||
      !(item = gb_tree_node_get_item (selected)) ||
      !GB_IS_PROJECT_FILE (item) ||
      !(file = gb_project_file_get_file (GB_PROJECT_FILE (item))) ||
      !(file_info = gb_project_file_get_file_info (GB_PROJECT_FILE (item))))
    return;

  is_dir = (g_file_info_get_file_type (file_info) == G_FILE_TYPE_DIRECTORY);

  popover = g_object_new (GB_TYPE_RENAME_FILE_POPOVER,
                          "file", file,
                          "is-directory", is_dir,
                          "position", GTK_POS_RIGHT,
                          NULL);
  g_signal_connect_object (popover,
                           "rename-file",
                           G_CALLBACK (gb_project_tree_actions__rename_file_cb),
                           self,
                           G_CONNECT_SWAPPED);
  gb_tree_node_show_popover (selected, popover);
}

static void
gb_project_tree_actions__trash_file_cb (GObject      *object,
                                        GAsyncResult *result,
                                        gpointer      user_data)
{
  IdeProject *project = (IdeProject *)object;
  g_autoptr(GbTreeNode) node = user_data;
  g_autoptr(GError) error = NULL;
  GbTreeNode *parent;

  g_assert (IDE_IS_PROJECT (project));
  g_assert (GB_IS_TREE_NODE (node));

  if (!ide_project_trash_file_finish (project, result, &error))
    {
      /* todo: warning dialog */
      g_warning ("%s", error->message);
      return;
    }

  if ((parent = gb_tree_node_get_parent (node)))
    {
      gb_tree_node_invalidate (parent);
      gb_tree_node_expand (parent, FALSE);
      gb_tree_node_select (parent);
    }
}

typedef struct
{
  GbDocument *document;
  GList      *views;
} ViewsRemoval;

static void
gb_project_tree_actions_close_views_cb (GtkWidget *widget,
                                        gpointer   user_data)
{
  GbDocument *document;
  ViewsRemoval *removal = user_data;
  GbView *view = (GbView *)widget;

  g_assert (GB_IS_VIEW (view));
  g_assert (removal != NULL);
  g_assert (GB_IS_DOCUMENT (removal->document));

  document = gb_view_get_document (view);

  if (document == removal->document)
    removal->views = g_list_prepend (removal->views, g_object_ref (view));
}

static void
gb_project_tree_actions_move_to_trash (GSimpleAction *action,
                                       GVariant      *param,
                                       gpointer       user_data)
{
  GbProjectTree *self = user_data;
  IdeBufferManager *buffer_manager;
  GbWorkbench *workbench;
  IdeContext *context;
  ViewsRemoval removal = { 0 };
  IdeBuffer *buffer;
  IdeProject *project;
  GbTreeNode *node;
  GFile *file;
  GObject *item;
  GList *iter;

  g_assert (G_IS_SIMPLE_ACTION (action));
  g_assert (GB_IS_PROJECT_TREE (self));

  workbench = gb_widget_get_workbench (GTK_WIDGET (self));
  context = gb_workbench_get_context (workbench);
  project = ide_context_get_project (context);
  buffer_manager = ide_context_get_buffer_manager (context);

  if (!(node = gb_tree_get_selected (GB_TREE (self))) ||
      !(item = gb_tree_node_get_item (node)) ||
      !GB_IS_PROJECT_FILE (item) ||
      !(file = gb_project_file_get_file (GB_PROJECT_FILE (item))))
    return;

  /*
   * Find all of the views that contain this file.
   * We do not close them until we leave the foreach callback.
   */
  if ((buffer = ide_buffer_manager_find_buffer (buffer_manager, file)))
    {
      removal.document = g_object_ref (buffer);
      gb_workbench_views_foreach (workbench,
                                  gb_project_tree_actions_close_views_cb,
                                  &removal);
      g_object_unref (removal.document);
    }

  /*
   * Close all of the views that match the document.
   */
  for (iter = removal.views; iter; iter = iter->next)
    {
      GtkWidget *stack;

      stack = gtk_widget_get_ancestor (iter->data, GB_TYPE_VIEW_STACK);
      if (stack != NULL)
        gb_view_stack_remove (GB_VIEW_STACK (stack), iter->data);
    }

  g_list_free_full (removal.views, g_object_unref);

  /*
   * Now move the file to the trash.
   */
  ide_project_trash_file_async (project,
                                file,
                                NULL,
                                gb_project_tree_actions__trash_file_cb,
                                g_object_ref (node));
}

static gboolean
is_files_node (GbTreeNode *node)
{
  if (node != NULL)
    {
      GObject *item = gb_tree_node_get_item (node);
      GbTreeNode *parent = gb_tree_node_get_parent (node);
      GObject *parent_item = gb_tree_node_get_item (parent);

      return (GB_IS_PROJECT_FILE (item) && !GB_IS_PROJECT_FILE (parent_item));
    }

  return FALSE;
}

static GActionEntry GbProjectTreeActions[] = {
  { "collapse-all-nodes",     gb_project_tree_actions_collapse_all_nodes },
  { "move-to-trash",          gb_project_tree_actions_move_to_trash },
  { "new-directory",          gb_project_tree_actions_new_directory },
  { "new-file",               gb_project_tree_actions_new_file },
  { "open",                   gb_project_tree_actions_open },
  { "open-containing-folder", gb_project_tree_actions_open_containing_folder },
  { "open-in-terminal",       gb_project_tree_actions_open_in_terminal },
  { "open-with",              gb_project_tree_actions_open_with, "s" },
  { "open-with-editor",       gb_project_tree_actions_open_with_editor },
  { "refresh",                gb_project_tree_actions_refresh },
  { "rename-file",            gb_project_tree_actions_rename_file },
};

void
gb_project_tree_actions_init (GbProjectTree *self)
{
  g_autoptr(GSettings) settings = NULL;
  g_autoptr(GSettings) tree_settings = NULL;
  g_autoptr(GSimpleActionGroup) actions = NULL;
  GAction *action;

  actions = g_simple_action_group_new ();

  settings = g_settings_new ("org.gtk.Settings.FileChooser");
  action = g_settings_create_action (settings, "sort-directories-first");
  g_action_map_add_action (G_ACTION_MAP (actions), action);
  g_clear_object (&action);

  g_action_map_add_action_entries (G_ACTION_MAP (actions),
                                   GbProjectTreeActions,
                                   G_N_ELEMENTS (GbProjectTreeActions),
                                   self);
  gtk_widget_insert_action_group (GTK_WIDGET (self),
                                  "project-tree",
                                  G_ACTION_GROUP (actions));

  tree_settings = g_settings_new ("org.gnome.builder.project-tree");

  action = g_settings_create_action (tree_settings, "show-ignored-files");
  g_action_map_add_action (G_ACTION_MAP (actions), action);
  g_clear_object (&action);

  action = g_settings_create_action (tree_settings, "show-icons");
  g_action_map_add_action (G_ACTION_MAP (actions), action);
  g_clear_object (&action);

  gb_project_tree_actions_update (self);
}

void
gb_project_tree_actions_update (GbProjectTree *self)
{
  GActionGroup *group;
  GbTreeNode *selection;
  GObject *item = NULL;

  IDE_ENTRY;

  g_assert (GB_IS_PROJECT_TREE (self));

  group = gtk_widget_get_action_group (GTK_WIDGET (self), "project-tree");
  g_assert (G_IS_SIMPLE_ACTION_GROUP (group));

  selection = gb_tree_get_selected (GB_TREE (self));
  if (selection != NULL)
    item = gb_tree_node_get_item (selection);

  action_set (group, "new-file",
              "enabled", GB_IS_PROJECT_FILE (item),
              NULL);
  action_set (group, "new-directory",
              "enabled", GB_IS_PROJECT_FILE (item),
              NULL);
  action_set (group, "open",
              "enabled", (GB_IS_PROJECT_FILE (item) && !project_file_is_directory (item)),
              NULL);
  action_set (group, "open-with-editor",
              "enabled", (GB_IS_PROJECT_FILE (item) && !project_file_is_directory (item)),
              NULL);
  action_set (group, "open-containing-folder",
              "enabled", GB_IS_PROJECT_FILE (item),
              NULL);
  action_set (group, "open-in-terminal",
              "enabled", GB_IS_PROJECT_FILE (item),
              NULL);
  action_set (group, "rename-file",
              "enabled", (GB_IS_PROJECT_FILE (item) && !is_files_node (selection)),
              NULL);
  action_set (group, "move-to-trash",
              "enabled", (GB_IS_PROJECT_FILE (item) && !is_files_node (selection)),
              NULL);

  IDE_EXIT;
}
