/** \file
 *
 *  \brief GTK+ 2 "About" window.
 *
 *  \copyright Copyright 2024 Ciaran Anscomb
 *
 *  \licenseblock This file is part of XRoar, a Dragon/Tandy CoCo emulator.
 *
 *  XRoar is free software; you can redistribute it and/or modify it under the
 *  terms of the GNU General Public License as published by the Free Software
 *  Foundation, either version 3 of the License, or (at your option) any later
 *  version.
 *
 *  See COPYING.GPL for redistribution conditions.
 *
 *  \endlicenseblock
 */

#include "top-config.h"

#include <stdlib.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <gtk/gtk.h>
#pragma GCC diagnostic pop

#include "gtk2/common.h"

static void close_about(GtkDialog *dialog, gint response_id, gpointer user_data) {
	(void)response_id;
	(void)user_data;
	gtk_widget_hide(GTK_WIDGET(dialog));
	gtk_widget_destroy(GTK_WIDGET(dialog));
}

void gtk2_create_about_window(struct ui_gtk2_interface *uigtk2) {
	// Create icon pixbuf from resource
	GdkPixbuf *logo_pixbuf = NULL;
	GError *error = NULL;
	GBytes *logo_bytes = g_resources_lookup_data("/uk/org/6809/xroar/gtk2/xroar-48x48.raw", 0, &error);
	if (logo_bytes) {
		logo_pixbuf = gdk_pixbuf_new_from_bytes(logo_bytes, GDK_COLORSPACE_RGB, 1, 8, 48, 48, 192);
		g_bytes_unref(logo_bytes);
	}

	// Create the dialog
	GtkAboutDialog *dialog = (GtkAboutDialog *)gtk_about_dialog_new();
	if (logo_pixbuf) {
		gtk_about_dialog_set_logo(dialog, logo_pixbuf);
		g_object_unref(logo_pixbuf);
	}
	gtk_about_dialog_set_version(dialog, VERSION);
	gtk_about_dialog_set_copyright(dialog, "Copyright © " PACKAGE_YEAR " Ciaran Anscomb <xroar@6809.org.uk>");
	gtk_about_dialog_set_license(dialog,
"XRoar is free software; you can redistribute it and/or modify it under\n"
"the terms of the GNU General Public License as published by the Free Free\n"
"Software Foundation, either version 3 of the License, or (at your option)\n"
"any later version.\n"
"\n"
"XRoar is distributed in the hope that it will be useful, but WITHOUT\n"
"ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or\n"
"FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License\n"
"for more details.\n"
"\n"
"You should have received a copy of the GNU General Public License along\n"
"with XRoar.  If not, see <https://www.gnu.org/licenses/>."
	);
	gtk_about_dialog_set_website(dialog, "https://www.6809.org.uk/xroar/");
	gtk_about_dialog_set_website_label(dialog, "https://www.6809.org.uk/xroar/");
	g_signal_connect(dialog, "response", G_CALLBACK(close_about), uigtk2);
	gtk_widget_show(GTK_WIDGET(dialog));
}
