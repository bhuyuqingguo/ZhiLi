# 离线模板实现与集成

## 内容和文件

- 80 个原创中文参考模板：活动组织、体系视图、能力视图、架构视图各 20 个，共 1,684 个可编辑节点。
- `assets/templates/catalog.json` 是目录，`assets/templates/files/*.smap` 是可直接打开的原生文件。
- `assets/templates.qrc` 把目录和文件编入程序，运行时不依赖安装目录写权限、Python、网络或外部数据。
- 内容说明、用途边界和完整目录见 `assets/templates/README.md`。

## API

`src/templates.h` 提供 `zhili::builtInTemplates()` 和 `zhili::loadBuiltInTemplate(id, document, error)`。

目录包含稳定 ID、分类、标题、说明、标签、节点数、推荐布局。返回的目录是独立副本。加载器先验证模板与目录一致，再为全部节点重新生成 UUID；失败不会更改目标文档，不接受任意文件路径。

`src/template_dialog.h` 提供 `QString zhili::chooseBuiltInTemplate(QWidget *parent = nullptr)`。

选择器支持分类、多关键词搜索、只读导图预览、原大查看和可读节点大纲。窄屏或低逻辑高度使用列表/预览切换；内容区域可滚动，确认和取消按钮始终固定在底部。接受时返回模板 ID，取消、Esc、关闭窗口均返回空字符串。选择器本身不触碰主窗口文档。

调用者应先显示选择器；取消直接返回。随后用自己的未保存文档确认流程处理离开，再把模板载入临时 `Document` 并通过新建文档路径原子替换。新建副本不保留原路径，应要求首次保存指定文件名。不要把取消选择视为新建成功，不要直接覆盖当前已保存文件。

## CMake 接线

在 `zhili_core` 之前运行：

```cmake
qt5_add_resources(zhili_template_resources assets/templates.qrc)
```

为现有 `zhili_core` 加入 `src/templates.cpp` 和 `${zhili_template_resources}`。该模块仅依赖 Qt Core。

```cmake
add_library(zhili_templates_ui STATIC src/template_dialog.cpp)
target_link_libraries(zhili_templates_ui PUBLIC zhili_core Qt5::Widgets)
```

把 `zhili_templates_ui` 加入 `simplemap` 和 `zhili_smoke` 的链接库。在 MSVC 下给新增 UI 和测试目标添加 `/utf-8`。

```cmake
add_executable(zhili_templates_test tests/templates_test.cpp)
target_link_libraries(zhili_templates_test PRIVATE zhili_core)
add_test(NAME zhili_templates COMMAND zhili_templates_test)

add_executable(zhili_template_dialog_test tests/template_dialog_test.cpp)
target_link_libraries(zhili_template_dialog_test PRIVATE zhili_templates_ui zhili_appearance Qt5::Test)
add_test(NAME zhili_template_dialog COMMAND zhili_template_dialog_test)
```

对话框测试应使用项目的平台设置，Windows 原生 QPA、Linux 无显示环境用 `offscreen`，设置 30 秒超时。另跑 `QT_SCALE_FACTOR=2;ZHILI_TEMPLATE_FONT_POINTS=22` 组合覆盖小逻辑屏幕和大字；测试会检查实际可用屏幕边界以及创建/取消按钮始终可达。

## 验证

- 全部 80 个目录条目可加载，类别计数、节点数、标题和推荐布局一致。
- 所有节点文本非空，未折叠，模板结构唯一；每次创建使用独立 UUID。
- JSON 和磁盘保存重开往返一致；两个布局中的全部节点无重叠。
- 无效 ID、路径伪装和空目标不改变既有文档；目录和文档修改不会污染内置内容。
- 对话框分类、中文多词搜索、标签搜索、无结果、恢复列表、确认、取消、Esc、重复打开和紧凑模式往返通过。
- 小屏大字测试同时检查对话框不超出屏幕、底部按钮不被裁切。

可设置 `ZHILI_TEMPLATE_SCREENSHOT=/absolute/path/template-picker.png` 输出渲染截图和 `.outline.png` 大纲截图。截图由测试实例的实际 Qt 对话框生成；它不是目标系统真机验收的替代。
