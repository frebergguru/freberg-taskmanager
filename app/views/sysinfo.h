#pragma once

#include "../shell/view.h"

namespace fg {

class InfoGrid;
class SamplerHub;

class SystemInfoView final : public View {
    Q_OBJECT
public:
    explicit SystemInfoView(SamplerHub& hub);
    void opened() override { refresh(); }
    void refresh() override;

private:
    SamplerHub& m_hub;
    InfoGrid* m_grid;
};

} // namespace fg
