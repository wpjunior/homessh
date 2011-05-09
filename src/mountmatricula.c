/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/* GIO - GLib Input, Output and Streaming Library
 *
 * Copyright (C) 2006-2007 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Alexander Larsson <alexl@redhat.com>
 */

#include <stdio.h>
#include <string.h>

#include <glib.h>
#include <gtk/gtk.h>
#include <locale.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif

#define STDIN_FILENO 0

static int outstanding_mounts = 0;
static GMainLoop *main_loop;


static gboolean mount_mountable = FALSE;
static gboolean mount_unmount = FALSE;
static gboolean mount_list = FALSE;
static gboolean extra_detail = FALSE;
static gboolean mount_monitor = FALSE;
static const char *unmount_scheme = NULL;
static const char *mount_device_file = NULL;
gchar SERVIDOR[] = "radioserra.com.br";
static const GOptionEntry entries[] =
{
  { "mountable", 'm', 0, G_OPTION_ARG_NONE, &mount_mountable, N_("Mount as mountable"), NULL },
  { "device", 'd', 0, G_OPTION_ARG_STRING, &mount_device_file, N_("Mount volume with device file"), NULL},
  { "unmount", 'u', 0, G_OPTION_ARG_NONE, &mount_unmount, N_("Unmount"), NULL},
  { "unmount-scheme", 's', 0, G_OPTION_ARG_STRING, &unmount_scheme, N_("Unmount all mounts with the given scheme"), NULL},
  { "list", 'l', 0, G_OPTION_ARG_NONE, &mount_list, N_("List"), NULL},
  { "detail", 'i', 0, G_OPTION_ARG_NONE, &extra_detail, N_("Show extra information for List and Monitor"), NULL},
  { "monitor", 'o', 0, G_OPTION_ARG_NONE, &mount_monitor, N_("Monitor events"), NULL},
  { NULL }
};

static char *
prompt_for (const char *prompt, const char *default_value, gboolean echo)
{
#ifdef HAVE_TERMIOS_H
  struct termios term_attr;
  int old_flags;
  gboolean restore_flags;
#endif
  char data[256];
  int len;

  if (default_value && *default_value != 0)
    g_print ("%s [%s]: ", prompt, default_value);
  else
    g_print ("%s: ", prompt);

  data[0] = 0;

#ifdef HAVE_TERMIOS_H
  restore_flags = FALSE;
  if (!echo && tcgetattr (STDIN_FILENO, &term_attr) == 0)
    {
      old_flags = term_attr.c_lflag;
      term_attr.c_lflag &= ~ECHO;
      restore_flags = TRUE;

      if (tcsetattr (STDIN_FILENO, TCSAFLUSH, &term_attr) != 0)
        g_print ("Warning! Password will be echoed");
    }

#endif

  fgets(data, sizeof (data), stdin);

#ifdef HAVE_TERMIOS_H
  if (restore_flags)
    {
      term_attr.c_lflag = old_flags;
      tcsetattr (STDIN_FILENO, TCSAFLUSH, &term_attr);
    }
#endif

  len = strlen (data);
  if (len > 0 && data[len-1] == '\n')
    data[len-1] = 0;

  if (!echo)
    g_print ("\n");

  if (*data == 0 && default_value)
    return g_strdup (default_value);
  return g_strdup (data);
}

static void
ask_password_cb (GMountOperation *op,
                 const char      *message,
                 const char      *default_user,
                 const char      *default_domain,
                 GAskPasswordFlags flags)
{
  char *s;
  g_print ("%s\n", message);

  if (flags & G_ASK_PASSWORD_NEED_USERNAME)
    {
      s = prompt_for ("User", default_user, TRUE);
      g_mount_operation_set_username (op, s);
      g_free (s);
    }

  if (flags & G_ASK_PASSWORD_NEED_DOMAIN)
    {
      s = prompt_for ("Domain", default_domain, TRUE);
      g_mount_operation_set_domain (op, s);
      g_free (s);
    }

  if (flags & G_ASK_PASSWORD_NEED_PASSWORD)
    {
      s = prompt_for ("Password", NULL, FALSE);
      g_mount_operation_set_password (op, s);
      g_free (s);
    }

  g_mount_operation_reply (op, G_MOUNT_OPERATION_HANDLED);
}

