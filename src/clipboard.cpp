#include "clipboard.h"

namespace {
bool reject(QString *error, const QString &message) {
    if (error) *error = message;
    return false;
}

bool validUnicode(const QString &text) {
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i).isHighSurrogate()) {
            if (++i == text.size() || !text.at(i).isLowSurrogate()) return false;
        } else if (text.at(i).isLowSurrogate()) {
            return false;
        }
    }
    return true;
}

bool insert(Document *document, const QString &parent, const Document &source,
            const QStringList &roots, QStringList *insertedRoots, QString *error) {
    if (!document || !document->nodes.contains(parent))
        return reject(error, QStringLiteral("请先选择粘贴位置"));

    // Validate before changing anything, including when called outside the UI.
    Document next;
    if (!next.parse(document->serialize(), error)) return false;
    const int added = source.nodes.size() - (roots.contains(source.root) ? 0 : 1);
    if (added > Document::maxNodes - next.nodes.size())
        return reject(error, QStringLiteral("粘贴后将超过节点数量上限"));

    QMap<QString, QString> ids;
    QSet<QString> occupied;
    for (auto it = next.nodes.cbegin(); it != next.nodes.cend(); ++it) occupied.insert(it.key());
    QVector<QString> pending = roots.toVector();
    while (!pending.isEmpty()) {
        const QString id = pending.takeLast();
        QString replacement;
        do {
            replacement = QUuid::createUuid().toString(QUuid::WithoutBraces);
        } while (occupied.contains(replacement));
        occupied.insert(replacement);
        ids.insert(id, replacement);
        pending += source.nodes.value(id).children.toVector();
    }
    QStringList inserted;
    for (const QString &root : roots) inserted.append(ids.value(root));
    for (auto it = ids.cbegin(); it != ids.cend(); ++it) {
        Node node = source.nodes.value(it.key());
        node.id = it.value();
        for (QString &child : node.children) child = ids.value(child);
        next.nodes.insert(node.id, node);
    }
    next.nodes[parent].children.append(inserted);
    next.nodes[parent].collapsed = false;

    // The combined tree may exceed the destination's remaining depth or byte
    // budget even when both individual trees are valid.
    Document checked;
    if (!checked.parse(next.serialize(), error)) return false;
    *document = std::move(next);
    if (insertedRoots) *insertedRoots = inserted;
    if (error) error->clear();
    return true;
}
}

