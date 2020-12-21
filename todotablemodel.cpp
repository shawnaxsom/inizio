#include "todotablemodel.h"
#include "todotxt.h"
#include "globals.h"
#include "def.h"
#include <QFont>
#include <QColor>
#include <QSettings>
#include <QDebug>

vector<QString> todo_data;

TodoTableModel::TodoTableModel(QObject *parent) :
    QAbstractTableModel(parent)
{
    todo = new todotxt();
    todo->parse();
}

TodoTableModel::~TodoTableModel()
{
    delete todo;
}

int TodoTableModel::rowCount(const QModelIndex &parent) const {
    Q_UNUSED(parent);
    if(todo_data.empty()){
        QString temp;
        todo->getAll(temp,todo_data);
    }
    int size = (int)todo_data.size();
    return size;
}


QString TodoTableModel::getTodoFile(){
    return todo->getTodoFilePath();
}

int TodoTableModel::columnCount(const QModelIndex &parent) const {
    Q_UNUSED(parent);
    return 2;
}

QVariant TodoTableModel::data(const QModelIndex &index, int role) const {
    QSettings settings;

    if (!index.isValid())
             return QVariant();

    if(todo_data.empty()){
        QString temp;
        todo->getAll(temp,todo_data);
    }

    if (index.row() >= (int) todo_data.size() || index.row() < 0)
             return QVariant();

    if (role == Qt::DisplayRole || role == Qt::EditRole|| role==Qt::ToolTipRole) {
        if(index.column()==1){
            QString s=todo->prettyPrint((todo_data.at(index.row())));
            return s;
        }
     }

    if(role == Qt::CheckStateRole) {
        if(index.column()==0)
            return todo->getState(todo_data.at(index.row()));
    }

    if(role == Qt::FontRole) {
        if(index.column()==1){
            QFont f;
            if(todo->isInactive(todo_data.at(index.row()))){
                f.fromString(settings.value(SETTINGS_INACTIVE_FONT).toString());
            } else {
                f.fromString(settings.value(SETTINGS_ACTIVE_FONT).toString());
            }
            f.setStrikeOut(todo->getState(todo_data.at(index.row()))); // Strike out if done

            QString url =todo->getURL(todo_data.at(index.row())) ;
            if(!url.isEmpty()){
                f.setUnderline(true);
            }

            return f;
        }
    }

    if (role == Qt::TextColorRole) {

        int due = todo->dueIn(todo_data.at(index.row())); // The settings check is done in the todo call
        bool active = true;
        if(todo_data.at(index.row()).startsWith("x ")){
            active = false;
        }

        if(active && due<=0){
            // We have passed due date
            return QVariant::fromValue(QColor::fromRgba(settings.value(SETTINGS_DUE_LATE_COLOR,DEFAULT_DUE_LATE_COLOR).toUInt()));
        } else if(active && due<=settings.value(SETTINGS_DUE_WARNING,DEFAULT_DUE_WARNING).toInt()){
            return QVariant::fromValue(QColor::fromRgba(settings.value(SETTINGS_DUE_WARNING_COLOR,DEFAULT_DUE_WARNING_COLOR).toUInt()));
        }
        else if(todo->isInactive(todo_data.at(index.row()))){
            return QVariant::fromValue(QColor::fromRgba(settings.value(SETTINGS_INACTIVE_COLOR,DEFAULT_INACTIVE_COLOR).toUInt()));
        } else {
            return QVariant::fromValue(QColor::fromRgba(settings.value(SETTINGS_ACTIVE_COLOR,DEFAULT_ACTIVE_COLOR).toUInt()));
        }
    }

    if(role == Qt::UserRole){
        // This one returns the RAW value of the row
        return todo_data.at(index.row());
    }

    if(role == Qt::UserRole+1){
        return todo->getURL(todo_data.at(index.row()));
    }

    return QVariant();
}