static void
mount_mountable_done_cb (GObject *object,
                         GAsyncResult *res,
                         gpointer user_data)
{
  GFile *target;
  GError *error = NULL;

  target = g_file_mount_mountable_finish (G_FILE (object), res, &error);

  if (target == NULL)
    g_printerr (_("Error mounting location: %s\n"), error->message);
  else
    g_object_unref (target);

  outstanding_mounts--;

  if (outstanding_mounts == 0)
    g_main_loop_quit (main_loop);
}

static void
mount_done_cb (GObject *object,
               GAsyncResult *res,
               gpointer user_data)
{
  gboolean succeeded;
  GError *error = NULL;

  succeeded = g_file_mount_enclosing_volume_finish (G_FILE (object), res, &error);

  if (!succeeded)
    g_printerr (_("Error mounting location: %s\n"), error->message);

  outstanding_mounts--;

  if (outstanding_mounts == 0)
    g_main_loop_quit (main_loop);
}

static GMountOperation *
new_mount_op (void)
{
  GMountOperation *op;

  op = g_mount_operation_new ();

  g_signal_connect (op, "ask_password", G_CALLBACK (ask_password_cb), NULL);

  /* TODO: we *should* also connect to the "aborted" signal but since we the
   *       main thread is blocked handling input we won't get that signal
   *       anyway...
   */

  return op;
}


static void
mount (GFile *file)
{
  GMountOperation *op;

  if (file == NULL)
    return;

  op = new_mount_op ();

  if (mount_mountable)
    g_file_mount_mountable (file, 0, op, NULL, mount_mountable_done_cb, op);
  else
    g_file_mount_enclosing_volume (file, 0, op, NULL, mount_done_cb, op);

  outstanding_mounts++;
}

static void
unmount_done_cb (GObject *object,
                 GAsyncResult *res,
                 gpointer user_data)
{
  gboolean succeeded;
  GError *error = NULL;

  succeeded = g_mount_unmount_with_operation_finish (G_MOUNT (object), res, &error);

  g_object_unref (G_MOUNT (object));

  if (!succeeded)
    g_printerr (_("Error unmounting mount: %s\n"), error->message);

  outstanding_mounts--;

  if (outstanding_mounts == 0)
    g_main_loop_quit (main_loop);
}

static void
unmount (GFile *file)
{
  GMount *mount;
  GError *error = NULL;
  GMountOperation *mount_op;

  if (file == NULL)
    return;

  mount = g_file_find_enclosing_mount (file, NULL, &error);
  if (mount == NULL)
    {
      g_printerr (_("Error finding enclosing mount: %s\n"), error->message);
      return;
    }

  mount_op = new_mount_op ();
  g_mount_unmount_with_operation (mount, 0, mount_op, NULL, unmount_done_cb, NULL);
  g_object_unref (mount_op);

  outstanding_mounts++;
}

/* =============== list mounts ================== */

static gboolean
iterate_gmain_timeout_function (gpointer data)
{
  g_main_loop_quit (main_loop);
  return FALSE;
}

static void
iterate_gmain(void)
{
  g_timeout_add (500, iterate_gmain_timeout_function, NULL);
  g_main_loop_run (main_loop);
}

static void
show_themed_icon_names (GThemedIcon *icon, int indent)
{
  char **names;
  char **iter;

  g_print ("%*sthemed icons:", indent, " ");

  names = NULL;

  g_object_get (icon, "names", &names, NULL);

  for (iter = names; *iter; iter++)
    g_print ("  [%s]", *iter);

  g_print ("\n");
  g_strfreev (names);
}

