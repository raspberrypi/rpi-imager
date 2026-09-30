// Fuzz the OS list parser against the bytes a repository actually serves.
//
// The URL is user-settable and the document is fetched over the network, so
// every branch here runs on input nobody vetted. filterInvalidInitFormats()
// recurses through "subitems", which makes nesting depth as interesting as
// the field values.
#include "oslistparser.h"
#include "fuzz_silence.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <cmath>
#include <cstdint>

namespace {

// Each entry as its own compact JSON, sorted, so two lists can be compared
// for what they hold without caring what order they hold it in.
QStringList fingerprints(const QJsonArray &list)
{
    QStringList out;
    out.reserve(list.size());
    // Wrapped in an array rather than handed to fromVariant: a document can
    // only hold an object or an array, so a bare number or string became a
    // null document and every scalar fingerprinted alike -- which made the
    // comparison below say two different lists were the same.
    for (const QJsonValue &v : list) {
        QJsonArray one;
        one.append(v);
        out << QString::fromUtf8(QJsonDocument(one).toJson(QJsonDocument::Compact));
    }
    out.sort();
    return out;
}


// Every byte count in the document, wherever it is nested. The repository is
// a setting -- the bootloader's own flash can name it -- so these numbers are
// chosen by whoever serves the list, and a size that survives the conversion
// is one the capacity check will later trust.
void checkByteCounts(const QJsonValue &v, int depth)
{
    if (depth > 24)
        return;

    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        for (auto it = o.begin(); it != o.end(); ++it)
            checkByteCounts(it.value(), depth + 1);
    } else if (v.isArray()) {
        for (const QJsonValue &child : v.toArray())
            checkByteCounts(child, depth + 1);
    }

    const quint64 n = oslist::byteCountFromJson(v);
    if (n == 0)
        return;                 // "unknown", which every caller already reads

    // A non-zero answer is a promise that the value really was a whole,
    // finite, non-negative number the type can hold. -1 and 1e30 both used to
    // come back as something a card was then measured against.
    if (!v.isDouble())
        __builtin_trap();
    const double d = v.toDouble();
    if (!std::isfinite(d) || d < 0.0 || d >= 18446744073709551616.0)
        __builtin_trap();
    if (static_cast<double>(n) > d || d - static_cast<double>(n) >= 1.0)
        __builtin_trap();       // not the truncation it claims to be
}

// The sanitiser drops, passes through, or prefixes "../" -- and nothing else.
// It never builds a new string, which is what keeps a repository's icon field
// from being re-encoded into something QML resolves differently from what was
// inspected. Deliberately not asserting the header's stronger claim to be an
// allow-list: the code passes unknown schemes through on purpose, so a
// harness holding it to the header would be reporting a disagreement that is
// already written down rather than finding anything.
void checkIcon(const QString &raw)
{
    const QString out = oslist::sanitizeIconSource(raw);
    if (out.isEmpty())
        return;
    if (out == raw)
        return;
    if (out == QStringLiteral("../") + raw)
        return;
    __builtin_trap();           // a third kind of answer
}

// Routing adds a prefix to what the sanitiser returned, or returns it
// unchanged. It never edits the string in between, so an icon the sanitiser
// approved cannot be re-encoded into something QML resolves elsewhere.
void checkRouting(const QString &raw)
{
    const QString sanitized = oslist::sanitizeIconSource(raw);
    const QString routed = oslist::iconSourceFor(raw);
    if (routed == sanitized)
        return;
    if (routed == QStringLiteral("image://icons/") + sanitized)
        return;
    __builtin_trap();           // neither the icon nor the icon routed
}

