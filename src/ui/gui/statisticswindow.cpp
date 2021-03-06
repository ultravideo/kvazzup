#include "statisticswindow.h"

#include "ui_statisticswindow.h"

#include "common.h"
#include "logger.h"

#include <QCloseEvent>
#include <QDateTime>


const int BUFFERSIZE = 65536;

const int FPSPRECISION = 4;

const int CHARTVALUES = 20;

enum TabType {
  SIP_TAB = 0, PARAMETERS_TAB = 1, DELIVERY_TAB = 2,
  FILTER_TAB = 3, PERFORMANCE_TAB = 4
};


StatisticsWindow::StatisticsWindow(QWidget *parent) :
QDialog(parent),
StatisticsInterface(),
  sessions_(),
  buffers_(),
  nextFilterID_(1),
  ui_(new Ui::StatisticsWindow),
  sessionMutex_(),
  filterMutex_(),
  sipMutex_(),
  deliveryMutex_(),
  dirtyBuffers_(false),
  videoIndex_(0), // ringbuffer index
  videoPackets_(BUFFERSIZE,nullptr), // ringbuffer
  audioIndex_(0), // ringbuffer index
  audioPackets_(BUFFERSIZE,nullptr), // ringbuffer
  inIndex_(0),
  inBandWidth_(BUFFERSIZE,nullptr),
  outIndex_(0),
  outBandwidth_(BUFFERSIZE,nullptr),
  sendPacketCount_(0),
  transferredData_(0),
  receivePacketCount_(0),
  receivedData_(0),
  packetsDropped_(0),
  videoEncDelayIndex_(0),
  videoEncDelay_(BUFFERSIZE,nullptr),
  audioEncDelayIndex_(0),
  audioEncDelay_(BUFFERSIZE,nullptr),
  guiTimer_(),
  guiUpdates_(0),
  lastTabIndex_(254) // an invalid value so we will update the tab immediately
{
  ui_->setupUi(this);

  connect(ui_->update_period, &QAbstractSlider::valueChanged,
          this, &StatisticsWindow::changeUpdatePeriod);

  connect(ui_->sample_window, &QAbstractSlider::valueChanged,
          this, &StatisticsWindow::changeSampleWindow);

  // Initiate all charts

  // Delivery-tab
  ui_->bandwidth_chart->init(800, 8, true, CHARTVALUES, "Bandwidth (kbit/s)");
  ui_->bandwidth_chart->addLine("In");
  ui_->bandwidth_chart->addLine("Out");

  // performance-tab
  ui_->v_bitrate_chart->init(500, 5, true, CHARTVALUES, "Bit rates (kbit/s)");
  ui_->a_bitrate_chart->init(50, 5, false, CHARTVALUES, "Bit rates (kbit/s)");
  ui_->v_delay_chart->init(100, 5, true, CHARTVALUES, "Latencies (ms)");
  ui_->a_delay_chart->init(10, 5, false, CHARTVALUES, "Latencies (ms)");
  ui_->v_framerate_chart->init(30, 5, false, CHARTVALUES, "Frame rates (fps)");

  chartVideoID_ = ui_->v_bitrate_chart->addLine("Outgoing");
  chartAudioID_ = ui_->a_bitrate_chart->addLine("Outgoing");

  ui_->v_delay_chart->addLine("Outgoing");
  ui_->a_delay_chart->addLine("Outgoing");

  ui_->v_framerate_chart->addLine("Outgoing");

  // init headers of call parameter table
  fillTableHeaders(ui_->table_outgoing, sessionMutex_,
                          {"IP", "Audio Ports", "Video Ports"});
  fillTableHeaders(ui_->table_incoming, sessionMutex_,
                          {"IP", "Audio Ports", "Video Ports"});
  fillTableHeaders(ui_->filterTable, filterMutex_,
                          {"Filter", "Info", "TID", "Buffer Size", "Dropped"});
  fillTableHeaders(ui_->sent_list, sipMutex_,
                          {"Type", "Destination"});
  fillTableHeaders(ui_->received_list, sipMutex_,
                          {"Type", "Source"});
}


StatisticsWindow::~StatisticsWindow()
{
  delete ui_;
}


