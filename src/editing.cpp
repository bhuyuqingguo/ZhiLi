#include "editing.h"
#include "importer.h"

namespace {
bool unicode(const QString &text) {
    for (int i=0;i<text.size();++i) {
        if(text[i].isHighSurrogate()) { if(++i==text.size() || !text[i].isLowSurrogate()) return false; }
        else if(text[i].isLowSurrogate()) return false;
    }
    return true;
}
}

bool pasteContent(Document *target, const QString &parent, QByteArray native, const QString &text,
                  PasteContent mode, bool keepStyle, bool keepNotes, QStringList *inserted, QString *error) {
    Document source;
    const QByteArray marker("zhili-forest\n");
    bool forest = native.startsWith(marker);
    if (mode == PasteContent::Original && !native.isEmpty()) {
        if (!source.parse(forest ? native.mid(marker.size()) : native, error)) return false;
    } else if (mode == PasteContent::Markdown || (mode == PasteContent::Original &&
        QRegularExpression(QStringLiteral("(?m)^ {0,3}(#{1,6}\\s|[-*+]\\s|[0-9]+[.)]\\s)")).match(text).hasMatch())) {
        if (!importMarkdownText(text, &source, error)) return false;
        forest = source.root == "import-1";
    } else if (mode == PasteContent::TextOnly) {
        if (!unicode(text)) { if(error) *error=QStringLiteral("剪贴板包含无效字符"); return false; }
        Node node; node.id = "text"; node.text = text; source.root = node.id; source.nodes.insert(node.id, node);
        Document checked; if (!checked.parse(source.serialize(), error)) return false;
        forest = false;
    } else {
        if (!BranchClipboard::parseOutline(text, {}, &source, error)) return false;
        forest = true;
    }
    for (auto &node : source.nodes) {
        if (!keepStyle) node.style = TopicStyle();
        if (!keepNotes) node.notes.clear();
    }
    return BranchClipboard::paste(target, parent, (forest ? marker : QByteArray()) + source.serialize(), inserted, error);
}

QStringList findTopics(const Document &doc, const QString &query, const FindOptions &options) {
    QStringList result, pending{options.root.isEmpty() ? doc.root : options.root};
    if (query.isEmpty()) return result;
    const auto sensitivity = options.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    while (!pending.isEmpty()) {
        const QString id = pending.takeLast();
        if (!doc.nodes.contains(id)) continue;
        const auto &node = doc.nodes[id];
        if ((options.text && node.text.contains(query, sensitivity)) ||
            (options.notes && node.notes.contains(query, sensitivity))) result.append(id);
        for (auto it = node.children.crbegin(); it != node.children.crend(); ++it)
            if (options.hidden || !node.collapsed || (id==doc.root && doc.floating.contains(*it))) pending.append(*it);
    }
    return result;
}

bool replaceTopics(Document *doc, const QStringList &ids, const QString &query, const QString &replacement,
                   const FindOptions &options, bool firstOnly, int *count, QString *error) {
    if (count) *count = 0;
    if (!doc || query.isEmpty()) return false;
    if(!unicode(query) || !unicode(replacement)) { if(error) *error=QStringLiteral("替换文字包含无效字符"); return false; }
    Document next = *doc; int replaced = 0;
    const auto sensitivity = options.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    QSet<QString> seen;
    for (const auto &id : ids) {
        if (!next.nodes.contains(id) || seen.contains(id)) continue;
        seen.insert(id);
        for (int field = 0; field < 2; ++field) {
            if ((field == 0 && !options.text) || (field == 1 && !options.notes)) continue;
            QString &value = field == 0 ? next.nodes[id].text : next.nodes[id].notes;
            int start = 0, at;
            while ((at = value.indexOf(query, start, sensitivity)) >= 0) {
                if (value.size() - query.size() + replacement.size() > Document::maxText) {
                    if (error) *error = QStringLiteral("替换后文字超过 10 万字符，未修改任何内容"); return false;
                }
                value.replace(at, query.size(), replacement); start = at + replacement.size(); ++replaced;
                if (firstOnly) break;
            }
            if (firstOnly && replaced) break;
        }
        if (firstOnly && replaced) break;
    }
    Document checked; if (!checked.parse(next.serialize(), error)) return false;
    *doc = std::move(checked); if (count) *count = replaced; return true;
}

QString LocalHistory::directory(const QString &path) {
    QString canonical = QFileInfo(path).canonicalFilePath();
    if (canonical.isEmpty()) canonical = QFileInfo(path).absoluteFilePath();
#ifdef Q_OS_WIN
    canonical = canonical.toCaseFolded();
#endif
    const QString key = QString::fromLatin1(QCryptographicHash::hash(canonical.toUtf8(), QCryptographicHash::Sha256).toHex());
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/history/" + key;
}

QStringList LocalHistory::versions(const QString &path) {
    const QDir dir(directory(path)); QStringList result;
    for (const auto &name : dir.entryList({"*.smap"}, QDir::Files, QDir::Name | QDir::Reversed)) result.append(dir.filePath(name));
    return result;
}

bool LocalHistory::record(const QString &path, const QByteArray &bytes, QString *error) {
    if(path.isEmpty()) { if(error) *error=QStringLiteral("请先保存导图"); return false; }
    Document checked; if (!checked.parse(bytes, error)) return false;
    const QString dir = directory(path);
    if (!QDir().mkpath(dir)) { if (error) *error = QStringLiteral("无法创建历史目录"); return false; }
    QLockFile lock(QDir(dir).filePath("history.lock"));
    if(!lock.tryLock(100)) { if(error) *error=QStringLiteral("历史目录正忙，请稍后重试"); return false; }
    const auto prior = versions(path);
    if (!prior.isEmpty()) { QFile file(prior.first()); if (file.open(QIODevice::ReadOnly) && file.readAll() == bytes) return true; }
    QDateTime stamp=QDateTime::currentDateTimeUtc();
    if(!prior.isEmpty()) {
        auto previous=QDateTime::fromString(QFileInfo(prior.first()).fileName().left(19),"yyyyMMdd-HHmmss-zzz"); previous.setTimeSpec(Qt::UTC);
        if(previous.isValid() && stamp<=previous) stamp=previous.addMSecs(1);
    }
    const QString name = stamp.toString("yyyyMMdd-HHmmss-zzz") + "-" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8) + ".smap";
    if (!writeAtomically(QDir(dir).filePath(name), bytes, error)) return false;
    qint64 total = bytes.size(); int kept = 1;
    for (const auto &version : prior) {
        total += QFileInfo(version).size();
        if (++kept > 30 || total > 200 * 1024 * 1024) QFile::remove(version);
    }
    return true;
}
