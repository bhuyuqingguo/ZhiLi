# 枝理离线素材库

128 个原创 SVG，分为 8 类，每类 16 个：空中平台、水面平台、地面平台、水下平台、感知与测量、指挥与协同、通信与网络、保障与设施。用于活动组织、体系视图、能力视图与架构表达。图形是通用示意，不代表装备型号、技术参数、标准军用符号或运行方案。

- 所有 SVG 是 64×64 的无背景矢量图，统一 2.2 单位线宽；不依赖字体、图片、网络、脚本或远程样式。
- 小图标建议 32 逻辑像素；24 像素仍保留基本轮廓。素材选择器支持 32–64 像素缩略图，画布和导出使用矢量绘制。
- `catalog.json` 是可读的来源与分类清单，包含稳定 ID、中英文名称、关键词、资源路径。
- `index.html` 是可以直接离线打开的完整图集。
- 来源：本项目原创的几何绘制，生成源代码为 `tools/generate_symbols.py`；无下载图标或第三方图稿。
- 图稿、目录与图集按 CC0-1.0 提供，见 `LICENSE.txt`。软件代码沿用项目自身许可约定。

## 构建与更新

可以将环境变量 `ZHILI_SYMBOL_REVIEW_DIR` 指向一个输出目录来运行 `zhili_symbols_test`，生成图集和选择器截图；`ZHILI_SYMBOL_FONT_FILE` 可指定内置中文字体路径供截图使用。

Qt 将整个目录通过 `assets/symbols.qrc` 编入程序。运行时不需要 Python、额外目录或联网。新增图稿时编辑生成器，再运行：

```sh
python3 tools/generate_symbols.py
cmake -S . -B build
cmake --build build
cd build && ctest --output-on-failure -R zhili_symbols
```

生成器是确定性的，不添加时间戳。现有 ID 不应随显示名称调整而改变，保证文档跨版本兼容。

## 编辑器接入

`SymbolLibrary::assets()` / `categories()` 返回只读全量目录。`find(id)` 返回生命周期稳定的指针，找不到时为 `nullptr`。`search(query, categoryId)` 支持大小写不敏感的中英文搜索，多词以空格分隔时做 AND 匹配。可使用 `QSvgRenderer(asset->resourcePath)` ；直接用 SVG 路径构造 `QIcon` 时需额外部署 Qt SVG 图标插件。素材选择器已自行用 `QSvgRenderer` 生成多分辨率图标，不依赖该插件。

`SymbolPickerDialog(parent, initialId)` 是标准 QDialog。仅在返回 `Accepted` 后读取 `selectedSymbolId()` 并提交一次可撤销编辑；空 ID 表示移除。`Rejected`（取消或 Esc）不得修改文档。关闭窗口不应用未确认的选择。

文档应保存 `symbolId` 字符串，而不是本地文件名或 SVG 字节。旧客户端不认识的 ID 应继续保留；没有对应图稿时不绘制图标，避免读写丢失未来素材。
