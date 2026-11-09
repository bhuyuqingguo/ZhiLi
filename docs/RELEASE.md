# 枝理候选包构建与验收

GitHub Actions 在提交和拉取请求上执行两个独立任务，分别构建 Ubuntu 22.04 ARM64 和 Windows x64 候选包，运行离屏测试并保留安装包或压缩包及 SHA-256。Ubuntu 结果用于提前发现 ARM64 编译问题。麒麟候选包须以目标机或同发行版环境原生构建，并完成真机验收。工作流只保存 CI 产物。

## 银河麒麟 V10 / 飞腾 D2000 ARM64

在联网构建机或经批准的离线软件源中准备 `build-essential`、`cmake`（3.13 或以上）、`dpkg-dev`、`qtbase5-dev` 和 `libqt5svg5-dev`。使用目标系统仓库中的 Qt 版本。脚本会根据构建结果计算包的系统库依赖。

```bash
bash packaging/linux-arm64.sh 1.0~preview.1
```

候选包位于 `build-linux-arm64/dist/`。将 `.deb` 及校验文件复制到目标机，在该目录执行 `sha256sum -c zhili_1.0~preview.1_arm64.deb.sha256`。按 `dpkg-deb --info` 的 Depends 字段从目标系统的软件源准备同架构的依赖包，离线机先装缺失依赖，再执行以下命令。启动入口为应用菜单中的「枝理」。

```bash
sudo dpkg -i ./zhili_1.0~preview.1_arm64.deb
sudo dpkg -r zhili    # 验收卸载时执行
```

## Windows x64

准备 Visual Studio 2022 的 C++ 桌面开发工具、CMake 3.21 或以上、Qt 5.15.2 MSVC x64 SDK，然后从工程根目录运行。

```powershell
./packaging/windows-x64.ps1 -QtPrefix 'C:\Qt\5.15.2\msvc2019_64'
```

`build-windows-x64/dist/` 下的 ZIP 包含应用和 Qt 运行库。Windows 目标机需安装与构建工具兼容的 Microsoft Visual C++ v14 x64 运行时，可提前从 Microsoft 下载离线安装程序并随测试介质交付。将 ZIP 完整解压到本地目录，双击 `ZhiLi\simplemap.exe`。验收卸载时关闭程序并删除解压目录，用户文档和自动恢复目录分开管理。 [Microsoft 运行时下载说明](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist)

## 目标机验收

- 记录提交号、包校验值、CPU 架构、操作系统补丁版本、Qt 版本、图形会话和中文输入法。麒麟额外记录 `uname -m`、`cat /etc/os-release`、`qmake -v` 和 `fc-match sans:lang=zh`。
- 在断网、无开发 SDK 的机器上验证安装、启动、卸载。在非管理员账户下完成正常编辑。
- 在低分辨率和高 DPI 屏幕分别检查菜单、工具栏、对话框、画布和输入法候选窗。测试 100%、125%、150%、200% 缩放，以及不同缩放比例的屏幕之间移动窗口。
- 验证中文 IME 候选确认、节点换行、拖放、快捷键和撤销。对旧文件、新文件及中文路径执行跨平台往返，核对 PNG、SVG、PDF 中文输出。
- 用真实异常退出和保存失败模拟验证恢复和原文件保护。在 D2000 上记录 100 至 3000 节点的打开与布局时间、缩放和拖动帧耗时，并完成两小时稳定性测试。
- 对外发布前确认源码分发授权、Qt 再分发义务、运行库与品牌材料授权，以及 Windows 签名和最终安装介质。
