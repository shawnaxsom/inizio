#include <map>
#include <iostream>
#include <sstream>
#include <vector>

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "todotablemodel.h"

#include "todotxt.h"
#include "settingsdialog.h"
#include "quickadddialog.h"
#include "aboutbox.h"
#include "globals.h"
#include "def.h"

#include <QSortFilterProxyModel>
#include <QFileSystemWatcher>
#include <QTime>
#include <QDebug>
#include <QSettings>
#include <QShortcut>
#include <QCloseEvent>
#include <QtAwesome.h>
#include <QDesktopWidget>
#include <QDir>
#include <QSystemTrayIcon>
#include <QDesktopServices>
#include <QUuid>
#include <QDate>
#include <QDateEdit>
#include <QInputDialog>
#include <QStringListModel>
#include <QAbstractItemView>
#include <QTimer>
#include <QMessageBox>

QNetworkAccessManager *networkaccessmanager;
TodoTableModel *model = NULL;
QStringListModel *listModel = NULL;

QString saved_selection; // Used for selection memory
int saved_row = -1;      // Used for selection memory
int saved_column = 0;    // Used for selection memory

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent),
                                          ui(new Ui::MainWindow)
{
    todo = new todotxt();

    ui->setupUi(this);
    QString title = this->windowTitle();

#ifdef QT_NO_DEBUG
    QCoreApplication::setOrganizationName("Nerdur");
    QCoreApplication::setOrganizationDomain("nerdur.com");
    QCoreApplication::setApplicationName("Todour");
    title.append("-");
#else
    QCoreApplication::setOrganizationName("Nerdur-debug");
    QCoreApplication::setOrganizationDomain("nerdur-debug.com");
    QCoreApplication::setApplicationName("Todour-Debug");
    title.append("-DEBUG-");
#endif

    title.append(VER);
    baseTitle = title;
    this->setWindowTitle(title);

    // Check if we're supposed to have the settings from .ini file or not
    if (QCoreApplication::arguments().contains("-portable"))
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir::currentPath());
        qDebug() << "Setting ini file path to: " << QDir::currentPath() << Qt::endl;
    }

    hotkey = new UGlobalHotkeys();
    setHotkey();

    // Restore the position of the window
    auto rec = QApplication::desktop()->screenGeometry();
    auto maxx = rec.height();
    auto maxy = rec.width();
    auto minx = rec.topLeft().x();
    auto miny = rec.topLeft().y();

    QSettings settings;
    restoreGeometry(settings.value(SETTINGS_GEOMETRY, saveGeometry()).toByteArray());
    restoreState(settings.value(SETTINGS_SAVESTATE, saveState()).toByteArray());
    auto p = settings.value(SETTINGS_POSITION, pos()).toPoint();

    if (p.x() >= maxx - 100 || p.x() < minx)
    {
        p.setX(minx); // Set to minx for safety
    }
    if (p.y() >= maxy - 100 || p.y() < miny)
    {
        p.setY(miny); // Set to miny for safety
    }
    move(p);
    resize(settings.value(SETTINGS_SIZE, size()).toSize());
    if (settings.value(SETTINGS_MAXIMIZED, isMaximized()).toBool())
        showMaximized();

    ui->lineEdit_2->setText(settings.value(SETTINGS_SEARCH_STRING, DEFAULT_SEARCH_STRING).toString());
    ui->lineEdit_3->setText(settings.value(SETTINGS_CONTEXT_STRING, "").toString());
    ui->lineEdit_4->setText(settings.value(SETTINGS_DEFAULT_TEXT_STRING, "(B)").toString());
    ui->lineEdit_4->setMaximumWidth(150);

    // Check that we have an UUID for this application (used for undo for example)
    if (!settings.contains(SETTINGS_UUID))
    {
        settings.setValue(SETTINGS_UUID, QUuid::createUuid().toString());
    }

    // Fix some font-awesome stuff
    QtAwesome *awesome = new QtAwesome(qApp);
    awesome->initFontAwesome(); // This line is important as it loads the font and initializes the named icon map
    awesome->setDefaultOption("scale-factor", 0.9);
    ui->btn_Alphabetical->setIcon(awesome->icon(fa::sortalphaasc));
    ui->pushButton_3->setIcon(awesome->icon(fa::signout));
    ui->pushButton_4->setIcon(awesome->icon(fa::refresh));
    ui->pushButton->setIcon(awesome->icon(fa::plus));
    ui->pushButton_2->setIcon(awesome->icon(fa::minus));
    ui->context_lock->setIcon(awesome->icon(fa::lock));
    ui->pb_closeVersionBar->setIcon(awesome->icon(fa::times));

    // Set some defaults if they dont exist
    if (!settings.contains(SETTINGS_LIVE_SEARCH))
    {
        settings.setValue(SETTINGS_LIVE_SEARCH, DEFAULT_LIVE_SEARCH);
    }

    // Started. Lets open the todo.txt file, parse it and show it.
    parse_todotxt();
    setFileWatch();
    networkaccessmanager = new QNetworkAccessManager(this);

    // Set up shortcuts . Mac translates the Ctrl -> Cmd
    // http://doc.qt.io/qt-5/qshortcut.html
    auto editshortcut = new QShortcut(QKeySequence(tr("Ctrl+n")), this);
    QObject::connect(editshortcut, SIGNAL(activated()), ui->lineEdit, SLOT(setFocus()));

    auto findshortcut = new QShortcut(QKeySequence(tr("Ctrl+f")), this);
    QObject::connect(findshortcut, SIGNAL(activated()), ui->lineEdit_2, SLOT(setFocus()));

    auto clearshortcut = new QShortcut(QKeySequence(tr("Esc")), ui->lineEdit_2);
    clearshortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(clearshortcut, SIGNAL(activated()), this, SLOT(clearSearch()));

    auto clearshortcut2 = new QShortcut(QKeySequence(tr("Esc")), ui->tableView);
    clearshortcut2->setContext(Qt::WidgetShortcut);
    QObject::connect(clearshortcut2, SIGNAL(activated()), this, SLOT(clearSearch()));

    auto returntolistshortcut = new QShortcut(QKeySequence(tr("Esc")), ui->lineEdit);
    returntolistshortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(returntolistshortcut, SIGNAL(activated()), this, SLOT(focusTodoList()));

    auto launchshortcut = new QShortcut(QKeySequence(tr("o")), ui->tableView);
    launchshortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(launchshortcut, SIGNAL(activated()), this, SLOT(launchUrl()));

    auto completeTasksShortcut = new QShortcut(QKeySequence(Qt::Key_Space), ui->tableView);
    completeTasksShortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(completeTasksShortcut, SIGNAL(activated()), this, SLOT(completeTasks()));

    auto upshortcut = new QShortcut(QKeySequence(tr("k")), ui->tableView);
    upshortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(upshortcut, SIGNAL(activated()), this, SLOT(up()));

    auto shiftupshortcut = new QShortcut(QKeySequence(tr("Shift+k")), ui->tableView);
    shiftupshortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(shiftupshortcut, SIGNAL(activated()), this, SLOT(shiftUp()));

    auto downshortcut = new QShortcut(QKeySequence(tr("j")), ui->tableView);
    downshortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(downshortcut, SIGNAL(activated()), this, SLOT(down()));

    auto shiftdownshortcut = new QShortcut(QKeySequence(tr("Shift+j")), ui->tableView);
    shiftdownshortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(shiftdownshortcut, SIGNAL(activated()), this, SLOT(shiftDown()));

    auto leftshortcut = new QShortcut(QKeySequence(tr("h")), ui->tableView);
    leftshortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(leftshortcut, SIGNAL(activated()), this, SLOT(left()));

    auto rightshortcut = new QShortcut(QKeySequence(tr("l")), ui->tableView);
    rightshortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(rightshortcut, SIGNAL(activated()), this, SLOT(right()));

    auto appendshortcut = new QShortcut(QKeySequence(tr("a")), ui->tableView);
    appendshortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(appendshortcut, SIGNAL(activated()), this, SLOT(showAppendDialog()));

    auto removalshortcut = new QShortcut(QKeySequence(tr("r")), ui->tableView);
    removalshortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(removalshortcut, SIGNAL(activated()), this, SLOT(showRemovalDialog()));

    auto dueshortcut = new QShortcut(QKeySequence(tr("d")), ui->tableView);
    dueshortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(dueshortcut, SIGNAL(activated()), this, SLOT(showDueDialog()));

    if (settings.value(SETTINGS_THRESHOLD).toBool())
    {
        auto thresholdTodayShortcut = new QShortcut(QKeySequence(tr(",")), ui->tableView);
        thresholdTodayShortcut->setContext(Qt::WidgetShortcut);
        QObject::connect(thresholdTodayShortcut, SIGNAL(activated()), this, SLOT(setThresholdToToday()));

        auto thresholdTomorrowShortcut = new QShortcut(QKeySequence(tr(".")), ui->tableView);
        thresholdTomorrowShortcut->setContext(Qt::WidgetShortcut);
        QObject::connect(thresholdTomorrowShortcut, SIGNAL(activated()), this, SLOT(setThresholdToTomorrow()));

        auto thresholdshortcut = new QShortcut(QKeySequence(tr("t")), ui->tableView);
        thresholdshortcut->setContext(Qt::WidgetShortcut);
        QObject::connect(thresholdshortcut, SIGNAL(activated()), this, SLOT(showThresholdDialog()));
    }

    auto pageupshortcut = new QShortcut(QKeySequence(tr("Ctrl+u")), ui->tableView);
    pageupshortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(pageupshortcut, SIGNAL(activated()), this, SLOT(pageUp()));

    auto pagedownshortcut = new QShortcut(QKeySequence(tr("Ctrl+d")), ui->tableView);
    pagedownshortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(pagedownshortcut, SIGNAL(activated()), this, SLOT(pageDown()));

    auto todayshortcut = new QShortcut(QKeySequence(tr("Ctrl+t")), ui->tableView);
    todayshortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(todayshortcut, SIGNAL(activated()), this, SLOT(toggleToday()));

    auto deleteshortcut = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_Backspace), ui->tableView);
    deleteshortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(deleteshortcut, SIGNAL(activated()), this, SLOT(deleteSelected()));

    auto increasepriorityshortcut = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_Up), ui->tableView);
    increasepriorityshortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(increasepriorityshortcut, SIGNAL(activated()), this, SLOT(increasePriority()));

    auto increasepriorityshortcut2 = new QShortcut(QKeySequence(tr("Ctrl+k")), ui->tableView);
    increasepriorityshortcut2->setContext(Qt::WidgetShortcut);
    QObject::connect(increasepriorityshortcut2, SIGNAL(activated()), this, SLOT(increasePriority()));

    auto decreasepriorityshortcut = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_Down), ui->tableView);
    decreasepriorityshortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(decreasepriorityshortcut, SIGNAL(activated()), this, SLOT(decreasePriority()));

    auto decreasepriorityshortcut2 = new QShortcut(QKeySequence(tr("Ctrl+j")), ui->tableView);
    decreasepriorityshortcut2->setContext(Qt::WidgetShortcut);
    QObject::connect(decreasepriorityshortcut2, SIGNAL(activated()), this, SLOT(decreasePriority()));

    //auto contextshortcut = new QShortcut(QKeySequence(tr("Ctrl+l")),this);
    //QObject::connect(contextshortcut,SIGNAL(activated()),ui->context_lock,SLOT(setChecked(!(ui->context_lock->isChecked()))));
    QObject::connect(model, SIGNAL(dataChanged(const QModelIndex, const QModelIndex)), this, SLOT(dataInModelChanged(QModelIndex, QModelIndex)));

    // Resize tableView row height on first load, and then again when resizing window
    QTimer::singleShot(1, ui->tableView, SLOT(resizeRowsToContents()));
    connect(
        ui->tableView->horizontalHeader(),
        SIGNAL(sectionResized(int, int, int)),
        ui->tableView,
        SLOT(resizeRowsToContents()));

    /*
    These should now be handled in the menu system
    auto undoshortcut = new QShortcut(QKeySequence(tr("Ctrl+z")),this);
    QObject::connect(undoshortcut,SIGNAL(activated()),this,SLOT(undo()));
    auto redoshortcut = new QShortcut(QKeySequence(tr("Ctrl+r")),this);
    QObject::connect(redoshortcut,SIGNAL(activated()),this,SLOT(redo()));*/

    // Do this late as it triggers action using data
    ui->btn_Alphabetical->setChecked(settings.value(SETTINGS_SORT_ALPHA).toBool());
    ui->context_lock->setChecked(settings.value(SETTINGS_CONTEXT_LOCK, DEFAULT_CONTEXT_LOCK).toBool());
    updateSearchResults(); // Since we may have set a value in the search window

    /* ui->lv_activetags->hide(); //  Not being used yet */
    ui->lv_activetags->setMaximumSize(QSize(150, 99999));
    ui->lv_activetags->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->newVersionView->hide(); // This defaults to not being shown
    ui->cb_showaall->setChecked(settings.value(SETTINGS_SHOW_ALL, DEFAULT_SHOW_ALL).toBool());
    ui->cb_threshold_inactive->setChecked(settings.value(SETTINGS_THRESHOLD_INACTIVE, DEFAULT_THRESHOLD_INACTIVE).toBool());

    setTray();
    setFontSize();

    // Version check
    if (settings.value(SETTINGS_CHECK_UPDATES, DEFAULT_CHECK_UPDATES).toBool())
    {
        QString last_check = settings.value(SETTINGS_LAST_UPDATE_CHECK, "").toString();
        if (last_check.isEmpty())
        {
            // We set this up so that first check will be later, giving users ample time to turn off the feature.
            last_check = QDate::currentDate().toString("yyyy-MM-dd");
            settings.setValue(SETTINGS_LAST_UPDATE_CHECK, last_check);
        }
        QDate lastCheck = QDate::fromString(last_check, "yyyy-MM-dd");
        QDate nextCheck = lastCheck.addDays(7);

        qDebug() << "Last update check date: " << last_check << " and next is " << nextCheck.toString("yyyy-MM-dd") << Qt::endl;
        int daysToNextcheck = QDate::currentDate().daysTo(nextCheck);
        if (daysToNextcheck < 0)
        {
            QString URL = VERSION_URL;
            requestPage(URL);
        }
    }
}

