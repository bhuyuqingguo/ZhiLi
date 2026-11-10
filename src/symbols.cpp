#include "symbols.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QResource>
#include <QSet>

// Must remain outside a namespace: rcc's generated symbol is global.
static void ensureSymbolResources() {
    Q_INIT_RESOURCE(symbols);
}

namespace {
struct Catalog {
    QList<SymbolCategory> categories;
    QList<SymbolAsset> assets;
    QString error;

    Catalog() {
        ensureSymbolResources();
        QFile file(QStringLiteral(":/symbols/catalog.json"));
        if (!file.open(QIODevice::ReadOnly)) {
            error = QStringLiteral("无法读取内置素材目录。");
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parseError);
        const QJsonObject root = json.object();
        if (parseError.error != QJsonParseError::NoError ||
            root.value("format").toString() != QStringLiteral("zhili-symbol-catalog") ||
            root.value("version").toInt() != 1) {
            error = QStringLiteral("内置素材目录格式不正确。");
            return;
        }
        QSet<QString> categoryIds;
        for (const QJsonValue &value : root.value("categories").toArray()) {
            const QJsonObject object = value.toObject();
            SymbolCategory category;
            category.id = object.value("id").toString();
            category.name = object.value("name").toString();
            for (const QJsonValue &keyword : object.value("keywords").toArray())
                category.keywords.append(keyword.toString());
            if (category.id.isEmpty() || category.name.isEmpty() || categoryIds.contains(category.id)) {
                fail();
                return;
            }
            categoryIds.insert(category.id);
            categories.append(category);
        }
        QSet<QString> ids;
        for (const QJsonValue &value : root.value("assets").toArray()) {
            const QJsonObject object = value.toObject();
            SymbolAsset asset;
            asset.id = object.value("id").toString();
            asset.name = object.value("name").toString();
            asset.englishName = object.value("englishName").toString();
            asset.categoryId = object.value("categoryId").toString();
            asset.categoryName = object.value("categoryName").toString();
            asset.resourcePath = object.value("resourcePath").toString();
            for (const QJsonValue &keyword : object.value("keywords").toArray())
                asset.keywords.append(keyword.toString());
            const QString expected = QStringLiteral(":/symbols/%1/%2.svg").arg(asset.categoryId, asset.id);
            if (asset.id.isEmpty() || ids.contains(asset.id) || asset.name.isEmpty() ||
                !categoryIds.contains(asset.categoryId) || asset.resourcePath != expected ||
                !QFile::exists(expected)) {
                fail();
                return;
            }
            ids.insert(asset.id);
            assets.append(asset);
        }
        if (assets.isEmpty() || categories.isEmpty()) fail();
    }

    void fail() {
        categories.clear();
        assets.clear();
        error = QStringLiteral("内置素材目录不完整或包含重复标识。");
    }
};

const Catalog &catalog() {
    static const Catalog instance;
    return instance;
}
}

const QList<SymbolAsset> &SymbolLibrary::assets() { return catalog().assets; }
const QList<SymbolCategory> &SymbolLibrary::categories() { return catalog().categories; }
QString SymbolLibrary::loadError() { return catalog().error; }

const SymbolAsset *SymbolLibrary::find(const QString &id) {
    if (id.isEmpty()) return nullptr;
    for (const SymbolAsset &asset : assets()) {
        if (asset.id == id) return &asset;
    }
    return nullptr;
}

QList<SymbolAsset> SymbolLibrary::search(const QString &query, const QString &categoryId) {
    const QString normalized = query.simplified();
    // simplified() also normalizes Unicode whitespace. Avoid version-specific
    // SkipEmptyParts enums so this module remains compatible with Qt 5.9.
    const QStringList words = normalized.isEmpty() ? QStringList() : normalized.split(QLatin1Char(' '));
    QList<SymbolAsset> found;
    for (const SymbolAsset &asset : assets()) {
        if (!categoryId.isEmpty() && asset.categoryId != categoryId) continue;
        QStringList fields{asset.id, asset.name, asset.englishName, asset.categoryName};
        fields.append(asset.keywords);
        for (const SymbolCategory &category : categories()) {
            if (category.id == asset.categoryId) {
                fields.append(category.keywords);
                break;
            }
        }
        const QString haystack = fields.join(QLatin1Char(' '));
        bool matches = true;
        for (const QString &word : words) {
            if (!haystack.contains(word, Qt::CaseInsensitive)) {
                matches = false;
                break;
            }
        }
        if (matches) found.append(asset);
    }
    return found;
}