/* don't copy-paste this code */
static char *
get_type_name (gpointer object)
{
  const char *type_name;
  char *ret;

  type_name = g_type_name (G_TYPE_FROM_INSTANCE (object));
  if (strcmp ("GProxyDrive", type_name) == 0)
    {
      ret = g_strdup_printf ("%s (%s)",
                             type_name,
                             (const char *) g_object_get_data (G_OBJECT (object),
                                                               "g-proxy-drive-volume-monitor-name"));
    }
  else if (strcmp ("GProxyVolume", type_name) == 0)
    {
      ret = g_strdup_printf ("%s (%s)",
                             type_name,
                             (const char *) g_object_get_data (G_OBJECT (object),
                                                               "g-proxy-volume-volume-monitor-name"));
    }
  else if (strcmp ("GProxyMount", type_name) == 0)
    {
      ret = g_strdup_printf ("%s (%s)",
                             type_name,
                             (const char *) g_object_get_data (G_OBJECT (object),
                                                               "g-proxy-mount-volume-monitor-name"));
    }
  else if (strcmp ("GProxyShadowMount", type_name) == 0)
    {
      ret = g_strdup_printf ("%s (%s)",
                             type_name,
                             (const char *) g_object_get_data (G_OBJECT (object),
                                                               "g-proxy-shadow-mount-volume-monitor-name"));
    }
  else
    {
      ret = g_strdup (type_name);
    }

  return ret;
}

static void
list_mounts (GList *mounts,
             int indent,
             gboolean only_with_no_volume)
{
  GList *l;
  int c;
  GMount *mount;
  GVolume *volume;
  char *name, *uuid, *uri;
  GFile *root, *default_location;
  GIcon *icon;
  char **x_content_types;
  char *type_name;

  for (c = 0, l = mounts; l != NULL; l = l->next, c++)
    {
      mount = (GMount *) l->data;

      if (only_with_no_volume)
        {
          volume = g_mount_get_volume (mount);
          if (volume != NULL)
            {
              g_object_unref (volume);
              continue;
            }
        }

      name = g_mount_get_name (mount);
      root = g_mount_get_root (mount);
      uri = g_file_get_uri (root);

      g_print ("%*sMount(%d): %s -> %s\n", indent, "", c, name, uri);

      type_name = get_type_name (mount);
      g_print ("%*sType: %s\n", indent+2, "", type_name);
      g_free (type_name);

      if (extra_detail)
        {
          uuid = g_mount_get_uuid (mount);
          if (uuid)
            g_print ("%*suuid=%s\n", indent + 2, "", uuid);

          default_location = g_mount_get_default_location (mount);
          if (default_location)
            {
              char *loc_uri = g_file_get_uri (default_location);
              g_print ("%*sdefault_location=%s\n", indent + 2, "", loc_uri);
              g_free (loc_uri);
              g_object_unref (default_location);
            }

          icon = g_mount_get_icon (mount);
          if (icon)
            {
              if (G_IS_THEMED_ICON (icon))
                show_themed_icon_names (G_THEMED_ICON (icon), indent + 2);

              g_object_unref (icon);
            }

          x_content_types = g_mount_guess_content_type_sync (mount, FALSE, NULL, NULL);
          if (x_content_types != NULL && g_strv_length (x_content_types) > 0)
            {
              int n;
              g_print ("%*sx_content_types:", indent + 2, "");
              for (n = 0; x_content_types[n] != NULL; n++)
                  g_print (" %s", x_content_types[n]);
              g_print ("\n");
            }
          g_strfreev (x_content_types);

          g_print ("%*scan_unmount=%d\n", indent + 2, "", g_mount_can_unmount (mount));
          g_print ("%*scan_eject=%d\n", indent + 2, "", g_mount_can_eject (mount));
          g_print ("%*sis_shadowed=%d\n", indent + 2, "", g_mount_is_shadowed (mount));
          g_free (uuid);
        }

      g_object_unref (root);
      g_free (name);
      g_free (uri);
    }
}