// This method is for making sure we're re-selecting the item that has been edited
void MainWindow::dataInModelChanged(QModelIndex i1, QModelIndex i2)
{
    Q_UNUSED(i2);
    //qDebug()<<"Data in Model changed emitted:"<<i1.data(Qt::UserRole)<<"::"<<i2.data(Qt::UserRole)<<endl;
    //qDebug()<<"Changed:R="<<i1.row()<<":C="<<i1.column()<<endl;
    saved_selection = i1.data(Qt::UserRole).toString();
    QSettings settings;
    if (settings.value(SETTINGS_AUTOREFRESH).toBool() == false)
    {
        // Reselect the previously selected line
        // Avoid with autorefresh, since that will also attempt this after the file reloads
        // TODO: settings seem cached, the app has to restart for this to apply if the user toggles the setting.
        resetTableSelection();
    }
    updateTitle();
}

MainWindow::~MainWindow()
{
    delete todo;
    delete ui;
    delete networkaccessmanager;
    delete model;
    delete listModel;
}

// proxyModel is a filtered view of the UI model
QSortFilterProxyModel *proxyModel = NULL;

QFileSystemWatcher *watcher;

void MainWindow::updateTitle()
{
    // The title is initialized to the name and version number at start and that is stored in baseTitle

    if (proxyModel != NULL)
    {
        int visible = proxyModel->rowCount();
        int total = proxyModel->sourceModel()->rowCount();

        this->setWindowTitle(baseTitle + " (" + QString::number(visible) + "/" + QString::number(total) + ")");
    }

    // Check the undo and redo meny items
    ui->actionUndo->setEnabled(model->undoPossible());
    ui->actionRedo->setEnabled(model->redoPossible());
}

