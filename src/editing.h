#pragma once
#include "clipboard.h"

enum class PasteContent { Original, PlainOutline, Markdown, TextOnly };
bool pasteContent(Document *target, const QString &parent, QByteArray native, const QString &text,
                  PasteContent mode, bool keepStyle, bool keepNotes, QStringList *inserted, QString *error);

struct FindOptions {
    bool text = true, notes = false, caseSensitive = false, hidden = true;
    QString root;
};
QStringList findTopics(const Document &doc, const QString &query, const FindOptions &options);
bool replaceTopics(Document *doc, const QStringList &ids, const QString &query, const QString &replacement,
                   const FindOptions &options, bool firstOnly, int *count, QString *error);

class LocalHistory {
public:
    static QString directory(const QString &path);
    static QStringList versions(const QString &path);
    static bool record(const QString &path, const QByteArray &bytes, QString *error);
};
