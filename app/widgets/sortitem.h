// A list row whose number columns sort by value, though they are shown as text.
#pragma once

#include <QTreeWidgetItem>

namespace fg {

class SortItem final : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;

    void setSortValue(int column, double value) { setData(column, kRole, value); }

    bool operator<(const QTreeWidgetItem& other) const override
    {
        const int column = treeWidget()->sortColumn();
        const QVariant mine = data(column, kRole);
        const QVariant theirs = other.data(column, kRole);
        return mine.isValid() && theirs.isValid() ? mine.toDouble() < theirs.toDouble() : QTreeWidgetItem::operator<(other);
    }

private:
    static constexpr int kRole = Qt::UserRole + 8;
};

} // namespace fg
