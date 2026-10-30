#include "inspector.h"

#include "../format.h"
#include "../shell/sheet.h"

#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace fg {

namespace {

QWidget* textPage(const QString& name, const QString& text)
{
    auto* edit = new QPlainTextEdit(text);
    edit->setReadOnly(true);
    edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    edit->setAccessibleName(name);
    return edit;
}

QString joined(const std::vector<std::string>& lines)
{
    QStringList out;
    out.reserve(static_cast<qsizetype>(lines.size()));
    for (const std::string& line : lines)
        out << QString::fromStdString(line);
    return out.join(QLatin1Char('\n'));
}

// Most of /proc is closed for other users' processes; say so instead of showing nothing.
QString orDenied(const std::string& text)
{
    return text.empty() ? Format::why(Availability::AccessDenied) : QString::fromStdString(text);
}

QString orDenied(const std::vector<std::string>& lines)
{
    return lines.empty() ? InspectorDialog::tr("None visible. Other users' processes are closed to us.") : joined(lines);
}

} // namespace

InspectorDialog::InspectorDialog(const ProcessInfo& info, const ProcessDetails& details, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Properties: %1 (PID %2)").arg(QString::fromStdString(info.name)).arg(info.id.pid));
    resize(620, 460);

    auto* layout = new QVBoxLayout(this);
    auto* sheet = new Sheet(this);

    const QString general = tr("Executable: %1\nCommand line: %2\ncgroup: %3\n\n%4")
                                .arg(QString::fromStdString(info.exePath), QString::fromStdString(info.commandLine), Format::text(info.cgroup), orDenied(details.status));
    sheet->addPage(tr("General"), textPage(tr("General"), general));
    sheet->addPage(tr("Open Files"), textPage(tr("Open Files"), orDenied(details.openFiles)));
    sheet->addPage(tr("Sockets"), textPage(tr("Sockets"), orDenied(details.sockets)));
    sheet->addPage(tr("Threads"), textPage(tr("Threads"), orDenied(details.threads)));
    sheet->addPage(tr("Memory Map"), textPage(tr("Memory Map"), orDenied(details.memoryMaps)));
    sheet->addPage(tr("Environment"), textPage(tr("Environment"), orDenied(details.environment)));
    sheet->addPage(tr("Limits"), textPage(tr("Limits"), orDenied(details.limits)));
    sheet->setCurrentIndex(0);
    layout->addWidget(sheet, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

} // namespace fg