void StatisticsWindow::showEvent(QShowEvent * event)
{
  Q_UNUSED(event)
  // start refresh timer
  clearCharts();

  // this makes sure the window does not open outside the screen above the parent.
  this->setGeometry(QStyle::alignedRect(Qt::LeftToRight,
                                        Qt::AlignHCenter,
                                        this->size(),
                                        parentWidget()->geometry()));

}


void StatisticsWindow::closeEvent(QCloseEvent *event)
{
  Q_UNUSED(event)
  accept();
}


void StatisticsWindow::videoInfo(double framerate, QSize resolution)
{
  // done only once, so setting ui directly is ok.
  ui_->value_framerate->setText( QString::number(framerate, 'g', FPSPRECISION)+" fps");
  ui_->value_resolution->setText( QString::number(resolution.width()) + "x"
                          + QString::number(resolution.height()));

  // set max framerate as this. Set the y-line every 5 fps, 10 fps if fps is over 60
  if (framerate <= 60)
  {
    ui_->v_framerate_chart->init(framerate, framerate/5, false,
                                 CHARTVALUES, "Frame rates (fps)");
  }
  else
  {
    ui_->v_framerate_chart->init(framerate, framerate/10, false,
                                 CHARTVALUES, "Frame rates (fps)");
  }
}


void StatisticsWindow::audioInfo(uint32_t sampleRate, uint16_t channelCount)
{
  if(sampleRate == 0 || sampleRate == 4294967295)
  {
    ui_->value_channels->setText("No Audio");
    ui_->value_samplerate->setText("No Audio");
  }
  else
  {
    ui_->value_channels->setText(QString::number(channelCount));
    ui_->value_samplerate->setText(QString::number(sampleRate) + " Hz");
  }
}

void StatisticsWindow::addSession(uint32_t sessionID)
{
  if (sessions_.find(sessionID) != sessions_.end())
  {
    Logger::getLogger()->printProgramError(this, "Session already exists");
    return;
  }

  sessions_[sessionID] = {0, std::vector<ValueInfo*>(BUFFERSIZE, nullptr),
                          0, std::vector<ValueInfo*>(BUFFERSIZE, nullptr),
                          0, std::vector<ValueInfo*>(BUFFERSIZE, nullptr),
                          0, std::vector<ValueInfo*>(BUFFERSIZE, nullptr),
                          0, std::vector<ValueInfo*>(BUFFERSIZE, nullptr),
                          0, std::vector<ValueInfo*>(BUFFERSIZE, nullptr),
                          -1};
}


void StatisticsWindow::incomingMedia(uint32_t sessionID, QString name, QStringList& ipList,
                                     QStringList &audioPorts, QStringList &videoPorts)
{
  if (ipList.size() == 0)
  {
    return;
  }

  addMedia(ui_->table_incoming, sessionID, ipList, audioPorts, videoPorts);

  ui_->v_delay_chart->addLine(name);
  ui_->a_delay_chart->addLine(name);
  ui_->v_bitrate_chart->addLine(name);
  ui_->a_bitrate_chart->addLine(name);
  ui_->v_framerate_chart->addLine(name);
}


void StatisticsWindow::outgoingMedia(uint32_t sessionID, QString name, QStringList& ipList,
                                     QStringList& audioPorts, QStringList& videoPorts)
{
  addMedia(ui_->table_outgoing, sessionID, ipList, audioPorts, videoPorts);
}


void StatisticsWindow::addMedia(QTableWidget* table, uint32_t sessionID, QStringList& ipList,
                                QStringList audioPorts, QStringList videoPorts)
{
  if (sessions_.find(sessionID) == sessions_.end())
  {
    Logger::getLogger()->printProgramError(this, "Session for media doesn't exist");
    return;
  }

  int index = addTableRow(table, sessionMutex_,
                          {combineList(ipList), combineList(audioPorts),
                           combineList(videoPorts)});

  if (sessions_[sessionID].tableIndex == -1 || sessions_[sessionID].tableIndex == index)
  {
    sessions_[sessionID].tableIndex = index;
  }
  else
  {
    Logger::getLogger()->printProgramError(this, "Wrong table index detected in sessions for media!");
    return;
  }
}