namespace BranchClipboard {
const char *mimeType() {
    return "application/x-zhili-branch+json";
}

QByteArray encode(const Document &document, const QString &root, QString *error) {
    Document branch;
    branch.root = root;
    branch.mindMap = document.mindMap;
    QVector<QString> pending{root};
    while (!pending.isEmpty()) {
        const QString id = pending.takeLast();
        if (!document.nodes.contains(id) || branch.nodes.contains(id)) {
            reject(error, QStringLiteral("无法复制无效的分支"));
            return {};
        }
        const Node node = document.nodes.value(id);
        // Qt 5.12 may replace lone surrogates during JSON serialization. Check
        // the original strings first so a "lossless" copy never normalizes them.
        if (!validUnicode(node.id) || !validUnicode(node.text) || !validUnicode(node.symbolId)) {
            reject(error, QStringLiteral("分支文字包含无效字符，无法无损复制"));
            return {};
        }
        branch.nodes.insert(id, node);
        pending += node.children.toVector();
    }
    const QByteArray bytes = branch.serialize();
    Document checked;
    if (!checked.parse(bytes, error)) return {};
    return bytes;
}

QString outline(const Document &document, const QString &root) {
    QStringList lines;
    QSet<QString> visited;
    QVector<QPair<QString, int>> pending{{root, 0}};
    while (!pending.isEmpty()) {
        const auto entry = pending.takeLast();
        if (!document.nodes.contains(entry.first) || visited.contains(entry.first)) return {};
        visited.insert(entry.first);
        const Node node = document.nodes.value(entry.first);
        QString text = node.text;
        text.replace("\r\n", " ");
        text.replace('\n', ' ');
        text.replace('\r', ' ');
        lines.append(QString(entry.second, '\t') + text);
        for (auto child = node.children.crbegin(); child != node.children.crend(); ++child)
            pending.append(qMakePair(*child, entry.second + 1));
    }
    return lines.join('\n');
}

bool paste(Document *document, const QString &parent, const QByteArray &data,
           QStringList *insertedRoots, QString *error) {
    Document branch;
    if (!branch.parse(data, error)) return false;
    return insert(document, parent, branch, {branch.root}, insertedRoots, error);
}

bool pasteOutline(Document *document, const QString &parent, const QString &text,
                  QStringList *insertedRoots, QString *error) {
    // Validate before toUtf8()/JSON, which can hide malformed UTF-16 on Qt 5.12.
    if (!validUnicode(text))
        return reject(error, QStringLiteral("剪贴板文字包含无效字符，请重新复制"));
    if (text.size() > Document::maxBytes || text.toUtf8().size() > Document::maxBytes)
        return reject(error, QStringLiteral("剪贴板文字超过 50 MiB"));
    QString normalized = text;
    normalized.replace("\r\n", "\n");
    normalized.replace('\r', '\n');

    Document source;
    source.root = "clipboard-root";
    Node root;
    root.id = source.root;
    source.nodes.insert(root.id, root);
    QVector<int> indents;
    QStringList ancestors;
    int baseline = -1;
    const QStringList lines = normalized.split('\n');
    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const QString &line = lines.at(lineIndex);
        if (line.trimmed().isEmpty()) continue;
        int prefix = 0;
        int indentation = 0;
        while (prefix < line.size() && (line.at(prefix) == ' ' || line.at(prefix) == '\t')) {
            // Tabs advance to conventional four-space tab stops. Actual space
            // widths need not be fixed: a deeper indent creates one level, and
            // a dedent must return to an earlier indentation column.
            indentation = line.at(prefix) == '\t' ? (indentation / 4 + 1) * 4 : indentation + 1;
            ++prefix;
        }
        if (baseline < 0) baseline = indentation;
        if (indentation < baseline)
            return reject(error, QStringLiteral("第 %1 行缩进小于首行，请对齐大纲").arg(lineIndex + 1));
        if (!indents.isEmpty() && indentation > indents.last()) {
            indents.append(indentation);
        } else {
            while (!indents.isEmpty() && indents.last() > indentation) {
                indents.removeLast();
                ancestors.removeLast();
            }
            if (indents.isEmpty()) indents.append(indentation);
            else if (indents.last() != indentation)
                return reject(error, QStringLiteral("第 %1 行缩进未对齐已有层级").arg(lineIndex + 1));
        }
        const int level = indents.size() - 1;
        if (level >= Document::maxDepth)
            return reject(error, QStringLiteral("大纲层级过深"));
        if (source.nodes.size() >= Document::maxNodes)
            return reject(error, QStringLiteral("大纲节点数量过多"));
        const QString label = line.mid(prefix);
        if (label.size() > Document::maxText)
            return reject(error, QStringLiteral("第 %1 行文字超过 10 万字符").arg(lineIndex + 1));
        Node node;
        node.id = QString::number(lineIndex);
        node.text = label;
        const QString parentId = level == 0 ? source.root : ancestors.at(level - 1);
        source.nodes[parentId].children.append(node.id);
        source.nodes.insert(node.id, node);
        if (ancestors.size() > level) ancestors[level] = node.id;
        else ancestors.append(node.id);
    }
    const QStringList roots = source.nodes.value(source.root).children;
    if (roots.isEmpty()) return reject(error, QStringLiteral("剪贴板中没有可粘贴的主题"));
    Document checked;
    if (!checked.parse(source.serialize(), error)) return false;
    return insert(document, parent, source, roots, insertedRoots, error);
}
}
