#pragma once

#include <QtCore>

enum class TextAlignment { Center, Left, Right };
enum class BranchDirection { Both, Right, Left };

struct TopicStyle {
    TextAlignment alignment = TextAlignment::Center;
    QString family;
    int points = 0;
    int weight = -1;
    QString color;

    bool operator==(const TopicStyle &other) const {
        return alignment == other.alignment && family == other.family && points == other.points
            && weight == other.weight && color == other.color;
    }
};

struct Node {
    QString id, text;
    // Optional built-in symbol identifier. Unknown IDs remain round-trippable.
    QString symbolId;
    QStringList children;
    bool collapsed = false;
    TopicStyle style;
    BranchDirection rootLane = BranchDirection::Both;
};

enum class ConnectorStyle { Automatic, Orthogonal, Straight, Curved };

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

    QByteArray serialize() const;
    bool parse(const QByteArray &bytes, QString *error = nullptr);
    bool readFile(const QString &path, QString *error = nullptr);
};

bool writeAtomically(const QString &path, const QByteArray &bytes, QString *error);