// A simple delay function I pulled of the 'net.. Need to delay reading a file a little bit.. A second seems to be enough
// really don't like this though as I have no idea of knowing when a second is enough.
// Having more than one second will impact usability of the application as it changes the focus.
void delay()
{
    QTime dieTime = QTime::currentTime().addSecs(1);
    while (QTime::currentTime() < dieTime)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
}

void MainWindow::fileModified(const QString &str)
{
    Q_UNUSED(str);
    //qDebug()<<"MainWindow::fileModified  "<<watcher->files()<<" --- "<<str;
    saveTableSelection();
    model->refresh();
    if (model->count() == 0)
    {
        // This sometimes happens when the file is being updated. We have gotten the signal a bit soon so the file is still empty. Wait and try again
        delay();
        model->refresh();
        updateTitle();
    }
    resetTableSelection();
    setFileWatch();
}

void MainWindow::clearFileWatch()
{
    if (watcher != NULL)
    {
        delete watcher;
        watcher = NULL;
    }
}

void MainWindow::setFileWatch()
{
    QSettings settings;
    if (settings.value(SETTINGS_AUTOREFRESH).toBool() == false)
        return;

    clearFileWatch();

    watcher = new QFileSystemWatcher();
    //qDebug()<<"Mainwindow::setFileWatch :: "<<watcher->files();
    watcher->removePaths(watcher->files()); // Make sure this is empty. Should only be this file we're using in this program, and only one instance
    watcher->addPath(model->getTodoFile());
    QObject::connect(watcher, SIGNAL(fileChanged(QString)), this, SLOT(fileModified(QString)));
}

void MainWindow::parse_todotxt()
{

    model = new TodoTableModel(this);
    proxyModel = new QSortFilterProxyModel(this);
    proxyModel->setSourceModel(model);
    proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    ui->tableView->setModel(proxyModel);
    //ui->tableView->resizeColumnsToContents();
    //ui->tableView->horizontalHeader()->setResizeMode(QHeaderView::Stretch);
    ui->tableView->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui->tableView->resizeColumnToContents(0); // Checkboxes kept small
    ui->tableView->setWordWrap(true);

    listModel = new QStringListModel(this);
    ui->lv_activetags->setModel(listModel);
    int row = listModel->rowCount();

    listModel->insertRows(row, 1000);
    // listModel->setData(listModel->index(0), "(A)");
    // listModel->setData(listModel->index(1), "(B)");
    // listModel->setData(listModel->index(2), "(C)");
    // listModel->setData(listModel->index(3), "@now");
    // listModel->setData(listModel->index(4), "@today");
    // listModel->setData(listModel->index(5), "@tonight");
    // listModel->setData(listModel->index(6), "@waiting");
    // listModel->setData(listModel->index(7), "@home");
    // listModel->setData(listModel->index(8), "@work");
    // listModel->setData(listModel->index(9), "@break");
    // listModel->setData(listModel->index(10), "@sideprojects");
    // listModel->setData(listModel->index(11), "@habits");
    // listModel->setData(listModel->index(12), "@learning");
    // listModel->setData(listModel->index(13), "@research");
    // listModel->setData(listModel->index(14), "+background");
}

