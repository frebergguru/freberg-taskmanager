#include "app/style/scheme.h"

#include <QGuiApplication>
#include <QTest>

using namespace fg;

class TestScheme : public QObject {
    Q_OBJECT

    static void checkContrast(const Scheme& s)
    {
        const auto check = [](const QColor& a, const QColor& b, double need, const char* what) {
            const double ratio = contrastRatio(a, b);
            if (ratio < need)
                QFAIL(qPrintable(QStringLiteral("%1: %2 < %3 (%4 on %5)").arg(what).arg(ratio).arg(need).arg(a.name(), b.name())));
        };
        check(s.text, s.face, 4.5, "text on face");
        check(s.text, s.base, 4.5, "text on base");
        check(s.disabledText, s.face, 4.5, "disabled text on face");
        check(s.selectionText, s.selection, 4.5, "selected text");
        check(s.titleText, s.titleStart, 4.5, "title text"); // the title sits at the left end
        if (s.design != Design::Classic) {
            check(s.accent, s.graphBack, 3.0, "accent");
            check(s.text, s.hover, 4.5, "text on a hovered or chosen row");
        }
        check(s.graphLine, s.graphBack, 3.0, "graph line");
        check(s.graphLine2, s.graphBack, 3.0, "second graph line");
    }

private slots:
    void textMeetsAA_data()
    {
        QTest::addColumn<QString>("scheme");
        for (const Scheme& s : schemes())
            QTest::newRow(qPrintable(s.id)) << s.id;
    }

    void textMeetsAA()
    {
        QFETCH(QString, scheme);
        for (int saturation = 0; saturation <= 10; ++saturation) {
            const Scheme s = withSaturation(schemeById(scheme), saturation);
            checkContrast(s);
            if (QTest::currentTestFailed())
                QFAIL(qPrintable(QStringLiteral("at saturation %1").arg(saturation)));
        }
    }

    void everyThemeHasALightAndADarkMember()
    {
        const QStringList families = schemeFamilies();
        QCOMPARE(families.size(), 11);
        QCOMPARE(schemes().size(), 22);
        for (const QString& family : families) {
            const Scheme& light = schemeFor(family, false);
            const Scheme& dark = schemeFor(family, true);
            QCOMPARE(light.family, family);
            QCOMPARE(dark.family, family);
            QVERIFY(!light.isDark);
            QVERIFY(dark.isDark);
            QCOMPARE(light.design, dark.design);
            // A dark member really is darker, on the page as well as the window.
            QVERIFY2(contrastRatio(dark.base, QColor(0, 0, 0)) < contrastRatio(light.base, QColor(0, 0, 0)), qPrintable(family));
            QVERIFY2(contrastRatio(dark.face, QColor(0, 0, 0)) < contrastRatio(light.face, QColor(0, 0, 0)), qPrintable(family));
        }
    }

    void onlyTheNeonThemeHasTheNeonDesign()
    {
        int neon = 0, modern = 0;
        for (const Scheme& s : schemes()) {
            neon += s.design == Design::Neon;
            modern += s.design == Design::Modern;
        }
        QCOMPARE(neon, 2);
        QCOMPARE(modern, 2);
        QCOMPARE(schemeFor("neon", false).design, Design::Neon);
        QCOMPARE(schemeFor("modern", true).design, Design::Modern);
    }

    void earlierSavedChoicesStillWork()
    {
        QCOMPARE(schemeById("standard").id, QStringLiteral("classic-light")); // also what a new install gets
        QCOMPARE(schemeById("dark").id, QStringLiteral("classic-dark"));
        QCOMPARE(schemeById("green").id, QStringLiteral("green-dark"));
        QCOMPARE(schemeById("no-such-theme").id, QStringLiteral("classic-light"));
    }
};

QTEST_MAIN(TestScheme)
#include "test_scheme.moc"
