#include "logger.h"

#include "global.h"

#include <QDebug>

const int BEGIN_LENGTH = 40;


std::shared_ptr<Logger> Logger::instance_ = nullptr;


Logger::Logger():
  printMutex_(),
  logFile_(),
  triedOpeningFile_(false)
{}


Logger::~Logger()
{
  triedOpeningFile_ = false;
  logFile_.close();
}

std::shared_ptr<Logger> Logger::getLogger()
{
  if (Logger::instance_ == nullptr)
  {
    Logger::instance_ = std::shared_ptr<Logger>(new Logger());
  }

  return Logger::instance_;
}


void Logger::printDebug(DebugType type, const QObject *object, QString description,
                        QStringList valueNames, QStringList values)
{
  printDebug(type, object->metaObject()->className(),
             description, valueNames, values);
}


void Logger::printNormal(const QObject *object, QString description,
                         QString valueName, QString value)
{
  printDebug(DEBUG_NORMAL, object, description, {valueName}, {value});
}


void Logger::printNormal(const QString module, QString description,
                         QString valueName, QString value)
{
  printDebug(DEBUG_NORMAL, module, description, {valueName}, {value});
}


void Logger::printImportant(const QObject* object, QString description,
                            QString valueName, QString value)
{
  printDebug(DEBUG_IMPORTANT, object, description, {valueName}, {value});
}


void Logger::printImportant(const QString module, QString description,
                            QString valueName, QString value)
{
  printDebug(DEBUG_IMPORTANT, module, description, {valueName}, {value});
}


void Logger::printWarning(const QObject* object, QString description,
                          QString valueName, QString value)
{
  printDebug(DEBUG_WARNING, object, description, {valueName}, {value});
}


void Logger::printWarning(const QString module, QString description,
                          QString valueName, QString value)
{
  printDebug(DEBUG_WARNING, module, description, {valueName}, {value});
}


void Logger::printError(const QObject *object, QString description,
                        QString valueName, QString value)
{
  printDebug(DEBUG_ERROR, object, description, {valueName}, {value});
}


void Logger::printError(const QString module, QString description,
                        QString valueName, QString value)
{
  printDebug(DEBUG_ERROR, module, description, {valueName}, {value});
}


void Logger::printProgramError(const QObject *object, QString description,
                               QString valueName, QString value)
{
  printDebug(DEBUG_PROGRAM_ERROR, object, description, {valueName}, {value});
}


void Logger::printProgramError(const QString module, QString description,
                               QString valueName, QString value)
{
  printDebug(DEBUG_PROGRAM_ERROR, module, description, {valueName}, {value});
}


void Logger::printProgramWarning(const QObject *object, QString description,
                                 QString valueName, QString value)
{
  printDebug(DEBUG_PROGRAM_WARNING, object, description, {valueName}, {value});
}


void Logger::printProgramWarning(const QString module, QString description,
                                 QString valueName, QString value)
{
  printDebug(DEBUG_PROGRAM_WARNING, module, description, {valueName}, {value});
}


void Logger::printPeerError(const QObject *object, QString description,
                            QString valueName, QString value)
{
  printDebug(DEBUG_PEER_ERROR, object, description, {valueName}, {value});
}


void Logger::printPeerError(const QString module, QString description,
                            QString valueName, QString value)
{
  printDebug(DEBUG_PEER_ERROR, module, description, {valueName}, {value});
}


void Logger::printUnimplemented(const QObject* object, QString whatIsNotImplemented)
{
  printDebug(DEBUG_PROGRAM_WARNING, object,
             QString("NOT IMPLEMENTED: ") + whatIsNotImplemented);
}


void Logger::printUnimplemented(const QString module, QString whatIsNotImplemented)
{
  printDebug(DEBUG_PROGRAM_WARNING, module,
             QString("NOT IMPLEMENTED: ") + whatIsNotImplemented);
}


void Logger::printDebug(DebugType type, QString className, QString description,
                        QStringList valueNames, QStringList values)
{
  PrintSet print;

  QString black   = "\033[0m";
  QString yellow  = "\033[1;33m";
  QString red     = "\033[31m";
  QString blue    = "\033[34m";

  // This could be reduced, but it might change so not worth probably at the moment.
  // Choose which text to print based on type.
  switch (type) {
  case DEBUG_NORMAL:
  {
    createPrintSet(print, className, description, valueNames, values);
    printHelper(black, print);
    break;
  }
  case DEBUG_IMPORTANT:
  {
    createPrintSet(print, className, description, valueNames, values);
    printHelper(blue, print, true);
    break;
  }
  case DEBUG_ERROR:
  {
    createPrintSet(print, className, "ERROR! " + description, valueNames, values);
    printHelper(red, print);
    break;
  }
  case DEBUG_WARNING:
  {
    createPrintSet(print, className, "Warning! " + description, valueNames, values);
    printHelper(yellow, print);
    break;
  }
  case DEBUG_PEER_ERROR:
  {
    createPrintSet(print, className, "PEER ERROR: " + description, valueNames, values);
    printHelper(red, print);
    break;
  }
  case DEBUG_PROGRAM_ERROR:
  {
    createPrintSet(print, className, "BUG: " + description, valueNames, values);
    printHelper(red, print);
    break;
  }
  case DEBUG_PROGRAM_WARNING:
  {
    createPrintSet(print, className, "Minor bug: " + description, valueNames, values);
    printHelper(yellow, print);
    break;
  }
  }
}


