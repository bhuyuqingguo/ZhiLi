#pragma once

#include "document.h"

QMap<QString, QPointF> layoutNodes(const Document &document, const QMap<QString, QSizeF> &sizes);
