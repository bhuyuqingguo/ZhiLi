#pragma once

#include "document.h"

// One-way, local conversion of supported .mmap and legacy XML .emmx files.
// Only topic hierarchy, order and plain text are imported. Success returns
// explicit limitations in warnings. Failure leaves out and warnings unchanged.
// The caller must treat the result as unsaved, never save over the source file.
bool importMindMap(const QString &path, Document *out, QStringList *warnings = nullptr,
                   QString *error = nullptr);
