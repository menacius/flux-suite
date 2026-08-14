#pragma once

#include "product.h"

#include <QString>

namespace FileAssociations {

bool registerForProduct(const Product &product, const QString &installPath, QString *error);
void unregisterForProduct(const Product &product, const QString &installPath);

}
