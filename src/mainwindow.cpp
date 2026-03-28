/*

  Copyright (c) 2015, 2016 Hubert Denkmair <hubert@denkmair.de>

  This file is part of cangaroo.

  cangaroo is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  cangaroo is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with cangaroo.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QtWidgets>
#include <QMdiArea>
#include <QSignalMapper>
#include <QCloseEvent>
#include <QTimer>
#include <QLabel>
#include <QDockWidget>
#include <QStatusBar>
#include <QDomDocument>
#include <QPalette>
#include <QActionGroup>
#include <QEvent>
#include <QFileInfo>

#include "core/MeasurementSetup.h"
#include "core/Backend.h"
#include "core/CanTrace.h"
#include "core/ThemeManager.h"
#include <window/TraceWindow/TraceWindow.h>
#include <window/SetupDialog/SetupDialog.h>
#include <window/LogWindow/LogWindow.h>
#include <window/GraphWindow/GraphWindow.h>
#include <window/CanStatusWindow/CanStatusWindow.h>
#include <window/RawTxWindow/RawTxWindow.h>
#include <window/TxGeneratorWindow/TxGeneratorWindow.h>
#include <window/ScriptWindow/ScriptWindow.h>
#include <window/ReplayWindow/ReplayWindow.h>
#include <window/SettingsDialog.h>

#include <driver/SLCANDriver/SLCANDriver.h>
#include <driver/GrIPDriver/GrIPDriver.h>
#include <driver/CANBlastDriver/CANBlasterDriver.h>

#if defined(__linux__)
#include <driver/SocketCanDriver/SocketCanDriver.h>
#else
#include <driver/CandleApiDriver/CandleApiDriver.h>
#endif

#ifdef KVASER_DRIVER
#include <driver/KvaserDriver/KvaserDriver.h>
#endif


MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    _baseWindowTitle = windowTitle();

    QCoreApplication::setApplicationVersion(VERSION_STRING);
    QCoreApplication::setOrganizationName("Schildkroet");
    QCoreApplication::setApplicationName("CANgaroo");

    QLabel* versionLabel = new QLabel(this);
    versionLabel->setText(QString("v%1").arg(QCoreApplication::applicationVersion()));
    versionLabel->setStyleSheet("padding-right: 15px; font-weight: bold; font-size: 11px;");
    statusBar()->addPermanentWidget(versionLabel);

    QIcon icon(":/assets/cangaroo.png");
    setWindowIcon(icon);

    connect(ui->action_Trace_View, &QAction::triggered, this, [this]() { createTraceWindow(); });
    connect(ui->actionLog_View, &QAction::triggered, this, [this]() { addLogWidget(); });
    connect(ui->actionGraph_View, &QAction::triggered, this, [this]() { createGraphWindow(); });
    connect(ui->actionGraph_View_2, &QAction::triggered, this, [this]() { addGraphWidget(); });
    connect(ui->actionSetup, &QAction::triggered, this, &MainWindow::showSetupDialog);
    connect(ui->actionTransmit_View, &QAction::triggered, this, [this]() { addRawTxWidget(); });
    connect(ui->actionGenerator_View, &QAction::triggered, this, &MainWindow::on_actionGenerator_View_triggered);
    connect(ui->actionScript_View, &QAction::triggered, this, &MainWindow::on_actionScript_View_triggered);
    connect(ui->actionReplay_View, &QAction::triggered, this, &MainWindow::on_actionReplay_View_triggered);
    connect(ui->actionSettings, &QAction::triggered, this, &MainWindow::showSettingsDialog);

    QAction *actionStandaloneGraph = new QAction(tr("Standalone Graph"), this);
    actionStandaloneGraph->setShortcut(QKeySequence("Ctrl+Shift+B"));
    ui->menuWindow->addAction(actionStandaloneGraph);
    connect(actionStandaloneGraph, &QAction::triggered, this, &MainWindow::createStandaloneGraphWindow);

    connect(ui->actionStart_Measurement, &QAction::triggered, this, &MainWindow::startMeasurement);
    connect(ui->btnStartMeasurement, &QPushButton::released, this, &MainWindow::startMeasurement);
    connect(ui->actionStop_Measurement, &QAction::triggered, this, &MainWindow::stopMeasurement);
    connect(ui->btnStopMeasurement, &QPushButton::released, this, &MainWindow::stopMeasurement);
    connect(ui->btnSetupMeasurement, &QPushButton::released, this, &MainWindow::showSetupDialog);

    connect(ui->actionReload_Interfaces, &QAction::triggered, this, &MainWindow::reloadInterfaces);

    connect(&backend(), &Backend::beginMeasurement, this, &MainWindow::updateMeasurementActions);
    connect(&backend(), &Backend::endMeasurement, this, &MainWindow::updateMeasurementActions);
    updateMeasurementActions();

    connect(ui->actionSave_Trace_to_file, &QAction::triggered, this, &MainWindow::saveTraceToFile);
    connect(ui->actionAbout, &QAction::triggered, this, &MainWindow::showAboutDialog);
    QMenu *traceMenu = ui->menu_Trace;

    QAction *actionExportFull = new QAction(tr("Export full trace"), this);
    connect(actionExportFull, &QAction::triggered, this, &MainWindow::exportFullTrace);
    traceMenu->addAction(actionExportFull);
    QAction *actionImportFull = new QAction(tr("Import full trace"), this);
    connect(actionImportFull, &QAction::triggered, this, &MainWindow::importFullTrace);
    traceMenu->addAction(actionImportFull);

    // Build "Open Recent" submenu and insert it after "Open Workspace..." in menuFile.
    m_recentFilesMenu = new QMenu(tr("Open Recent"), this);
    ui->menuFile->insertMenu(ui->action_WorkspaceSave, m_recentFilesMenu);
    ui->menuFile->insertSeparator(ui->action_WorkspaceSave);
    updateRecentFilesMenu();

    // Load settings
    bool restoreEnabled = settings.value("ui/restoreWindowGeometry", false).toBool();
    bool CANblasterEnabled = settings.value("mainWindow/CANblaster", false).toBool();

    ui->actionRestore_Window->setChecked(restoreEnabled);
    ui->actionCANblaster->setChecked(CANblasterEnabled);

    if (restoreEnabled)
    {
        if (!restoreGeometry(settings.value("mainWindow/geometry").toByteArray()))
        {
            resize(1365, 900);

            QScreen *screen = QGuiApplication::primaryScreen();
            if (screen)
            {
                move(screen->availableGeometry().center() - rect().center());
            }

            settings.setValue("mainWindow/maximized", false);
        }
        restoreState(settings.value("mainWindow/state").toByteArray());
    }

#if defined(__linux__)
    Backend::instance().addCanDriver(*(new SocketCanDriver(Backend::instance())));
#else
    Backend::instance().addCanDriver(*(new CandleApiDriver(Backend::instance())));
#endif
    Backend::instance().addCanDriver(*(new SLCANDriver(Backend::instance())));
    Backend::instance().addCanDriver(*(new GrIPDriver(Backend::instance())));

    if (CANblasterEnabled)
    {
        Backend::instance().addCanDriver(*(new CANBlasterDriver(Backend::instance())));
    }

#ifdef KVASER_DRIVER
    Backend::instance().addCanDriver(*(new KvaserDriver(Backend::instance())));
#endif

    setWorkspaceModified(false);
    newWorkspace();

    // Restore inner tab widget states after tabs have been created by newWorkspace().
    // QMainWindow::restoreState() (called above) only covers the outer window;
    // each tab is its own QMainWindow with independent dock-widget layout.
    //
    // Must be deferred via QTimer::singleShot(0) so it fires *after* the
    // resizeDocks() timer registered inside createTraceWindow() — both use
    // timeout 0, and Qt processes them FIFO, so the restore always wins.
    if (restoreEnabled)
    {
        for (int i = 0; i < ui->mainTabs->count(); i++)
        {
            QMainWindow *tab = qobject_cast<QMainWindow *>(ui->mainTabs->widget(i));
            if (tab)
            {
                const QByteArray tabState = settings.value(QString("mainWindow/tab_%1_state").arg(i)).toByteArray();
                if (!tabState.isEmpty())
                {
                    QTimer::singleShot(0, tab, [tab, tabState]()
                    {
                        tab->restoreState(tabState);
                    });
                }
            }
        }
    }

    // NOTE: must be called after drivers/plugins are initialized
    _setupDlg = new SetupDialog(Backend::instance(), 0);

    _showSetupDialog_first = false;

    // Start/Stop Button Design
    setStyleSheet(
        "QMainWindow::separator {"
        "  background: transparent;"
        "  width: 6px;"
        "  height: 6px;"
        "}"
        "QMainWindow::separator:hover {"
        "  background: #0078d7;"
        "}"
        "QSplitter::handle {"
        "  background: transparent;"
        "  width: 6px;"
        "  height: 6px;"
        "}"
        "QSplitter::handle:hover {"
        "  background: #0078d7;"
        "}"
        "QPushButton#btnStartMeasurement {"
        "  background-color: #218838;"
        "  color: white;"
        "  border-radius: 12px;"
        "  padding: 5px 15px;"
        "  font-weight: bold;"
        "}"
        "QPushButton#btnStartMeasurement:hover {"
        "  background-color: #28a745;"
        "}"
        "QPushButton#btnStartMeasurement:pressed {"
        "  background-color: #196b2c;"
        "}"
        "QPushButton#btnStartMeasurement:disabled {"
        "  background-color: #94d3a2;"
        "}"
        "QPushButton#btnStopMeasurement {"
        "  background-color: #c82333;"
        "  color: white;"
        "  border-radius: 12px;"
        "  padding: 5px 15px;"
        "  font-weight: bold;"
        "}"
        "QPushButton#btnStopMeasurement:hover {"
        "  background-color: #dc3545;"
        "}"
        "QPushButton#btnStopMeasurement:pressed {"
        "  background-color: #a71d2a;"
        "}"
        "QPushButton#btnStopMeasurement:disabled {"
        "  background-color: #f1aeb5;"
        "}"
        );

    qApp->installTranslator(&m_translator);
    createLanguageMenu();

    // Load saved application style/theme
    QString savedStyle = settings.value("ui/applicationStyle", "").toString();
    if (!savedStyle.isEmpty())
    {
        QStringList availableStyles = QStyleFactory::keys();
        bool styleFound = false;
        for (const QString &style : availableStyles)
        {
            if (style.compare(savedStyle, Qt::CaseInsensitive) == 0)
            {
                styleFound = true;
                break;
            }
        }
        if (styleFound)
        {
            QApplication::setStyle(QStyleFactory::create(savedStyle));
            qDebug() << "Loaded saved style:" << savedStyle;


            if(isDarkMode())
            {
                qDebug() << "DarkMode";
                ThemeManager::instance().applyTheme(ThemeManager::Dark);
            }
        }
    }

    // Load saved font size
    int savedFontSize = settings.value("ui/fontSize", 0).toInt();
    if (savedFontSize > 0)
    {
        applyFontSize(savedFontSize);
    }

    // Open Standalone Graph Button
    QPushButton *btnOpenGraph = new QPushButton(tr("Graph"), this);
    btnOpenGraph->setIcon(QIcon(":/assets/graph.svg"));
    btnOpenGraph->setToolTip(tr("Open Standalone Graph Window (Ctrl+Shift+B)"));
    btnOpenGraph->setCursor(Qt::PointingHandCursor);
    ui->horizontalLayoutControls->insertWidget(3, btnOpenGraph); // Insert after Setup Interface button
    connect(btnOpenGraph, &QPushButton::clicked, this, &MainWindow::createStandaloneGraphWindow);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::addToRecentFiles(const QString &filename)
{
    QStringList recent = settings.value("recentFiles/list").toStringList();
    recent.removeAll(filename);        // remove duplicate if present
    recent.prepend(filename);          // most recent first
    while (recent.size() > MaxRecentFiles)
    {
        recent.removeLast();
    }
    settings.setValue("recentFiles/list", recent);
    updateRecentFilesMenu();
}

void MainWindow::updateRecentFilesMenu()
{
    m_recentFilesMenu->clear();

    const QStringList recent = settings.value("recentFiles/list").toStringList();
    for (const QString &path : recent)
    {
        QAction *action = m_recentFilesMenu->addAction(QFileInfo(path).fileName());
        action->setToolTip(path);
        action->setStatusTip(path);
        connect(action, &QAction::triggered, this, [this, path]()
        {
            if (askSaveBecauseWorkspaceModified() != QMessageBox::Cancel)
            {
                loadWorkspaceFromFile(path);
            }
        });
    }

    m_recentFilesMenu->setEnabled(!recent.isEmpty());

    if (!recent.isEmpty())
    {
        m_recentFilesMenu->addSeparator();
        QAction *clearAction = m_recentFilesMenu->addAction(tr("Clear Recent Files"));
        connect(clearAction, &QAction::triggered, this, [this]()
        {
            settings.remove("recentFiles/list");
            updateRecentFilesMenu();
        });
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (askSaveBecauseWorkspaceModified() != QMessageBox::Cancel)
    {
        backend().stopMeasurement();

        // Auto-save to the current workspace file if one is set
        if (!_workspaceFileName.isEmpty())
        {
            saveWorkspaceToFile(_workspaceFileName);
        }

        event->accept();
    }
    else
    {
        event->ignore();
    }

    settings.setValue("mainWindow/geometry", saveGeometry());
    settings.setValue("mainWindow/state", saveState());
    settings.setValue("mainWindow/maximized", isMaximized());
    settings.setValue("ui/restoreWindowGeometry", ui->actionRestore_Window->isChecked());
    settings.setValue("mainWindow/CANblaster", ui->actionCANblaster->isChecked());

    // Save each tab's inner dock-widget layout independently.
    for (int i = 0; i < ui->mainTabs->count(); i++)
    {
        QMainWindow *tab = qobject_cast<QMainWindow *>(ui->mainTabs->widget(i));
        if (tab)
        {
            settings.setValue(QString("mainWindow/tab_%1_state").arg(i), tab->saveState());
        }
    }

    QMainWindow::closeEvent(event);
}

bool MainWindow::isMaximizedWindow()
{
    return settings.value("mainWindow/maximized").toBool();
}

bool MainWindow::isDarkMode()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const auto scheme = QGuiApplication::styleHints()->colorScheme();
    return scheme == Qt::ColorScheme::Dark;
#else
    const QPalette defaultPalette;
    const auto text = defaultPalette.color(QPalette::WindowText);
    const auto window = defaultPalette.color(QPalette::Window);
    return text.lightness() > window.lightness();
#endif // QT_VERSION
}

void MainWindow::updateMeasurementActions()
{
    bool running = backend().isMeasurementRunning();
    ui->actionStart_Measurement->setEnabled(!running);
    ui->actionSetup->setEnabled(!running);
    ui->actionStop_Measurement->setEnabled(running);

    ui->btnStartMeasurement->setEnabled(!running);
    ui->btnSetupMeasurement->setEnabled(!running);
    ui->btnStopMeasurement->setEnabled(running);
}

Backend &MainWindow::backend()
{
    return Backend::instance();
}

QMainWindow *MainWindow::createTab(QString title)
{
    QMainWindow *mm = new QMainWindow(this);
    QPalette pal(palette());
    pal.setColor(QPalette::Window, QColor(0xeb, 0xeb, 0xeb));
    mm->setAutoFillBackground(true);
    mm->setPalette(pal);
    ui->mainTabs->addTab(mm, title);
    return mm;
}

QMainWindow *MainWindow::currentTab()
{
    return (QMainWindow*)ui->mainTabs->currentWidget();
}

void MainWindow::stopAndClearMeasurement()
{
    backend().stopMeasurement();
    QCoreApplication::processEvents();
    backend().clearTrace();
    backend().clearLog();
}

void MainWindow::clearWorkspace()
{
    while (ui->mainTabs->count() > 0) {
        QWidget *w = ui->mainTabs->widget(0);
        ui->mainTabs->removeTab(0);
        delete w;
    }

    // Close and clear standalone windows to prevent dangling pointers to signals
    while (!_standaloneGraphWindows.isEmpty()) {
        GraphWindow *gw = _standaloneGraphWindows.takeFirst();
        if (gw) {
            gw->close(); // This will trigger WA_DeleteOnClose
        }
    }

    _workspaceFileName.clear();
    setWorkspaceModified(false);
}

bool MainWindow::loadWorkspaceTab(QDomElement el)
{
    QMainWindow *mw = 0;
    QString type = el.attribute("type");
    if (type == "TraceWindow")
    {
        mw = createTraceWindow(el.attribute("title"));
    }
    else if (type == "GraphWindow")
    {
        mw = createGraphWindow(el.attribute("title"));
    }
    else
    {
        return false;
    }

    if (mw)
    {
        ConfigurableWidget *mdi = dynamic_cast<ConfigurableWidget*>(mw->centralWidget());
        if (mdi)
        {
            mdi->loadXML(backend(), el);
        }

        // Load TxGeneratorWindow dock content (cyclic frames) if present.
        TxGeneratorWindow *gen = mw->findChild<TxGeneratorWindow *>();
        QDomElement genEl = el.firstChildElement("txgeneratorwindow");
        if (gen && !genEl.isNull())
        {
            gen->loadXML(backend(), genEl);
        }

        // Load ScriptWindow dock content (script code + autorun) if present.
        ScriptWindow *script = mw->findChild<ScriptWindow *>();
        QDomElement scriptEl = el.firstChildElement("scriptwindow");
        if (script && !scriptEl.isNull())
        {
            script->loadXML(backend(), scriptEl);
        }

        // Restore dock layout state (splits, tabification, sizes).
        // Deferred so it runs after the default layout timer from createTraceWindow().
        QString dockState = el.attribute("dockstate");
        if (!dockState.isEmpty())
        {
            QByteArray state = QByteArray::fromBase64(dockState.toLatin1());
            QTimer::singleShot(0, mw, [mw, state]()
            {
                mw->restoreState(state);
            });
        }
    }

    return true;
}

bool MainWindow::loadWorkspaceSetup(QDomElement el)
{
    MeasurementSetup setup(&backend());
    if (setup.loadXML(backend(), el))
    {
        backend().setSetup(setup);
        return true;
    }
    else
    {
        return false;
    }
}

void MainWindow::loadWorkspaceFromFile(QString filename)
{
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        log_error(QString(tr("Cannot open workspace settings file: %1")).arg(filename));
        return;
    }

    QDomDocument doc;
    if (!doc.setContent(&file))
    {
        file.close();
        log_error(QString(tr("Cannot load settings from file: %1")).arg(filename));
        return;
    }
    file.close();

    stopAndClearMeasurement();
    clearWorkspace();

    QDomElement root = doc.documentElement();
    if (root.tagName() != "cangaroo-workspace")
    {
        log_error(QString("Invalid workspace file format: %1").arg(filename));
        return;
    }

    QDomElement tabsRoot = root.firstChildElement("tabs");
    QDomNodeList tabs = tabsRoot.elementsByTagName("tab");
    for (int i = 0; i < tabs.length(); i++)
    {
        if (!loadWorkspaceTab(tabs.item(i).toElement()))
        {
            log_warning(QString(tr("Could not read window %1 from file: %2")).arg(QString::number(i), filename));
            continue;
        }
    }

    QDomElement setupRoot = root.firstChildElement("setup");
    if (loadWorkspaceSetup(setupRoot))
    {
        _workspaceFileName = filename;
        addToRecentFiles(filename);
    }
    else
    {
        log_error(QString(tr("Unable to read measurement setup from workspace config file: %1")).arg(filename));
    }

    if (ui->mainTabs->count() > 0)
    {
        ui->mainTabs->setCurrentIndex(0);
    }
    setWorkspaceModified(false);
}

bool MainWindow::saveWorkspaceToFile(QString filename)
{
    QDomDocument doc;
    QDomElement root = doc.createElement("cangaroo-workspace");
    doc.appendChild(root);

    QDomElement tabsRoot = doc.createElement("tabs");
    root.appendChild(tabsRoot);

    for (int i = 0; i < ui->mainTabs->count(); i++)
    {
        QMainWindow *w = (QMainWindow*)ui->mainTabs->widget(i);

        QDomElement tabEl = doc.createElement("tab");
        tabEl.setAttribute("title", ui->mainTabs->tabText(i));

        ConfigurableWidget *mdi = dynamic_cast<ConfigurableWidget*>(w->centralWidget());
        if (!mdi || !mdi->saveXML(backend(), doc, tabEl))
        {
            log_error(QString(tr("Cannot save window settings to file: %1")).arg(filename));
            return false;
        }

        // Save dock layout state so splits/tabification/sizes are preserved.
        tabEl.setAttribute("dockstate", QString::fromLatin1(w->saveState().toBase64()));

        // Save TxGeneratorWindow dock content (cyclic frames) as a sibling element.
        TxGeneratorWindow *gen = w->findChild<TxGeneratorWindow *>();
        if (gen)
        {
            QDomElement genEl = doc.createElement("txgeneratorwindow");
            gen->saveXML(backend(), doc, genEl);
            tabEl.appendChild(genEl);
        }

        // Save ScriptWindow dock content (script code + autorun).
        ScriptWindow *script = w->findChild<ScriptWindow *>();
        if (script)
        {
            QDomElement scriptEl = doc.createElement("scriptwindow");
            script->saveXML(backend(), doc, scriptEl);
            tabEl.appendChild(scriptEl);
        }

        tabsRoot.appendChild(tabEl);
    }

    QDomElement setupRoot = doc.createElement("setup");
    if (!backend().getSetup().saveXML(backend(), doc, setupRoot))
    {
        log_error(QString(tr("Cannot save measurement setup to file: %1")).arg(filename));
        return false;
    }
    root.appendChild(setupRoot);

    QFile outFile(filename);
    if (outFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QTextStream stream(&outFile);
        stream << doc.toString();
        outFile.close();
        _workspaceFileName = filename;
        setWorkspaceModified(false);
        addToRecentFiles(filename);
        log_info(QString(tr("Saved workspace settings to file: %1")).arg(filename));
        return true;
    }
    else
    {
        log_error(QString(tr("Cannot open workspace file for writing: %1")).arg(filename));
        return false;
    }
}

void MainWindow::newWorkspace()
{
    if (askSaveBecauseWorkspaceModified() != QMessageBox::Cancel)
    {
        stopAndClearMeasurement();
        clearWorkspace();
        createTraceWindow();
        backend().setDefaultSetup();

        // Clear the workspace filename for a fresh start
        _workspaceFileName.clear();
        setWorkspaceModified(false);
    }
}

void MainWindow::loadWorkspace()
{
    if (askSaveBecauseWorkspaceModified() != QMessageBox::Cancel)
    {
        QString filename = QFileDialog::getOpenFileName(this, tr("Open workspace configuration"), "", tr("Workspace config files (*.cangaroo)"));
        if (!filename.isNull())
        {
            loadWorkspaceFromFile(filename);
        }
    }
}

bool MainWindow::saveWorkspace()
{
    if (_workspaceFileName.isEmpty())
    {
        return saveWorkspaceAs();
    }
    else
    {
        return saveWorkspaceToFile(_workspaceFileName);
    }
}

bool MainWindow::saveWorkspaceAs()
{
    QString filename = QFileDialog::getSaveFileName(this, tr("Save workspace configuration"), "", tr("Workspace config files (*.cangaroo)"));
    if (!filename.isNull())
    {
        // Ensure the filename has .cangaroo extension
        if (!filename.endsWith(".cangaroo", Qt::CaseInsensitive))
        {
            filename += ".cangaroo";
        }
        return saveWorkspaceToFile(filename);
    }
    else
    {
        return false;
    }
}

void MainWindow::setWorkspaceModified(bool modified)
{
    _workspaceModified = modified;

    QString title = _baseWindowTitle;
    if (!_workspaceFileName.isEmpty())
    {
        QFileInfo fi(_workspaceFileName);
        title += " - " + fi.fileName();
    }
    if (_workspaceModified)
    {
        title += '*';
    }
    setWindowTitle(title);
}

int MainWindow::askSaveBecauseWorkspaceModified()
{
    if (_workspaceModified)
    {
        QMessageBox msgBox;
        msgBox.setText(tr("The current workspace has been modified."));
        msgBox.setInformativeText(tr("Do you want to save your changes?"));
        msgBox.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Save);

        msgBox.button(QMessageBox::Save)->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
        msgBox.button(QMessageBox::Discard)->setIcon(style()->standardIcon(QStyle::SP_DialogDiscardButton));
        msgBox.button(QMessageBox::Cancel)->setIcon(style()->standardIcon(QStyle::SP_DialogCancelButton));

        msgBox.setWindowFlag(Qt::FramelessWindowHint);
        msgBox.setStyleSheet(QStringLiteral("QMessageBox { border: 3px solid palette(highlight); padding: 10px; }"));

        // Center on main window
        msgBox.adjustSize();
        QPoint center = mapToGlobal(rect().center());
        msgBox.move(center.x() - msgBox.width() / 2, center.y() - msgBox.height() / 2);

        int result = msgBox.exec();
        if (result == QMessageBox::Save)
        {
            if (saveWorkspace())
            {
                return QMessageBox::Save;
            }
            else
            {
                return QMessageBox::Cancel;
            }
        }
        return result;
    }
    else
    {
        return QMessageBox::Discard;
    }
}

QMainWindow *MainWindow::createTraceWindow(QString title)
{
    if (title.isNull())
    {
        title = tr("Trace");
    }
    QMainWindow *mm = createTab(title);
    TraceWindow *trace = new TraceWindow(mm, backend());
    mm->setCentralWidget(trace);

    QDockWidget *dockLogWidget = addLogWidget(mm);
    QDockWidget *dockStatusWidget = addStatusWidget(mm);
    QDockWidget *dockGeneratorWidget = addTxGeneratorWidget(mm);
    QDockWidget *dockGraphWidget = addGraphWidget(mm);
    QDockWidget *dockScriptWidget = addScriptWidget(mm);
    QDockWidget *dockReplayWidget = addReplayWidget(mm);

    TxGeneratorWindow *gen = qobject_cast<TxGeneratorWindow*>(dockGeneratorWidget->widget());
    if (gen) {
        connect(gen, &TxGeneratorWindow::loopbackFrame, trace, &TraceWindow::addMessage);
    }

    mm->splitDockWidget(dockGeneratorWidget,dockLogWidget,Qt::Horizontal);
    mm->splitDockWidget(dockGraphWidget,dockLogWidget,Qt::Horizontal);
    mm->splitDockWidget(dockScriptWidget,dockLogWidget,Qt::Horizontal);
    mm->splitDockWidget(dockReplayWidget,dockLogWidget,Qt::Horizontal);
    mm->tabifyDockWidget(dockGeneratorWidget, dockGraphWidget);
    mm->tabifyDockWidget(dockGraphWidget, dockScriptWidget);
    mm->tabifyDockWidget(dockScriptWidget, dockReplayWidget);
    mm->splitDockWidget(dockStatusWidget,dockLogWidget,Qt::Horizontal);
    mm->tabifyDockWidget(dockStatusWidget, dockLogWidget); // Status first, Log next

    // Use QTimer to resize docks and ensure correct focus/visibility after layout is complete
    QTimer::singleShot(0, mm, [mm, dockLogWidget, dockGeneratorWidget, dockStatusWidget, dockScriptWidget, dockReplayWidget]() {
        dockStatusWidget->show();
        dockStatusWidget->raise();
        dockGeneratorWidget->show();
        dockGeneratorWidget->raise();

        mm->resizeDocks({dockLogWidget, dockGeneratorWidget, dockStatusWidget, dockScriptWidget, dockReplayWidget}, {600, 600, 600, 600, 600}, Qt::Vertical);
        mm->resizeDocks({dockLogWidget, dockGeneratorWidget, dockStatusWidget, dockScriptWidget, dockReplayWidget}, {1200, 1200, 1200, 1200, 1200}, Qt::Horizontal);
    });

    ui->mainTabs->setCurrentWidget(mm);

    return mm;
}

QMainWindow *MainWindow::createGraphWindow(QString title)
{
    if (title.isNull())
    {
        title = tr("Graph");
    }
    QMainWindow *mm = createTab(title);
    mm->setCentralWidget(new GraphWindow(mm, backend()));
    addLogWidget(mm);

    return mm;
}

void MainWindow::createStandaloneGraphWindow()
{
    GraphWindow *gw = new GraphWindow(nullptr, backend());
    gw->setWindowTitle(tr("Standalone Graph"));
    gw->setAttribute(Qt::WA_DeleteOnClose);

    _standaloneGraphWindows.append(gw);
    connect(gw, &QObject::destroyed, this, [this, gw]() {
        _standaloneGraphWindows.removeAll(gw);
    });

    gw->show();
}

QDockWidget *MainWindow::addGraphWidget(QMainWindow *parent)
{
    if (!parent)
    {
        parent = currentTab();
    }
    QDockWidget *dock = new QDockWidget(tr("Graph"), parent);
    dock->setObjectName(QStringLiteral("dock_graph"));
    dock->setWidget(new GraphWindow(dock, backend()));
    parent->addDockWidget(Qt::BottomDockWidgetArea, dock);
    setupDockFloatReparent(dock, parent);

    return dock;
}

QDockWidget *MainWindow::addRawTxWidget(QMainWindow *parent)
{
    if (!parent)
    {
        parent = currentTab();
    }
    QDockWidget *dock = new QDockWidget(tr("Message View"), parent);
    dock->setObjectName(QStringLiteral("dock_rawtx"));
    RawTxWindow *rawTx = new RawTxWindow(dock, backend());
    dock->setWidget(rawTx);
    parent->addDockWidget(Qt::BottomDockWidgetArea, dock);
    setupDockFloatReparent(dock, parent);

    return dock;
}

QDockWidget *MainWindow::addLogWidget(QMainWindow *parent)
{
    if (!parent)
    {
        parent = currentTab();
    }
    QDockWidget *dock = new QDockWidget(tr("Log"), parent);
    dock->setObjectName(QStringLiteral("dock_log"));
    dock->setWidget(new LogWindow(dock, backend()));
    parent->addDockWidget(Qt::BottomDockWidgetArea, dock);
    setupDockFloatReparent(dock, parent);

    return dock;
}

QDockWidget *MainWindow::addStatusWidget(QMainWindow *parent)
{
    if (!parent)
    {
        parent = currentTab();
    }
    QDockWidget *dock = new QDockWidget(tr("CAN Status"), parent);
    dock->setObjectName(QStringLiteral("dock_status"));
    dock->setWidget(new CanStatusWindow(dock, backend()));
    parent->addDockWidget(Qt::BottomDockWidgetArea, dock);
    setupDockFloatReparent(dock, parent);

    return dock;
}

QDockWidget *MainWindow::addTxGeneratorWidget(QMainWindow *parent)
{
    if (!parent)
    {
        parent = currentTab();
    }
    QDockWidget *dock = new QDockWidget(tr("Generator View"), parent);
    dock->setObjectName(QStringLiteral("dock_generator"));
    TxGeneratorWindow *gen = new TxGeneratorWindow(dock, backend());
    dock->setWidget(gen);
    parent->addDockWidget(Qt::BottomDockWidgetArea, dock);
    setupDockFloatReparent(dock, parent);

    return dock;
}

QDockWidget *MainWindow::addScriptWidget(QMainWindow *parent)
{
    if (!parent)
    {
        parent = currentTab();
    }
    QDockWidget *dock = new QDockWidget(tr("Python Script"), parent);
    dock->setObjectName(QStringLiteral("dock_script"));
    dock->setWidget(new ScriptWindow(dock, backend()));
    parent->addDockWidget(Qt::BottomDockWidgetArea, dock);
    setupDockFloatReparent(dock, parent);

    return dock;
}

QDockWidget *MainWindow::addReplayWidget(QMainWindow *parent)
{
    if (!parent)
    {
        parent = currentTab();
    }
    QDockWidget *dock = new QDockWidget(tr("Replay"), parent);
    dock->setObjectName(QStringLiteral("dock_replay"));
    dock->setWidget(new ReplayWindow(dock, backend()));
    parent->addDockWidget(Qt::BottomDockWidgetArea, dock);
    setupDockFloatReparent(dock, parent);

    return dock;
}

void MainWindow::setupDockFloatReparent(QDockWidget *dock, QMainWindow *innerParent)
{
    (void) innerParent;
    connect(dock, &QDockWidget::topLevelChanged, this, [dock](bool floating)
    {
        if (floating)
        {
            // Deferred so we don't destroy the window handle mid-drag.
            // Only override decoration flags — Qt::Window is already set
            // by Qt when the dock floats, so setParent() is not called.
            QTimer::singleShot(0, dock, [dock]()
            {
                dock->setWindowFlags(Qt::Window | Qt::CustomizeWindowHint
                                     | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
                dock->show();
            });
        }
    });
}

void MainWindow::on_actionCan_Status_View_triggered()
{
    addStatusWidget();
}

bool MainWindow::showSetupDialog()
{
    MeasurementSetup new_setup(&backend());
    new_setup.cloneFrom(backend().getSetup());
    backend().setDefaultSetup();
    if (backend().getSetup().countNetworks() == new_setup.countNetworks())
    {
        backend().setSetup(new_setup);
    }
    else
    {
        new_setup.cloneFrom(backend().getSetup());
    }
    if (_setupDlg->showSetupDialog(new_setup))
    {
        if (!_setupDlg->isReflashNetworks())
            backend().setSetup(new_setup);

        setWorkspaceModified(true);
        _showSetupDialog_first = true;
        return true;
    }
    else
    {
        return false;
    }
}

void MainWindow::reloadInterfaces()
{
    backend().refreshInterfaces();
}

void MainWindow::showAboutDialog()
{
    QMessageBox::about(this,
                       tr("About CANgaroo"),
                       "CANgaroo\n"
                       "Open Source CAN bus analyzer\n"
                       "https://github.com/Schildkroet/CANgaroo"
                       "\n"
                       "Version " VERSION_STRING "\n"
                       "\n"
                       "(c)2015-2017 Hubert Denkmair\n"
                       "(c)2018-2022 Ethan Zonca\n"
                       "(c)2024 WeAct Studio\n"
                       "(c)2024-2026 Schildkroet\n"
                       "(c)2025 Wikilift\n"
                       "(c)2026 Jayachandran Dharuman"
                       "\n\n"
                       "CANgaroo is free software licensed"
                       "\nunder the GPL v2 license.");
}

void MainWindow::startMeasurement()
{
    if (!_showSetupDialog_first)
    {
        if (showSetupDialog())
        {
            backend().clearTrace();
            backend().startMeasurement();
            _showSetupDialog_first = true;
        }
    }
    else
    {
        backend().startMeasurement();
    }
}

void MainWindow::stopMeasurement()
{
    backend().stopMeasurement();

    for (auto *gen : findChildren<TxGeneratorWindow*>())
    {
        gen->stopAll();
    }
}

void MainWindow::saveTraceToFile()
{
    QString filters("Vector ASC (*.asc);;Vector MDF4 (*.mf4);;Linux candump (*.candump)");
    QString defaultFilter("Vector ASC (*.asc)");

    QFileDialog fileDialog(0, tr("Save Trace to file"), QDir::currentPath(), filters);
    fileDialog.setAcceptMode(QFileDialog::AcceptSave);
    fileDialog.setOption(QFileDialog::DontConfirmOverwrite, false);
    fileDialog.selectNameFilter(defaultFilter);
    fileDialog.setDefaultSuffix("asc");
    if (fileDialog.exec())
    {
        QString filename = fileDialog.selectedFiles().at(0);
        QFile file(filename);
        if (file.open(QIODevice::ReadWrite | QIODevice::Truncate))
        {
            if (filename.endsWith(".candump", Qt::CaseInsensitive))
            {
                backend().getTrace()->saveCanDump(file);
            }
            else if (filename.endsWith(".mf4", Qt::CaseInsensitive))
            {
                backend().getTrace()->saveVectorMdf(file);
            }
            else
            {
                backend().getTrace()->saveVectorAsc(file);
            }

            file.close();
        }
        else
        {
            QMessageBox::warning(this, tr("Error"), tr("Cannot open file for writing."));
        }
    }
}

void MainWindow::on_action_TraceClear_triggered()
{
    backend().clearTrace();
    backend().clearLog();
}

void MainWindow::on_action_WorkspaceSave_triggered()
{
    saveWorkspace();
}

void MainWindow::on_action_WorkspaceSaveAs_triggered()
{
    saveWorkspaceAs();
}

void MainWindow::on_action_WorkspaceOpen_triggered()
{
    loadWorkspace();
}

void MainWindow::on_action_WorkspaceNew_triggered()
{
    newWorkspace();
}

void MainWindow::on_actionGenerator_View_triggered()
{
    addTxGeneratorWidget();
}

void MainWindow::on_actionScript_View_triggered()
{
    addScriptWidget();
}

void MainWindow::on_actionReplay_View_triggered()
{
    addReplayWidget();
}

void MainWindow::switchLanguage(QAction *action)
{
    QString locale = action->data().toString();

    qApp->removeTranslator(&m_translator);

    if (locale == "en_US")
    {
        std::ignore = m_translator.load("");
    }
    else
    {
        QString qmPath = ":/translations/i18n_" + locale + ".qm";
        if (!m_translator.load(qmPath))
        {
            qDebug() << "Could not load translation: " << qmPath;
        }
    }

    qApp->installTranslator(&m_translator);
    settings.setValue("ui/language", locale);
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
    {
        ui->retranslateUi(this);

        _baseWindowTitle = tr("CANgaroo");
        setWorkspaceModified(_workspaceModified);
    }

    QMainWindow::changeEvent(event);
}

void MainWindow::createLanguageMenu()
{
    m_languageActionGroup = new QActionGroup(this);
    connect(m_languageActionGroup, &QActionGroup::triggered, this, &MainWindow::switchLanguage);

    QString savedLocale = settings.value("ui/language", "en_US").toString();

    QAction *actionEn = new QAction(tr("English"), this);
    actionEn->setCheckable(true);
    actionEn->setData("en_US");
    m_languageActionGroup->addAction(actionEn);

    QAction *actionEs = new QAction(tr("Español"), this);
    actionEs->setCheckable(true);
    actionEs->setData("es_ES");
    m_languageActionGroup->addAction(actionEs);

    QAction *actionDe = new QAction(tr("Deutsch"), this);
    actionDe->setCheckable(true);
    actionDe->setData("de_DE");
    m_languageActionGroup->addAction(actionDe);

    QAction *actionCN = new QAction(tr("Chinese"), this);
    actionCN->setCheckable(true);
    actionCN->setData("zh_cn");
    m_languageActionGroup->addAction(actionCN);

    // Restore saved language selection
    for (QAction *action : m_languageActionGroup->actions())
    {
        if (action->data().toString() == savedLocale)
        {
            action->setChecked(true);
            if (savedLocale != "en_US")
            {
                switchLanguage(action);
            }
            break;
        }
    }
}

void MainWindow::exportFullTrace()
{
    /*TraceWindow *tw = currentTab()->findChild<TraceWindow *>();
    if (!tw)
    {
        QMessageBox::warning(this, tr("Error"), tr("No Trace window active"));
        return;
    }

    auto *model = tw->linearModel();
    CanTrace *trace = backend().getTrace();

    QString filename = QFileDialog::getSaveFileName(
        this,
        tr("Export full trace"),
        "",
        tr("CANgaroo Trace (*.ctrace)"));
    if (filename.isEmpty())
        return;
    if (!filename.endsWith(".ctrace"))
        filename += ".ctrace";

    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        QMessageBox::warning(this, tr("Error"), tr("Cannot write file"));
        return;
    }

    QJsonObject root;

    QJsonArray msgsJson;
    unsigned long count = trace->size();

    for (unsigned long i = 0; i < count; i++)
    {
        const CanMessage *msg = trace->getMessage(i);
        if (!msg)
            continue;

        QJsonObject m;
        m["timestamp"] = msg->getFloatTimestamp();
        m["raw_id"] = static_cast<int>(msg->getRawId());
        m["id"] = msg->getIdString();
        m["dlc"] = msg->getLength();
        m["data"] = msg->getDataHexString();
        m["direction"] = msg->isRX() ? "RX" : "TX";
        m["comment"] = model->exportedComment(i);

        msgsJson.append(m);
    }

    root["messages"] = msgsJson;

    QJsonObject colorsJson;
    for (auto it = model->exportedColors().begin(); it != model->exportedColors().end(); ++it)
        colorsJson[it.key()] = it.value().name();
    root["colors"] = colorsJson;

    QJsonObject aliasJson;
    for (auto it = model->exportedAliases().begin(); it != model->exportedAliases().end(); ++it)
        aliasJson[it.key()] = it.value();
    root["aliases"] = aliasJson;

    file.write(QJsonDocument(root).toJson());
    file.close();*/
}