void MainWindow::on_lineEdit_2_textEdited(const QString &arg1)
{
    Q_UNUSED(arg1);
    QSettings settings;

    bool liveUpdate = settings.value(SETTINGS_LIVE_SEARCH).toBool();
    if (!ui->cb_showaall->checkState() && liveUpdate)
    {
        updateSearchResults();
    }
}

void MainWindow::on_lineEdit_3_textEdited(const QString &arg1)
{
    Q_UNUSED(arg1);
    QSettings settings;

    bool liveUpdate = settings.value(SETTINGS_LIVE_SEARCH).toBool();
    if (!ui->cb_showaall->checkState() && liveUpdate)
    {
        updateSearchResults();
    }
}

QString get_second(pair<QString, int> i) { return i.first + " (" + QString::number(i.second, 'f', 0) + ")"; }

bool compareInterval(QString i1, QString i2)
{
    QRegularExpression projectRegex("^[+][a-zA-Z]");
    QRegularExpressionMatch m_i1project = projectRegex.match(i1);
    QRegularExpressionMatch m_i2project = projectRegex.match(i2);

    QRegularExpression contextRegex("^[@]");
    QRegularExpressionMatch m_i1context = contextRegex.match(i1);
    QRegularExpressionMatch m_i2context = contextRegex.match(i2);

    // Sort projects to top of list
    if (m_i1project.hasMatch() && !m_i2project.hasMatch())
    {
        return true;
    }
    else if (!m_i1project.hasMatch() && m_i2project.hasMatch())
    {
        return false;
    }

    // Sort contexts after projects
    // Then sort by standard words
    if (m_i1context.hasMatch() && !m_i2context.hasMatch())
    {
        return true;
    }
    else if (!m_i1context.hasMatch() && m_i2context.hasMatch())
    {
        return false;
    }

    QRegularExpression priorityRegex("[(]([0-9]+)[)]$");
    QRegularExpressionMatch m_i1 = priorityRegex.match(i1);
    QRegularExpressionMatch m_i2 = priorityRegex.match(i2);

    string s1 = m_i1.captured(1).toUtf8().constData();
    string s2 = m_i2.captured(1).toUtf8().constData();
    stringstream c1(s1);
    stringstream c2(s2);
    int x1 = 0;
    int x2 = 0;
    c1 >> x1;
    c2 >> x2;

    return (x1 > x2);
}

/* template <class InputIt, class OutputIt, class Pred, class Fct> */
/* auto transform_if =  [](auto first, auto last, auto dest, auto pred, auto transform) */
/* { */
/*    while (first != last) { */
/*       if (pred(*first)) */
/*          *dest++ = transform(*first); */

/*       ++first; */
/*    } */
/* }; */

static const QString stopwordsArr[] = {"and", "for", "to", "on", "with", "from", "in", "up", "about", "the", "of", "out", "new", "be", "do", "we", "is", "at", "that", "as", "by", "how", "other", "set", "an", "over", "if", "can", "get"};
vector<QString> stopwords(stopwordsArr, stopwordsArr + sizeof(stopwordsArr) / sizeof(stopwordsArr[0]));

void MainWindow::updateSearchResults()
{
    // Take the text of the format of match1 match2 !match3 and turn it into
    //(?=.*match1)(?=.*match2)(?!.*match3) - all escaped of course
    QString fullPhrase = ui->lineEdit_3->text() + " " + ui->lineEdit_2->text();
    QStringList words = fullPhrase.simplified().split(QRegularExpression("\\s+"));
    QString regexpstring = "(?=^.*$)"; // Seems a negative lookahead can't be first (!?), so this is a workaround
    for (QString word : words)
    {
        QString start = "(?=^.*";
        if (word.length() > 0 && word.at(0) == '!')
        {
            start = "(?!^.*";
            word = word.remove(0, 1);
        }
        if (word.length() == 0)
            break;
        regexpstring += start + QRegExp::escape(word) + ".*$)";
    }
    QRegExp regexp(regexpstring, Qt::CaseInsensitive);
    proxyModel->setFilterRegExp(regexp);
    //qDebug()<<"Setting filter: "<<regexp.pattern();
    proxyModel->setFilterKeyColumn(1);
    updateTitle();

    int rowCount = listModel->rowCount();
    for (int i = 0; i < rowCount; ++i)
    {
        listModel->setData(listModel->index(i), "");
    }

    std::map<QString, int> contexts = {};

    // for (int i = 0; i < ui->tableView->model()->rowCount() && i < 100; ++i)
    for (int i = 0; i < ui->tableView->model()->rowCount(); ++i)
    {
        auto index = ui->tableView->model()->index(i, 1);
        QString rowText = ui->tableView->model()->data(index, Qt::UserRole).toString();
        // QRegularExpression contextsRegex("([@][a-zA-Z0-9]+)");
        // QRegularExpression contextsRegex("[ ([]([+@a-zA-Z][@a-zA-Z0-9-:/.]+)");
        QRegularExpression contextsRegex("[ ([]([+@][@a-zA-Z0-9-:/.]+)");

        QRegularExpressionMatchIterator j = contextsRegex.globalMatch(rowText);
        while (j.hasNext())
        {
            QRegularExpressionMatch match = j.next();
            if (match.hasMatch())
            {
                QString context = match.captured(1);
                if (
                    ui->lineEdit_2->text().indexOf(context) == -1 && ui->lineEdit_3->text().indexOf(context) == -1)
                {
                    contexts[QString(context)]++;
                }
            }
        }
    }

    vector<QString> v(contexts.size());

    for (std::pair<const QString, int> &x : contexts)
    {
        if (std::find(stopwords.begin(), stopwords.end(), x.first) == stopwords.end())
        {
            v.push_back(x.first + " (" + QString::number(x.second, 'f', 0) + ")");
        }
    }

    sort(v.begin(), v.end(), compareInterval);

    v.insert(v.begin(), "-----------");
    v.insert(v.begin(), "rec:");
    v.insert(v.begin(), "due:");
    v.insert(v.begin(), "@home");
    v.insert(v.begin(), "@work");
    v.insert(v.begin(), "@tonight");
    v.insert(v.begin(), "@today");
    v.insert(v.begin(), "(F)");
    v.insert(v.begin(), "(E)");
    v.insert(v.begin(), "(D)");
    v.insert(v.begin(), "(C)");
    v.insert(v.begin(), "(B)");
    v.insert(v.begin(), "(A)");

    for (int i = 0; i < v.size(); i++)
    {
        listModel->setData(listModel->index(i), v[i]);
    }
}

