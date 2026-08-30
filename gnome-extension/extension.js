/*
 * mdnote Quake Mode for GNOME Shell 45+
 *
 * Pins the mdnote sliding notebook (https://github.com -- see the main
 * mdnote project) to a screen edge and gives it a Yakuake-style dropdown
 * animation on GNOME/mutter, which has no wlr-layer-shell support and no
 * client-side "always on top" -- capabilities only the shell itself has
 * for windows it doesn't own.
 *
 * Adapted from Quake Terminal (https://github.com/diegodario88/quake-terminal),
 * Copyright 2025 Diego Dario, used and modified here under the terms
 * below with permission of its GPL-3.0-or-later license.
 * Modifications for mdnote: 2026.
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

import Meta from "gi://Meta";
import Shell from "gi://Shell";
import {
  Extension,
  gettext as _,
} from "resource:///org/gnome/shell/extensions/extension.js";
import * as Main from "resource:///org/gnome/shell/ui/main.js";
import { QuakeMode } from "./quake-mode.js";

export default class MdnoteQuakeExtension extends Extension {
  enable() {
    // Explicit domain (rather than the UUID default, which contains
    // "@"/"." and isn't a normal gettext catalog filename) -- looked up
    // first in this extension's own bundled locale/ directory, matching
    // the .mo files installed alongside extension.js.
    this.initTranslations("mdnote-quake");

    this._settings = this.getSettings();
    this._appSystem = Shell.AppSystem.get_default();
    this._quakeMode = null;

    Main.wm.addKeybinding(
      "toggle-shortcut",
      this._settings,
      Meta.KeyBindingFlags.IGNORE_AUTOREPEAT,
      Shell.ActionMode.NORMAL |
        Shell.ActionMode.OVERVIEW |
        Shell.ActionMode.POPUP,
      () =>
        this._handleToggle().catch((reason) =>
          // The dynamic part (reason) stays outside the translated
          // string -- a template literal with ${} interpolation baked
          // in isn't something xgettext's JS extraction can pick up as
          // a translatable, matchable msgid.
          Main.notify("mdnote-quake", `${_("Error:")} ${reason}`)
        )
    );
  }

  disable() {
    Main.wm.removeKeybinding("toggle-shortcut");

    if (this._quakeMode) {
      this._quakeMode.destroy();
    }

    this._settings = null;
    this._appSystem = null;
    this._quakeMode = null;
  }

  _handleToggle() {
    if (this._quakeMode) {
      if (
        this._quakeMode._internalState === QuakeMode.LIFECYCLE.STARTING ||
        this._quakeMode._internalState === QuakeMode.LIFECYCLE.CREATED_ACTOR
      ) {
        return Promise.resolve();
      }
    }

    if (
      !this._quakeMode ||
      this._quakeMode._internalState === QuakeMode.LIFECYCLE.DEAD
    ) {
      const appId = this._settings.get_string("app-id");

      if (!appId) {
        Main.notify(_("No app-id configured for mdnote Quake Mode."));
        return Promise.resolve();
      }

      const app = this._appSystem.lookup_app(appId);

      if (!app) {
        Main.notify(
          `${_("mdnote Quake Mode: no application found with id:")} ${appId}.`
        );
        return Promise.resolve();
      }

      this._quakeMode = new QuakeMode(app, this._settings);
      return this._quakeMode.toggle();
    }

    return this._quakeMode.toggle();
  }
}