QString StatisticsWindow::combineList(QStringList &list)
{
  QString listed = "";

  for (int i = list.size() - 1; i >= 0; --i)
  {
    // don't record anything if we have to of the same strings
    if (i > 0 && list[i] != list[i - 1])
    {
      listed += list[i];
      listed += ", ";
    }
    else if (i == 0)
    {
      listed += list[i];
    }
  }

  return listed;
}

uint32_t StatisticsWindow::addFilter(QString type, QString identifier, uint64_t TID)
{
  QString threadID = QString::number(TID);
  threadID = threadID.rightJustified(5, '0');

  int rowIndex = addTableRow(ui_->filterTable, filterMutex_,
                                  {type, identifier, threadID, "-/-", "0"});

  filterMutex_.lock();
  uint32_t id = nextFilterID_;
  ++nextFilterID_;
  if (nextFilterID_ >= UINT32_MAX - 2)
  {
    nextFilterID_ = 10;
  }
  buffers_[id] = FilterStatus{0,QString::number(TID), 0, 0, rowIndex};
  filterMutex_.unlock();

  return id;
}


void StatisticsWindow::removeFilter(uint32_t id)
{
  filterMutex_.lock();
  if (buffers_.find(id) == buffers_.end())
  {
    filterMutex_.unlock();
    Logger::getLogger()->printProgramWarning(this, "Tried to remove non-existing filter.",
                          {"Id"}, {QString::number(id)});
    return;
  }
  if (ui_->filterTable->rowCount() < buffers_[id].tableIndex)
  {
    filterMutex_.unlock();
    Logger::getLogger()->printProgramWarning(this, "Filter doesn't exist in filter table when removing.",
                                             {"Id: Table size vs expected place"}, 
                                             {QString::number(id) + ":" + 
                                              QString::number(ui_->filterTable->rowCount())
                                              + " vs " + QString::number(buffers_[id].tableIndex)});
    return;
  }

  ui_->filterTable->removeRow(buffers_[id].tableIndex);

  // adjust all existing indexes
  for (auto& buffer: buffers_)
  {
    if (buffers_[id].tableIndex < buffer.second.tableIndex)
    {
      buffer.second.tableIndex -= 1;
    }
  }

  buffers_.erase(id);
  filterMutex_.unlock();
}


void StatisticsWindow::removeSession(uint32_t sessionID)
{
  // check that peer exists
  if (sessions_.find(sessionID) == sessions_.end())
  {
    return;
  }

  sessionMutex_.lock();

  int index = sessions_[sessionID].tableIndex;

  // check that index points to a valid row
  if (ui_->table_incoming->rowCount() <= index ||
      ui_->table_outgoing->rowCount() <= index)
  {
    sessionMutex_.unlock();
    Logger::getLogger()->printProgramWarning(this, "Missing participant row for participant");
    return;
  }

  // remove row from UI
  ui_->table_incoming->removeRow(index);
  ui_->table_outgoing->removeRow(index);

  // adjust the rest of the peers if needed
  for (auto &peer : sessions_)
  {
    if (peer.second.tableIndex > index)
    {
      --peer.second.tableIndex;
    }
  }

  index += 2; // +1 because it is an ID, not index and +1 for local before peers.

  // remove line from all charts. Charts automatically adjust their lineID:s
  // after removal
  ui_->v_bitrate_chart->removeLine(index);
  ui_->a_bitrate_chart->removeLine(index);
  ui_->v_delay_chart->removeLine(index);
  ui_->a_delay_chart->removeLine(index);
  ui_->v_framerate_chart->removeLine(index);


  // TODO: There is still unreleased memory in session!!

  sessions_.erase(sessionID);


  sessionMutex_.unlock();
}


void StatisticsWindow::sendDelay(QString type, uint32_t delay)
{
  if(type == "video" || type == "Video")
  {
    updateValueBuffer(videoEncDelay_,
                      videoEncDelayIndex_, delay);
  }
  else if(type == "audio" || type == "Audio")
  {
    updateValueBuffer(audioEncDelay_,
                      audioEncDelayIndex_, delay);
  }
}


