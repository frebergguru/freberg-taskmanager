// Everything about one process, read once when the dialog opens.
#pragma once

#include "core/snapshot.h"
#include "platform/backend.h"

#include <QDialog>

namespace fg {

class InspectorDialog final : public QDialog {
    Q_OBJECT
public:
    InspectorDialog(const ProcessInfo& info, const ProcessDetails& details, QWidget* parent = nullptr);
};

} // namespace fg
