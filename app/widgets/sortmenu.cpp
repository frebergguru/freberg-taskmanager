#include "sortmenu.h"

#include <QAbstractItemModel>
#include <QActionGroup>
#include <QCoreApplication>
#include <QHeaderView>
#include <QMenu>
#include <QTreeView>

namespace fg {

QTreeView* sortableList(const QWidget* page)
{
    if (!page)
        return nullptr;
    QTreeView* first = nullptr;
    for (QTreeView* list : page->findChildren<QTreeView*>()) {
        if (!list->isSortingEnabled() || !list->isVisibleTo(page))
            continue;
        if (list->hasFocus())
            return list;
        if (!first)
            first = list;
    }
    return first;
}

void fillSortMenu(QMenu* menu, QTreeView* list)
{
    menu->clear();
    if (!list || !list->model())
        return;

    const QHeaderView* header = list->header();
    const int sorted = header->sortIndicatorSection();
    const Qt::SortOrder order = header->sortIndicatorOrder();
    auto* columns = new QActionGroup(menu);
    bool sortedColumnShown = false;
    for (int column = 0; column < list->model()->columnCount(); ++column) {
        if (list->isColumnHidden(column))
            continue;
        QString name = list->model()->headerData(column, Qt::Horizontal).toString();
        name.replace(QLatin1Char('&'), QLatin1String("&&")); // a heading has no underlined letter
        QAction* action = menu->addAction(name);
        action->setCheckable(true);
        action->setChecked(column == sorted);
        sortedColumnShown = sortedColumnShown || column == sorted;
        columns->addAction(action);
        // Going through the list, so a page that deletes it while the menu is open can't leave this dangling.
        QObject::connect(action, &QAction::triggered, list, [list, column, order] { list->sortByColumn(column, order); });
    }

    menu->addSeparator();
    auto* orders = new QActionGroup(menu);
    const struct {
        QString label;
        Qt::SortOrder order;
    } choices[] = {{QCoreApplication::translate("fg::SortMenu", "&Ascending"), Qt::AscendingOrder}, {QCoreApplication::translate("fg::SortMenu", "&Descending"), Qt::DescendingOrder}};
    for (const auto& choice : choices) {
        QAction* action = menu->addAction(choice.label);
        action->setCheckable(true);
        action->setChecked(sortedColumnShown && order == choice.order);
        action->setEnabled(sortedColumnShown); // an order means nothing until a column is chosen
        orders->addAction(action);
        QObject::connect(action, &QAction::triggered, list, [list, sorted, order = choice.order] { list->sortByColumn(sorted, order); });
    }
}

} // namespace fg