void MainWindow::focusTodoList()
{
    auto index = ui->tableView->model()->index(0, 1);
    saved_selection = ui->tableView->model()->data(index, Qt::UserRole).toString();
    ui->tableView->selectionModel()->select(index, QItemSelectionModel::Select);
    ui->tableView->setCurrentIndex(index);
    ui->tableView->setFocus(Qt::OtherFocusReason);
}

void MainWindow::on_lineEdit_3_returnPressed()
{
    MainWindow::on_lineEdit_2_returnPressed();
}

void MainWindow::on_lineEdit_2_returnPressed()
{
    QSettings settings;
    bool liveUpdate = settings.value(SETTINGS_LIVE_SEARCH).toBool();

    if (!liveUpdate || ui->cb_showaall->isChecked())
    {
        updateSearchResults();
    }
    else
    {
        focusTodoList();
    }
}

void MainWindow::on_hotkey()
{
    auto dlg = new QuickAddDialog();
    dlg->setModal(true);
    dlg->show();
    dlg->exec();
    if (dlg->accepted)
    {
        this->addTodo(dlg->text);
    }
}

void MainWindow::setHotkey()
{
    QSettings settings;
    if (settings.value(SETTINGS_HOTKEY_ENABLE).toBool())
    {
        hotkey->registerHotkey(settings.value(SETTINGS_HOTKEY, DEFAULT_HOTKEY).toString());
        connect(hotkey, &UGlobalHotkeys::activated, [=](size_t id)
                {
                    Q_UNUSED(id);
                    on_hotkey();
                });
    }
    else
    {
        hotkey->unregisterAllHotkeys();
    }
}

void MainWindow::on_actionAbout_triggered()
{
    AboutBox d;
    d.setModal(true);
    d.show();
    d.exec();
    //myanalytics->check_update();
}

void MainWindow::on_actionSettings_triggered()
{
    SettingsDialog d;
    d.setModal(true);
    d.show();
    d.exec();
    if (d.refresh)
    {
        delete model;
        model = new TodoTableModel(this);
        proxyModel->setSourceModel(model);
        ui->tableView->setModel(proxyModel);
        ui->tableView->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        ui->tableView->resizeColumnToContents(0);
        saveTableSelection();
        model->refresh(); // Not 100% sure why this is needed.. Should be done by re-setting the model above
        resetTableSelection();
        setFileWatch();
        setTray();
        setFontSize();
    }
}

void MainWindow::setFontSize()
{
    QSettings settings;
    int size = settings.value(SETTINGS_FONT_SIZE).toInt();
    if (size > 0)
    {
        auto f = qApp->font();
        f.setPointSize(size);
        qApp->setFont(f);
    }
}

void MainWindow::setTray()
{
    QSettings settings;
    if (settings.value(SETTINGS_TRAY_ENABLED, DEFAULT_TRAY_ENABLED).toBool())
    {
        // We should be showing a tray icon. Are we?
        if (trayicon == NULL)
        {
            trayicon = new QSystemTrayIcon(this);
            traymenu = new QMenu(this);
            minimizeAction = new QAction(tr("Mi&nimize"), this);
            connect(minimizeAction, SIGNAL(triggered()), this, SLOT(hide()));
            maximizeAction = new QAction(tr("Ma&ximize"), this);
            connect(maximizeAction, SIGNAL(triggered()), this, SLOT(showMaximized()));
            restoreAction = new QAction(tr("&Restore"), this);
            connect(restoreAction, SIGNAL(triggered()), this, SLOT(showNormal()));
            quitAction = new QAction(tr("&Quit"), this);
            connect(quitAction, SIGNAL(triggered()), this, SLOT(cleanup()));
            connect(QApplication::instance(), SIGNAL(aboutToQuit()), this, SLOT(cleanup()));

            traymenu->addAction(minimizeAction);
            traymenu->addAction(maximizeAction);
            traymenu->addAction(restoreAction);
            traymenu->addSeparator();
            traymenu->addAction(quitAction);
            trayicon->setContextMenu(traymenu);
            connect(trayicon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
                    this, SLOT(iconActivated(QSystemTrayIcon::ActivationReason)));

            trayicon->setIcon(QIcon(":/icons/newicon.png"));
        }
        trayicon->show();
    }
    else
    {
        if (trayicon != NULL)
        {
            trayicon->hide();
        }
    }
}

void MainWindow::iconActivated(QSystemTrayIcon::ActivationReason reason)
{
    switch (reason)
    {
    case QSystemTrayIcon::Trigger:
    case QSystemTrayIcon::DoubleClick:
        // Make sure the window is open
        this->show();
        break;
    case QSystemTrayIcon::MiddleClick:
        // Do nothing
        break;
    default:;
    }
}

void MainWindow::on_pushButton_clicked()
{
    if (ui->lineEdit->text() == "")
    {
        MainWindow::on_lineEdit_2_returnPressed();
        return;
    }

    QRegularExpression priorityRegex("([(][A-Z][)])");
    QRegularExpressionMatch m_lineEdit = priorityRegex.match(ui->lineEdit->text());
    QRegularExpressionMatch m_lineEdit_4 = priorityRegex.match(ui->lineEdit_4->text());
    QString txt;
    if (
        m_lineEdit.hasMatch() && m_lineEdit_4.hasMatch())
    {
        // Strip the priority label from the lineEdit_4 default text, so the lineEdit priority label is used instead
        txt = ui->lineEdit->text() + " " + ui->lineEdit_4->text().replace(m_lineEdit_4.captured(1), "");
    }
    else
    {
        txt = ui->lineEdit_4->text() + " " + ui->lineEdit->text();
    }
    addTodo(txt);
    ui->lineEdit->clear();
}

void MainWindow::addTodo(QString &s)
{

    if (ui->context_lock->isChecked())
    {
        // The line should have the context of the search field except any negative search
        QString fullPhrase = ui->lineEdit_3->text() + " " + ui->lineEdit_2->text();
        QStringList contexts = fullPhrase.simplified().split(QRegExp("\\s"));
        for (QString context : contexts)
        {
            if (context.length() > 0 && context.at(0) != '@' && !context.contains("rec:") && !context.contains("t:") && !context.contains("due:") && context.at(0) != '(' && context.at(0) != '+')
                continue; // ignore this one

            if (!s.contains(context, Qt::CaseInsensitive))
            {
                if (context.at(0) == "(" && context.at(2) == ")")
                {
                    if (s.at(0) != "(")
                    {
                        s.prepend(context + " ");
                    }
                }
                else
                {
                    s.append(" " + context);
                }
            }
        }
    }
    model->add(s);
    updateTitle();
}

