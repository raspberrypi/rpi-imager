#include "urlfmt.h"

QString UrlFmt::display(const QUrl &u) const {
    return u.toString(QUrl::PreferLocalFile | QUrl::NormalizePathSegments);
}