static void
list_volumes (GList *volumes,
              int indent,
              gboolean only_with_no_drive)
{
  GList *l, *mounts;
  int c, i;
  GMount *mount;
  GVolume *volume;
  GDrive *drive;
  char *name;
  char *uuid;
  GFile *activation_root;
  char **ids;
  GIcon *icon;
  char *type_name;

  for (c = 0, l = volumes; l != NULL; l = l->next, c++)
    {
      volume = (GVolume *) l->data;

      if (only_with_no_drive)
        {
          drive = g_volume_get_drive (volume);
          if (drive != NULL)
            {
              g_object_unref (drive);
              continue;
            }
        }

      name = g_volume_get_name (volume);

      g_print ("%*sVolume(%d): %s\n", indent, "", c, name);
      g_free (name);

      type_name = get_type_name (volume);
      g_print ("%*sType: %s\n", indent+2, "", type_name);
      g_free (type_name);

      if (extra_detail)
        {
          ids = g_volume_enumerate_identifiers (volume);
          if (ids && ids[0] != NULL)
            {
              g_print ("%*sids:\n", indent+2, "");
              for (i = 0; ids[i] != NULL; i++)
                {
                  char *id = g_volume_get_identifier (volume,
                                                      ids[i]);
                  g_print ("%*s %s: '%s'\n", indent+2, "", ids[i], id);
                  g_free (id);
                }
            }
          g_strfreev (ids);

          uuid = g_volume_get_uuid (volume);
          if (uuid)
            g_print ("%*suuid=%s\n", indent + 2, "", uuid);
          activation_root = g_volume_get_activation_root (volume);
          if (activation_root)
            {
              char *uri;
              uri = g_file_get_uri (activation_root);
              g_print ("%*sactivation_root=%s\n", indent + 2, "", uri);
              g_free (uri);
              g_object_unref (activation_root);
            }
          icon = g_volume_get_icon (volume);
          if (icon)
            {
              if (G_IS_THEMED_ICON (icon))
                show_themed_icon_names (G_THEMED_ICON (icon), indent + 2);

              g_object_unref (icon);
            }

          g_print ("%*scan_mount=%d\n", indent + 2, "", g_volume_can_mount (volume));
          g_print ("%*scan_eject=%d\n", indent + 2, "", g_volume_can_eject (volume));
          g_print ("%*sshould_automount=%d\n", indent + 2, "", g_volume_should_automount (volume));
          g_free (uuid);
        }

      mount = g_volume_get_mount (volume);
      if (mount)
        {
          mounts = g_list_prepend (NULL, mount);
          list_mounts (mounts, indent + 2, FALSE);
          g_list_free (mounts);
          g_object_unref (mount);
        }
    }
}

static void
list_drives (GList *drives,
             int indent)
{
  GList *volumes, *l;
  int c, i;
  GDrive *drive;
  char *name;
  char **ids;
  GIcon *icon;
  char *type_name;

  for (c = 0, l = drives; l != NULL; l = l->next, c++)
    {
      drive = (GDrive *) l->data;
      name = g_drive_get_name (drive);

      g_print ("%*sDrive(%d): %s\n", indent, "", c, name);
      g_free (name);

      type_name = get_type_name (drive);
      g_print ("%*sType: %s\n", indent+2, "", type_name);
      g_free (type_name);

      if (extra_detail)
        {
          GEnumValue *enum_value;
          gpointer klass;

          ids = g_drive_enumerate_identifiers (drive);
          if (ids && ids[0] != NULL)
            {
              g_print ("%*sids:\n", indent+2, "");
              for (i = 0; ids[i] != NULL; i++)
                {
                  char *id = g_drive_get_identifier (drive,
                                                     ids[i]);
                  g_print ("%*s %s: '%s'\n", indent+2, "", ids[i], id);
                  g_free (id);
                }
            }
          g_strfreev (ids);

          icon = g_drive_get_icon (drive);
          if (icon)
          {
                  if (G_IS_THEMED_ICON (icon))
                          show_themed_icon_names (G_THEMED_ICON (icon), indent + 2);
                  g_object_unref (icon);
          }

          g_print ("%*sis_media_removable=%d\n", indent + 2, "", g_drive_is_media_removable (drive));
          g_print ("%*shas_media=%d\n", indent + 2, "", g_drive_has_media (drive));
          g_print ("%*sis_media_check_automatic=%d\n", indent + 2, "", g_drive_is_media_check_automatic (drive));
          g_print ("%*scan_poll_for_media=%d\n", indent + 2, "", g_drive_can_poll_for_media (drive));
          g_print ("%*scan_eject=%d\n", indent + 2, "", g_drive_can_eject (drive));
          g_print ("%*scan_start=%d\n", indent + 2, "", g_drive_can_start (drive));
          g_print ("%*scan_stop=%d\n", indent + 2, "", g_drive_can_stop (drive));

          enum_value = NULL;
          klass = g_type_class_ref (G_TYPE_DRIVE_START_STOP_TYPE);
          if (klass != NULL)
            {
              enum_value = g_enum_get_value (klass, g_drive_get_start_stop_type (drive));
              g_print ("%*sstart_stop_type=%s\n", indent + 2, "",
                       enum_value != NULL ? enum_value->value_nick : "UNKNOWN");
              g_type_class_unref (klass);
            }
        }
      volumes = g_drive_get_volumes (drive);
      list_volumes (volumes, indent + 2, FALSE);
      g_list_foreach (volumes, (GFunc)g_object_unref, NULL);
      g_list_free (volumes);
    }
}


