#include "common.h"

#include "settingskeys.h"

// Didn't find sleep in QCore
#ifdef Q_OS_WIN
#include <winsock2.h> // for windows.h
#include <windows.h> // for Sleep
#endif


#include <QSettings>
#include <QDebug>
#include <QMutex>


// global variable until this becomes a static class
QMutex printMutex_;

// TODO move this to a different file from common.h
void qSleep(int ms)
{

#ifdef Q_OS_WIN
    Sleep(uint(ms));
#else
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000 * 1000 };
    nanosleep(&ts, nullptr);
#endif
}


const int BEGIN_LENGTH = 40;


//TODO use cryptographically secure callID generation to avoid collisions.
const QString alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                         "abcdefghijklmnopqrstuvwxyz"
                         "0123456789";


void printHelper(QString color, QString beginString, QString valueString, QString description, int valuenames);


QString generateRandomString(uint32_t length)
{
  // TODO make this cryptographically secure to avoid collisions
  QString string;
  for( unsigned int i = 0; i < length; ++i )
  {
    string.append(alphabet.at(qrand()%alphabet.size()));
  }
  return string;
}


void printDebug(DebugType type, const QObject *object, QString description,
                QStringList valueNames, QStringList values)
{
  printDebug(type, object->metaObject()->className(),
             description, valueNames, values);
}


void printNormal(const QObject *object, QString description,
                      QString valueName, QString value)
{
  printDebug(DEBUG_NORMAL, object, description, {valueName}, {value});
}


void printImportant(const QObject* object, QString description,
                   QString valueName, QString value)
{
  printDebug(DEBUG_IMPORTANT, object, description, {valueName}, {value});
}


void printWarning(const QObject* object, QString description,
                  QString valueName, QString value)
{
  printDebug(DEBUG_WARNING, object, description, {valueName}, {value});
}


void printError(const QObject *object, QString description,
                QString valueName, QString value)
{
  printDebug(DEBUG_ERROR, object, description, {valueName}, {value});
}


void printProgramError(const QObject *object, QString description,
                      QString valueName, QString value)
{
  printDebug(DEBUG_PROGRAM_ERROR, object, description, {valueName}, {value});
}


void printProgramWarning(const QObject *object, QString description,
                         QString valueName, QString value)
{
  printDebug(DEBUG_PROGRAM_WARNING, object, description, {valueName}, {value});
}


void printPeerError(const QObject *object, QString description,
                    QString valueName, QString value)
{
  printDebug(DEBUG_PEER_ERROR, object, description, {valueName}, {value});
}


void printUnimplemented(const QObject* object, QString whatIsNotImplemented)
{
  printDebug(DEBUG_PROGRAM_WARNING, object,
             QString("NOT IMPLEMENTED: ") + whatIsNotImplemented);
}




