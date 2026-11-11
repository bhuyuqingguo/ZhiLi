# 便携包增量升级接口

`tools/zhili_update.py` 使用 Python 3.8+ 标准库，可在离线环境运行。Release 中同一工具以 `ZhiLi-v1.0.4-update.py` 提供。输入目录根部是 `simplemap.exe` 或 `ZhiLi`。Windows x64 和 Linux ARM64 各自使用对应的增量包，Linux ARM64 包应在 Linux 上应用，以保留执行权限。

## 生成增量包

以下以 `v1.0.3` 为基线、`v1.0.4` 为目标。使用同一平台的原始发布包解压目录，命令会输出增量包的 SHA-256。

```sh
python tools/zhili_update.py create --platform windows-x64 --from-version v1.0.3 --to-version v1.0.4 --base baseline/ZhiLi --target target/ZhiLi --output ZhiLi-v1.0.3-to-v1.0.4-Windows-x64.update.zip
```

ZIP 内的 `manifest.json` 使用 UTF-8 JSON。接口版本为 `zhili-update-v1`，包含平台、起止版本和两份文件清单。清单逐文件记录相对路径、字节数、SHA-256 和是否可执行。新增或变化的文件位于 `files/`，从目标清单移除的文件不进入升级结果。增量以文件为单位，变化的文件按完整内容打包。

## 校验并应用

从可信发布渠道取得增量包和对应 SHA-256。关闭枝理，然后将下面的 `PUBLISHED_SHA256` 替换为发布方提供的实际值。

```sh
python ZhiLi-v1.0.4-update.py apply --platform windows-x64 --from-version v1.0.3 --base baseline/ZhiLi --package ZhiLi-v1.0.3-to-v1.0.4-Windows-x64.update.zip --sha256 PUBLISHED_SHA256 --output ZhiLi-v1.0.4
```

工具先验证增量包及基线清单，再重建并校验全部目标文件。校验完成后在同一磁盘上发布到指定的新目录。基线目录保留，可继续使用；命令拒绝覆盖已有输出目录。自行放在旧程序目录里的导图仍留在原处，升级后的枝理可以直接打开。系统中的枝理个人设置按原位置读取。

该接口使用 SHA-256 验证完整性。更新来源由发布渠道提供，应用内的在线更新检查和自动下载不属于这个接口。
