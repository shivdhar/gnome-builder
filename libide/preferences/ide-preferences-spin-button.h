/* ide-preferences-spin-button.h
 *
 * Copyright (C) 2015 Christian Hergert <chergert@redhat.com>
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

#ifndef IDE_PREFERENCES_SPIN_BUTTON_H
#define IDE_PREFERENCES_SPIN_BUTTON_H

#include <gtk/gtk.h>

#include "ide-preferences-bin.h"

G_BEGIN_DECLS

#define IDE_TYPE_PREFERENCES_SPIN_BUTTON (ide_preferences_spin_button_get_type())

G_DECLARE_FINAL_TYPE (IdePreferencesSpinButton, ide_preferences_spin_button, IDE, PREFERENCES_SPIN_BUTTON, IdePreferencesBin)

GtkWidget *ide_preferences_spin_button_get_spin_button (IdePreferencesSpinButton *self);

G_END_DECLS

#endif /* IDE_PREFERENCES_SPIN_BUTTON_H */