static void
list_monitor_items(void)
{
  GVolumeMonitor *volume_monitor;
  GList *drives, *volumes, *mounts;

  volume_monitor = g_volume_monitor_get();

  /* populate gvfs network mounts */
  iterate_gmain();

  drives = g_volume_monitor_get_connected_drives (volume_monitor);
  list_drives (drives, 0);
  g_list_foreach (drives, (GFunc)g_object_unref, NULL);
  g_list_free (drives);

  volumes = g_volume_monitor_get_volumes (volume_monitor);
  list_volumes (volumes, 0, TRUE);
  g_list_foreach (volumes, (GFunc)g_object_unref, NULL);
  g_list_free (volumes);

  mounts = g_volume_monitor_get_mounts (volume_monitor);
  list_mounts (mounts, 0, TRUE);
  g_list_foreach (mounts, (GFunc)g_object_unref, NULL);
  g_list_free (mounts);

  g_object_unref (volume_monitor);
}

static void
unmount_all_with_scheme (const char *scheme)
{
  GVolumeMonitor *volume_monitor;
  GList *mounts;
  GList *l;

  volume_monitor = g_volume_monitor_get();

  /* populate gvfs network mounts */
  iterate_gmain();

  mounts = g_volume_monitor_get_mounts (volume_monitor);
  for (l = mounts; l != NULL; l = l->next) {
    GMount *mount = G_MOUNT (l->data);
    GFile *root;

    root = g_mount_get_root (mount);
    if (g_file_has_uri_scheme (root, scheme)) {
            unmount (root);
    }
    g_object_unref (root);
  }
  g_list_foreach (mounts, (GFunc)g_object_unref, NULL);
  g_list_free (mounts);

  g_object_unref (volume_monitor);
}

static void
mount_with_device_file_cb (GObject *object,
                           GAsyncResult *res,
                           gpointer user_data)
{
  GVolume *volume;
  gboolean succeeded;
  GError *error = NULL;

  volume = G_VOLUME (object);

  succeeded = g_volume_mount_finish (volume, res, &error);

  if (!succeeded)
    {
      g_printerr (_("Error mounting %s: %s\n"),
                  g_volume_get_identifier (volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE),
                  error->message);
    }
  else
    {
      GMount *mount;
      GFile *root;
      char *mount_path;

      mount = g_volume_get_mount (volume);
      root = g_mount_get_root (mount);
      mount_path = g_file_get_path (root);

      g_print (_("Mounted %s at %s\n"),
               g_volume_get_identifier (volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE),
               mount_path);

      g_object_unref (mount);
      g_object_unref (root);
      g_free (mount_path);
    }

  outstanding_mounts--;

  if (outstanding_mounts == 0)
    g_main_loop_quit (main_loop);
}

static void
mount_with_device_file (const char *device_file)
{
  GVolumeMonitor *volume_monitor;
  GList *volumes;
  GList *l;

  volume_monitor = g_volume_monitor_get();

  volumes = g_volume_monitor_get_volumes (volume_monitor);
  for (l = volumes; l != NULL; l = l->next)
    {
      GVolume *volume = G_VOLUME (l->data);

      if (g_strcmp0 (g_volume_get_identifier (volume,
                                              G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE), device_file) == 0)
        {
          GMountOperation *op;

          op = new_mount_op ();

          g_volume_mount (volume,
                          G_MOUNT_MOUNT_NONE,
                          op,
                          NULL,
                          mount_with_device_file_cb,
                          op);

          outstanding_mounts++;
        }
    }
  g_list_foreach (volumes, (GFunc) g_object_unref, NULL);
  g_list_free (volumes);

  if (outstanding_mounts == 0)
    {
      g_print (_("No volume for device file %s\n"), device_file);
      return;
    }

  g_object_unref (volume_monitor);
}