void MainWindow::on_lineEdit_returnPressed()
{
    on_pushButton_clicked();
}

void MainWindow::on_pushButton_2_clicked()
{
    deleteSelected();
}

void MainWindow::deleteSelected()
{
    saveCurrentIndex();

    forEachSelection(
        [=](QModelIndex index, QString data)
        {
            QString t = model->data(proxyModel->mapToSource(index), Qt::UserRole).toString(); // User Role is Raw data
            //QString t=proxyModel->data(i).toString();
            model->remove(t, false);
        },
        [=]()
        {
            model->endReset();
            ui->tableView->setCurrentIndex(ui->tableView->model()->index(saved_row, saved_column));
            ui->tableView->setFocus(Qt::OtherFocusReason);
        });

    updateTitle();
}

void MainWindow::on_pushButton_3_clicked()
{
    // Archive
    saveTableSelection();
    model->archive();
    resetTableSelection();
    updateTitle();
}

void MainWindow::on_pushButton_4_clicked()
{
    saveTableSelection();
    model->refresh();
    resetTableSelection();
    updateTitle();
}

void MainWindow::saveTableSelection()
{
    //auto index = ui->tableView->selectionModel()->selectedIndexes();
    auto index = ui->tableView->selectionModel()->currentIndex();
    //    if(index.count()>0){
    if (index.isValid())
    {
        // Vi har någonting valt.
        // qDebug()<<"Selected index: "<<index.at(0)<<endl;
        //saved_selection = model->data(index.at(0),Qt::UserRole).toString();
        saved_selection = model->data(index, Qt::UserRole).toString();
        //qDebug()<<"Selected text: "<<saved_selection<<endl;
    }
}

void MainWindow::showMessage(QString message)
{
    QMessageBox *qmb;
    qmb = new QMessageBox(QMessageBox::NoIcon,
                          "Hey",
                          message,
                          QMessageBox::Ok,
                          this);
    qmb->exec();
    delete qmb;
}

void MainWindow::showMessage(int message)
{
    showMessage(QString::number(message, 'f', 2));
}

void MainWindow::resetTableSelection()
{
    /* showMessage(saved_row); */
    if (saved_row >= 0)
    {
        auto index = ui->tableView->model()->index(saved_row, saved_column);
        ui->tableView->setCurrentIndex(index);
        ui->tableView->selectionModel()->select(index, QItemSelectionModel::Select);
        ui->tableView->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
        ui->tableView->setFocus(Qt::OtherFocusReason);

        saved_row = -1;
    }
    else if (saved_selection.size() > 0)
    {
        // Set the selection again
        QModelIndexList foundIndexes = model->match(QModelIndex(), Qt::UserRole, saved_selection);
        if (foundIndexes.count() > 0)
        {
            auto index = proxyModel->mapFromSource(foundIndexes.at(0));
            ui->tableView->selectionModel()->select(index, QItemSelectionModel::Select);
            ui->tableView->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
            ui->tableView->setCurrentIndex(index);
            ui->tableView->setFocus(Qt::OtherFocusReason);
        }

        saved_selection = "";
    }
}

void MainWindow::cleanup()
{
    QSettings settings;

    settings.setValue(SETTINGS_GEOMETRY, saveGeometry());
    settings.setValue(SETTINGS_SAVESTATE, saveState());
    settings.setValue(SETTINGS_MAXIMIZED, isMaximized());
    if (!isMaximized())
    {
        settings.setValue(SETTINGS_POSITION, pos());
        settings.setValue(SETTINGS_SIZE, size());
    }
    settings.setValue(SETTINGS_SEARCH_STRING, ui->lineEdit_2->text());
    settings.setValue(SETTINGS_CONTEXT_STRING, ui->lineEdit_3->text());
    settings.setValue(SETTINGS_DEFAULT_TEXT_STRING, ui->lineEdit_4->text());
    if (trayicon != NULL)
    {
        delete trayicon;
        trayicon = NULL;
    }
    qApp->quit();
}

void MainWindow::closeEvent(QCloseEvent *ev)
{

    if (trayicon != NULL && trayicon->isVisible())
    {
        hide();
        ev->ignore();
    }
    else
    {
        cleanup();
        ev->accept();
    }
}

void MainWindow::on_btn_Alphabetical_toggled(bool checked)
{
    QSettings settings;
    settings.setValue(SETTINGS_SORT_ALPHA, checked);
    on_pushButton_4_clicked(); // Refresh
}

void MainWindow::on_context_lock_toggled(bool checked)
{
    QSettings settings;
    settings.setValue(SETTINGS_CONTEXT_LOCK, checked);
}

void MainWindow::on_cb_showaall_stateChanged(int arg1)
{
    QSettings settings;
    settings.setValue(SETTINGS_SHOW_ALL, arg1);
    on_pushButton_4_clicked();
}

void MainWindow::on_cb_threshold_inactive_stateChanged(int arg1)
{
    QSettings settings;
    settings.setValue(SETTINGS_THRESHOLD_INACTIVE, arg1);
    on_pushButton_4_clicked();
}

void MainWindow::on_pb_closeVersionBar_clicked()
{
    ui->newVersionView->hide();
}

bool forced_check_version = false;
void MainWindow::requestReceived(QNetworkReply *reply)
{
    QString replyText;
    QSettings settings;
    if (reply->error() == QNetworkReply::NoError)
    {

        // Get the http status code
        int v = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (v >= 200 && v < 300) // Success
        {
            replyText = reply->readAll();
            double latest_version = replyText.toDouble();
            double this_version = QString(VER).toDouble();
            qDebug() << "Checked version - Latest: " << latest_version << " this version " << this_version << Qt::endl;
            if (latest_version > this_version || forced_check_version)
            {
                if (latest_version >= this_version)
                {
                    ui->lbl_latestVersion->hide();
                }
                else
                {
                    ui->lbl_newVersion->hide();
                }
                ui->txtLatestVersion->setText("(v" + QString::number(latest_version, 'f', 2) + ")");
                ui->newVersionView->show();
                forced_check_version = false;
            }
            // Update the last checked since we were successful
            settings.setValue(SETTINGS_LAST_UPDATE_CHECK, QDate::currentDate().toString("yyyy-MM-dd"));
        }
    }
    reply->deleteLater();
}

