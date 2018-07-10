#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QModelIndex>
#include <uglobalhotkeys.h>
#include <QSystemTrayIcon>
#include <QMenu>

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT
    
public:
    explicit MainWindow(QWidget *parent = 0);
    void parse_todotxt();
    void addTodo(QString &s);
    ~MainWindow();
    
public slots:
    void fileModified(const QString& str);

private slots:
    void on_lineEdit_2_textEdited(const QString &arg1);

    void on_actionSettings_triggered();

    void on_actionAbout_triggered();

    void on_pushButton_clicked();

    void on_lineEdit_returnPressed();

    void on_pushButton_2_clicked();

    void on_pushButton_3_clicked();

    void on_pushButton_4_clicked();

    void dataInModelChanged(QModelIndex i1,QModelIndex i2);

    void on_btn_Alphabetical_toggled(bool checked);

    void on_lineEdit_2_returnPressed();

    void on_hotkey();

    void on_context_lock_toggled(bool checked);


    void on_cb_showaall_stateChanged(int arg1);

    void iconActivated(QSystemTrayIcon::ActivationReason reason);
    void cleanup(); // Need to have a quit slot of my own to save settings and so on.


private:
    void setFileWatch();
    void setTray();
    void clearFileWatch();
    void closeEvent(QCloseEvent *ev);
    Ui::MainWindow *ui;
    void saveTableSelection();
    void resetTableSelection();
    void updateSearchResults();
    UGlobalHotkeys *hotkey;
    void setHotkey();
    QSystemTrayIcon *trayicon = NULL;
    QMenu *traymenu=NULL;
    QAction *minimizeAction;
    QAction *maximizeAction;
    QAction *restoreAction;
    QAction *quitAction;
};

#endif // MAINWINDOW_H
