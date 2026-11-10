#pragma once
#include "document.h"

bool readBinaryEmmxPage(const QByteArray &bytes, Document *out, QString *error);
