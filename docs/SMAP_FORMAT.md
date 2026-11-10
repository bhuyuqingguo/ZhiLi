# 枝理 SMAP v1 文件格式

`.smap` 是 UTF-8 编码的标准 JSON。文件直接存储主题树、样式和布局参数，默认没有加密、压缩、账号绑定或网络依赖。其他程序可以使用通用 JSON 库读写。旧文件名 `.smap.json` 仍可打开。

## 最小示例

```json
{
  "format": "simplemap-json",
  "version": 1,
  "root": "main",
  "nodes": [
    { "id": "main", "text": "项目计划", "children": ["work"] },
    { "id": "work", "text": "设计与验证", "children": [] }
  ]
}
```

## 顶层字段

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `format` | 字符串，必填 | 固定为 `simplemap-json` |
| `version` | 整数，必填 | 本版为 `1` |
| `root` | 字符串，必填 | 中心主题的 ID |
| `nodes` | 数组，必填 | 所有主题，数组顺序没有语义 |
| `layout` | 字符串 | `mindmap` 为默认，`organization` 为纵向结构图 |
| `branchDirection` | 字符串 | 思维导图布局下可用 `left` 或 `right`，省略时双侧展开 |
| `connectorStyle` | 字符串 | `automatic` 为默认，也可用 `orthogonal`、`straight`、`curved` |

## 主题字段

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `id` | 字符串，必填 | 非空，在同一文件中唯一 |
| `text` | 字符串，必填 | 主题纯文字，可为空或含换行 |
| `children` | 字符串数组，必填 | 按显示顺序排列的子主题 ID，无子级时为 `[]` |
| `collapsed` | 布尔值 | 默认 `false`，折叠时仍须存储全部后代 |
| `symbolId` | 字符串 | 可选的枝理内置符号 ID，未知 ID 可以保留 |
| `rootLane` | 字符串 | 双侧布局的中心直属分支可指定 `left` 或 `right`，省略时自动分配 |
| `style` | 对象 | 可选的文字样式 |

`style` 中可写 `alignment`（`center`、`left`、`right`，默认居中）、`family`（字体名）、`points`（9–48 的整数）、`bold`（布尔值）和 `color`（`#RRGGBB`）。未指定的字体、字号、字重和颜色采用应用主题。字体和符号图像不嵌入文件。

## 读写规则

- 所有节点须从 `root` 可达。除中心外每个节点只有一个父节点，不能有循环、断开的主题或重复引用。
- 枝理当前限制为文件 50 MiB、10 万个主题、最多 512 层边（中心深度为 0），每个主题文字最多 10 万个 UTF-16 代码单元。字体名和符号 ID 最长 128 个 UTF-16 代码单元。
- 写入器应保留 `children` 的顺序。外部工具可以自选 ID，只要文件内引用一致。画布坐标由读取方按布局计算。
- 枝理 v1 忽略未知字段，重新保存时可能丢弃这些字段。需要保留的自定义数据应留在源文件，或由转换器另行管理。未知文件版本应报错。
- 导入的文件保留在原位置，转换写入新的 `.smap`。树以外的关系、资源与样式属于各导入器的映射范围，参见[格式兼容清单](FORMAT_COMPATIBILITY.md)。
