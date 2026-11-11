# MindMaster 编辑习惯对照

核对日期：2026-11-11。对象为 MindMaster / EdrawMind（现官方中文页面称万兴脑图）的 Windows 桌面操作说明。本文件描述 v1.0.4。公开文档可以核对按键和功能定义；尚未逐项在同版本 MindMaster 真机上并排验收，不能宣称完全复刻。

## 查阅和设置

枝理「帮助 → 快捷键设置」列出已接通的命令，可搜索、替换、增加备用按键、清除和恢复默认。保存前检查冲突；设置保存在当前用户配置中，重启继续使用。菜单、按钮及情境提示引用实际绑定。

第二页「MindMaster 完整对照」包含官方目录的全部功能类别，逐项标明已实现、部分支持、差异或待实现。机器可读清单为 [mindmaster-shortcuts.tsv](../assets/help/mindmaster-shortcuts.tsv)。表中的参考按键不注册为占位动作。

## 已接通的高频编辑流程

| 流程 | 枝理的实现 |
| --- | --- |
| 快速录入 | Enter 同级、Shift+Enter 前插、Tab / Insert / Ctrl+Enter 子级、Shift+Insert 父级，中心主题自动创建主分支。 |
| 进入文字编辑 | 双击 / F2，Space 末尾，Shift+Space 开头；Enter 确认，Esc 取消，三种修饰键加 Enter 换行；中文组词不提前提交。 |
| 批量录入 | Ctrl+M 输入缩进大纲，选择插入子级或同级；错误缩进不修改原图；一次撤销整次插入。 |
| 拆分与合并 | 编辑时 Shift+Alt+Enter 在光标处拆分；同级多选可合并，文字按原顺序换行、备注合并、子级保留。 |
| 删除 | Delete / Backspace 删除分支；Shift+Delete 保留子级并按原位置提升，支持嵌套多选。 |
| 选择与导航 | 方向键、Shift+方向键、Home / End、Shift+Home / End、Ctrl+Home；兄弟、下一级、父级、分支、相同层级选择。 |
| 调整顺序 | Ctrl+Shift+Up / Down，Ctrl+Shift+Home / End；多选可整组移动，不同父主题内分别处理；支持撤销。 |
| 调整层级 | Ctrl+[ 移到前一兄弟下，Ctrl+] 提升到父主题后；根节点受保护，防止形成循环或超过层级限制。 |
| 分支复制 | Ctrl+D / Ctrl+Alt+Down 后插副本，Ctrl+Alt+Up 前插副本，Ctrl+Insert 复制为子主题；保留样式和备注，重新生成节点 ID。 |
| 拖拽 | 多选整组拖动，落点预览、吸附、Ctrl 拖拽复制；Esc 取消，靠近视口边缘自动滚动。 |
| 大纲 | F10 / Ctrl+Shift+O 开关侧栏，同一份数据双向同步；F2 改文字，Enter 同级、Insert 子级、Tab 缩进、Shift+Tab 提升、Shift+Delete 保留子级删除。 |
| 文字格式 | 整主题或多选的粗体、斜体、下划线、删除线、字号和颜色；SMAP、原生剪贴板保留样式，PNG / SVG / PDF 和 PPTX 渲染支持新增字形。 |
| 备注 | Ctrl+T 纯文字备注，主题内标记可打开，复制和分支导出保留。 |
| 展开层级 | Alt+1…9 显示当前视图层级，Alt+0 全部展开，Alt+D 逐层展开，Ctrl+F3 折叠 / 展开；聚焦时不修改其他分支。 |
| 视图 | F3 居中、F4 双区聚焦、Ctrl+0 100%、Alt+F3 适宽、Alt+Shift+F3 适窗、F11 全屏；纵横翻页及滚轮操作。 |
| 关系线 | Ctrl+R 起点后点击终点；Ctrl+Shift+R 直线；双击标题、拖动控制点、删除和撤销。 |
| 外框 | Ctrl+Shift+B 包住所选分支及后代，随布局和内容伸缩，双击编辑标题。 |
| 浮动主题 | Alt+F 后落点、空白双击、整组自由移动、微移，拖到主题接回树结构。 |