void StatisticsWindow::receiveDelay(uint32_t sessionID, QString type, int32_t delay)
{
  if(sessions_.find(sessionID) != sessions_.end())
  {
    if(type == "video" || type == "Video")
    {
      updateValueBuffer(sessions_.at(sessionID).videoDelay,
                        sessions_.at(sessionID).videoDelayIndex, delay);
    }
    else if(type == "audio" || type == "Audio")
    {
      updateValueBuffer(sessions_.at(sessionID).audioDelay,
                        sessions_.at(sessionID).audioDelayIndex, delay);
    }
  }
}


void StatisticsWindow::presentPackage(uint32_t sessionID, QString type)
{
  Q_ASSERT(sessions_.find(sessionID) != sessions_.end());
  if(sessions_.find(sessionID) != sessions_.end())
  {
    if(type == "video" || type == "Video")
    {
      updateValueBuffer(sessions_.at(sessionID).pVideoPackets,
                            sessions_.at(sessionID).pVideoIndex, 0);
    }
    else if (type == "audio" || type == "Audio")
    {
      updateValueBuffer(sessions_.at(sessionID).pAudioPackets,
                            sessions_.at(sessionID).pAudioIndex, 0);
    }
  }
}


void StatisticsWindow::addEncodedPacket(QString type, uint32_t size)
{
  if(type == "video" || type == "Video")
  {
    updateValueBuffer(videoPackets_, videoIndex_, size);
  }
  else if(type == "audio" || type == "Audio")
  {
    updateValueBuffer(audioPackets_, audioIndex_, size);
  }
}


void StatisticsWindow::updateValueBuffer(std::vector<ValueInfo*>& packets,
                                             uint32_t& index, uint32_t value)
{
  // delete previous value from ring-buffer
  if(packets[index%BUFFERSIZE])
  {
    delete packets.at(index%BUFFERSIZE);
  }

  // add packet at this timestamp
  packets[index%BUFFERSIZE] = new ValueInfo{QDateTime::currentMSecsSinceEpoch(), value};
  ++index;
}



uint32_t StatisticsWindow::calculateAverageAndRate(std::vector<ValueInfo*>& packets, uint32_t index,
                                                   float& rate, int64_t interval, bool calcData)
{
  if(index == 0)
    return 0;

  int64_t now = QDateTime::currentMSecsSinceEpoch();
  int64_t average = 0;
  uint16_t frames = 0;
  uint32_t currentTs = 0;
  rate = 0.0f;

  // set timestamp indexes to ringbuffer
  if(index == 0)
  {
    currentTs = BUFFERSIZE - 1;
  }
  else
  {
    currentTs = index - 1;
  }

  // sum all values and time intervals in ring-buffer for specified timeperiod
  while(packets[currentTs%BUFFERSIZE] && now - packets[currentTs%BUFFERSIZE]->timestamp
        < interval)
  {
    average += packets[currentTs%BUFFERSIZE]->value;
    ++frames;
    if(currentTs != 0)
    {
      --currentTs;
    }
    else
    {
      currentTs = BUFFERSIZE - 1;
    }
  }

  // calculate frame rate and the average amount of bits per timeinterval (bitrate)
  if(frames > 0)
  {
    // rate per second
    rate = (float)frames*1000/interval;

    if (calcData)
    {
      // return the amount of value per second converted to kbits/s
      return 8*average/(interval);
    }
    else
    {
      // return the average size of value
      return average/frames;
    }
  }
  return 0;
}


uint32_t StatisticsWindow::calculateAverage(std::vector<ValueInfo*>& packets, uint32_t index,
                                            int64_t interval, bool kbitConversion)
{
  float rate = 0.0f;
  return calculateAverageAndRate(packets, index, rate, interval, kbitConversion);
}


void StatisticsWindow::addSendPacket(uint16_t size)
{
  deliveryMutex_.lock();
  ++sendPacketCount_;
  transferredData_ += size;

  updateValueBuffer(outBandwidth_, outIndex_, size);
  deliveryMutex_.unlock();
}


