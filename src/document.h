#pragma once

#include <QtCore>

struct Node {
    QString id, text;
    // Optional built-in symbol identifier. Unknown IDs remain round-trippable.
    QString symbolId;
    QStringList children;
    bool collapsed = false;
};

struct Document {
    static constexpr int maxNodes = 100000;
    static constexpr int maxDepth = 512;
    static constexpr int maxText = 100000;
    static constexpr int maxBytes = 50 * 1024 * 1024;

    QMap<QString, Node> nodes;
    QString root;
    bool mindMap = true;

    QByteArray serialize() const;
    bool parse(const QByteArray &bytes, QString *error = nullptr);
    bool readFile(const QString &path, QString *error = nullptr);
};

bool writeAtomically(const QString &path, const QByteArray &bytes, QString *error);
