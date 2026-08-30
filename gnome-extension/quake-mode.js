/*
 * mdnote Quake Mode -- window lifecycle/positioning logic.
 *
 * Adapted from Quake Terminal's quake-mode.js
 * (https://github.com/diegodario88/quake-terminal),
 * Copyright 2025 Diego Dario, GPL-3.0-or-later.
 * Modifications for mdnote: 2026.
 */

import Clutter from "gi://Clutter";
import GLib from "gi://GLib";
import Shell from "gi://Shell";
import Meta from "gi://Meta";
import * as Main from "resource:///org/gnome/shell/ui/main.js";

const STARTUP_TIMER_IN_SECONDS = 5;

/**
 * How long (seconds) to wait for `stage-views-changed` after the actor is
 * created before giving up and positioning the window anyway (CREATED_ACTOR
 * -> RUNNING fallback).
 */
const CREATED_ACTOR_TIMEOUT_IN_SECONDS = 5;

const ACTOR_NAME = "mdnote-quake";

const _DEBUG = false;

function _log(message) {
  if (_DEBUG) {
    console.log(message);
  }
}

/**
 * Quake Mode Module
 *
 * Manages the mdnote window's lifecycle: launching it, positioning/sizing
 * it on a screen edge, keeping it above other windows, and animating it
 * in and out like a dropdown terminal.
 */