void StatisticsWindow::addReceivePacket(uint32_t sessionID, QString type,
                                        uint16_t size)
{
  deliveryMutex_.lock();
  ++receivePacketCount_;
  receivedData_ += size;

  updateValueBuffer(inBandWidth_, inIndex_, size);
  deliveryMutex_.unlock();

  if(sessions_.find(sessionID) != sessions_.end())
  {
    if(type == "video" || type == "Video")
    {
      updateValueBuffer(sessions_.at(sessionID).videoPackets,
                            sessions_.at(sessionID).videoIndex, size);
    }
    else if (type == "audio" || type == "Audio")
    {
      updateValueBuffer(sessions_.at(sessionID).audioPackets,
                            sessions_.at(sessionID).audioIndex, size);
    }
  }
}


void StatisticsWindow::updateBufferStatus(uint32_t id, uint16_t buffersize,
                                          uint16_t maxBufferSize)
{
  filterMutex_.lock();
  if(buffers_.find(id) != buffers_.end())
  {
    if(buffers_[id].bufferStatus != buffersize ||
       buffers_[id].bufferSize != maxBufferSize)
    {
      dirtyBuffers_ = true;
      buffers_[id].bufferStatus = buffersize;
      buffers_[id].bufferSize = maxBufferSize;
    }
  }
  else
  {
    Logger::getLogger()->printProgramWarning(this, "Couldn't find correct filter for buffer status",
                                             "Filter id", QString::number(id));
  }
  filterMutex_.unlock();
}


void StatisticsWindow::packetDropped(uint32_t id)
{
  ++packetsDropped_;
  filterMutex_.lock();
  if(buffers_.find(id) != buffers_.end())
  {
    ++buffers_[id].dropped;
    dirtyBuffers_ = true;
  }
  else
  {
    Logger::getLogger()->printProgramWarning(this, "Couldn't find correct filter for dropped packet",
                                             "Filter id", QString::number(id));
  }
  filterMutex_.unlock();
}