static void
monitor_print_mount (GMount *mount)
{
  if (extra_detail)
    {
      GList *l;
      l = g_list_prepend (NULL, mount);
      list_mounts (l, 2, FALSE);
      g_list_free (l);
      g_print ("\n");
    }
}

static void
monitor_print_volume (GVolume *volume)
{
  if (extra_detail)
    {
      GList *l;
      l = g_list_prepend (NULL, volume);
      list_volumes (l, 2, FALSE);
      g_list_free (l);
      g_print ("\n");
    }
}

static void
monitor_print_drive (GDrive *drive)
{
  if (extra_detail)
    {
      GList *l;
      l = g_list_prepend (NULL, drive);
      list_drives (l, 2);
      g_list_free (l);
      g_print ("\n");
    }
}

static void
monitor_mount_added (GVolumeMonitor *volume_monitor, GMount *mount)
{
  char *name;
  name = g_mount_get_name (mount);
  g_print ("Mount added: '%s'\n", name);
  g_free (name);
  monitor_print_mount (mount);
}

static void
monitor_mount_removed (GVolumeMonitor *volume_monitor, GMount *mount)
{
  char *name;
  name = g_mount_get_name (mount);
  g_print ("Mount removed: '%s'\n", name);
  g_free (name);
  monitor_print_mount (mount);
}

static void
monitor_mount_changed (GVolumeMonitor *volume_monitor, GMount *mount)
{
  char *name;
  name = g_mount_get_name (mount);
  g_print ("Mount changed: '%s'\n", name);
  g_free (name);
  monitor_print_mount (mount);
}

static void
monitor_mount_pre_unmount (GVolumeMonitor *volume_monitor, GMount *mount)
{
  char *name;
  name = g_mount_get_name (mount);
  g_print ("Mount pre-unmount:  '%s'\n", name);
  g_free (name);
  monitor_print_mount (mount);
}

static void
monitor_volume_added (GVolumeMonitor *volume_monitor, GVolume *volume)
{
  char *name;
  name = g_volume_get_name (volume);
  g_print ("Volume added:       '%s'\n", name);
  g_free (name);
  monitor_print_volume (volume);
}

static void
monitor_volume_removed (GVolumeMonitor *volume_monitor, GVolume *volume)
{
  char *name;
  name = g_volume_get_name (volume);
  g_print ("Volume removed:     '%s'\n", name);
  g_free (name);
  monitor_print_volume (volume);
}

static void
monitor_volume_changed (GVolumeMonitor *volume_monitor, GVolume *volume)
{
  char *name;
  name = g_volume_get_name (volume);
  g_print ("Volume changed:     '%s'\n", name);
  g_free (name);
  monitor_print_volume (volume);
}

static void
monitor_drive_connected (GVolumeMonitor *volume_monitor, GDrive *drive)
{
  char *name;
  name = g_drive_get_name (drive);
  g_print ("Drive connected:    '%s'\n", name);
  g_free (name);
  monitor_print_drive (drive);
}

static void
monitor_drive_disconnected (GVolumeMonitor *volume_monitor, GDrive *drive)
{
  char *name;
  name = g_drive_get_name (drive);
  g_print ("Drive disconnected: '%s'\n", name);
  g_free (name);
  monitor_print_drive (drive);
}

static void
monitor_drive_changed (GVolumeMonitor *volume_monitor, GDrive *drive)
{
  char *name;
  name = g_drive_get_name (drive);
  g_print ("Drive changed:      '%s'\n", name);
  g_free (name);
  monitor_print_drive (drive);
}

static void
monitor_drive_eject_button (GVolumeMonitor *volume_monitor, GDrive *drive)
{
  char *name;
  name = g_drive_get_name (drive);
  g_print ("Drive eject button: '%s'\n", name);
  g_free (name);
}

