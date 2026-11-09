# 枝理 / ZhiLi

枝理是纯离线的桌面思维导图编辑器。中心主题下的同一棵树可以在经典左右导图和自上而下结构图之间切换。当前工作区为 **1.0 设计验收版**，首发目标是银河麒麟桌面 V10 / 飞腾 D2000 ARM64，另支持 Windows x64 构建验证。

## 体验

- 双击主题或按 F2 原位编辑。Enter 换行，Ctrl+Enter 确认，Esc 取消。macOS 上的 Qt Ctrl 快捷键显示为 ⌘。
- 在画布上按 Tab 新增子级、Enter 新增同级、Space 折叠、Delete 删除分支。拖到目标主题中心可移入子级，拖到边缘可排到目标前后。
- Ctrl+1 / Ctrl+2 切换布局，Ctrl+0 适应窗口。用状态栏的 + / −、Ctrl+= / Ctrl+- 或 Ctrl+滚轮缩放。拖动空白区域平移。
- 用方向键选择主题。经典图左右切换父子、上下切换同级；结构图上下切换父子、左右切换同级。
- 窗口较窄时，工具栏保留保存、添加子级、布局菜单和适应窗口。系统 DPI 与画布缩放分别处理。
- Ctrl+F 查找主题，F3 跳到下一项。匹配在折叠分支内时会展开路径，该展开可以撤销。
- 文件菜单提供新建、打开、保存、另存为和 PNG、SVG、PDF 导出。意外关闭后，下次打开可恢复最近的本地快照。恢复内容会另存，原文件保留。

## 字体与外观

在「视图 → 外观设置」独立调整界面和导图主题的字体、字号及常规／粗体，并切换浅色或高对比配色。设置保存在本机。内置思源黑体和思源宋体简体中文版本，无需安装到系统。可选用已安装的其他字体；同一份导图在另一台机器打开时会使用那台机器的外观设置。

两套内置字体由 Adobe 发布，按 SIL Open Font License 1.1 随应用分发。源版本、校验值和许可见 [字体说明](assets/fonts/README.md)。

## 构建

以下命令使用 CMake 3.13+、C++11、Qt 5.9+ Widgets / Svg。启用自动化测试时还需要 Qt Test。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

麒麟原生构建需要本机仓库对应的编译工具链和 Qt 开发包，通常为 `build-essential cmake qtbase5-dev libqt5svg5-dev`。构建后运行 `./build/simplemap`。若 CMake 早于 3.20，请在构建目录内执行 `ctest`。

Windows 使用匹配编译器与架构的 Qt5 SDK，并通过 `-DCMAKE_PREFIX_PATH=...` 指向 SDK。多配置生成器的程序一般位于 `build/Release/simplemap.exe`。macOS 在构建目录生成 `simplemap.app`，安装预览包需另外嵌入 Qt 运行库。

## 设计和验收记录

参见 [1.0 设计与验收](docs/V1_DESIGN.md) 和 [候选包构建与目标机验收](docs/RELEASE.md)。
