#include "thermals.h"

#include "../format.h"
#include "../widgets/graph.h"
#include "../widgets/infogrid.h"
#include "../widgets/sortitem.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <climits>

namespace fg {

namespace {

constexpr double kGraphTopC = 110.0;
constexpr double kNearLimitC = 15.0;
constexpr double kAtLimitC = 5.0;
constexpr int kSensorRole = Qt::UserRole;

enum SensorColumn { ChipColumn, NameColumn, NowColumn, LowColumn, HighColumn, LimitColumn, MarginColumn, StateColumn };

// A difference between two temperatures, which has no offset in Fahrenheit.
QString degreesApart(double celsius)
{
    return Format::temperatureDifference(celsius);
}

} // namespace

ThermalsView::ThermalsView()
    : m_graphTitle(new QLabel), m_graph(new Graph), m_grid(new InfoGrid), m_sensors(new QTreeWidget), m_fans(new QTreeWidget), m_fansLabel(new QLabel(tr("&Fans:"))), m_noFans(new QLabel)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* top = new QHBoxLayout;
    auto* graphColumn = new QVBoxLayout;
    QFont bold = m_graphTitle->font();
    bold.setBold(true);
    m_graphTitle->setFont(bold);
    graphColumn->addWidget(m_graphTitle);
    graphColumn->addWidget(m_graph, 1);
    m_graph->setFixedMaximum(kGraphTopC);
    top->addLayout(graphColumn, 3);
    top->addWidget(m_grid, 2);

    auto* upper = new QWidget;
    upper->setLayout(top);
    top->setContentsMargins(0, 0, 0, 0);

    m_sensors->setRootIsDecorated(false);
    m_sensors->setUniformRowHeights(true);
    // No column chosen yet, so rows stay in the system's order until a header is clicked.
    m_sensors->header()->setSortIndicator(-1, Qt::AscendingOrder);
    m_sensors->setSortingEnabled(true);
    m_sensors->setHeaderLabels({tr("Chip"), tr("Sensor"), tr("Now"), tr("Lowest"), tr("Highest"), tr("Limit"), tr("Margin"), tr("State")});
    m_sensors->setAccessibleName(tr("Temperature sensors. Choose one to see its history in the graph."));
    const int unit = fontMetrics().averageCharWidth();
    m_sensors->setColumnWidth(ChipColumn, unit * 12);
    m_sensors->setColumnWidth(NameColumn, unit * 14);
    for (const int column : {NowColumn, LowColumn, HighColumn, LimitColumn, MarginColumn})
        m_sensors->setColumnWidth(column, unit * 13);

    auto* fansBox = new QWidget;
    auto* fansLayout = new QVBoxLayout(fansBox);
    fansLayout->setContentsMargins(0, 0, 0, 0);
    m_fansLabel->setBuddy(m_fans);
    m_fans->setRootIsDecorated(false);
    m_fans->setUniformRowHeights(true);
    m_fans->setHeaderLabels({tr("Chip"), tr("Fan"), tr("Speed"), tr("Power")});
    m_fans->setAccessibleName(tr("Fans"));
    m_noFans->setText(tr("This system reports no fan sensors. Many laptops leave the fans to their firmware."));
    m_noFans->setWordWrap(true);
    fansLayout->addWidget(m_fansLabel);
    fansLayout->addWidget(m_fans, 1);
    fansLayout->addWidget(m_noFans);
    fansLayout->addStretch(0);

    auto* split = new QSplitter(Qt::Vertical);
    split->addWidget(upper);
    split->addWidget(m_sensors);
    split->addWidget(fansBox);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 3);
    split->setStretchFactor(2, 0);
    layout->addWidget(split, 1);

    connect(m_sensors, &QTreeWidget::currentItemChanged, this, &ThermalsView::showSelected);
    showSelected();
}

