# 更新记录

## 1.0.6

解压之后只有一个文件夹和一个入口。Windows 双击 `ZhiLi.exe`，银河麒麟双击 `zhili.sh`。第一次运行在桌面建立带枝理图标的快捷方式。

- Windows 10／11 x64：`ZhiLi.exe` 和 `ZhiLi/`。入口 exe 带图标。快捷方式是桌面上的 `枝理.lnk`。
- 银河麒麟 V10 ARM64：`zhili.sh`（权限 0755）和 `zhili/`。桌面文件的 `Icon` 是包内 `zhili.png` 的绝对路径，并写 `StartupWMClass=ZhiLi`。
- 缺运行文件、缺图标、顶层多出名字或出现符号链接时，打包失败。
- 不提供 v1.0.5 到 v1.0.6 的增量包。顶层布局变了，旧更新器只对比里面的 `ZhiLi/` 目录。
- 不另打 Windows 7 包，也不打 macOS 包。Windows 包的子系统版本是 6.1，链接 `msvcrt.dll`。Qt 5.15 的支持平台包含 Windows 7。没有在 Windows 7 上实际开机。

编辑功能与 1.0.5 相同。
