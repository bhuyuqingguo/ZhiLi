#pragma once

#include <QtCore>

enum class TextAlignment { Center, Left, Right };
enum class BranchDirection { Both, Right, Left };

struct TopicStyle {
    TextAlignment alignment = TextAlignment::Center;
    QString family;
    int points = 0;
    int weight = -1;
    bool italic = false;
    bool underline = false;
    bool strikeOut = false;
    QString color;

    bool operator==(const TopicStyle &other) const {
        return alignment == other.alignment && family == other.family && points == other.points
            && weight == other.weight && color == other.color && italic == other.italic
            && underline == other.underline && strikeOut == other.strikeOut;
    }
};

struct Node {
    QString id, text;
    QString notes;
    QByteArray imagePng;
    QStringList links;
    // Optional built-in symbol identifier. Unknown IDs remain round-trippable.
    QString symbolId;
    QStringList children;
    bool collapsed = false;
    TopicStyle style;
    BranchDirection rootLane = BranchDirection::Both;
};

enum class ConnectorStyle { Automatic, Orthogonal, Straight, Curved };

struct Relationship {
    QString id, from, to, text;
    bool straight = false;
    QPointF bend = QPointF(0, -80);
};

struct Boundary {
    QString id, text;
    QStringList members;
};

struct Document {
    static constexpr int maxNodes = 100000;
    static constexpr int maxDepth = 512;
    static constexpr int maxText = 100000;
    static constexpr int maxBytes = 50 * 1024 * 1024;

    QMap<QString, Node> nodes;
    QString root;
    bool mindMap = true;
    BranchDirection branchDirection = BranchDirection::Both;
    ConnectorStyle connectorStyle = ConnectorStyle::Automatic;
    // Floating roots remain children of the storage root, but have no tree edge.
    QMap<QString, QPointF> floating;
    QVector<Relationship> relationships;
    QVector<Boundary> boundaries;

    void pruneDecorations();

    QByteArray serialize() const;
    bool parse(const QByteArray &bytes, QString *error = nullptr);
    bool readFile(const QString &path, QString *error = nullptr);
};

bool writeAtomically(const QString &path, const QByteArray &bytes, QString *error);