QVariant TodoTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    Q_UNUSED(orientation);
    if(role == Qt::DisplayRole)
    {
      if(section==0){
          return "Done";
      }
      if(section==1){
          return "Todo";
      }

    }

  return QVariant::Invalid;
}

bool TodoTableModel::setData(const QModelIndex & index, const QVariant & value, int role)
{
    setData(index, value, role, true);
}

bool TodoTableModel::setData(const QModelIndex & index, const QVariant & value, int role, bool shouldEndResetModel)
{
    if(role == Qt::CheckStateRole)
    {
        beginResetModel();
        todo->update(todo_data.at(index.row()),value.toBool(),todo_data.at(index.row()));
    }
    else if(role == Qt::EditRole){
        beginResetModel();
        bool checked = true?todo_data.at(index.row()).at(0)=='x':false;
        QString s=value.toString();
        todo->update(todo_data.at(index.row()),checked,s);
    } else {
        // Nothing changed
        return false;
    }

  if (shouldEndResetModel) {
    todo_data.clear();
    endResetModel(); // Can't call this if working in a batch list or it will segfault
  }

  emit dataChanged(index, index); // Detta innebär ju också att denna item är den som är selected just nu så vi kan lyssna på den signalen

  return true;
}

bool TodoTableModel::toggleRow(const QModelIndex & index, bool shouldEndResetModel)
{
    bool newCheckedValue = todo_data.at(index.row()).at(0) == 'x' ? false : true;
    qDebug()<<"New checked value"<<newCheckedValue<<"index:"<<index;
    return setData(index, newCheckedValue, Qt::CheckStateRole, shouldEndResetModel);
}

void TodoTableModel::endReset(){
    endResetModel();
}

void TodoTableModel::add(QString text){
    beginResetModel();
    QString temp;
    todo->update(temp,false,text.replace('\n',' ')); // Make sure newlines don't get through as that would create multiple rows
    todo_data.clear();
    endResetModel();
}

void TodoTableModel::remove(QString text){
    beginResetModel();
    todo->remove(text);
    // Old way : QString temp;todo->update(text,false,temp); // Sending in an empty string = remove
    todo_data.clear();
    endResetModel();
}

void TodoTableModel::archive(){
    beginResetModel();
    todo->archive();
    todo_data.clear();
    endResetModel();
}

void TodoTableModel::refresh(){
    beginResetModel();
    todo->refresh();
    todo_data.clear();
    endResetModel();
}

Qt::ItemFlags TodoTableModel::flags(const QModelIndex& index) const
{
  Qt::ItemFlags returnFlags = QAbstractTableModel::flags(index);

  if (index.column() == 0)
  {
    returnFlags |= Qt::ItemIsUserCheckable;
  }
  if (index.column()==1){
     returnFlags |= Qt::ItemIsEditable | Qt::ItemNeverHasChildren;
  }

  return returnFlags;
}

QModelIndexList TodoTableModel::match(const QModelIndex &start, int role, const QVariant &value, int hits , Qt::MatchFlags flags) const {
    Q_UNUSED(start);
    Q_UNUSED(hits);
    Q_UNUSED(flags);
    QModelIndexList ret;
    int rows = this->rowCount(QModelIndex()); // Denna tar och laddar modellen också med data
    // Gå igenom alla rader och leta efter en exakt träff
    for(int i=0;i<rows;i++){
        // Skapa ett index
        QModelIndex index= createIndex(i,1);
        if(value.toString() == this->data(index,role)){
            ret.append(index);
        }
    }

    return ret;
}

bool TodoTableModel::undo()
{
    return todo->undo();
}

bool TodoTableModel::redo()
{
    return todo->redo();
}

bool TodoTableModel::undoPossible()
{
    return todo->undoPossible();
}

bool TodoTableModel::redoPossible()
{
    return todo->redoPossible();
}

int TodoTableModel::count(){
    return this->rowCount(QModelIndex());
}
