#pragma once

#include "document.h"

// Supplying leafRails lays out terminal sibling groups as columns in structure view.
// Each value is the x-coordinate of the leaf's connector rail.
QMap<QString, QPointF> layoutNodes(const Document &document, const QMap<QString, QSizeF> &sizes,
                                  QMap<QString, qreal> *leafRails = nullptr, QVector<qreal> *layerSizes = nullptr);
