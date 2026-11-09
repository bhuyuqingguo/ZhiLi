# SimpleMap MVP-01 — Qt5 原型源码

目标：银河麒麟桌面 V10 / 飞腾 D2000 ARM64，兼顾 Windows x64。C++11 / Qt5 Widgets / QtSvg。

## 已实现的原型功能
- 双布局：经典左右思维导图、层级自上而下结构图（Ctrl+1 / Ctrl+2）
- 节点新增子级（Tab）、新增同级（Enter）、编辑（F2）、删除（Del）、折叠（Space）
- 撤销/重做（Ctrl+Z / Ctrl+Y）；打开（Ctrl+O）、保存（Ctrl+S）、另存为
- 原子保存 `.smap.json`（实验格式）、导出 PNG / SVG、Ctrl+鼠标滚轮缩放、空白拖动画布
- 读取 JSON 时做格式版本、节点数、结构环与重复引用校验

## 已知限制（重要）
- 此交付为**源码原型，尚未在目标麒麟 D2000 或 Windows 设备编译/运行验证**，不能宣称已完成兼容认证。
- 双击直接编辑、鼠标拖拽节点重排、自动保存恢复、节点备注、关联线、XMind/MindManager/MindMaster 文件转换尚未实现。
- 当前编辑使用 Qt 输入对话框，能够调用系统输入法，但需要在真机测试中文输入。
- `.smap.json` 是开发阶段的中间格式，并非最终 ZIP 封装 `.smap`。
- 结构图树较宽时需要继续优化布局算法；当前采用保守简单布局。
- Qt5 的授权、动态链接和分发方式应在发布前核查。

## 麒麟 ARM64 上编译（需本机已有开发依赖）
```bash
sudo apt install build-essential cmake qtbase5-dev libqt5svg5-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
./build/simplemap
```
上述安装命令仅供网络/离线软件源允许的开发机使用；如为隔离环境，应由管理员提供离线依赖包。无需 sudo 运行软件本体。

## Windows 上编译
安装 Qt5（Widgets、Svg）、CMake 和匹配的 C++ 工具链，使用 Qt 命令行环境运行：
```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## 设计约束
一份树结构，多个布局视图；保存不依赖坐标；纯离线；无账号、云同步和 AI。保留文件版本字段供迁移。

## 下一步优先级
1. 麒麟 D2000 本机编译并修复编译错误；检查中文输入法、字体、窗口缩放。
2. 把画布拆分为 NodeModel / DocumentService / LayoutEngine / CanvasView / MainWindow。
3. 实现点击选中、双击原位编辑与节点拖拽重排；修复节点尺寸与长文本溢出。
4. 完整 QUndoStack、自动恢复文件、单元测试和 100/500/1000/3000 节点压力测试。
5. 将开发格式升级为版本化 ZIP `.smap`，做安全校验及跨平台往返测试。
