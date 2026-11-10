# 枝理发布包构建与验收

正式 tag 形如 `v1.0.0`，发布说明放在 `docs/releases/<tag>.md`。测试 tag 形如 `v1.0.0-test.1`，对应预发布。GitHub Actions 并行构建 ARM64 和 Windows x64，运行回归测试并生成 SHA-256。两个平台通过测试且下载包校验一致后才发布。ARM64 使用 Ubuntu 20.04 构建容器，实际兼容性须在目标机验收。

从已发布的同平台便携包生成更新时，可调用[离线增量升级接口](INCREMENTAL_UPDATE.md)。

## Linux ARM64 免安装包

在原生 ARM64 构建机准备 `build-essential`、`cmake`、`file`、`qtbase5-dev` 和 `libqt5svg5-dev`。构建目录需可写。脚本生成 tar.gz 和 SHA-256，运行端依赖系统 Qt5 和 glibc。

```bash
bash packaging/linux-arm64-portable.sh
```

输出位于 `build-linux-arm64-portable/dist/`。解压进入 `ZhiLi` 目录并执行 `./ZhiLi`。如需在目标发行版构建可安装的 deb，仍可运行 `bash packaging/linux-arm64.sh 1.0~preview.1`。

## Windows x64 免安装包

准备 Visual Studio 2022 C++ 桌面开发工具、CMake 3.21+、Qt 5.15.2 MSVC x64 SDK，并从对应 Qt Base 源码包提取 `LICENSE.LGPL3` 和 `LICENSE.GPL3` 至 `build-windows-x64/qt-license-notices/`。然后运行：

```powershell
./packaging/windows-x64.ps1 -QtPrefix 'C:\Qt\5.15.2\msvc2019_64'
```

输出位于 `build-windows-x64/dist/`。解压后运行 `ZhiLi\simplemap.exe`。Qt 和 VC v14 x64 运行库随程序目录部署，Windows 10 / 11 提供 UCRT。发布 ZIP 时同时提供对应 Qt Base 和 Qt SVG 源码及许可。 [Microsoft 应用本地部署说明](https://learn.microsoft.com/en-us/cpp/windows/choosing-a-deployment-method) · [Qt LGPL 分发说明](https://www.qt.io/development/open-source-lgpl-obligations)

## 目标机验收

- 记录提交号、包校验值、CPU 架构、操作系统补丁版本、Qt 版本、图形会话和中文输入法。麒麟额外记录 `uname -m`、`cat /etc/os-release`、`qmake -v` 和 `fc-match sans:lang=zh`。
- 在断网、无开发 SDK 的机器上验证安装、启动、卸载。在非管理员账户下完成正常编辑。
- 在低分辨率和高 DPI 屏幕分别检查菜单、工具栏、对话框、画布和输入法候选窗。测试 100%、125%、150%、200% 缩放，以及不同缩放比例的屏幕之间移动窗口。
- 验证中文 IME 候选确认、节点换行、拖放、快捷键和撤销。对旧文件、新文件及中文路径执行跨平台往返，核对 PNG、SVG、PDF 中文输出。
- 用真实异常退出和保存失败模拟验证恢复和原文件保护。在 D2000 上记录 100 至 3000 节点的打开与布局时间、缩放和拖动帧耗时，并完成两小时稳定性测试。
- 对外发布前确认源码分发授权、Qt 再分发义务、运行库与品牌材料授权，以及 Windows 签名和最终安装介质。