void MainWindow::undo()
{
    if (model->undo())
    {
        model->refresh();
    }
}

void MainWindow::redo()
{
    if (model->redo())
    {
        model->refresh();
    }
}

void MainWindow::clearSearch()
{
    ui->lineEdit_2->setText("");

    updateSearchResults();

    ui->lineEdit_2->setFocus();
}

void MainWindow::launchUrl()
{
    auto index = ui->tableView->selectionModel()->currentIndex();
    QString URL = ui->tableView->model()->data(index, Qt::UserRole + 1).toString();
    if (!URL.isEmpty())
    {
        QDesktopServices::openUrl(URL);
    }
}

void MainWindow::saveCurrentIndex()
{
    auto index = ui->tableView->selectionModel()->selection().indexes().first();
    saved_row = index.row();
    saved_column = index.column();
}

void MainWindow::completeTasks()
{
    saveCurrentIndex();

    forEachSelection([=](QModelIndex index, QString data)
                     {
                         auto checkbox = ui->tableView->model()->index(index.row(), 0);
                         model->toggleRow(proxyModel->mapToSource(checkbox), false);
                     },
                     [=]()
                     {
                         model->endReset();
                         ui->tableView->setCurrentIndex(ui->tableView->model()->index(saved_row, saved_column));
                         ui->tableView->setFocus(Qt::OtherFocusReason);
                     });
}

void MainWindow::up()
{
    QKeyEvent *key_press = new QKeyEvent(QKeyEvent::KeyPress, Qt::Key_Up, Qt::NoModifier, "Up");
    QApplication::sendEvent(ui->tableView, key_press);
}

void MainWindow::shiftUp()
{
    QKeyEvent *key_press = new QKeyEvent(QKeyEvent::KeyPress, Qt::Key_Up, Qt::ShiftModifier, "Up");
    QApplication::sendEvent(ui->tableView, key_press);
}

void MainWindow::down()
{
    QKeyEvent *key_press = new QKeyEvent(QKeyEvent::KeyPress, Qt::Key_Down, Qt::NoModifier, "Down");
    QApplication::sendEvent(ui->tableView, key_press);
}

void MainWindow::shiftDown()
{
    QKeyEvent *key_press = new QKeyEvent(QKeyEvent::KeyPress, Qt::Key_Down, Qt::ShiftModifier, "Down");
    QApplication::sendEvent(ui->tableView, key_press);
}

void MainWindow::left()
{
    QKeyEvent *key_press = new QKeyEvent(QKeyEvent::KeyPress, Qt::Key_Left, Qt::NoModifier, "Left");
    QApplication::sendEvent(ui->tableView, key_press);
}

void MainWindow::right()
{
    QKeyEvent *key_press = new QKeyEvent(QKeyEvent::KeyPress, Qt::Key_Right, Qt::NoModifier, "Right");
    QApplication::sendEvent(ui->tableView, key_press);
}

void MainWindow::pageUp()
{
    QKeyEvent *key_press = new QKeyEvent(QKeyEvent::KeyPress, Qt::Key_PageUp, Qt::NoModifier, "PageUp");
    QApplication::sendEvent(ui->tableView, key_press);
}

void MainWindow::pageDown()
{
    QKeyEvent *key_press = new QKeyEvent(QKeyEvent::KeyPress, Qt::Key_PageDown, Qt::NoModifier, "PageDown");
    QApplication::sendEvent(ui->tableView, key_press);
}

void MainWindow::toggleToday()
{
    saveCurrentIndex();

    auto text = "@today";

    QRegularExpression todayRegex("([ ]?@today)");
    forEachSelection([=](QModelIndex index, QString data)
                     {
                         QRegularExpressionMatch m = todayRegex.match(data);
                         if (m.hasMatch())
                         {
                             QString text = m.captured(1);
                             data.replace(text, "");
                         }
                         else
                         {
                             data.append(" ");
                             data.append(text);
                         }
                         model->setData(proxyModel->mapToSource(index), data.simplified(), Qt::EditRole, false);
                     },
                     [=]()
                     {
                         model->endReset();
                         ui->tableView->setCurrentIndex(ui->tableView->model()->index(saved_row, saved_column));
                         ui->tableView->setFocus(Qt::OtherFocusReason);
                     });
}

void MainWindow::showAppendDialog()
{
    saveCurrentIndex();

    auto dialog = new QInputDialog(this);
    dialog->setWindowTitle("Append Text");
    dialog->setLabelText("Text to append");
    dialog->setTextValue("");
    dialog->setTextEchoMode(QLineEdit::Normal);
    const int ret = dialog->exec();
    if (ret)
    {
        auto text = dialog->textValue();

        forEachSelection([=](QModelIndex index, QString data)
                         {
                             data.append(" ");
                             data.append(text);
                             model->setData(proxyModel->mapToSource(index), data.simplified(), Qt::EditRole, false);
                         },
                         [=]()
                         { model->endReset(); });
    }
}

void MainWindow::showRemovalDialog()
{
    saveCurrentIndex();

    auto dialog = new QInputDialog(this);
    dialog->setWindowTitle("Remove Text");
    dialog->setLabelText("Text to remove");
    dialog->setTextValue("");
    dialog->setTextEchoMode(QLineEdit::Normal);
    const int ret = dialog->exec();
    if (ret)
    {
        auto text = dialog->textValue();

        forEachSelection([=](QModelIndex index, QString data)
                         {
                             data.replace(text, "");
                             model->setData(proxyModel->mapToSource(index), data.simplified(), Qt::EditRole, false);
                         },
                         [=]()
                         { model->endReset(); });
    }
}

void MainWindow::setThresholdToToday()
{
    saveCurrentIndex();
    setThresholdForSelected("0d");
}

void MainWindow::setThresholdToTomorrow()
{
    saveCurrentIndex();
    setThresholdForSelected("1d");
}

void MainWindow::showThresholdDialog()
{
    saveCurrentIndex();
    showDateDialog("Threshold", "t:", "(t:[\\d-]+)");
}

void MainWindow::showDueDialog()
{
    saveCurrentIndex();
    showDateDialog("Due", "due:", "(due:[\\d-]+)");
}

