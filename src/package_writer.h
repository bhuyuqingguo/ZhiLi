#pragma once

#include <QtCore>

struct PackagePart { QString name; QByteArray bytes; };

bool buildStoredPackage(const QVector<PackagePart> &parts, int byteLimit, QByteArray *bytes);
