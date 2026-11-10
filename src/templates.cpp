#include "templates.h"

// Resource registration must be outside a namespace. Referencing the generated
// symbol explicitly also retains the resource when linked from a static archive.
static void initializeTemplateResources() {
    Q_INIT_RESOURCE(templates);
}

namespace {

QVector<zhili::TemplateInfo> readCatalog() {
    initializeTemplateResources();
    QFile file(QStringLiteral(":/templates/catalog.json"));
    if (!file.open(QIODevice::ReadOnly)) return {};
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !json.isObject()) return {};
    const auto object = json.object();
    if (object.value("version") != QJsonValue(1) || !object.value("templates").isArray()) return {};
    const auto entries = object.value("templates").toArray();
    QVector<zhili::TemplateInfo> result;
    QSet<QString> seen;
    const QRegularExpression validId(QStringLiteral("^[a-z0-9]+(?:-[a-z0-9]+)*$"));
    for (const auto &entry : entries) {
        if (!entry.isObject()) return {};
        const auto fields = entry.toObject();
        zhili::TemplateInfo info;
        info.id = fields.value("id").toString();
        info.category = fields.value("category").toString();
        info.title = fields.value("title").toString();
        info.description = fields.value("description").toString();
        info.nodeCount = fields.value("nodeCount").toInt();
        const auto layout = fields.value("layout");
        if (!validId.match(info.id).hasMatch() || seen.contains(info.id)
            || info.category.isEmpty() || info.title.isEmpty() || info.description.isEmpty()
            || info.nodeCount < 1 || info.nodeCount > Document::maxNodes
            || fields.value("nodeCount") != QJsonValue(info.nodeCount)
            || (layout != QJsonValue("mindmap") && layout != QJsonValue("organization"))
            || !fields.value("tags").isArray()) return {};
        info.mindMap = layout == QJsonValue("mindmap");
        for (const auto &tag : fields.value("tags").toArray()) {
            if (!tag.isString() || tag.toString().isEmpty()) return {};
            info.tags.append(tag.toString());
        }
        seen.insert(info.id);
        result.append(info);
    }
    return result;
}

const QVector<zhili::TemplateInfo> &catalog() {
    static const QVector<zhili::TemplateInfo> entries = readCatalog();
    return entries;
}

} // namespace

namespace zhili {

QVector<TemplateInfo> builtInTemplates() {
    return catalog();
}

bool loadBuiltInTemplate(const QString &id, Document *document, QString *error) {
    const auto reject = [error](const QString &message) {
        if (error) *error = message;
        return false;
    };
    if (!document) return reject(QStringLiteral("没有指定模板目标文档"));
    const auto &entries = catalog();
    if (entries.isEmpty()) return reject(QStringLiteral("内置模板目录不可用"));
    const TemplateInfo *selected = nullptr;
    for (const auto &entry : entries) {
        if (entry.id == id) {
            selected = &entry;
            break;
        }
    }
    if (!selected) return reject(QStringLiteral("找不到指定的内置模板"));

    QFile file(QStringLiteral(":/templates/%1.smap").arg(selected->id));
    if (!file.open(QIODevice::ReadOnly))
        return reject(QStringLiteral("无法读取内置模板：%1").arg(file.errorString()));
    Document next;
    QString parseError;
    if (!next.parse(file.readAll(), &parseError))
        return reject(QStringLiteral("模板数据无效：%1").arg(parseError));
    if (next.nodes.size() != selected->nodeCount || next.mindMap != selected->mindMap
        || next.nodes.value(next.root).text != selected->title)
        return reject(QStringLiteral("模板与目录信息不一致"));

    QMap<QString, QString> ids;
    QSet<QString> generated;
    for (auto it = next.nodes.cbegin(); it != next.nodes.cend(); ++it) {
        QString fresh;
        do {
            fresh = QUuid::createUuid().toString(QUuid::WithoutBraces);
        } while (generated.contains(fresh) || document->nodes.contains(fresh));
        generated.insert(fresh);
        ids.insert(it.key(), fresh);
    }
    QMap<QString, Node> remapped;
    for (const auto &node : next.nodes) {
        Node copy = node;
        copy.id = ids.value(node.id);
        for (auto &child : copy.children) child = ids.value(child);
        remapped.insert(copy.id, copy);
    }
    next.root = ids.value(next.root);
    next.nodes = std::move(remapped);
    *document = std::move(next);
    if (error) error->clear();
    return true;
}

} // namespace zhili