void StatisticsWindow::paintEvent(QPaintEvent *event)
{
  Q_UNUSED(event);

  // clear old points from charts since they are obsolete
  if(lastTabIndex_ != ui_->Statistics_tabs->currentIndex())
  {
    if (ui_->Statistics_tabs->currentIndex() == PERFORMANCE_TAB)
    {
      ui_->v_bitrate_chart->clearPoints();
      ui_->a_bitrate_chart->clearPoints();
      ui_->v_delay_chart->clearPoints();
      ui_->a_delay_chart->clearPoints();
      ui_->v_framerate_chart->clearPoints();
    }
    else if (ui_->Statistics_tabs->currentIndex() == DELIVERY_TAB)
    {
      ui_->bandwidth_chart->clearPoints();
    }
  }

  // should we update the outlook of statistics
  if(lastTabIndex_ != ui_->Statistics_tabs->currentIndex()
     || guiUpdates_*ui_->update_period->value() < guiTimer_.elapsed())
  {
    // do not take this account if this was only a tab switch
    if (lastTabIndex_ == ui_->Statistics_tabs->currentIndex())
    {
      ++guiUpdates_;
    }

    lastTabIndex_ = ui_->Statistics_tabs->currentIndex();

    // update only the tab which is visible to reduce processing
    switch(ui_->Statistics_tabs->currentIndex())
    {
    case SIP_TAB:
    {
      // do nothing since SIP has no continous data
      break;
    }
    case PARAMETERS_TAB:
    {
      // do nothing since parameters have no continous data
      break;
    }
    case DELIVERY_TAB:
    {
      deliveryMutex_.lock();
      ui_->packets_sent_value->setText( QString::number(sendPacketCount_));
      ui_->data_sent_value->setText( QString::number(transferredData_));
      ui_->packets_received_value->setText( QString::number(receivePacketCount_));
      ui_->data_received_value->setText( QString::number(receivedData_));

      // bandwidth chart
      float packetRate = 0.0f; // not interested in this at the moment.
      uint32_t inBandwidth = calculateAverageAndRate(inBandWidth_, inIndex_, packetRate, 5000, true);
      uint32_t outBandwidth = calculateAverageAndRate(outBandwidth_, outIndex_, packetRate, 5000, true);

      ui_->bandwidth_chart->addPoint(1, inBandwidth);
      ui_->bandwidth_chart->addPoint(2, outBandwidth);
      deliveryMutex_.unlock();

      break;
    }
    case PERFORMANCE_TAB:
      {
        // how long a tail should we consider in bitrate calculations
        int64_t interval = ui_->update_period->value() * ui_->sample_window->value();

        // calculate local video bitrate and framerate
        float videoFramerate = 0.0f;
        uint32_t videoBitrate = calculateAverageAndRate(videoPackets_, videoIndex_, videoFramerate, interval, true);

        // calculate local audio bitrate
        uint32_t audioBitrate = calculateAverage(audioPackets_, audioIndex_, interval, true);

        uint32_t videoEncoderDelay = calculateAverage(videoEncDelay_, videoEncDelayIndex_, interval, false);
        uint32_t audioEncoderDelay = calculateAverage(audioEncDelay_, audioEncDelayIndex_, interval, false);

        // add points to chart
        ui_->v_bitrate_chart->addPoint(chartVideoID_, videoBitrate);
        ui_->a_bitrate_chart->addPoint(chartAudioID_, audioBitrate);
        ui_->v_delay_chart->addPoint(chartVideoID_, videoEncoderDelay);
        ui_->a_delay_chart->addPoint(chartAudioID_, audioEncoderDelay);
        ui_->v_framerate_chart->addPoint(chartVideoID_, videoFramerate);

        // add points for all existing sessions
        for(auto& d : sessions_)
        {
          sessionMutex_.lock();

          float receiveVideorate = 0; // not shown at the moment. We show presentation framerate instead
          uint32_t videoBitrate = calculateAverageAndRate(d.second.videoPackets, d.second.videoIndex,
                                                          receiveVideorate, interval, true);

          float presentationVideoFramerate = 0;
          calculateAverageAndRate(d.second.pVideoPackets, d.second.pVideoIndex,
                                  presentationVideoFramerate, interval, true);

          uint32_t audioBitrate = calculateAverage(d.second.audioPackets, d.second.audioIndex,
                                                   interval, true);

          uint32_t videoDelay = calculateAverage(d.second.videoDelay, d.second.videoDelayIndex,
                                                 interval, false);
          uint32_t audioDelay = calculateAverage(d.second.audioDelay, d.second.audioDelayIndex,
                                                 interval, false);

          ui_->v_bitrate_chart->addPoint(d.second.tableIndex + 2, videoBitrate);
          ui_->a_bitrate_chart->addPoint(d.second.tableIndex + 2, audioBitrate);
          ui_->v_delay_chart->addPoint(d.second.tableIndex + 2, videoDelay);
          ui_->a_delay_chart->addPoint(d.second.tableIndex + 2, audioDelay);
          ui_->v_framerate_chart->addPoint(d.second.tableIndex + 2, presentationVideoFramerate);

          sessionMutex_.unlock();
        }


        break;
      }
    case FILTER_TAB:
    {
      if(dirtyBuffers_)
      {
        uint32_t totalBuffers = 0;

        filterMutex_.lock();
        for(auto& it : buffers_)
        {
          totalBuffers += it.second.bufferStatus;

          if (it.second.tableIndex >= ui_->filterTable->rowCount() ||
              it.second.tableIndex == -1)
          {
            filterMutex_.unlock();
            Logger::getLogger()->printProgramError(this, "Invalid filtertable index detected!", 
                                                   {"Name"}, {it.first});
            return;
          }

          ui_->filterTable->setItem(it.second.tableIndex, 3,
                                    new QTableWidgetItem(QString::number(it.second.bufferStatus) +
                                                         "/" + QString::number(it.second.bufferSize)));
          ui_->filterTable->setItem(it.second.tableIndex, 4,
                                    new QTableWidgetItem(QString::number(it.second.dropped)));

          ui_->filterTable->item(it.second.tableIndex, 3)->setTextAlignment(Qt::AlignHCenter);
          ui_->filterTable->item(it.second.tableIndex, 4)->setTextAlignment(Qt::AlignHCenter);
        }
        filterMutex_.unlock();

        ui_->value_buffers->setText(QString::number(totalBuffers));
        ui_->value_dropped->setText(QString::number(packetsDropped_));
        dirtyBuffers_ = false;

      }
      break;
    }
    default:
    {
      break;
    }
    }
  }

  QDialog::paintEvent(event);
}


