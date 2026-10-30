#pragma once

#include <QDialog>

namespace fg {

class OptionsDialog final : public QDialog {
    Q_OBJECT
public:
    explicit OptionsDialog(QWidget* parent = nullptr);
};

} // namespace fg
