// The one way to ask before something destructive. "No" is the default, Esc means no, and
// the text is always plain, so a process or file name can't turn into markup.
#pragma once

#include <QString>

class QWidget;

namespace fg {

bool confirmed(QWidget* parent, const QString& title, const QString& question);

} // namespace fg
