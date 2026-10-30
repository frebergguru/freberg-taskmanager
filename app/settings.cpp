#include "settings.h"

namespace fg {

Settings::Settings() : m_store(QSettings::IniFormat, QSettings::UserScope, "Freberg.guru", "TaskManager") {}

Settings& Settings::instance()
{
    static Settings settings;
    return settings;
}

void Settings::setValue(const QString& key, const QVariant& value)
{
    if (m_store.value(key) == value)
        return;
    m_store.setValue(key, value);
    emit changed(key);
}

} // namespace fg
