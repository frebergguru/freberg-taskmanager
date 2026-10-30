#pragma once

#include "../shell/view.h"

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;

namespace fg {

class ServicesView final : public View {
    Q_OBJECT
public:
    ServicesView();
    void opened() override { refresh(); }
    void closed() override;
    void refresh() override;

private:
    QString selectedUnit() const;
    void act(const char* method, const QString& label);
    void setEnabledState(bool enabled);
    void showJournal();
    void listWithoutSystemd();
    void applyFilter();

    QTreeWidget* m_list;
    QLineEdit* m_filter;
    QPlainTextEdit* m_journal;
    QLabel* m_message;
    QPushButton* m_start;
    QPushButton* m_stop;
    QPushButton* m_restart;
    QPushButton* m_enable;
    QPushButton* m_disable;
    bool m_managed = true;
};

} // namespace fg
