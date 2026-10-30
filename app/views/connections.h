#pragma once

#include "../shell/view.h"
#include "platform/backend.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QTimer;
class QTreeWidget;

namespace fg {

class SamplerHub;

class ConnectionsView final : public View {
    Q_OBJECT
public:
    explicit ConnectionsView(SamplerHub& hub);
    void opened() override;
    void closed() override;
    void refresh() override;

private:
    void applyFilter();
    void endOwner();

    SamplerHub& m_hub;
    QTreeWidget* m_list;
    QLineEdit* m_filter;
    QCheckBox* m_auto;
    QCheckBox* m_showUnix;
    QLabel* m_message;
    QTimer* m_timer;
};

} // namespace fg