export const QuakeMode = class {
  static LIFECYCLE = {
    READY: "READY",
    STARTING: "STARTING",
    CREATED_ACTOR: "CREATED_ACTOR",
    RUNNING: "RUNNING",
    DEAD: "DEAD",
  };

  /**
   * @param {Shell.App} app - The mdnote application instance.
   * @param {Gio.Settings} settings - The Gio.Settings object for configuration.
   */
  constructor(app, settings) {
    _log(`*** MdnoteQuake@constructor - App = ${app.get_name()} ***`);

    /** @type {Shell.App} */
    this._app = app;
    this._settings = settings;
    this._internalState = QuakeMode.LIFECYCLE.READY;

    this._sourceTimeoutLoopId = null;
    this._stageViewFallbackTimeoutId = null;
    this._windowUnmanagedId = null;
    this._windowFocusId = null;
    this._wmMapSignalId = null;
    this._appChangedId = null;
    this._actorStageViewChangedId = null;

    /** @type {Meta.Window} */
    this._appWindow = null;
    this._isTaskbarConfigured = null;

    // Tracked ourselves rather than read from appWindow.has_focus()/
    // is_hidden() at toggle time: this compositor session has repeatedly
    // shown stale/lagging state right after a cross-monitor move (see the
    // move_resize_frame() and actor.width staleness fixed elsewhere in
    // this file), and has_focus() lagging the same way after activation
    // made the window un-hideable. Our own state can't race like that.
    this._isShown = false;

    /** We will monkey-patch this method. Let's store the original one. */
    // @ts-ignore
    this._original_shouldAnimateActor = Main.wm._shouldAnimateActor;

    this._configureActorCloseAnimation();

    /** @type {number[]} */
    this._settingsWatchingListIds = [];

    ["vertical-margin-px", "horizontal-size", "horizontal-alignment"].forEach(
      (prefAdjustment) => {
        const settingsId = settings.connect(
          `changed::${prefAdjustment}`,
          () => {
            this._fitWindowToMainMonitor();
          }
        );
        this._settingsWatchingListIds.push(settingsId);
      }
    );

    const alwaysOnTopSettingsId = settings.connect(
      "changed::always-on-top",
      () => this._handleAlwaysOnTop()
    );
    this._settingsWatchingListIds.push(alwaysOnTopSettingsId);

    const skipTaskbarSettingsId = settings.connect(
      "changed::skip-taskbar",
      () => this._configureSkipTaskbarProperty()
    );
    this._settingsWatchingListIds.push(skipTaskbarSettingsId);
  }

  get appWindow() {
    if (!this._app) {
      return null;
    }

    if (!this._appWindow) {
      let ourWindow = this._app.get_windows().find((w) => {
        const actor = w.get_compositor_private();
        return actor && actor.get_name() === ACTOR_NAME && w.is_alive;
      });

      if (!ourWindow) {
        return null;
      }

      this._appWindow = ourWindow;
      if (!this._windowUnmanagedId) {
        this._windowUnmanagedId = this._appWindow.connect("unmanaged", () => {
          _log(`*** MdnoteQuake@Unmanaged - window went away ***`);
          this.destroy();
        });
      }
    }

    return this._appWindow;
  }

  get actor() {
    if (!this.appWindow) {
      return null;
    }

    /** @type {Meta.WindowActor & { ease: Function }} */
    const actor = this.appWindow.get_compositor_private();

    if (!actor) {
      return null;
    }

    if ("clip_y" in actor) {
      return actor;
    }

    Object.defineProperty(actor, "clip_y", {
      get() {
        return this.clip_rect.origin.y;
      },
      set(y) {
        const rect = this.clip_rect;
        this.set_clip(rect.origin.x, y, rect.size.width, rect.size.height);
      },
    });

    return actor;
  }

  get monitorDisplayScreenIndex() {
    if (this._settings.get_boolean("render-on-primary-monitor")) {
      return Shell.Global.get().display.get_primary_monitor();
    }

    const userSelectionDisplayIndex = this._settings.get_int("monitor-screen");
    const availableDisplaysIndexes =
      Shell.Global.get().display.get_n_monitors() - 1;

    if (
      userSelectionDisplayIndex >= 0 &&
      userSelectionDisplayIndex <= availableDisplaysIndexes
    ) {
      return userSelectionDisplayIndex;
    }

    // No monitor explicitly configured (monitor-screen left at its -1
    // "unset" default): follow the mouse via the display's current
    // monitor, matching ddterm's own default/fallback behavior, rather
    // than hand-rolling a pointer-to-monitor lookup (which needs
    // Mtk.Rectangle on recent mutter versions, not Meta.Rectangle --
    // geometry types moved to their own Mtk namespace).
    return Shell.Global.get().display.get_current_monitor();
  }

  destroy() {
    _log(`*** MdnoteQuake@destroy - Starting destroy action ***`);
    if (this._sourceTimeoutLoopId) {
      GLib.Source.remove(this._sourceTimeoutLoopId);
      this._sourceTimeoutLoopId = null;
    }

    if (this._stageViewFallbackTimeoutId) {
      GLib.Source.remove(this._stageViewFallbackTimeoutId);
      this._stageViewFallbackTimeoutId = null;
    }

    if (this._settingsWatchingListIds.length && this._settings) {
      this._settingsWatchingListIds.forEach((id) => {
        this._settings.disconnect(id);
      });
    }

    if (this.actor && this._actorStageViewChangedId) {
      this.actor.disconnect(this._actorStageViewChangedId);
      this._actorStageViewChangedId = null;
    }

    if (this._windowUnmanagedId && this.appWindow) {
      this.appWindow.disconnect(this._windowUnmanagedId);
      this._windowUnmanagedId = null;
    }

    if (this._appChangedId && this._app) {
      this._app.disconnect(this._appChangedId);
      this._appChangedId = null;
    }

    if (this._windowFocusId) {
      Shell.Global.get().display.disconnect(this._windowFocusId);
      this._windowFocusId = null;
    }

    if (this._wmMapSignalId) {
      Shell.Global.get().window_manager.disconnect(this._wmMapSignalId);
      this._wmMapSignalId = null;
    }

    this._settingsWatchingListIds = [];
    this._app = null;
    this._appWindow = null;
    this._internalState = QuakeMode.LIFECYCLE.DEAD;
    this._isTaskbarConfigured = null;
    // @ts-ignore
    Main.wm._shouldAnimateActor = this._original_shouldAnimateActor;
  }

  /**
   * Toggles the visibility of the mdnote window with animations.
   *
   * @returns {Promise<void>}
   */
  async toggle() {
    if (!this.appWindow) {
      try {
        await this._launchWindow();

        if (this._adjustWindowPosition()) {
          return;
        }
      } catch (error) {
        _log(`*** MdnoteQuake@toggle - Catch error ${error} ***`);
        this.destroy();
        return;
      }
    }

    if (!this._isTaskbarConfigured) {
      this._configureSkipTaskbarProperty();
    }

    if (this._isShown) {
      return this._hideWindowWithAnimationBottomUp();
    }

    this._fitWindowToMainMonitor();
    return this._showWindowWithAnimationTopDown();
  }

  /**
   * Launches mdnote and waits for its window to appear.
   *
   * @returns {Promise<boolean>}
   */
  _launchWindow() {
    this._internalState = QuakeMode.LIFECYCLE.STARTING;

    if (!this._app) {
      return Promise.reject(Error("mdnote Quake Mode - app is null"));
    }

    const info = this._app.get_app_info();
    _log(`*** MdnoteQuake@_launchWindow - launching ${info.get_name()} ***`);

    const promiseWindowInLessThanFiveSeconds = new Promise(
      (resolve, reject) => {
        const shellAppWindowsChangedHandler = () => {
          GLib.Source.remove(this._sourceTimeoutLoopId);
          this._sourceTimeoutLoopId = null;

          if (!this._app) {
            return reject(
              Error("mdnote Quake Mode - app reference was destroyed")
            );
          }

          if (this._internalState !== QuakeMode.LIFECYCLE.STARTING) {
            this._app.disconnect(this._appChangedId);
            return;
          }

          if (this._app.get_n_windows() < 1) {
            return reject(
              Error(`mdnote Quake Mode - app launched but has no windows`)
            );
          }

          const ourWindow = this._app.get_windows()[0];
          const actor = ourWindow.get_compositor_private();
          actor.set_name(ACTOR_NAME);
          this._appWindow = ourWindow;
          this._internalState = QuakeMode.LIFECYCLE.CREATED_ACTOR;

          this._configureSkipTaskbarProperty();
          this._handleAlwaysOnTop();

          this._windowUnmanagedId = this.appWindow.connect("unmanaged", () => {
            _log(`*** MdnoteQuake@Unmanaged ***`);
            this.destroy();
          });

          this._windowFocusId = Shell.Global.get().display.connect(
            "notify::focus-window",
            (source) => {
              this._handleHideOnFocusLoss(source).catch((e) =>
                _log(`*** MdnoteQuake@focus-window - ${e} ***`)
              );
            }
          );

          resolve(true);
        };

        this._appChangedId = this._app.connect(
          "windows-changed",
          shellAppWindowsChangedHandler
        );

        try {
          this._app.activate();
        } catch (e) {
          reject(e);
        }

        this._sourceTimeoutLoopId = GLib.timeout_add_seconds(
          GLib.PRIORITY_DEFAULT,
          STARTUP_TIMER_IN_SECONDS,
          () => {
            reject(
              Error(
                `mdnote Quake Mode - timeout after ${STARTUP_TIMER_IN_SECONDS}s waiting for mdnote to open`
              )
            );
            return GLib.SOURCE_REMOVE;
          }
        );
      }
    );

    return promiseWindowInLessThanFiveSeconds;
  }

  /**
   * Adjusts the window's initial position and wires up map/sizing signals.
   *
   * @returns {boolean} True when the window was advanced to RUNNING immediately.
   */
  _adjustWindowPosition() {
    if (!this.appWindow || !this.actor) {
      return false;
    }

    this.appWindow.stick();

    const advanceToRunning = () => {
      if (this._stageViewFallbackTimeoutId) {
        GLib.Source.remove(this._stageViewFallbackTimeoutId);
        this._stageViewFallbackTimeoutId = null;
      }
      if (this._actorStageViewChangedId && this.actor) {
        this.actor.disconnect(this._actorStageViewChangedId);
        this._actorStageViewChangedId = null;
      }
      if (this._wmMapSignalId) {
        Shell.Global.get().window_manager.disconnect(this._wmMapSignalId);
        this._wmMapSignalId = null;
      }
      if (this._internalState !== QuakeMode.LIFECYCLE.CREATED_ACTOR) return;
      this._internalState = QuakeMode.LIFECYCLE.RUNNING;
      this._fitWindowToMainMonitor();
      // A freshly-mapped window's very first move_resize_frame() request
      // can be ignored -- it races the Wayland client's own initial
      // configure/commit handshake. A second show (window already
      // mapped) always lands correctly, so re-apply once more a beat
      // later before the show animation actually starts, while the
      // actor is still at opacity 0 so nothing wrong is ever visible.
      GLib.timeout_add(GLib.PRIORITY_DEFAULT, 60, () => {
        if (this._internalState !== QuakeMode.LIFECYCLE.RUNNING) {
          return GLib.SOURCE_REMOVE;
        }
        this._fitWindowToMainMonitor();
        this._showWindowWithAnimationTopDown();
        return GLib.SOURCE_REMOVE;
      });
    };

    if (this.actor.is_mapped()) {
      // The `map` signal already fired before we could connect to it.
      this.actor.opacity = 0;
      Shell.Global.get().window_manager.emit("kill-window-effects", this.actor);
      advanceToRunning();
      return true;
    }

    const mapSignalHandler = (wm, metaWindowActor) => {
      if (metaWindowActor !== this.actor) {
        return;
      }
      this.actor.opacity = 0;
      Shell.Global.get().window_manager.disconnect(this._wmMapSignalId);
      this._wmMapSignalId = null;
      wm.emit("kill-window-effects", this.actor);
      this._actorStageViewChangedId = this.actor.connect(
        "stage-views-changed",
        () => advanceToRunning()
      );
    };

    this._wmMapSignalId = Shell.Global.get().window_manager.connect(
      "map",
      mapSignalHandler
    );

    this._stageViewFallbackTimeoutId = GLib.timeout_add_seconds(
      GLib.PRIORITY_DEFAULT,
      CREATED_ACTOR_TIMEOUT_IN_SECONDS,
      () => {
        this._stageViewFallbackTimeoutId = null;
        advanceToRunning();
        return GLib.SOURCE_REMOVE;
      }
    );

    return false;
  }

  _shouldAvoidAnimation() {
    return !this.actor;
  }

  _showWindowWithAnimationTopDown() {
    this._isShown = true;

    if (this._shouldAvoidAnimation()) {
      return;
    }

    const parent = this.actor.get_parent();
    if (!parent) {
      return;
    }

    parent.set_child_above_sibling(this.actor, null);
    this.actor.show();

    // Squash in from zero width, anchored at the right edge (the docked
    // edge), instead of translating the whole actor in from off-stage.
    // Translation bleeds visibly onto the neighboring monitor whenever
    // this monitor isn't the outermost one in the physical layout, since
    // Clutter's stage spans every monitor with no gap between adjacent
    // ones. A scale anchored at the actor's own edge stays within the
    // actor's already-correct on-screen bounds the whole time, so
    // there's nothing to bleed. Matches ddterm's own squash-style
    // show/hide animation.
    this.actor.set_pivot_point(1, 0);
    this.actor.scale_x = 0;
    // The cold-launch path (_adjustWindowPosition()) sets opacity 0 while
    // the window is still being positioned, so nothing wrong is visible
    // mid-setup -- restore it here since this scale-based animation
    // doesn't otherwise touch opacity at all.
    this.actor.opacity = 255;

    Main.wm.skipNextEffect(this.actor);
    Main.activateWindow(this.actor.meta_window);

    this.actor.ease({
      mode: Clutter.AnimationMode.EASE_IN_QUAD,
      scale_x: 1,
      duration: this._settings.get_int("animation-time"),
      onComplete: () => {
        this._isTransitioning = false;
      },
    });
  }

  _hideWindowWithAnimationBottomUp() {
    this._isShown = false;

    if (this._shouldAvoidAnimation()) {
      return;
    }

    this.actor.set_pivot_point(1, 0);

    this.actor.ease({
      mode: Clutter.AnimationMode.EASE_OUT_QUAD,
      scale_x: 0,
      duration: this._settings.get_int("animation-time"),
      onComplete: () => {
        Main.wm.skipNextEffect(this.actor);
        // meta_window.minimize() doesn't actually work for this window:
        // Wayland minimize is only ever a hint the client can ignore, and
        // mdnote doesn't act on it. Moving the real window off-screen via
        // move_resize_frame() doesn't work either -- mutter clamps window
        // positions back onto the visible desktop. Hiding the actor at
        // the Clutter/rendering layer sidesteps that clamp entirely --
        // the real window geometry never changes, so there's nothing for
        // mutter to "helpfully" correct.
        this.actor.hide();
        this.actor.scale_x = 1;
        // Hiding here only ever touches the Clutter actor (see above) --
        // the underlying Meta.Window is never unmapped, so mutter still
        // considers it focused even while invisible. A real client-side
        // hide would naturally hand focus to the next window; do that
        // explicitly here so things like Dash to Dock's "autohide unless
        // a window has focus" don't stay convinced something is focused
        // when nothing visible actually is.
        Shell.Global.get().display.focus_default_window(
          Shell.Global.get().get_current_time()
        );
      },
    });
  }

  _fitWindowToMainMonitor() {
    if (!this.appWindow) {
      return;
    }

    const monitorDisplayScreenIndex = this.monitorDisplayScreenIndex;
    const area = this.appWindow.get_work_area_for_monitor(
      monitorDisplayScreenIndex
    );

    const verticalMarginPx = this._settings.get_int("vertical-margin-px");
    const horizontalSettingsValue = this._settings.get_int("horizontal-size");
    const horizontalAlignmentSettingsValue = this._settings.get_int(
      "horizontal-alignment"
    );

    // Vertically centered rather than filling the work area top-to-bottom,
    // with an equal margin above and below -- computed from the margin
    // rather than applied directly as the Y offset, so it stays exactly
    // centered even if area.height is odd/undivisible by 2.
    const windowHeight = Math.max(100, area.height - verticalMarginPx * 2);
    const windowY = area.y + Math.round((area.height - windowHeight) / 2);
    const windowWidth = Math.round((horizontalSettingsValue * area.width) / 100);

    // 0 = left edge, 1 = right edge, 2 = centered.
    const windowX =
      area.x +
      Math.round(
        horizontalAlignmentSettingsValue &&
          (area.width - windowWidth) / horizontalAlignmentSettingsValue
      );

    this.appWindow.move_to_monitor(monitorDisplayScreenIndex);
    this.appWindow.move_resize_frame(false, windowX, windowY, windowWidth, windowHeight);
  }

  _configureSkipTaskbarProperty() {
    const appWindow = this.appWindow;
    if (!appWindow) {
      return;
    }
    const shouldSkipTaskbar = this._settings.get_boolean("skip-taskbar");

    // hide_from_window_list()/show_in_window_list() set the real C-level
    // skip-taskbar flag that shell-app.c's window tracking (and therefore
    // Dash to Dock's running-apps indicator, not just literal taskbars)
    // actually consults -- a JS-side override of is_skip_taskbar() never
    // reaches those C-internal callers.
    if (shouldSkipTaskbar) {
      appWindow.hide_from_window_list();
    } else {
      appWindow.show_in_window_list();
    }

    this._isTaskbarConfigured = true;
  }

  _configureActorCloseAnimation() {
    const self = this;

    // @ts-ignore
    Main.wm._shouldAnimateActor = function (actor, types) {
      const stack = new Error().stack;
      const forClosing = stack.includes("_destroyWindow@");

      if (!forClosing || actor !== self.actor) {
        return self._original_shouldAnimateActor.apply(this, [actor, types]);
      }

      const originalActorAnimate = actor.ease;

      actor.ease = function () {
        actor.ease = originalActorAnimate;
        actor.set_pivot_point(1, 0);

        originalActorAnimate.call(actor, {
          mode: Clutter.AnimationMode.EASE_OUT_QUAD,
          scale_x: 0,
          duration: self._settings.get_int("animation-time"),
          onComplete: () => {
            // @ts-ignore
            Main.wm._destroyWindowDone(Main.wm._shellwm, actor);
          },
        });
      };

      return true;
    };
  }

  /**
   * Hides mdnote when it loses focus (if auto-hide is enabled).
   *
   * @param {Meta.Display} source
   */
  async _handleHideOnFocusLoss(source) {
    const shouldAutoHide = this._settings.get_boolean("auto-hide-window");

    if (!shouldAutoHide || !source) {
      return;
    }

    if (source.focus_window === this.appWindow) {
      return;
    }

    this._hideWindowWithAnimationBottomUp();
  }

  _handleAlwaysOnTop() {
    const shouldAlwaysOnTop = this._settings.get_boolean("always-on-top");

    if (!shouldAlwaysOnTop && !this.appWindow.is_above()) {
      return;
    }

    if (!shouldAlwaysOnTop && this.appWindow.is_above()) {
      this.appWindow.unmake_above();
      return;
    }

    this.appWindow.make_above();
  }
};
