#include <stdio.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <gio/gio.h>


void values_changed_cb (GtkWidget *widget, GtkBuilder *builder) {
  g_print ("values changed");
}


int main (int argc, char *argv[]) {
  GtkBuilder *builder;
  GtkWidget *dialog;


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

  dialog = gtk_builder_get_object (builder, "dialog");
  gtk_dialog_run (GTK_DIALOG(dialog));			 

  gtk_main();
}
