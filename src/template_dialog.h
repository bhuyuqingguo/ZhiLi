#pragma once

#include <QString>

class QWidget;

namespace zhili {

// Shows an offline, read-only template browser. Returns a catalog ID only when
// the user accepts; Cancel, Escape and closing the dialog return an empty string.
// The caller owns the unsaved-document check and document replacement.
QString chooseBuiltInTemplate(QWidget *parent = nullptr);

} // namespace zhili
