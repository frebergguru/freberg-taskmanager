#include "../shell/view.h"
#include "apphistory.h"
#include "performance.h"
#include "placeholder.h"
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
        {tr("Power && Freq"), [](SamplerHub&) -> View* { return new PlaceholderView; }},
        {tr("Connections"), [](SamplerHub&) -> View* { return new PlaceholderView; }},
        {tr("Installed Apps"), [](SamplerHub&) -> View* { return new PlaceholderView; }},
        {tr("Drivers"), [](SamplerHub&) -> View* { return new PlaceholderView; }},
        {tr("Disk Space"), [](SamplerHub&) -> View* { return new PlaceholderView; }},
        {tr("Benchmarks"), [](SamplerHub&) -> View* { return new PlaceholderView; }},
        {tr("Flight Recorder"), [](SamplerHub&) -> View* { return new PlaceholderView; }},
    };
}

} // namespace fg