static void
monitor (void)
{
  GVolumeMonitor *volume_monitor;

  volume_monitor = g_volume_monitor_get ();

  g_signal_connect (volume_monitor, "mount-added", (GCallback) monitor_mount_added, NULL);
  g_signal_connect (volume_monitor, "mount-removed", (GCallback) monitor_mount_removed, NULL);
  g_signal_connect (volume_monitor, "mount-changed", (GCallback) monitor_mount_changed, NULL);
  g_signal_connect (volume_monitor, "mount-pre-unmount", (GCallback) monitor_mount_pre_unmount, NULL);
  g_signal_connect (volume_monitor, "volume-added", (GCallback) monitor_volume_added, NULL);
  g_signal_connect (volume_monitor, "volume-removed", (GCallback) monitor_volume_removed, NULL);
  g_signal_connect (volume_monitor, "volume-changed", (GCallback) monitor_volume_changed, NULL);
  g_signal_connect (volume_monitor, "drive-connected", (GCallback) monitor_drive_connected, NULL);
  g_signal_connect (volume_monitor, "drive-disconnected", (GCallback) monitor_drive_disconnected, NULL);
  g_signal_connect (volume_monitor, "drive-changed", (GCallback) monitor_drive_changed, NULL);
  g_signal_connect (volume_monitor, "drive-eject-button", (GCallback) monitor_drive_eject_button, NULL);

  g_print ("Monitoring events. Press Ctrl+C to quit.\n");

  g_main_loop_run (main_loop);
}

void values_changed_cb (GtkWidget *widget, GtkBuilder *builder) {
  GObject *obj;
  gint u, p;

  obj = gtk_builder_get_object(builder, "username");
  u = gtk_entry_get_text_length (GTK_ENTRY(obj));

  obj = gtk_builder_get_object(builder, "password");
  p = gtk_entry_get_text_length (GTK_ENTRY(obj));
  
  obj = gtk_builder_get_object(builder, "btn_connect");

  if ((u < 1) || (p < 1))
    gtk_widget_set_sensitive (GTK_WIDGET(obj), FALSE);
  else 
    gtk_widget_set_sensitive (GTK_WIDGET(obj), TRUE);
  
}

int
main (int argc, char *argv[])
{
  
  GFile *file;
  GtkBuilder *builder;
  GtkWidget *dialog, *obj;
  gchar *username, *password;
  gchar *cmd;
  GError *error;
  setlocale (LC_ALL, "");

  gtk_init(&argc, &argv);
  g_type_init ();

  error = NULL;

  main_loop = g_main_loop_new (NULL, FALSE);

  builder = gtk_builder_new();

  /* Carrega o arquivo */
  if (!gtk_builder_add_from_file(builder,
				 "gui.ui", /* TODO auto procura-lo */
				 NULL)) {
    
    dialog = gtk_message_dialog_new (NULL,
				     GTK_DIALOG_DESTROY_WITH_PARENT,
				     GTK_MESSAGE_ERROR,
				     GTK_BUTTONS_CLOSE,
				     "Não foi possível encontrar o arquivo gui.ui");
    gtk_dialog_run(GTK_DIALOG(dialog));
    return 1;
    
  }

  gtk_builder_connect_signals (builder, builder);

  dialog = GTK_WIDGET(gtk_builder_get_object (builder, "dialog"));
  gint result = gtk_dialog_run (GTK_DIALOG(dialog));
  obj = gtk_builder_get_object (builder, "username");
  username = g_strdup(gtk_entry_get_text (GTK_ENTRY(obj)));

  obj = gtk_builder_get_object (builder, "password");
  password = g_strdup(gtk_entry_get_text (GTK_ENTRY(obj)));

  
  gtk_widget_destroy (dialog);

  if (!result)
    return 1;

  cmd = g_strdup_printf ("ftp://%s@%s:/home/%s/",
			 username, SERVIDOR, username);


  file = g_file_new_for_commandline_arg (cmd);
  mount(file);
  g_object_unref (file);

  if (outstanding_mounts > 0)
    g_main_loop_run (main_loop);

  return 0;
}


