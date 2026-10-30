#include "../shell/view.h"
#include "apphistory.h"
#include "benchmarks.h"
#include "connections.h"
#include "diskspace.h"
#include "drivers.h"
#include "flightrecorder.h"
#include "installedapps.h"
#include "powerfreq.h"
#include "performance.h"
#include "processes.h"
#include "services.h"
#include "startup.h"
#include "summary.h"
#include "sysinfo.h"
#include "users.h"

#include <QCoreApplication>

namespace fg {

QList<ViewDef> allViews()
{
    const auto title = [](const char* text) { return QCoreApplication::translate("fg::Views", text); };
    return {
        {title(QT_TRANSLATE_NOOP("fg::Views", "Summary")), [](SamplerHub&) -> View* { return new SummaryView; }},
        {title(QT_TRANSLATE_NOOP("fg::Views", "Performance")), [](SamplerHub& hub) -> View* { return new PerformanceView(hub); }},
        {title(QT_TRANSLATE_NOOP("fg::Views", "Processes")), [](SamplerHub& hub) -> View* { return new ProcessesView(hub); }},
        {title(QT_TRANSLATE_NOOP("fg::Views", "App History")), [](SamplerHub& hub) -> View* { return new AppHistoryView(hub); }},
        {title(QT_TRANSLATE_NOOP("fg::Views", "System Info")), [](SamplerHub& hub) -> View* { return new SystemInfoView(hub); }},
        {title(QT_TRANSLATE_NOOP("fg::Views", "Startup")), [](SamplerHub&) -> View* { return new StartupView; }},
        {title(QT_TRANSLATE_NOOP("fg::Views", "Users")), [](SamplerHub&) -> View* { return new UsersView; }},
        {title(QT_TRANSLATE_NOOP("fg::Views", "Services")), [](SamplerHub&) -> View* { return new ServicesView; }},
        {title(QT_TRANSLATE_NOOP("fg::Views", "Power && Freq")), [](SamplerHub& hub) -> View* { return new PowerFreqView(hub); }},
        {title(QT_TRANSLATE_NOOP("fg::Views", "Connections")), [](SamplerHub& hub) -> View* { return new ConnectionsView(hub); }},
        {title(QT_TRANSLATE_NOOP("fg::Views", "Installed Apps")), [](SamplerHub&) -> View* { return new InstalledAppsView; }},
        {title(QT_TRANSLATE_NOOP("fg::Views", "Drivers")), [](SamplerHub& hub) -> View* { return new DriversView(hub); }},
        {title(QT_TRANSLATE_NOOP("fg::Views", "Disk Space")), [](SamplerHub&) -> View* { return new DiskSpaceView; }},
        {title(QT_TRANSLATE_NOOP("fg::Views", "Benchmarks")), [](SamplerHub&) -> View* { return new BenchmarksView; }},
        {title(QT_TRANSLATE_NOOP("fg::Views", "Flight Recorder")), [](SamplerHub& hub) -> View* { return new FlightRecorderView(hub); }},
    };
}

} // namespace fg
