# 枝理 ZhiLi

枝理是一款纯离线的桌面思维导图编辑器。同一棵树可在经典左右导图和自上而下结构图之间切换。导图保存在本机，可以导出 PNG、SVG 或 PDF。

## 下载和启动

在 [GitHub Releases](https://github.com/bhuyuqingguo/ZhiLi/releases) 选择带 `-test` 的测试版。下载对应平台的应用压缩包。Release 中的 Source code 和 Qt 源码压缩包用于查阅或重新构建。

### Windows 10 / 11 x64

1. 下载 `ZhiLi-Windows-x64.zip`，右键选择「全部解压缩」。
2. 进入解压目录的 `ZhiLi` 文件夹，双击 `simplemap.exe`。请保留同目录 DLL 和 `platforms` 文件夹。
3. 软件是未签名的测试版，若单位电脑限制运行新程序，请让本机管理员按单位流程处理。可以先在 PowerShell 中执行 `Get-FileHash -Algorithm SHA256 .\ZhiLi-Windows-x64.zip`，与同名 `.sha256` 校验文件比对。

### 银河麒麟桌面 V10 SP1 / 飞腾 D2000 ARM64

测试包在 Ubuntu 20.04 ARM64 基线上编译，运行需要 glibc 2.31 或兼容版本，以及系统的 Qt5 Core、Gui、Widgets、Svg 和桌面平台插件。依赖齐备时，枝理可解压运行，无需安装或管理员权限。银河麒麟 D2000 仍需真机验收。

下载 `ZhiLi-Linux-ARM64-Portable.tar.gz` 和对应的 `.sha256` 文件，在下载目录打开终端，执行：

```sh
uname -m
sha256sum -c ZhiLi-Linux-ARM64-Portable.tar.gz.sha256
tar -xzf ZhiLi-Linux-ARM64-Portable.tar.gz
cd ZhiLi
./ZhiLi
```

`uname -m` 应显示 `aarch64`。若双击没有反应，从终端运行，或用 `ldd ./ZhiLi | grep 'not found'` 查看缺少的库。需要补齐依赖时，请使用本机麒麟软件源或单位批准的同架构离线包。若程序目录所在介质禁止执行，把整个 `ZhiLi` 文件夹复制到个人目录再启动。 [麒麟 V10 SP1 组件版本说明](https://kylinos.cn/upload/product/20230509/466b4da07a7220fc1442901e42b560a7.pdf)

## 第一次使用

1. 在「文件 → 打开」里选取解压目录中 `examples/研发项目交付.smap`，或新建空白导图。
2. 选择一个主题，按 **Tab** 添加子主题，按 **Enter** 添加同级主题。中心主题没有同级。
3. 双击主题或按 **F2** 原位编辑。编辑时 **Enter** 换行，**Ctrl+Enter** 确认，**Esc** 取消。使用中文输入法时先按输入法的方式确认候选，再确认节点编辑。
4. 拖到另一个主题中央可改为其子主题，拖到主题边缘可在同一层排序。按 **Space** 折叠或展开，按 **Delete** 删除整个选中分支。可用 **Ctrl+Z** 撤销，**Ctrl+Y** 重做。
5. 在工具栏切换「经典导图」和「结构图」，或用 **Ctrl+1 / Ctrl+2**。按 **Ctrl+0** 适应窗口。用状态栏加减按钮、**Ctrl+滚轮** 或 **Ctrl+= / Ctrl+-** 缩放。拖动空白区域平移。
6. 用 **Ctrl+F** 查找，**F3** 跳到下一个匹配。结果位于折叠分支内时，枝理会展开路径，该操作可撤销。
7. 用 **Ctrl+S** 保存，**Ctrl+Shift+S** 另存为。导出位于「文件」菜单。

方向键可移动选择。经典图的左右键切换父子、上下键切换同级。结构图的上下键切换父子、左右键切换同级。macOS 的 Qt Ctrl 快捷键显示为 ⌘。

## 字体、字号和分辨率

打开「视图 → 外观设置」，可分别调整界面和导图节点的字体、字号、常规或粗体，以及浅色或高对比配色。默认思源黑体，内置思源宋体。系统里已安装的其他字体也可选择。界面字号 9–22 pt，导图字号 9–32 pt，中心主题比节点设定字号大 4 pt。设置保存在本机。

小窗口切换到紧凑工具栏。布局和适应窗口仍可从工具栏或视图菜单使用。设置「适应窗口」后，画布会跟随窗口大小重新计算缩放；手动缩放后会保留该比例。内置字体来源及许可随源码和测试包提供。 [字体说明](https://github.com/bhuyuqingguo/ZhiLi/blob/main/assets/fonts/README.md)

## 文件、导出与恢复

- 原生文件为 `.smap`，内部使用版本化 JSON。可读取早期原型的 `.smap.json`。两种布局共享同一份节点数据，切换后保存时记录当前布局。
- PNG 适合直接插图，SVG 适合缩放和后期编辑，PDF 按单页 A4 导出。大图可先折叠分支，或使用矢量格式。SVG 在其他机器查看时，文字效果取决于当地字体。
- 如果发生异常退出，下次启动会在存在恢复快照时询问是否恢复。恢复内容通过另存为保存，原文件保留。卸载便携版时，退出程序并删除解压目录；已保存导图和个人设置位于各自的保存位置。

## 反馈测试结果

如果遇到问题，请在 [Issues](https://github.com/bhuyuqingguo/ZhiLi/issues) 写明测试 tag、操作系统和补丁版本、CPU 架构、屏幕分辨率及缩放比例、输入法、复现步骤和错误信息。提交导图样例前请去掉涉密或个人内容。真机重点检查中文候选确认、低分辨率与高 DPI、保存重开、跨平台往返和异常恢复。

## 从源码构建

依赖 C++11、CMake 3.13+ 与 Qt5 Widgets、Svg。启用测试还需 Qt Test。在 Windows 上使用与编译器架构匹配的 Qt5 SDK，可用 `-DCMAKE_PREFIX_PATH` 指向 Qt SDK。Qt 5.9+ 是源代码最低版本，便携包的工具链见发布说明。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
cd build
ctest -C Release --output-on-failure
```

[1.0 设计与验收](https://github.com/bhuyuqingguo/ZhiLi/blob/main/docs/V1_DESIGN.md) · [候选包构建与目标机验收](https://github.com/bhuyuqingguo/ZhiLi/blob/main/docs/RELEASE.md)
