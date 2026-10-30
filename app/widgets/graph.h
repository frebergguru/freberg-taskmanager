// A line graph on a dark grid. It draws only when told a new sample has arrived,
// and a missing sample stays a gap: the line is never carried across it.
#pragma once

#include "core/history.h"

#include <QList>
#include <QWidget>

namespace fg {

class Graph final : public QWidget {
    Q_OBJECT
public:
    explicit Graph(QWidget* parent = nullptr);

    void addSeries(const History* history, bool secondary = false);
    void clearSeries();

    // A fixed scale, such as 0-100 for percentages. Without it the scale follows the data.
    void setFixedMaximum(double maximum) { m_fixedMax = maximum; }

    // Call after pushing a sample. description is what a screen reader hears.
    void sampled(const QString& description);

    QSize sizeHint() const override { return QSize(240, 120); }
    QSize minimumSizeHint() const override { return QSize(48, 32); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct Series {
        const History* history;
        bool secondary;
    };
    QList<Series> m_series;
    double m_fixedMax = 0;
    int m_scroll = 0;
};

} // namespace fg
