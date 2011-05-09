#include <stdio.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <gio/gio.h>

gchar SERVIDOR[] = "localhost";

static void
mount_mountable_done_cb (GObject *object,
                         GAsyncResult *res,
                         gpointer user_data)
{
  GFile *target;
  GError *error = NULL;

  target = g_file_mount_mountable_finish (G_FILE (object), res, &error);

  g_print ("Destino, %s", target);
  
  if (target == NULL)
    g_printerr ("Error mounting location: %s\n", error->message);
  else
    g_object_unref (target);
  /*
  outstanding_mounts--;

  if (outstanding_mounts == 0)
    g_main_loop_quit (main_loop);
    */
}

void mount (char *username, char *password) {
  GFile *file;
  gchar *cmd;
  GMountOperation *op;

  cmd = g_strdup_printf("ssh://%s@%s:/home/%s",
		       username, SERVIDOR, username);

  g_print ("mountando: %s\n", cmd);

  file = g_file_new_for_commandline_arg (cmd);
  op = g_mount_operation_new ();
  g_mount_operation_set_password (op, password);
  g_file_mount_mountable (file, 0, op, NULL, mount_mountable_done_cb, op);
  
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


int main (int argc, char *argv[]) {
  GtkBuilder *builder;
  GtkWidget *dialog, *obj;
  gchar *username, *password;

  gtk_init(&argc, &argv);
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

  if (result) {
    obj = gtk_builder_get_object (builder, "username");
    username = gtk_entry_get_text (GTK_ENTRY(obj));

    obj = gtk_builder_get_object (builder, "password");
    password = gtk_entry_get_text (GTK_ENTRY(obj));
    mount (username, password);
    
  }
  gtk_main();
}