void ThermalsView::samplesMissed()
{
    if (m_hotspot.size() > 0) {
        m_hotspot.push(History::gap());
        for (Tracked& tracked : m_tracked)
            tracked.history.push(History::gap());
        m_graph->update();
    }
}

void ThermalsView::showSelected()
{
    const QTreeWidgetItem* item = m_sensors->currentItem();
    const int chosen = item ? item->data(0, kSensorRole).toInt() : -1;
    if (chosen == m_shownSensor)
        return;
    m_shownSensor = chosen;
    m_graph->clearSeries();
    if (chosen >= 0 && chosen < static_cast<int>(m_tracked.size())) {
        m_graphTitle->setText(tr("%1 %2").arg(item->text(ChipColumn), item->text(NameColumn)));
        m_graph->addSeries(&m_tracked[static_cast<std::size_t>(chosen)].history);
    } else {
        m_graphTitle->setText(tr("CPU hotspot"));
        m_graph->addSeries(&m_hotspot);
    }
    m_graph->setAccessibleName(m_graphTitle->text());
    m_graph->update();
}

QString ThermalsView::statusOf(const ThermalSensor& sensor) const
{
    if (!sensor.tempC.known())
        return Format::why(sensor.tempC.state);
    if (!sensor.limitC.known())
        return tr("No limit");
    const double margin = sensor.limitC.value - sensor.tempC.value;
    if (margin <= 0)
        return tr("Over the limit");
    if (margin <= kAtLimitC)
        return tr("At the limit");
    if (margin <= kNearLimitC)
        return tr("Near the limit");
    return tr("Normal");
}

