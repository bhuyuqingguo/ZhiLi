#pragma once

#include "document.h"

// Export the complete tree, including collapsed branches, as editable native
// PowerPoint shapes and connectors. Large maps get overview/detail pages. Long
// labels get explicitly referenced text continuation pages. The original .smap is
// unchanged. Export is atomic and bounded to 500 slides / 64 MiB.
bool exportPptx(const Document &document, const QString &path, QString *error = nullptr);

// Optional icons, keyed by original node ID. The caller supplies small PNGs
// (up to 1 MiB and 4096x4096 each). They remain separate movable pictures;
// topic shapes, text, and connectors remain native and editable.
bool exportPptx(const Document &document, const QString &path,
                const QMap<QString, QByteArray> &nodePngs, QString *error = nullptr);