新元素支持 SMAP、图片、SVG、PDF 和 draw.io 写出；PPTX、Visio 和大纲等目标的完整映射尚缺，遇到新元素会提示并拒绝有损写出。跨分支复制仅保留复制范围内的关系线和外框成员。

## 当前差异和后续实现范围

| 类别 | 仍缺少或不同的部分 |
| --- | --- |
| 结构元素 | 已有浮动主题、跨主题关系线、自动外框；仍缺智能关系线避障、外框精细样式、概要和标注。 |
| 内容元素 | 自定义图片、附件、超链接、表格、公式、优先级、编号、标签和待办。现有 SVG 符号选择器不等于完整任务标记系统。 |
| 文字和样式 | 局部富文本、上标下标、文字高亮、手动主题宽度；当前按主题整体设置格式。 |
| 选择 | Ctrl+A 只选当前可见主题，Alt+A 当前按可见层级选择；隐藏主题、对象类型、反选和按层级扩选仍需补齐。 |
| 剪贴板 | Ctrl+Alt+V 当前直接粘贴为同级，完整的选择性粘贴选项尚缺；样式复制后用 Ctrl+Shift+V 粘贴，没有连续点击式格式刷。 |
| 查找 | 支持全图文字查找和折叠路径定位，尚缺替换、替换范围和内容筛选。 |
| 文档与演示 | 多文档标签、多页面、原生打印、遍历演示、演讲画笔、头脑风暴计时和记录板。 |
| 导出 | 已有 PNG / SVG / PDF / PPTX 及多种交换格式；Word、Excel、HTML 和 MindManager 原生写出尚缺。 |
| 服务类 | 云端协作、社区、AI、云笔记和商业授权功能不属于现有离线编辑实现；快捷键目录中仍列出它们供完整核对。 |

下一阶段优先顺序：结构元素 → 内容元素与查找替换 → 更细的选择和格式操作 → 文档与演示。每项须完成保存、重开、复制、撤销和导出映射后才能从待实现改为可用。

## 旧版枝理迁移

| 旧绑定 | 当前行为 |
| --- | --- |
| Space 折叠 | Space 编辑末尾；折叠改 Ctrl+F3。 |
| Shift+Insert 粘贴 | 插入父主题；粘贴仍用 Ctrl+V。 |
| Shift+Delete 剪切 | 删除主题保留子级；剪切仍用 Ctrl+X。 |
| Ctrl+0 适应窗口 | 100%；适应窗口改 Alt+Shift+F3。 |
| F3 查找下一个 | 居中主题；查找下一个改 Ctrl+Shift+F3。 |
| Ctrl+Shift+J 聚焦 | 改 F4，保留全图与局部同时显示。 |
| Ctrl+Shift+O 导入 | 改 Ctrl+Alt+O，原按键用于大纲。 |
| Ctrl+Shift+B 框选 | 改 Ctrl+Alt+B，原绑定用于添加外框。 |
| Ctrl+1…4 布局 | 改 Ctrl+Alt+1…4，为优先级预留原绑定。 |

可在快捷键设置中按个人习惯重新分配。输入框中固有的文字编辑键和大纲上下文键不参与画布重绑。

## 官方依据

- [完整快捷键目录](https://edraw.wondershare.cn/mindmaster/guide/shortcuts.html)
- [主题编辑](https://edraw.wondershare.cn/mindmaster/guide/edit-topics.html)
- [添加及批量添加主题](https://edraw.wondershare.cn/mindmaster/guide/add-topics.html)
- [删除主题](https://edraw.wondershare.cn/mindmaster/guide/delete-topics.html)
- [移动主题](https://edraw.wondershare.cn/mindmaster/guide/move-topics.html)
- [选择主题](https://edraw.wondershare.cn/mindmaster/guide/select-edit-file.html)
- [居中聚焦](https://edraw.wondershare.cn/mindmaster/guide/focus-on-selected-topic.html)
- [显示层级](https://edraw.wondershare.cn/mindmaster/guide/display-level-of-mind-map.html)
