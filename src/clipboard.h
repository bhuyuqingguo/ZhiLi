#pragma once

#include "document.h"

// Native clipboard data preserves the complete branch. Plain text is an
// interoperable, tab-indented outline, with multiline labels joined by spaces.
namespace BranchClipboard {
const char *mimeType();
QByteArray encode(const Document &document, const QString &root, QString *error = nullptr);
QString outline(const Document &document, const QString &root);

// A successful paste appends new children to parent and expands it. Every copied
// node receives a fresh ID. Failure leaves document and insertedRoots unchanged.
bool paste(Document *document, const QString &parent, const QByteArray &data,
           QStringList *insertedRoots = nullptr, QString *error = nullptr);
bool pasteOutline(Document *document, const QString &parent, const QString &text,
                  QStringList *insertedRoots = nullptr, QString *error = nullptr);
}
