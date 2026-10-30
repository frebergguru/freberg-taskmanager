#pragma once

#include "../shell/view.h"

class QLineEdit;
class QPlainTextEdit;
class QTreeWidget;

namespace fg {

class SamplerHub;

class DriversView final : public View {
    Q_OBJECT
public:
    explicit DriversView(SamplerHub& hub);
    void opened() override { refresh(); }
    void closed() override;
    void refresh() override;

private:
    void applyFilter();
    void showDetails();

    SamplerHub& m_hub;
    QTreeWidget* m_list;
    QLineEdit* m_filter;
    QPlainTextEdit* m_details;
};

} // namespace fg
