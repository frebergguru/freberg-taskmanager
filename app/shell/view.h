// One page of the window. Views are created the first time they are opened, and
// state which subsystems they need, so what is not on screen is not sampled.
#pragma once

#include "core/snapshot.h"

#include <QWidget>

namespace fg {

class SamplerHub;

class View : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;

    virtual Subsystems wants() const { return SubNone; }
    virtual void onSnapshot(const Snapshot&) {}

    // Called whenever the page is opened. Views with no timer load their data here.
    virtual void opened() {}
    // Called when the page is left. Heavy views drop their data here.
    virtual void closed() {}
    // The user asked for fresh data.
    virtual void refresh() {}
    // Only the Processes view does anything with this.
    virtual void goToProcess(const ProcessId&) {}

signals:
    void wantsChanged();
    void openProcess(const fg::ProcessId& id);
};

struct ViewDef {
    QString title;
    View* (*create)(SamplerHub& hub);
};

QList<ViewDef> allViews();

} // namespace fg
