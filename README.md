# mdnote

Yakuake 风格、从右侧滑出的 Markdown 速记本。F10 呼出/收起，源码模式（纯 Markdown 文本）与正常模式（富文本，基于 Qt 原生的 `setMarkdown`/`toMarkdown` 双向转换）之间切换，笔记就是普通的 `.md` 文件。

## 依赖（Debian testing 已确认可用的包名）

```bash
sudo apt install build-essential cmake qt6-base-dev \
  libkf6config-dev libkf6windowsystem-dev libkf6globalaccel-dev \
  libkf6coreaddons-dev libkf6dbusaddons-dev liblayershellqtinterface-dev \
  pandoc
```

`pandoc` is a runtime-optional dependency (see [保存 markdown 的转换管线](#保存-markdown-的转换管线-语义化-html--pandoc) below) — the app builds and runs without it, saving just falls back to Qt's own (occasionally buggy) markdown writer.

## 构建 & 安装

装到 `~/.local` 不需要 root，`~/.local/bin` 记得加入 PATH：

```bash
cmake -B build
cmake --build build -j$(nproc)
cmake --install build --prefix ~/.local
```

安装后跑一下 `kbuildsycoca6` 让 KDE 识别新装的 `org.kde.mdnote.desktop`。

## 开机自启（推荐）

mdnote 是常驻后台等 F10 的模式,不是"要用了再手动开"的程序,所以建议做成开机自启,登录后就一直在后台待命：

```bash
mkdir -p ~/.config/autostart
cp ~/.local/share/applications/org.kde.mdnote.desktop ~/.config/autostart/
echo "X-GNOME-Autostart-enabled=true" >> ~/.config/autostart/org.kde.mdnote.desktop
```

## 运行 & F10

`~/.local/bin/mdnote` 启动后不会显示窗口,在后台等 F10。第一次运行会在 kglobalaccel 里注册好 F10,「系统设置→快捷键→mdnote」能看到。

**踩坑记录（如果你也遇到"F10 在系统设置里显示已绑定,但按了没反应"）**：根因是 `HotkeyManager` 里给 `QAction` 设置了一个 `isConfigurationAction` 属性 —— 这个属性名会被 kglobalaccel 识别为"这只是设置界面里占位用的配置动作,不是真的要触发的全局快捷键",于是快捷键会被正常记录/在设置里显示,但 kglobalaccel **永远不会把它标记为"活跃"、也永远不会真的把按键事件投递给它**。删掉那一行属性设置后就正常了。如果你以后自己加新的全局快捷键 `QAction`,不要加这个属性。

另外把单实例逻辑从手写的 `QLocalSocket`/`QLocalServer` 换成了 KDE Frameworks 的 `KDBusService`,这样才会在会话总线上注册 `org.kde.mdnote` 这个正式的 D-Bus 服务名,同时也顺带修掉了启动时那条 "Failed to register with host portal ... App info not found" 的警告。

## 正常模式的富文本命令（段落 / 格式 菜单）

工具栏的"段落""格式"两个按钮下拉菜单，只在正常（预览）模式下可用，源码模式下会置灰——这些命令直接操作富文本的 `QTextCharFormat`/`QTextBlockFormat`，在纯文本源码里没有对应操作。

已实现：一~六级标题、段落、提升/降低标题级别、引用、有序/无序列表、水平分割线、代码块、表格、加粗/斜体/下划线/删除线/行内代码、超链接、清除样式。

菜单里显示的快捷键（Ctrl+1、Ctrl+B 之类）目前只有加粗/斜体/下划线是真的能按键触发的——那是 `QTextEdit` 自带的原生行为，不是我们注册的。其余的快捷键文字目前只是仿 Typora 菜单画的提示文本，还没有真正接上全局按键，需要点菜单才能触发。如果这一点体验上觉得别扭，之后可以把这些也接成真正可用的快捷键。

没做的（Qt 的纯文本框架原生不支持,或者需要额外渲染引擎才能做，工作量明显更大）：任务列表（勾选框交互）、公式块（LaTeX 渲染）、警告框、代码工具子菜单、链接引用、脚注、内容目录自动生成、YAML Front Matter、注释语法。这些 Typora 有而这里暂时没做。

## 图片与链接（正常模式）

- 网络图片（`![](http://...)`）现在会异步下载后显示，加载完成前是个占位框。本地文件路径的图片走 Qt 自带的本地资源解析，不受影响。
- 链接默认单击是把光标移进去（编辑态文本框的标准行为，方便你点进链接文字修改），**按住 Ctrl 再点击**才会用系统默认浏览器打开链接。鼠标悬停时按住 Ctrl 会看到指针变成手型、并显示链接地址的提示。

## 保存 markdown 的转换管线（语义化 HTML + pandoc）

正常模式保存/切回源码模式时，不再直接用 `QTextDocument::toMarkdown()`。原因是实测出的两个真 bug：代码块紧跟表格会在一次转换里互相污染，空表格会导出成裸的 `||||`。查证过程见下，结论是：

1. `QTextDocument::toHtml()` 生成的是"所见即所得编辑器"风格的 HTML（加粗用内联 `style="font-weight:700"`、表头就是普通 `<td>`、代码块是 `<pre><span style=...>`），标准的 HTML→Markdown 转换器认不出这些非语义化标签。
2. 试过 [html2md](https://github.com/tim-gromeyer/html2md)，即使喂给它干净的语义化 HTML，表格还是会丢列——这是它自己表格解析逻辑的 bug，换输入救不了。
3. [`QBasicHtmlExporter`](https://gitlab.com/Open-App-Library/QBasicHtmlExporter)（2019 年的老项目，未指定协议）思路是对的，但直接用有顾虑（license 不明确、Qt6 下编译不过、标题/代码块标签也有问题），所以没有直接拿它的代码，而是照着"QTextDocument → 语义化 HTML"这个思路自己写了一个更小的：[`SemanticHtmlExporter.cpp`](src/SemanticHtmlExporter.cpp)，只覆盖这个 app 实际会用到的格式（标题、粗斜体/删除线/行内代码、链接、图片、引用、有序/无序列表、分割线、表格、代码块）。
4. 最后一棒交给 `pandoc`（`-f html -t gfm`，`QProcess` 子进程调用），实测过表格、代码块围栏、粗体/斜体/删除线、引用、列表、分割线、链接，round-trip 两次输出完全一致（不再像 Qt 自带那样每转一次就多损坏一点）。

如果启动时找不到 `pandoc`（`QStandardPaths::findExecutable`），会自动退回到 `QTextDocument::toMarkdown()`——功能不受影响，只是又暴露在原来那两个 bug 面前。

## 已知限制 / 后续可以打磨的点

- **Wayland 下没有真正的位移滑动动画**：KWin Wayland 不允许客户端自由摆放/移动自己的顶层窗口，所以用了 `LayerShellQt` 把窗口锚定在屏幕右侧（上下贴边、右边贴边）。呼出/收起目前是**直接显示/隐藏**（没有过渡动画——最初想做透明度淡入淡出，但这个 QPA 插件不支持 `setWindowOpacity()`，日志会打「This plugin does not support setting window opacity」，动画自然也就没效果）。X11 会话下走的是另一条路径，是真正的位移滑动动画。如果想要 Wayland 下也有过渡效果，大概率得改成动画 `LayerShellQt::Window` 的尺寸（比如宽度从 0 展开）而不是透明度。
- 打开文件（markdown → 富文本）依然用的是 Qt `QTextDocument::setMarkdown()`，不是 100% 覆盖 GFM（比如脚注、部分扩展语法可能丢失），日常笔记场景应该够用；保存这一头已经换成了上面说的 语义化 HTML + pandoc 管线。
- 历史文档列表目前直接按文件夹里 `.md` 文件的修改时间排序,没有单独维护"最近打开"的索引 — 更简单可靠，但如果你想要的是"最近打开"而不是"最近修改"的顺序,需要另外加一个小索引。
- 侧边栏折叠、失焦自动隐藏(`hideOnFocusLost`)选项在 `ConfigManager` 里已经留了接口,但还没有接到设置界面上,目前只能改 `~/.config/mdnoterc` 里的值。
- 还没有设置界面(宽度比例、默认文件夹等目前只能手改 `~/.config/mdnoterc`)。

## 配置文件

`~/.config/mdnoterc`:

```ini
[General]
widthRatio=0.5
animationDurationMs=220
hideOnFocusLost=false
lastMode=normal

[Folders]
defaultFolder=/home/you/Documents/Notes
recentFolders=...
```
