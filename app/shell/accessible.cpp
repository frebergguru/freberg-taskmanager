#include "accessible.h"

#include "../popout/popout.h"
#include "../widgets/graph.h"
#include "sheet.h"
#include "titlebar.h"

#include <QAccessible>
#include <QAccessibleWidget>

namespace fg {

namespace {

class TabInterface final : public QAccessibleWidget {
public:
    explicit TabInterface(SheetTab* tab) : QAccessibleWidget(tab, QAccessible::PageTab) {}

    QAccessible::State state() const override
    {
        QAccessible::State s = QAccessibleWidget::state();
        s.selectable = true;
        s.selected = static_cast<SheetTab*>(widget())->isChecked();
        return s;
    }

    QStringList actionNames() const override { return {pressAction()}; }

    void doAction(const QString& name) override
    {
        if (name == pressAction())
            static_cast<SheetTab*>(widget())->click();
    }
};

class ButtonInterface final : public QAccessibleWidget {
public:
    explicit ButtonInterface(CaptionButton* button) : QAccessibleWidget(button, QAccessible::Button) {}

    QStringList actionNames() const override { return {pressAction()}; }

    void doAction(const QString& name) override
    {
        if (name == pressAction())
            static_cast<CaptionButton*>(widget())->click();
    }
};

// What a chart says is its description, which is kept up to date with each sample.
QAccessibleInterface* factory(const QString& className, QObject* object)
{
    if (!object || !object->isWidgetType())
        return nullptr;
    auto* widget = static_cast<QWidget*>(object);
    if (className == QLatin1String("fg::SheetTab"))
        return new TabInterface(static_cast<SheetTab*>(widget));
    if (className == QLatin1String("fg::CaptionButton"))
        return new ButtonInterface(static_cast<CaptionButton*>(widget));
    if (className == QLatin1String("fg::Graph") || className == QLatin1String("fg::TraceStrip"))
        return new QAccessibleWidget(widget, QAccessible::Chart);
    if (className == QLatin1String("fg::Treemap"))
        return new QAccessibleWidget(widget, QAccessible::Graphic);
    if (className == QLatin1String("fg::Popout") || className == QLatin1String("fg::Screensaver"))
        return new QAccessibleWidget(widget, QAccessible::Window);
    return nullptr;
}

} // namespace

void installAccessibility()
{
    QAccessible::installFactory(factory);
}

} // namespace fg
