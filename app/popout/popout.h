// A small borderless readout that can float over other windows. It samples only
// while shown, and only what its skin displays.
#pragma once

#include "../hub.h"
#include "core/history.h"
#include "skin.h"

#include <QHash>
#include <QWidget>
#include <deque>

namespace fg {

class Popout final : public QWidget {
    Q_OBJECT
public:
    explicit Popout(SamplerHub& hub);
    ~Popout() override;

    void applySkin(const Skin& skin);
    void reloadSkins();
    const SkinLibrary& library() const { return m_library; }
    void setPinned(bool pinned);
    bool isPinned() const { return m_pinned; }
    QSize sizeHint() const override;

signals:
    void visibilityChanged(bool shown);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Channel {
        QString key;
        QString label;
        QString valueText;
        double fraction = -1; // -1 means no reading, shown as such
        History history{120};
    };
    void onSampled(const SnapshotPtr& snapshot);
    void paintChannel(QPainter& p, const QRect& area, const Channel& channel) const;
    QColor accentFor(const QString& key) const;
    Subsystems subsystemsFor(const QStringList& channels) const;
    void showMenu(const QPoint& where);
    void updateShape();
    QString describe() const;

    SamplerHub& m_hub;
    SkinLibrary m_library;
    Skin m_skin;
    std::deque<Channel> m_channels;
    bool m_pinned = false;
    QRect m_closeRect;
};

} // namespace fg
