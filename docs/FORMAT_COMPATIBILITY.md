# 导图与图形格式兼容清单

「文件 → 格式转换」在本机直接读取下表已支持的源格式。选择 `.smap` 可统一为[开放的 UTF-8 JSON 格式](SMAP_FORMAT.md)。保留原文件，转换后可在枝理继续编辑。

## 可直接转为 SMAP

| 来源 | 文件 | 可读取内容 | 可生成同类文件 |
| --- | --- | --- | --- |
| 枝理 | `.smap`、旧 `.smap.json` | 完整枝理模型 | 是 |
| Xmind | `.xmind` | JSON 版和旧 XML 版的主题树，多工作表合入一图 | 暂未支持 |
| MindManager | `.mmap` | 受支持的单中心主题树 | 暂未支持 |
| MindMaster / EdrawMind | `.emmx` | 旧版单页 XML、10.7/10.9 已验证二进制页面的中心、主分支和子主题 | 暂未支持 |
| MindNode | `.mindnode` | Classic 二进制 `contents.xml` 的主题树、折叠状态；Next v1 `Contents.data` 的主题树 | 暂未支持 |
| MindMeister | `.mind` | 含 `map.json` 的主题树 | 暂未支持 |
| Mindomo | `.mom`、`.pmom` | 含 `nodes.json` 的主题文字、顺序、折叠状态。真实 `.mom` 已验证，`.pmom` 已验证含附件的测试包 | 暂未支持 |
| FreeMind / Freeplane | `.mm` | 主题树、文字和折叠标记 | 是 |
| 大纲交换 | `.opml` | OPML 1.0、1.1、2.0 的条目与层级 | 是 |
| Markdown、缩进文本 | `.md`、`.markdown`、`.txt` | UTF-8 标题、列表和缩进大纲 | 纯文本 |
| Mermaid | `.mmd`、`.mermaid` | UTF-8 `mindmap` 语法的主题和层级 | 暂未支持 |
| Obsidian 等 JSON Canvas 软件 | `.canvas` | 节点文字、文件或网址的引用文字、可映射的边 | 是 |
| draw.io / diagrams.net | `.drawio`、draw.io `.xml` | 压缩或非压缩页、形状文字、可映射的边 | 是 |
| Microsoft Visio | `.vsdx` | 页面、形状文字、可映射的边 | 是，单页 |
| Microsoft PowerPoint | `.pptx` | 幻灯片顺序、形状文字、可映射的连接 | 是，可编辑 PPTX |

导图优先保留主题文字、父子关系和顺序。图形中的循环、多父级、悬空连接会提示，枝理按树重新排版。原坐标、外部资源、图片、Visio 模具、SmartArt、动画和专有样式可能无法转换。PDF、PNG、SVG 可作为输出。

## 仍需支持的原生格式

| 来源 | 待支持的文件或版本 |
| --- | --- |
| MindMaster / EdrawMind | 未识别的二进制页面格式、旧版多页 XML，及二进制页中的独立标注、表格和浮动对象 |
| MindNode | 旧 XML 属性列表里使用旧式归档标题的文件，以及尚未验证的 Next 格式版本 |
| Mindomo | 无 `nodes.json` 的旧版原生档案 |
| SimpleMind | `.smmx` |
| iThoughts | `.itmz` |
| 旧版 Office | Visio `.vsd`、PowerPoint `.ppt` |

专有格式以实际文件版本验收。枝理不会尝试破解密码保护的源文件。

未打包的 MindNode Classic 文件夹可在转换对话框中进入 `.mindnode` 文件夹并选择 `contents.xml`。

## 原始样例验收

- EdrawMind 官方二进制模板 10.7.2 与 10.9.0。两张有层级的有机化学导图分别得到 24 和 11 个主题，《高效能人士的七个习惯》得到 30 个主题，七个主分支按源文件的子主题序号排列。表格型模板仅转换中心主题等受支持的导图节点。 [有机化学模板](https://edrawmind.wondershare.com/chemistry/organic-compound-concept-map.html) · [导图模板](https://edrawmind.wondershare.com/mind-map/free-mind-map-templates.html)
- MindNode 官方 Classic 样例《What is MindNode?》得到 32 个主题，官方 Next 2025.4 样例《Mind Mapping Quiz》得到 33 个原主题与 1 个合并根。 [MindNode 官方样例](https://www.mindnode.com/gallery)
- MindMeister 三份公开原生样例，分别得到 74、79 和 90 个主题。逐棵对比了原 `map.json` 与输出主题树。 [项目规划样例](https://www.stakeholdermap.com/project-templates/mindmap-project-plan.php)
- Mindomo Desktop 11.3.0 随附的四份 `.mom` 样例，分别得到 9、13、27 和 35 个主题。逐棵对比了原 `nodes.json` 与输出主题树，包含中文样例。 [Mindomo Desktop](https://www.mindomo.com/desktop/)

## 格式来源

- [OPML 2.0](https://2005.opml.org/spec2.html) · [FreeMind 文件格式](https://freemind.sourceforge.io/wiki/index.php/File_format) · [Xmind 官方 SDK](https://github.com/xmindltd/xmind-sdk-js)
- [Mermaid Mindmap](https://mermaid.js.org/syntax/mindmap.html) · [JSON Canvas 1.0](https://jsoncanvas.org/spec/1.0/) · [draw.io 文件格式](https://github.com/jgraph/drawio/wiki/File-Format)
- [Microsoft VSDX 格式](https://learn.microsoft.com/en-us/office/client-developer/visio/introduction-to-the-visio-file-formatvsdx) · [PresentationML](https://learn.microsoft.com/en-us/office/open-xml/presentation/working-with-presentations)
- [MindNode 格式](https://www.mindnode.com/support/guides/import-and-export) · [MindMeister 格式](https://support.mindmeister.com/hc/en-us/articles/8031990936466-Can-I-Import-Export-My-Mind-Maps) · [Mindomo 格式](https://help.mindomo.com/desktop/working-with-mindomo-desktop/)