void MainWindow::importFullTrace()
{
    /*TraceWindow *tw = currentTab()->findChild<TraceWindow*>();
    if (!tw)
    {
        QMessageBox::warning(this, tr("Error"), tr("No Trace window active"));
        return;
    }

    auto *linear = tw->linearModel();
    auto *agg    = tw->aggregatedModel();

    QString filename = QFileDialog::getOpenFileName(
        this,
        tr("Import full trace"),
        "",
        tr("CANgaroo Trace (*.ctrace)")
    );
    if (filename.isEmpty())
        return;

    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly))
    {
        QMessageBox::warning(this, tr("Error"), tr("Cannot read file"));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    QJsonObject root = doc.object();

    backend().clearTrace();

    {
        QJsonObject colors = root["colors"].toObject();
        for (auto it = colors.begin(); it != colors.end(); ++it)
        {
            QColor c(it.value().toString());

            linear->setMessageColorForIdString(it.key(), c);
            agg->setMessageColorForIdString(it.key(), c);
        }
    }

    {
        QJsonObject aliases = root["aliases"].toObject();
        for (auto it = aliases.begin(); it != aliases.end(); ++it)
        {
            QString alias = it.value().toString();

            linear->updateAliasForIdString(it.key(), alias);
            agg->updateAliasForIdString(it.key(), alias);
        }
    }

    QJsonArray msgs = root["messages"].toArray();

    for (int i = 0; i < msgs.size(); i++)
    {
        QJsonObject m = msgs[i].toObject();
        CanMessage msg;

        double ts = m["timestamp"].toDouble();
        msg.setTimestamp(ts);

        msg.setRawId(m["raw_id"].toInt());
        msg.setLength(m["dlc"].toInt());

        QByteArray ba = QByteArray::fromHex(m["data"].toString().toUtf8());
        for (int b = 0; b < ba.size(); b++)
            msg.setByte(b, static_cast<uint8_t>(ba[b]));

        msg.setRX(m["direction"].toString() == "RX");

        backend().getTrace()->enqueueMessage(msg, false);

        QString comment = m["comment"].toString();
        if (!comment.isEmpty())
        {
            linear->setCommentForMessage(i, comment);
            agg->setCommentForMessage(i, comment);
        }
    }

    QMetaObject::invokeMethod(linear, "modelReset", Qt::DirectConnection);
    QMetaObject::invokeMethod(agg,    "modelReset", Qt::DirectConnection);

    linear->layoutChanged();
    agg->layoutChanged();*/
}


