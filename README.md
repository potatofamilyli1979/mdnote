# mdnote

<p align="center"><a href="#english">English</a> · <a href="#简体中文">简体中文</a></p>

---

## English

A Yakuake-style, slide-out Markdown notebook for Linux. Press F10, jot something down, press F10 again — it's gone. Notes are plain `.md` files on disk; there's no database and no proprietary format.

![Normal mode, English UI](docs/screenshots/normal-mode-en.png)

### Features

- **Global F10 toggle** — slides in from the right edge, slides back out. Native `KGlobalAccel` binding on KDE Plasma; a companion GNOME Shell extension provides the same toggle under GNOME/Wayland, where ordinary clients can't register global shortcuts or force their own window placement.
- **Two editing modes** — source mode (plain Markdown text, syntax-highlighted) and normal mode (WYSIWYG rich text), switchable at any time. Saving always writes plain Markdown, via a serializer that walks the document directly to GFM Markdown — no external converter, no HTML intermediate.
- **Live theme switching** — nine built-in color presets plus the system default, applied instantly across the whole window.
- **Directory-based sidebar** — browse a folder of notes by all/recent/starred, with search, rename, and delete.
- **Multi-language UI** — English, Simplified Chinese, and Traditional Chinese, switched automatically by the system locale (see [Localization](#localization)).

### Dependencies

Package names confirmed on Debian testing:

```bash
sudo apt install build-essential cmake qt6-base-dev qt6-tools-dev \
  libkf6config-dev libkf6windowsystem-dev libkf6globalaccel-dev \
  libkf6coreaddons-dev libkf6dbusaddons-dev liblayershellqtinterface-dev
```

`qt6-wayland` is recommended at runtime on Wayland sessions.

### Build & install

No root needed to install into `~/.local` — just make sure `~/.local/bin` is on your `PATH`:

```bash
cmake -B build
cmake --build build -j$(nproc)
cmake --install build --prefix ~/.local
```

Run `kbuildsycoca6` afterward so KDE picks up the newly installed `org.kde.mdnote.desktop`.

### Autostart (recommended)

mdnote is meant to sit in the background waiting for F10, not be launched by hand each time — enabling it at login is the intended way to run it:

```bash
mkdir -p ~/.config/autostart
cp ~/.local/share/applications/org.kde.mdnote.desktop ~/.config/autostart/
echo "X-GNOME-Autostart-enabled=true" >> ~/.config/autostart/org.kde.mdnote.desktop
```

### Running

`~/.local/bin/mdnote` starts with no visible window, waiting for F10. On first run it registers the shortcut with `KGlobalAccel`; you can see it under Plasma's System Settings → Shortcuts → mdnote. Under GNOME, install the `gnome-shell-extension-mdnote-quake` package and enable it (`gnome-extensions enable mdnote-quake@localhost`, then log out and back in) for the same F10 toggle.

Single-instance behavior is handled by KDE Frameworks' `KDBusService`, which also registers `org.kde.mdnote` on the session bus.

### Localization

The UI's source language is English; Simplified and Traditional Chinese translations are bundled and selected automatically from the system locale (Simplified for `zh_Hans`/`zh_CN`/`zh_SG`-style locales, Traditional for `zh_Hant`/`zh_TW`/`zh_HK`), falling back to English for anything else. To force a specific language regardless of the system locale:

```bash
LANGUAGE=en_US LC_ALL=C mdnote      # force English
LANGUAGE=zh_CN LC_ALL=zh_CN.UTF-8 mdnote   # force Simplified Chinese
LANGUAGE=zh_TW LC_ALL=zh_TW.UTF-8 mdnote   # force Traditional Chinese
```

Translation sources live in `translations/*.ts` (Qt Linguist format); after adding or changing any UI string, run the `update_translations` CMake target to resync them:

```bash
cmake --build build --target update_translations
```

### Rich-text commands (normal mode)

The toolbar's Paragraph/Format menus (and the normal-mode right-click menu) are only enabled in normal mode, since they operate on rich-text formatting with no source-mode equivalent.

Implemented: headings 1–6, paragraph, promote/demote heading, blockquote, ordered/unordered lists, horizontal rule, code blocks, tables, bold/italic/underline/strikethrough/inline code, links, clear formatting.

Of the shortcuts shown in the menus, only Bold/Italic/Underline are currently wired to real key bindings (native `QTextEdit` behavior) — the rest are menu-only for now.

Not implemented (Qt's plain rich-text framework doesn't support these natively, or they'd need a dedicated rendering engine): interactive task-list checkboxes, LaTeX math blocks, admonition/callout boxes, footnotes, auto-generated table of contents, YAML front matter, comment syntax.

### Images & links (normal mode)

- Remote images (`![](http://...)`) load asynchronously, showing a placeholder box until they're ready. Local file paths resolve immediately via Qt's own resource loading.
- A plain click on a link moves the cursor into it (so you can edit the link text); **Ctrl+click** opens it in your default browser. Hovering while holding Ctrl shows a hand cursor and the link's target.

### Known limitations

- **No slide animation under Wayland** — KWin/mutter don't let a client freely move its own top-level window, so the window is pinned to the screen edge via `LayerShellQt` instead, and show/hide is an instant toggle rather than an animated transition. X11 sessions get a real position-slide animation.
- Opening a file uses Qt's own `QTextDocument::setMarkdown()`, which doesn't cover 100% of GFM (some extended syntax may not round-trip) — fine for everyday notes, but not a full CommonMark/GFM implementation.
- No dedicated settings UI yet beyond the theme picker — width ratio, default folder, and a few other options are config-file only (see below).

### Config file

`~/.config/mdnoterc`:

```ini
[General]
widthRatio=0.5
animationDurationMs=220
hideOnFocusLost=false
lastMode=normal
themeKey=

[Folders]
defaultFolder=/home/you/Documents/Notes
recentFolders=...

[Sidebar]
open=false
sortKey=mtime
sortAscending=false
```

### License

GPL-3.0-or-later — see [LICENSE](LICENSE). The GNOME Shell extension (`gnome-extension/quake-mode.js`) is adapted from [Quake Terminal](https://github.com/diegodario88/quake-terminal), used under the same license.

---

## 简体中文

Yakuake 风格、从右侧滑出的 Markdown 速记本。按 F10 呼出，随手记点东西，再按一下 F10 收起。笔记就是硬盘上普通的 `.md` 文件，没有数据库，没有私有格式。

![正常模式，中文界面](docs/screenshots/normal-mode-zh.png)

### 功能

- **全局 F10 呼出/收起** — 从屏幕右侧滑入滑出。KDE Plasma 下用原生 `KGlobalAccel` 绑定；GNOME/Wayland 下普通客户端既不能注册全局快捷键，也不能强制自己的窗口位置，所以配了一个同名的 GNOME Shell 扩展来实现同样的 F10 呼出。
- **源码/正常两种编辑模式** — 源码模式是带语法高亮的纯 Markdown 文本，正常模式是所见即所得的富文本，随时可以切换。保存时始终写出纯 Markdown，直接遍历文档结构序列化成 GFM Markdown，不依赖任何外部转换工具，也没有 HTML 中间环节。
- **实时主题切换** — 内置九套配色加系统默认，切换即时应用到整个窗口。
- **按目录浏览的侧边栏** — 全部/最近/收藏三个视图，支持搜索、重命名、删除。
- **多语言界面** — 英文、简体中文、繁体中文，跟随系统语言自动切换（见下方[多语言](#多语言)）。

### 依赖

Debian testing 上确认可用的包名：

```bash
sudo apt install build-essential cmake qt6-base-dev qt6-tools-dev \
  libkf6config-dev libkf6windowsystem-dev libkf6globalaccel-dev \
  libkf6coreaddons-dev libkf6dbusaddons-dev liblayershellqtinterface-dev
```

Wayland 会话下运行时建议装上 `qt6-wayland`。

### 构建 & 安装

装到 `~/.local` 不需要 root，记得把 `~/.local/bin` 加进 `PATH`：

```bash
cmake -B build
cmake --build build -j$(nproc)
cmake --install build --prefix ~/.local
```

装完跑一下 `kbuildsycoca6`，让 KDE 识别新装的 `org.kde.mdnote.desktop`。

### 开机自启（推荐）

mdnote 是常驻后台等 F10 的模式，不是"要用了再手动开"的程序，建议做成开机自启，登录后就一直在后台待命：

```bash
mkdir -p ~/.config/autostart
cp ~/.local/share/applications/org.kde.mdnote.desktop ~/.config/autostart/
echo "X-GNOME-Autostart-enabled=true" >> ~/.config/autostart/org.kde.mdnote.desktop
```

### 运行

`~/.local/bin/mdnote` 启动后不会显示窗口，在后台等 F10。第一次运行会向 `KGlobalAccel` 注册好快捷键，「系统设置→快捷键→mdnote」能看到。GNOME 下需要额外装 `gnome-shell-extension-mdnote-quake` 包并启用（`gnome-extensions enable mdnote-quake@localhost`，然后重新登录一次）才有同样的 F10 呼出。

单实例逻辑由 KDE Frameworks 的 `KDBusService` 负责，同时会在会话总线上注册 `org.kde.mdnote` 这个 D-Bus 服务名。

### 多语言

界面的源语言是英文，简体中文和繁体中文翻译内置在程序里，根据系统语言自动选择（`zh_Hans`/`zh_CN`/`zh_SG` 一类走简体，`zh_Hant`/`zh_TW`/`zh_HK` 一类走繁体），其他语言一律回落到英文。想在不改系统语言的情况下强制指定语言：

```bash
LANGUAGE=en_US LC_ALL=C mdnote             # 强制英文
LANGUAGE=zh_CN LC_ALL=zh_CN.UTF-8 mdnote   # 强制简体中文
LANGUAGE=zh_TW LC_ALL=zh_TW.UTF-8 mdnote   # 强制繁体中文
```

翻译源文件在 `translations/*.ts`（Qt Linguist 格式），改动或新增界面文字后跑一下 `update_translations` 这个 CMake target 重新同步：

```bash
cmake --build build --target update_translations
```

### 正常模式的富文本命令

工具栏的"段落""格式"下拉菜单（以及正常模式下的右键菜单）只在正常模式可用，因为它们直接操作富文本格式，源码模式下没有对应操作。

已实现：一~六级标题、段落、提升/降低标题级别、引用、有序/无序列表、水平分割线、代码块、表格、加粗/斜体/下划线/删除线/行内代码、超链接、清除样式。

菜单里显示的快捷键中，目前只有加粗/斜体/下划线是真的能按键触发的（`QTextEdit` 自带行为），其余的暂时只能点菜单触发。

没做的（Qt 原生富文本框架不支持，或者需要额外渲染引擎）：任务列表勾选框交互、LaTeX 公式块、警告框、脚注、自动生成目录、YAML Front Matter、注释语法。

### 图片与链接（正常模式）

- 网络图片（`![](http://...)`）异步加载，加载完成前是个占位框；本地文件路径的图片走 Qt 自带的资源解析，立即显示。
- 单击链接是把光标移进去，方便编辑链接文字；**按住 Ctrl 点击**才会用默认浏览器打开。鼠标悬停时按住 Ctrl 会看到指针变成手型并显示链接地址。

### 已知限制

- **Wayland 下没有滑动动画** — KWin/mutter 不允许客户端自由移动自己的顶层窗口，所以用 `LayerShellQt` 把窗口锚定在屏幕边缘，呼出/收起是直接显示/隐藏，没有过渡动画。X11 会话下是真正的位移滑动动画。
- 打开文件用的是 Qt 自带的 `QTextDocument::setMarkdown()`，没有 100% 覆盖 GFM 扩展语法，日常笔记场景够用，但不是完整的 CommonMark/GFM 实现。
- 除了主题选择，暂时没有独立的设置界面 — 窗口宽度比例、默认文件夹等选项目前只能改配置文件（见下）。

### 配置文件

`~/.config/mdnoterc`：

```ini
[General]
widthRatio=0.5
animationDurationMs=220
hideOnFocusLost=false
lastMode=normal
themeKey=

[Folders]
defaultFolder=/home/you/Documents/Notes
recentFolders=...

[Sidebar]
open=false
sortKey=mtime
sortAscending=false
```

### 许可证

GPL-3.0-or-later，见 [LICENSE](LICENSE)。GNOME Shell 扩展部分（`gnome-extension/quake-mode.js`）改写自 [Quake Terminal](https://github.com/diegodario88/quake-terminal)，沿用同一许可证。