void ThermalsView::onSnapshot(const Snapshot& snapshot)
{
    if (!(snapshot.collected & SubThermal))
        return;
    const ThermalSample& thermal = snapshot.thermal;

    m_hotspot.push(thermal.cpuHotspotC);
    while (m_tracked.size() < thermal.sensors.size())
        m_tracked.emplace_back();

    // Rows keep their place and selection, and only their text changes.
    const QLocale locale;
    const ThermalSensor* hottest = nullptr;
    const ThermalSensor* tightest = nullptr;
    for (std::size_t i = 0; i < thermal.sensors.size(); ++i) {
        const ThermalSensor& sensor = thermal.sensors[i];
        Tracked& tracked = m_tracked[i];
        tracked.history.push(sensor.tempC);
        if (sensor.tempC.known()) {
            const double now = sensor.tempC.value;
            tracked.lowest = tracked.lowest ? std::min(*tracked.lowest, now) : now;
            tracked.highest = tracked.highest ? std::max(*tracked.highest, now) : now;
            if (!hottest || now > hottest->tempC.value)
                hottest = &sensor;
            if (sensor.limitC.known() && (!tightest || sensor.limitC.value - now < tightest->limitC.value - tightest->tempC.value))
                tightest = &sensor;
        }

        // Sorting moves rows around, so each sensor keeps hold of its own.
        if (i == m_rows.size()) {
            auto* row = new SortItem(m_sensors);
            m_rows.push_back(row);
            row->setData(0, kSensorRole, static_cast<int>(i));
            row->setText(ChipColumn, QString::fromStdString(sensor.chip));
            row->setText(NameColumn, QString::fromStdString(sensor.label));
            for (const int column : {NowColumn, LowColumn, HighColumn, LimitColumn, MarginColumn})
                row->setTextAlignment(column, Qt::AlignRight | Qt::AlignVCenter);
        }
        SortItem* row = m_rows[i];
        const auto setIfChanged = [row](int column, const QString& text) {
            if (row->text(column) != text)
                row->setText(column, text);
        };
        setIfChanged(NowColumn, Format::temperature(sensor.tempC));
        setIfChanged(LowColumn, tracked.lowest ? Format::temperature(*tracked.lowest) : Format::why(sensor.tempC.state));
        setIfChanged(HighColumn, tracked.highest ? Format::temperature(*tracked.highest) : Format::why(sensor.tempC.state));
        setIfChanged(LimitColumn, Format::temperature(sensor.limitC));
        setIfChanged(MarginColumn, sensor.tempC.known() && sensor.limitC.known() ? degreesApart(sensor.limitC.value - sensor.tempC.value) : Format::why(sensor.limitC.known() ? sensor.tempC.state : sensor.limitC.state));
        setIfChanged(StateColumn, statusOf(sensor));
        constexpr double kUnknownSortsLast = -1e9;
        row->setSortValue(NowColumn, sensor.tempC.known() ? sensor.tempC.value : kUnknownSortsLast);
        row->setSortValue(LowColumn, tracked.lowest.value_or(kUnknownSortsLast));
        row->setSortValue(HighColumn, tracked.highest.value_or(kUnknownSortsLast));
        row->setSortValue(LimitColumn, sensor.limitC.known() ? sensor.limitC.value : kUnknownSortsLast);
        row->setSortValue(MarginColumn, sensor.tempC.known() && sensor.limitC.known() ? sensor.limitC.value - sensor.tempC.value : kUnknownSortsLast);
    }
    while (m_rows.size() > thermal.sensors.size()) {
        delete m_rows.back();
        m_rows.pop_back();
    }

    m_grid->set(tr("CPU hotspot:"), Format::temperature(thermal.cpuHotspotC));
    m_grid->set(tr("Hottest sensor:"), hottest ? tr("%1 %2: %3").arg(QString::fromStdString(hottest->chip), QString::fromStdString(hottest->label), Format::temperature(hottest->tempC)) : Format::why(Availability::Unsupported));
    m_grid->set(tr("Closest to its limit:"), tightest ? tr("%1 %2: %3 left").arg(QString::fromStdString(tightest->chip), QString::fromStdString(tightest->label), degreesApart(tightest->limitC.value - tightest->tempC.value)) : tr("No sensor here reports a limit"));

    if (thermal.throttleEvents.known()) {
        if (!m_firstThrottleCount)
            m_firstThrottleCount = thermal.throttleEvents.value;
        m_grid->set(tr("Thermal pressure:"), tr("%n throttle event(s) since start, %1 since this page opened", nullptr, static_cast<int>(std::min<std::uint64_t>(thermal.throttleEvents.value, INT_MAX))).arg(thermal.throttleEvents.value - *m_firstThrottleCount));
    } else {
        m_grid->set(tr("Thermal pressure:"), Format::why(thermal.throttleEvents.state));
    }

    const bool haveFans = !thermal.fans.empty();
    m_fans->setVisible(haveFans);
    m_fansLabel->setVisible(haveFans);
    m_noFans->setVisible(!haveFans);
    for (std::size_t i = 0; i < thermal.fans.size(); ++i) {
        const FanSample& fan = thermal.fans[i];
        QTreeWidgetItem* row = m_fans->topLevelItem(static_cast<int>(i));
        if (!row) {
            row = new QTreeWidgetItem(m_fans);
            row->setText(0, QString::fromStdString(fan.chip));
            row->setText(1, QString::fromStdString(fan.label));
        }
        row->setText(2, fan.rpm.known() ? tr("%1 RPM").arg(locale.toString(fan.rpm.value, 'f', 0)) : Format::why(fan.rpm.state));
        row->setText(3, Format::percent(fan.dutyPct));
    }
    while (m_fans->topLevelItemCount() > static_cast<int>(thermal.fans.size()))
        delete m_fans->takeTopLevelItem(m_fans->topLevelItemCount() - 1);

    m_graph->sampled(tr("%1: %2").arg(m_graphTitle->text(), m_shownSensor >= 0 && m_shownSensor < static_cast<int>(thermal.sensors.size()) ? Format::temperature(thermal.sensors[static_cast<std::size_t>(m_shownSensor)].tempC) : Format::temperature(thermal.cpuHotspotC)));
}

} // namespace fg
