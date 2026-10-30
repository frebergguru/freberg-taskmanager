#pragma once

#include "../hub.h"
#include "view.h"

#include <QList>
#include <QPointer>
#include <QWidget>

class QLabel;
class QMenuBar;
class QProgressBar;
class QStatusBar;
class QToolBar;

namespace fg {

class Popout;
class Screensaver;
class Sheet;
class TitleBar;
class TrayAndAlerts;

class MainWindow final : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(SamplerHub& hub);
    ~MainWindow() override;

    void openView(int index);
    int viewCount() const { return m_defs.size(); }
    QString viewTitle(int index) const { return m_defs[index].title; }
    void applyAppearance();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void buildMenus();
    void buildToolBar();
    void buildStatusBar();
    void onSampled(const SnapshotPtr& snapshot);
    void onCurrentChanged(int index);
    void updateWanted();
    bool isOnScreen() const;
    Qt::Edges edgesAt(const QPoint& position) const;
    void setUpdateSpeed(int intervalMs);
    void showOptions();
    void showAbout();
    void showAccessibility();
    void togglePopout();
    void showScreensaver();
    void runNewTask();
    void checkForUpdates();

    SamplerHub& m_hub;
    QList<ViewDef> m_defs;
    QList<View*> m_views;
    int m_currentIndex = -1;
    TitleBar* m_title;
    QMenuBar* m_menus;
    QToolBar* m_tools;
    Sheet* m_sheet;
    QStatusBar* m_status;
    QLabel* m_statusCpu;
    QLabel* m_statusMemory;
    QLabel* m_statusSelf;
    // Created when first asked for.
    Popout* m_popout = nullptr;
    Screensaver* m_screensaver = nullptr;
    TrayAndAlerts* m_tray = nullptr;
};

} // namespace fg
