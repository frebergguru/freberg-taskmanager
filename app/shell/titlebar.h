// The window's own title bar, so the frame looks the same on every desktop.
#pragma once

#include <QAbstractButton>
#include <QWidget>

class QMenu;

namespace fg {

class CaptionButton final : public QAbstractButton {
    Q_OBJECT
public:
    enum Kind { Minimize, Maximize, Restore, Close };
    CaptionButton(Kind kind, QWidget* parent);
    void setKind(Kind kind);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Kind m_kind;
};

class TitleBar final : public QWidget {
    Q_OBJECT
public:
    explicit TitleBar(QWidget* window);
    void showSystemMenu(const QPoint& where);
    void syncState();
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void toggleMaximized();

    QWidget* m_window;
    CaptionButton* m_minimize;
    CaptionButton* m_maximize;
    CaptionButton* m_close;
};

} // namespace fg
