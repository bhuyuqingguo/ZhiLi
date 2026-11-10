#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

// IDs are stable document data. Unknown IDs may be preserved by older clients.
struct SymbolAsset {
    QString id;
    QString name;
    QString englishName;
    QString categoryId;
    QString categoryName;
    QString resourcePath;
    QStringList keywords;
};

struct SymbolCategory {
    QString id;
    QString name;
    QStringList keywords;
};

namespace SymbolLibrary {
// The embedded catalog is loaded once. Returned references/pointers stay valid.
const QList<SymbolAsset> &assets();
const QList<SymbolCategory> &categories();
const SymbolAsset *find(const QString &id);
QByteArray svg(const SymbolAsset &asset, bool dimensional = false, bool darkBackground = false);
// AND-match whitespace-separated words; Chinese/English, case-insensitive.
// An empty category searches all categories. Unknown categories return no hits.
QList<SymbolAsset> search(const QString &query = QString(),
                         const QString &categoryId = QString());
// Empty on success. Exposes resource/catalog problems rather than silent failure.
QString loadError();
}
