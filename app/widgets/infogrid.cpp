#include "infogrid.h"

#include <QGridLayout>

namespace fg {

InfoGrid::InfoGrid(QWidget* parent) : QWidget(parent), m_layout(new QGridLayout(this))
{
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setHorizontalSpacing(12);
    m_layout->setVerticalSpacing(1);
    m_layout->setColumnStretch(1, 1);
    m_layout->setAlignment(Qt::AlignTop);
}

void InfoGrid::set(const QString& key, const QString& value)
{
    auto found = m_values.find(key);
    if (found == m_values.end()) {
        auto* name = new QLabel(key, this);
        auto* shown = new QLabel(this);
        shown->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        shown->setWordWrap(true);
        name->setBuddy(shown);
        shown->setAccessibleName(key);
        m_layout->addWidget(name, m_rows, 0, Qt::AlignTop);
        m_layout->addWidget(shown, m_rows, 1, Qt::AlignTop);
        ++m_rows;
        found = m_values.insert(key, shown);
    }
    if ((*found)->text() != value)
        (*found)->setText(value);
}

void InfoGrid::clear()
{
    while (QLayoutItem* item = m_layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_values.clear();
    m_rows = 0;
}

} // namespace fg