void MainWindow::setThresholdForSelected(QString threshold)
{
    QRegularExpression dateRegex("^(t:[\\d-]+)");
    forEachSelection([=](QModelIndex index, QString data)
                     {
                         QRegularExpressionMatch m = dateRegex.match(data);
                         if (m.hasMatch())
                         {
                             if (threshold == "t:")
                             {
                                 data = data.replace(" " + m.captured(1), "t:" + threshold);
                             }
                             else
                             {
                                 data = data.replace(m.captured(1), "t:" + threshold);
                             }
                         }
                         else
                         {
                             data.append(" t:");
                             data.append(threshold);
                         }

                         model->setData(proxyModel->mapToSource(index), data, Qt::EditRole, false);
                     },
                     [=]()
                     { model->endReset(); });
}

void MainWindow::showDateDialog(QString typeName, QString prefix, QString dateRegexString)
{
    auto dialog = new QInputDialog(this);
    dialog->setWindowTitle("Set " + typeName + " Date");
    dialog->setLabelText("YYYY-MM-DD or 999[dwmypb]");

    QModelIndex index = ui->tableView->selectionModel()->selection().indexes().first();
    QString firstData = ui->tableView->model()->data(index, Qt::UserRole).toString();

    QRegularExpression dateRegex(dateRegexString);
    QRegularExpressionMatch m = dateRegex.match(firstData);

    if (m.hasMatch())
    {
        QString value = m.captured(1);
        dialog->setTextValue(value.replace(prefix, ""));
    }
    else
    {
        QString value = prefix + todo->getToday();
        dialog->setTextValue(value.replace(prefix, ""));
    }

    dialog->setTextEchoMode(QLineEdit::Normal);
    const int ret = dialog->exec();
    if (ret)
    {
        QString text = prefix + dialog->textValue();

        setThresholdForSelected(text);
    }
}

void MainWindow::increasePriority()
{
    saveCurrentIndex();

    QRegularExpression priorityRegex("^\\(([A-Z])\\)");

    forEachSelection([=](QModelIndex index, QString data)
                     {
                         QRegularExpressionMatch m = priorityRegex.match(data);
                         if (m.hasMatch())
                         {
                             auto newValue = QString(m.captured(1)[0]).at(0).toLatin1() - 1;
                             if (newValue >= 65)
                             {
                                 data = data.replace("(" + m.captured(1) + ")", "(" + (QString(newValue)) + ")");
                             }

                             model->setData(proxyModel->mapToSource(index), data, Qt::EditRole, false);
                         }
                     },
                     [=]()
                     {
                         model->endReset();
                         ui->tableView->setCurrentIndex(ui->tableView->model()->index(saved_row, saved_column));
                         ui->tableView->setFocus(Qt::OtherFocusReason);
                     });
}

void MainWindow::decreasePriority()
{
    saveCurrentIndex();

    QRegularExpression priorityRegex("^\\(([A-Z])\\)");

    forEachSelection([=](QModelIndex index, QString data)
                     {
                         QRegularExpressionMatch m = priorityRegex.match(data);
                         if (m.hasMatch())
                         {
                             auto newValue = QString(m.captured(1)[0]).at(0).toLatin1() + 1;
                             if (newValue <= 90)
                             {
                                 data = data.replace("(" + m.captured(1) + ")", "(" + (QString(newValue)) + ")");
                             }

                             model->setData(proxyModel->mapToSource(index), data, Qt::EditRole, false);
                         }
                     },
                     [=]()
                     {
                         model->endReset();
                         ui->tableView->setCurrentIndex(ui->tableView->model()->index(saved_row, saved_column));
                         ui->tableView->setFocus(Qt::OtherFocusReason);
                     });
}

void MainWindow::forEachSelection(std::function<void(QModelIndex, QString)> func, std::function<void()> callback)
{
    QModelIndexList indexes = ui->tableView->selectionModel()->selection().indexes();

    for (QModelIndex index : indexes)
    {
        QString data = ui->tableView->model()->data(index, Qt::UserRole).toString();
        func(index, data);
    }

    callback();
}

// Check if there is an update available
void MainWindow::requestPage(QString &s)
{
    connect(networkaccessmanager, SIGNAL(finished(QNetworkReply *)), this, SLOT(requestReceived(QNetworkReply *)));
    networkaccessmanager->get(QNetworkRequest(QUrl(s)));
}

void MainWindow::on_actionCheck_for_updates_triggered()
{
    forced_check_version = true;
    QString URL = VERSION_URL + (QString) "?v=" + VER;
    requestPage(URL);
}

void MainWindow::on_tableView_customContextMenuRequested(const QPoint &pos)
{
    // This is used for triggering opening of a link.
    // Find out where we are
    auto index = ui->tableView->indexAt(pos);
    QString URL = ui->tableView->model()->data(index, Qt::UserRole + 1).toString();
    if (!URL.isEmpty())
    {
        QDesktopServices::openUrl(URL);
    }
}

void MainWindow::on_actionQuit_triggered()
{
    cleanup();
}

void MainWindow::on_actionUndo_triggered()
{
    undo();
    updateTitle();
}

void MainWindow::on_actionRedo_triggered()
{
    redo();
    updateTitle();
}

void MainWindow::on_lv_activetags_clicked(QModelIndex index)
{
    Qt::KeyboardModifiers key = QApplication::queryKeyboardModifiers();

    QString previousSearch = ui->lineEdit_2->text().simplified();
    QString newSearch = "";

    QString selectedContext = index.data().toString();

    QRegularExpression priorityRegex("([(][0-9]+[)])$");
    QRegularExpressionMatch m_selectedContext = priorityRegex.match(selectedContext);
    selectedContext = selectedContext.replace(m_selectedContext.captured(1), "");

    QStringList words = previousSearch.split(QRegularExpression("\\s+"));
    QString regexpstring = "(?=^.*$)"; // Seems a negative lookahead can't be first (!?), so this is a workaround

    bool found = false;

    for (QString word : words)
    {
        if (word == selectedContext)
        {
            found = true;
            newSearch += " !" + word;
        }
        else if (word == "!" + selectedContext)
        {
            found = true;
            continue;
        }
        else
        {
            newSearch += " " + word;
        }
    }

    if (!found)
    {
        if (key == Qt::ControlModifier)
        {
            newSearch += " !" + selectedContext;
        }
        else
        {
            newSearch += " " + selectedContext;
        }
    }

    ui->lineEdit_2->setText(newSearch.simplified() + " ");

    updateSearchResults();

    ui->lv_activetags->clearSelection();

    ui->lineEdit_2->setFocus();
}
