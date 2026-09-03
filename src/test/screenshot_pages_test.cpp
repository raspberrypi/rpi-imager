// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2025 Raspberry Pi Ltd
//
// The screenshot runner is a custom target rather than a test: it wants fonts,
// a user namespace and several seconds a page, and its output is meant to be
// looked at. What can be checked cheaply is the page list it is driven from,
// which names things in two other files and is silently wrong when either
// moves. A page naming a step the wizard does not have fails a thirteen-page
// run at the one page; a published name drifting from the metainfo ships a
// screenshot nothing points at, and leaves the listing showing the old one.

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

namespace {

QString readAll(const QString &path)
{
    QFile f(path);
    REQUIRE(f.open(QIODevice::ReadOnly | QIODevice::Text));
    return QString::fromUtf8(f.readAll());
}

struct PageRow
{
    QString page;
    QString mode;
    QString published;
};

// The list as shots.sh reads it: comments and blank lines dropped, three
// whitespace-separated columns kept.
std::vector<PageRow> pageRows()
{
    const QStringList lines = readAll(QStringLiteral(SCREENSHOT_PAGES_FILE))
                                  .split(QLatin1Char('\n'));
    std::vector<PageRow> rows;
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
            continue;
        const QStringList cols = trimmed.split(QRegularExpression(QStringLiteral("\\s+")),
                                               Qt::SkipEmptyParts);
        REQUIRE(cols.size() == 3);
        rows.push_back({cols.at(0), cols.at(1), cols.at(2)});
    }
    return rows;
}

} // namespace

TEST_CASE("Every page photographed names a step the wizard has", "[screenshots]")
{
    // The runner passes the page straight through as RPI_IMAGER_SCREENSHOT_STEP,
    // and the hook looks for a "step" + name property on the container. A page
    // the wizard has since renamed is only found when the run reaches it.
    const QString qml = readAll(QStringLiteral(WIZARD_CONTAINER_QML));

    for (const PageRow &row : pageRows()) {
        INFO("page " << row.page.toStdString());
        const QRegularExpression declared(
            QStringLiteral("property\\s+int\\s+step%1\\b").arg(row.page));
        CHECK(declared.match(qml).hasMatch());
    }
}

TEST_CASE("The page list uses write only where a write is needed", "[screenshots]")
{
    // Writing and Done are the two pages that cannot be reached by opening
    // them -- both are drawn from a source, a destination and the wizard's
    // summary, which a jump supplies none of. Marking any other page "write"
    // would stage a pointless write; marking these "jump" would photograph a
    // page reporting that nothing was selected.
    const QSet<QString> needAWrite = {QStringLiteral("Writing"), QStringLiteral("Done")};

    for (const PageRow &row : pageRows()) {
        INFO("page " << row.page.toStdString());
        CHECK((row.mode == QLatin1String("jump") || row.mode == QLatin1String("write")));
        CHECK((row.mode == QLatin1String("write")) == needAWrite.contains(row.page));
    }
}

TEST_CASE("The published screenshots are exactly the ones the metainfo points at",
          "[screenshots]")
{
    // The metainfo is what the store listings read, so a name only it knows is
    // a picture that never gets regenerated, and a name only the page list
    // knows is one that is generated and then never published.
    const QString metainfo = readAll(QStringLiteral(METAINFO_FILE));

    QSet<QString> published;
    for (const PageRow &row : pageRows()) {
        if (row.published == QLatin1String("-"))
            continue;
        INFO("page " << row.page.toStdString());
        CHECK_FALSE(published.contains(row.published));
        published.insert(row.published);
    }

    QSet<QString> referenced;
    QRegularExpressionMatchIterator it =
        QRegularExpression(QStringLiteral("([A-Za-z0-9._-]+)\\.png")).globalMatch(metainfo);
    while (it.hasNext())
        referenced.insert(it.next().captured(1));

    CHECK(published == referenced);
}
