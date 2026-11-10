#pragma once

#include "document.h"

namespace zhili {

struct TemplateInfo {
    QString id;
    QString category;
    QString title;
    QString description;
    QStringList tags;
    int nodeCount = 0;
    bool mindMap = true;
};

// Original templates are embedded, so the catalog never needs a network or
// writable installation directory. The returned values are independent copies.
QVector<TemplateInfo> builtInTemplates();

// Return a fully validated, independent document with fresh IDs. On failure,
// leave document unchanged and populate error. Unknown IDs are not file paths.
bool loadBuiltInTemplate(const QString &id, Document *document, QString *error = nullptr);

} // namespace zhili
