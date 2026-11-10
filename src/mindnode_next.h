#pragma once
#include "document.h"

bool readMindNodeNext(const QByteArray &bytes, const QString &title, Document *out, QString *error);
