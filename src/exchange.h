#pragma once

#include "document.h"

bool exportExchange(const Document &document, const QString &path,
                    const QString &format, QString *error);