void StatisticsWindow::addSentSIPMessage(QString type, QString message,
                                         QString address)
{
  addTableRow(ui_->sent_list, sipMutex_, {type, address}, message);
}


void StatisticsWindow::addReceivedSIPMessage(QString type, QString message,
                                             QString address)
{
  int row = addTableRow(ui_->received_list, sipMutex_, {type, address}, message);

  sipMutex_.lock();
  QTableWidgetItem * first = ui_->received_list->itemAt(0, row);
  //first->setBackground(QColor(235,235,235));
  QTableWidgetItem * second = ui_->received_list->itemAt(1, row);
  //second->setBackground(QColor(235,235,235));
  sipMutex_.unlock();
}


void StatisticsWindow::delayMsConversion(int& delay, QString& unit)
{
  if (delay >= 1000)
  {
    delay = (delay + 500)/1000;
    unit = "s";
  }
  else if (delay <= -1000)
  {
    delay = (delay - 500)/1000;
    unit = "s";
  }
  else
  {
    unit = "ms";
  }
}


void StatisticsWindow::fillTableHeaders(QTableWidget* table, QMutex& mutex,
                                        QStringList headers)
{
  if (!table)
  {
    return;
  }

  mutex.lock();

  table->setColumnCount(headers.size());
  for (int i = 0; i < headers.size(); ++i)
  {
    table->setHorizontalHeaderItem(i, new QTableWidgetItem(headers.at(i)));
  }

  table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  table->horizontalHeader()->setMinimumHeight(40);

  table->horizontalHeader()->setSectionsClickable(true);
  table->setSortingEnabled(true);

  mutex.unlock();
}


int StatisticsWindow::addTableRow(QTableWidget* table, QMutex& mutex,
                                  QStringList fields, QString tooltip)
{
  mutex.lock();
  table->insertRow(table->rowCount());

  for (int i = 0; i < fields.size(); ++i)
  {
    QTableWidgetItem* item = new QTableWidgetItem(fields.at(i));
    item->setTextAlignment(Qt::AlignHCenter);
    if (tooltip != "")
    {
      item->setToolTip(tooltip);
    }
    item->setFlags(item->flags() & ~(Qt::ItemIsEditable | Qt::ItemIsSelectable));
    table->setItem(table->rowCount() -1, i, item);
  }

  int index = table->rowCount() - 1;
  mutex.unlock();
  return index;
}


void StatisticsWindow::changeUpdatePeriod(int value)
{
  // this makes limits the update frequency to only discreet values
  // which I think is more suitable for this.
  int limitedValue = static_cast<int>((value + 50)/100);
  limitedValue *= 100;

  // move slider to discreet value
  ui_->update_period->setValue(limitedValue);

  ui_->update_period_label->setText("Update Period: "
                                    + getTimeConversion(limitedValue));

  changeSampleWindow(ui_->sample_window->value());
  clearCharts();
}


void StatisticsWindow::changeSampleWindow(int value)
{
  int sampleWindow = ui_->update_period->value() * value;
  ui_->sample_window_label->setText("Sample Window Length: "
                                    +  getTimeConversion(sampleWindow));
  clearCharts();
}


void StatisticsWindow::clearCharts()
{
  // clear charts
  ui_->v_delay_chart->clearPoints();
  ui_->a_delay_chart->clearPoints();
  ui_->v_bitrate_chart->clearPoints();
  ui_->a_bitrate_chart->clearPoints();
  ui_->v_framerate_chart->clearPoints();

  //ui_->bandwidth_chart->clearPoints();

  // reset GUI timer so the new frequency works
  guiUpdates_ = 0;
  guiTimer_.restart();

}


QString StatisticsWindow::getTimeConversion(int valueInMs)
{
  // show as seconds
  if (valueInMs >= 1000)
  {
    return (QString::number(valueInMs/1000) + "." + QString::number(valueInMs%1000/100) + " s");
  }
  // show as milliseconds
  return QString::number(valueInMs) + " ms";
}