void printDebug(DebugType type, QString className,
                QString description, QStringList valueNames, QStringList values)
{
  QString valueString = "";



  // do we have values.
  if( values.size() != 0)
  {
    // Add "name: value" because equal number of both.
    if (valueNames.size() == values.size()) // equal number of names and values
    {
      for (int i = 0; i < valueNames.size(); ++i)
      {
        if (valueNames.at(i) != "" && values.at(i) != "")
        {
          if (valueNames.size() != 1)
          {
            valueString.append(QString(BEGIN_LENGTH, ' '));
            valueString.append("-- ");
          }
          valueString.append(valueNames.at(i));
          valueString.append(": ");
          valueString.append(values.at(i));

          if (valueNames.size() != 1)
          {
            valueString.append("\r\n");
          }
        }
      }
    }
    else if (valueNames.size() == 1 && valueNames.at(0) != "") // if we have one name, add it
    {
      valueString.append(valueNames.at(0));
      valueString.append(": ");
    }

    // If we have one or zero names, just add all values, unless we have 1 of both
    // in which case they were added earlier.
    if (valueNames.empty() || (valueNames.size() == 1 && values.size() != 1))
    {
      for (int i = 0; i < values.size(); ++i)
      {
        valueString.append(values.at(i));
        if (i != values.size() - 1)
        {
          valueString.append(", ");
        }
      }
    }
    else if (valueNames.size() != values.size())
    {
      qDebug() << "Debug printing could not figure how to print error values."
               << "Names:" << valueNames.size()
               << "values: " << values.size();
    }
  }

  // TODO: Set a constant length for everything before description.

  QString beginString = className + ": ";

  QString black   = "\033[0m";
  QString yellow  = "\033[1;33m";
  QString red     = "\033[31m";
  QString blue    = "\033[34m";

  // This could be reduced, but it might change so not worth probably at the moment.
  // Choose which text to print based on type.
  switch (type) {
  case DEBUG_NORMAL:
  {
    printHelper(black, beginString, valueString, description, valueNames.size());
    break;
  }
  case DEBUG_IMPORTANT:
  {
    printMutex_.lock();
    // TODO: Center text in middle.
    qDebug();
    qDebug().noquote() << blue << "=============================================================================" << black;
    printHelper(blue, beginString, valueString, description, valueNames.size());
    qDebug().noquote() << blue << "=============================================================================" << black;
    qDebug();
    printMutex_.unlock();
    break;
  }
  case DEBUG_ERROR:
  {
    printMutex_.lock();
    printHelper(red, beginString, valueString, "ERROR! " + description, valueNames.size());
    printMutex_.unlock();
    break;
  }
  case DEBUG_WARNING:
  {
    printMutex_.lock();
    printHelper(yellow, beginString, valueString, "Warning! " + description, valueNames.size());
    printMutex_.unlock();
    break;
  }
  case DEBUG_PEER_ERROR:
  {
    printMutex_.lock();
    printHelper(red, beginString, valueString, "PEER ERROR: " + description, valueNames.size());
    printMutex_.unlock();
    break;
  }
  case DEBUG_PROGRAM_ERROR:
  {
    printMutex_.lock();
    printHelper(red, beginString, valueString, "BUG: " + description, valueNames.size());
    printMutex_.unlock();
    break;
  }
  case DEBUG_PROGRAM_WARNING:
  {
    printMutex_.lock();
    printHelper(yellow, beginString, valueString, "Minor bug: " + description, valueNames.size());
    printMutex_.unlock();
    break;
  }
  }
}


bool checkError(QObject* object, bool check, DebugType type,
                QString description, QStringList values)
{
  Q_ASSERT(check);

  if (!check)
  {
    QStringList names;
    for (int i = 0; i < values.size(); ++i)
    {
      names.push_back("Value " + QString::number(i+1));
    }

    printDebug(type, object, description, names, values);
  }

  return check;
}


bool settingEnabled(QString key)
{
  return settingValue(key) == 1;
}


int settingValue(QString key)
{
  QSettings settings(settingsFile, settingsFileFormat);

  if (!settings.value(key).isValid())
  {
    printDebug(DEBUG_WARNING, "Common", "Found faulty setting", {"Key"}, {key});
    return 0;
  }

  return settings.value(key).toInt();
}


QString settingString(QString key)
{
  QSettings settings(settingsFile, settingsFileFormat);

  if (!settings.value(key).isValid())
  {
    printDebug(DEBUG_WARNING, "Common", "Found faulty setting", {"Key"}, {key});
    return "";
  }

  return settings.value(key).toString();
}


QString getLocalUsername()
{
  QSettings settings(settingsFile, settingsFileFormat);

  return !settings.value(SettingsKey::localUsername).isNull()
      ? settings.value(SettingsKey::localUsername).toString() : "anonymous";
}


void printHelper(QString color, QString beginString, QString valueString, QString description, int valuenames)
{
  if (beginString.length() < BEGIN_LENGTH)
  {
    beginString = beginString.leftJustified(BEGIN_LENGTH, ' ');
  }

  QDebug printing = qDebug().nospace().noquote();
  printing << color << beginString << description;
  if (!valueString.isEmpty())
  {
    // print one value on same line
    if (valuenames == 1)
    {
      printing << " (" << valueString << ")";
    }
    else // pring each value on separate line
    {
      printing << "\r\n" << valueString;
    }
  }

  QString blackColor = "\033[0m";
  printing << blackColor;
}