// Every icon inside subitems_json has been routed, at whatever depth.
// OSSelectionStep flattens that string in JavaScript and hands what it finds
// straight to Image.source, so a bare http(s) icon there is a fetch by Qt
// Quick -- outside IconMultiFetcher, its cache and its scheme allow-list.
//
// The top level is deliberately not checked. Those icons are routed by
// OSListModel as it builds its rows, not by the parser, so asserting it here
// would hold the parser to somebody else's contract.
void checkRoutedIcons(const QJsonArray &list)
{
    for (const QJsonValue &v : list) {
        if (!v.isObject())
            continue;
        const QJsonObject entry = v.toObject();
        const QString icon = entry.value(QStringLiteral("icon")).toString();
        const QString scheme = QUrl(icon).scheme().toLower();
        if (scheme == QLatin1String("http") || scheme == QLatin1String("https"))
            __builtin_trap();   // reachable by Qt Quick
        const QJsonValue nested = entry.value(QStringLiteral("subitems"));
        if (nested.isArray())
            checkRoutedIcons(nested.toArray());
    }
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > 128 * 1024)
        return 0;

    const QByteArray raw(reinterpret_cast<const char *>(data), int(size));
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError)
        return 0;

    if (doc.isObject()) {
        const QJsonObject root = doc.object();
        const QJsonArray parsed = oslist::parseOSJson(root);
        for (const QJsonValue &v : parsed) {
            const QString encoded =
                v.toObject().value(QStringLiteral("subitems_json")).toString();
            if (encoded.isEmpty())
                continue;
            QJsonParseError subErr{};
            const QJsonDocument subDoc =
                QJsonDocument::fromJson(encoded.toUtf8(), &subErr);
            if (subErr.error == QJsonParseError::NoError && subDoc.isArray())
                checkRoutedIcons(subDoc.array());
        }
        (void)oslist::getListForLocale(root);
        (void)oslist::getListForLocale(root, QStringLiteral("pt_BR"));
        (void)oslist::getListForLocale(root, QString::fromUtf8(raw.left(16)));
    } else if (doc.isArray()) {
        QJsonArray list = doc.array();

        // Sorting is how the picker decides what to show first. It reorders
        // in place, so what has to hold is that it is a reordering: an entry
        // dropped here is an operating system missing from the list, and one
        // duplicated is one offered twice. Held to short lists because the
        // comparison is the expensive part of the case, not the sort.
        if (list.size() <= 64) {
            const QStringList before = fingerprints(list);

            oslist::applyArchitectureSorting(list, QStringLiteral("arm64"));
            if (fingerprints(list) != before)
                __builtin_trap();   // sorting changed what is in the list

            oslist::applyArchitectureSorting(list, QString::fromUtf8(raw.left(8)));
            if (fingerprints(list) != before)
                __builtin_trap();

            // Filtering may only take entries away, and what it leaves has
            // to be what it claims to leave: an entry whose init_format it
            // rejects, still in the list, is an image written with a
            // customisation the OS cannot run.
            const QJsonArray kept = oslist::filterInvalidInitFormats(list);
            if (kept.size() > list.size())
                __builtin_trap();   // filtering added entries

            const QStringList keptPrints = fingerprints(kept);
            for (const QString &print : keptPrints)
                if (!before.contains(print))
                    __builtin_trap();   // an entry nobody put in

            for (const QJsonValue &v : kept) {
                if (!v.isObject())
                    continue;
                const QJsonValue fmt = v.toObject().value(QStringLiteral("init_format"));
                if (fmt.isString() && !oslist::isValidInitFormat(fmt.toString()))
                    __builtin_trap();   // kept, and it is not one
            }
        } else {
            (void)oslist::filterInvalidInitFormats(list);
            oslist::applyArchitectureSorting(list, QStringLiteral("arm64"));
            oslist::applyArchitectureSorting(list, QString::fromUtf8(raw.left(8)));
        }
    }

    checkByteCounts(doc.isObject() ? QJsonValue(doc.object())
                                   : QJsonValue(doc.array()), 0);

    checkIcon(QString::fromUtf8(raw.left(512)));
    // The forms the sanitiser branches on, built from the input rather than
    // waited for: the corpus is JSON, so a bare "icons/..." or "qrc:/..."
    // would otherwise only ever appear inside a quoted string.
    checkIcon(QStringLiteral("icons/") + QString::fromUtf8(raw.left(64)));
    checkIcon(QStringLiteral("qrc:/") + QString::fromUtf8(raw.left(64)));
    checkIcon(QString::fromUtf8(raw.left(64)) + QStringLiteral("://")
              + QString::fromUtf8(raw.mid(64, 64)));

    (void)oslist::isValidInitFormat(QString::fromUtf8(raw.left(32)));
    checkRouting(QString::fromUtf8(raw.left(256)));
    checkRouting(QStringLiteral("icons/") + QString::fromUtf8(raw.left(64)));
    return 0;
}
