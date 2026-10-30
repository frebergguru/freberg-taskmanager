#include "app/style/scheme.h"

#include <QGuiApplication>
#include <QTest>

using namespace fg;

class TestScheme : public QObject {
    Q_OBJECT
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
        const Scheme& s = schemeById(scheme);
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
        check(s.graphLine, s.graphBack, 3.0, "graph line");
        check(s.graphLine2, s.graphBack, 3.0, "second graph line");
    }
};

QTEST_MAIN(TestScheme)
#include "test_scheme.moc"
