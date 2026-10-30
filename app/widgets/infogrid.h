// A two-column list of labelled values. Labels are only touched when their text
// really changes, so a steady value causes no repaint.
#pragma once

#include <QHash>
#include <QLabel>
#include <QWidget>

class QGridLayout;

namespace fg {

class InfoGrid final : public QWidget {
    Q_OBJECT
public:
    explicit InfoGrid(QWidget* parent = nullptr);
    void set(const QString& key, const QString& value);
    void clear();

private:
    QGridLayout* m_layout;
    QHash<QString, QLabel*> m_values;
    int m_rows = 0;
};

} // namespace fg