bool Logger::checkError(QObject* object, bool check, DebugType type,
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


void Logger::createPrintSet(PrintSet& set, QString className, QString description,
                            QStringList valueNames, QStringList values)
{
  set.firstLine = className + ": ";
  if (set.firstLine.length() < BEGIN_LENGTH)
  {
    set.firstLine = set.firstLine.leftJustified(BEGIN_LENGTH, ' ');
  }
  set.firstLine += description;

  // do we have values to print
  if (values.size() != 0)
  {
    // Add "name: value" because equal number of both.
    if (valueNames.size() == values.size()) // equal number of names and values
    {
      for (int i = 0; i < valueNames.size(); ++i)
      {
        if (valueNames.at(i) != "")
        {
          QString field = valueNames.at(i) + ": " + values.at(i);

          if (valueNames.size() != 1)
          {
            QString additionalLine;
            additionalLine.append(QString(BEGIN_LENGTH, ' '));
            additionalLine.append("-- ");
            additionalLine.append(field);

            set.additionalLines.push_back(additionalLine);
          }
          else
          {
            set.firstLine += " (" + field + ")";
          }
        }
      }
    }
    else if (valueNames.empty() || valueNames.size() == 1)
    {
      // If we have one or zero names, just add all values, unless we have 1 of both
      // in which case they were added earlier.

      QString additionalLine;
      if (valueNames.size() == 1)
      {
        additionalLine.append(valueNames.at(0) + ": ");
      }

      for (int i = 0; i < values.size(); ++i)
      {
        additionalLine.append(values.at(i));
        if (i != values.size() - 1)
        {
          additionalLine.append(", ");
        }
      }
      set.additionalLines.push_back(additionalLine);
    }
    else
    {
      qDebug() << "Debug printing could not figure how to print error values. "
                  "Description:" << description;
    }
  }
}


void Logger::printHelper(QString color, PrintSet &set, bool emphasize)
{
  printMutex_.lock();

  if (!triedOpeningFile_ && !logFile_.isOpen())
  {
    if (!openFileStream())
    {
      qDebug() << "ERROR: Could not open log file! Printing not working!";
      return;
    }
    qDebug() << "Opened log file for printing. Filename: " << logFile_.fileName();
  }

  // One additional line is added to printing when printing is destroyed
  QDebug printing = qDebug().nospace().noquote();
  QTextStream fileStream(&logFile_);

  QString longBar = "=============================================================================";

  if (emphasize)
  {
#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
    printing << color << endl << longBar << endl;
    fileStream << endl << longBar << endl;
#else
    printing << color << Qt::endl << longBar << Qt::endl;
    fileStream << Qt::endl << longBar << Qt::endl;
#endif
  }

  printing << color << set.firstLine;

#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
  fileStream << set.firstLine << endl;
#else
  fileStream << set.firstLine << Qt::endl;
#endif

  if (!set.additionalLines.empty() || emphasize)
  {
#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
    printing << endl;
#else
    printing << Qt::endl;
#endif
  }

  for (auto& additionalLine : set.additionalLines)
  {
#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
    printing << additionalLine << endl;
    fileStream  << additionalLine << endl;
#else
    printing << additionalLine << Qt::endl;
    fileStream  << additionalLine << Qt::endl;
#endif
  }

  if (!set.additionalLines.empty())
  {
#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
    fileStream << endl;
#else
    fileStream << Qt::endl;
#endif

  }

  if (emphasize)
  {
#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
    printing << color << longBar << endl;
    fileStream << longBar << endl << endl;
#else
    printing << color << longBar << Qt::endl;
    fileStream << longBar << Qt::endl << Qt::endl;
#endif
  }

  // make sure we reset the color back to previous color
  QString blackColor = "\033[0m";
  printing << blackColor;
  printMutex_.unlock();
}


bool Logger::openFileStream()
{
  logFile_.setFileName(LOG_FILE_NAME);

  if (!logFile_.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
  {
    return false;
  }

  triedOpeningFile_ = true;
  return true;
}
