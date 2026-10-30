// A property sheet with tabs in stacked rows. When a tab in a back row is chosen,
// its row moves to the front, next to the page.
#pragma once

#include <QAbstractButton>
#include <QList>
#include <QWidget>

class QStackedWidget;

namespace fg {

class SheetTab final : public QAbstractButton {
    Q_OBJECT
public:
    explicit SheetTab(const QString& text, QWidget* parent);
    QSize sizeHint() const override;

signals:
    void stepRequested(int direction);

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
};

class Sheet final : public QWidget {
    Q_OBJECT
public:
    explicit Sheet(QWidget* parent = nullptr);

    int addPage(const QString& title, QWidget* page);
    void setPage(int index, QWidget* page);
    QWidget* page(int index) const;
    int currentIndex() const { return m_current; }
    int count() const { return m_tabs.size(); }
    QSize minimumSizeHint() const override;

public slots:
    void setCurrentIndex(int index);
    void step(int direction);

signals:
    void currentChanged(int index);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    bool event(QEvent* event) override;

private:
    void relayout();

    QList<SheetTab*> m_tabs;
    QStackedWidget* m_stack;
    int m_current = -1;
    int m_tabsHeight = 0;
};

} // namespace fg
