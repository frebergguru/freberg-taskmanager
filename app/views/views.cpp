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
    const auto tr = [](const char* text) { return QCoreApplication::translate("fg::Views", text); };
    return {
        {tr("Summary"), [](SamplerHub&) -> View* { return new SummaryView; }},
        {tr("Performance"), [](SamplerHub& hub) -> View* { return new PerformanceView(hub); }},
        {tr("Processes"), [](SamplerHub& hub) -> View* { return new ProcessesView(hub); }},
        {tr("App History"), [](SamplerHub& hub) -> View* { return new AppHistoryView(hub); }},
        {tr("System Info"), [](SamplerHub& hub) -> View* { return new SystemInfoView(hub); }},
        {tr("Startup"), [](SamplerHub&) -> View* { return new StartupView; }},
        {tr("Users"), [](SamplerHub&) -> View* { return new UsersView; }},
        {tr("Services"), [](SamplerHub&) -> View* { return new ServicesView; }},
        {tr("Power && Freq"), [](SamplerHub& hub) -> View* { return new PowerFreqView(hub); }},
        {tr("Connections"), [](SamplerHub& hub) -> View* { return new ConnectionsView(hub); }},
        {tr("Installed Apps"), [](SamplerHub&) -> View* { return new InstalledAppsView; }},
        {tr("Drivers"), [](SamplerHub& hub) -> View* { return new DriversView(hub); }},
        {tr("Disk Space"), [](SamplerHub&) -> View* { return new DiskSpaceView; }},
        {tr("Benchmarks"), [](SamplerHub&) -> View* { return new BenchmarksView; }},
        {tr("Flight Recorder"), [](SamplerHub& hub) -> View* { return new FlightRecorderView(hub); }},
    };
}

} // namespace fg