void MainWindow::showSettingsDialog()
{
    SettingsDialog dlg(settings, m_languageActionGroup, this);

    if (dlg.exec() != QDialog::Accepted)
    {
        return;
    }

    // Apply theme
    QString newTheme = dlg.selectedTheme();
    QString currentTheme = QApplication::style()->objectName();
    if (newTheme.compare(currentTheme, Qt::CaseInsensitive) != 0)
    {
        QApplication::setStyle(QStyleFactory::create(newTheme));
        settings.setValue("ui/applicationStyle", newTheme);

        if (isDarkMode())
        {
            ThemeManager::instance().applyTheme(ThemeManager::Dark);
        }
    }

    // Apply language
    QString newLocale = dlg.selectedLanguage();
    QString currentLocale = settings.value("ui/language", "en_US").toString();
    if (newLocale != currentLocale)
    {
        for (QAction *action : m_languageActionGroup->actions())
        {
            if (action->data().toString() == newLocale)
            {
                action->setChecked(true);
                switchLanguage(action);
                break;
            }
        }
    }

    // Apply font size
    int newFontSize = dlg.selectedFontSize();
    if (newFontSize != QApplication::font().pointSize())
    {
        applyFontSize(newFontSize);
        settings.setValue("ui/fontSize", newFontSize);
    }

    // Apply restore window setting
    ui->actionRestore_Window->setChecked(dlg.restoreWindowEnabled());
    settings.setValue("ui/restoreWindowGeometry", dlg.restoreWindowEnabled());
}

void MainWindow::applyFontSize(int pointSize)
{
    QFont font = QApplication::font();
    font.setPointSize(pointSize);
    QApplication::setFont(font);

    // Propagate to all existing widgets
    for (QWidget *w : QApplication::allWidgets())
    {
        QFont wf = w->font();
        wf.setPointSize(pointSize);
        w->setFont(wf);
    }
}

