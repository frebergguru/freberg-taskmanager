// Sorting a list from the keyboard. List headers take no focus, so a menu does what a click on them does.
#pragma once

class QMenu;
class QTreeView;
class QWidget;

namespace fg {

// The list on this page that the menu acts on: the focused one if it can sort, or else the first
// shown that can. Null if the page has none.
QTreeView* sortableList(const QWidget* page);

// Replaces the menu's contents with the list's shown columns and the two orders. A null list leaves it empty.
void fillSortMenu(QMenu* menu, QTreeView* list);

} // namespace fg
