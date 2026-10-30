// Stands in for a view that is not built yet. Removed when the last view lands.
#pragma once

#include "../shell/view.h"

#include <QLabel>
#include <QVBoxLayout>

namespace fg {

class PlaceholderView final : public View {
    Q_OBJECT
public:
    PlaceholderView()
    {
        auto* layout = new QVBoxLayout(this);
        auto* label = new QLabel(tr("Not built yet."), this);
        label->setAlignment(Qt::AlignCenter);
        layout->addWidget(label);
    }
};

} // namespace fg
