#pragma once
#include <QByteArray>
#include <QtNetwork>

#include <queue>
#include <functional>

#include <stdint.h>

/* Handles one TCP connection */
// TODO: Implement a keep-alive CRLF sending.

class TCPConnection : public QThread
{
  Q_OBJECT
public:
  TCPConnection();
  ~TCPConnection();

  void stopConnection();

  // establishes a new TCP connection
  void establishConnection(QString const &destination, uint16_t port);

  // when a server receives a TCP connection,
  // use this to give the socket to Connection
  void setExistingConnection(qintptr socketDescriptor);

  void allowReceiving()
  {
    allowedToSendMessages_ = true;
  }

  // callback
  template <typename Class>
  void addDataOutCallback (Class* o, void (Class::*method) (QByteArray& data))
  {
    outDataCallback_ = ([o,method] (QByteArray& data)
    {
      return (o->*method)(data);
    });
  }

  bool waitUntilConnected();

  // returns empty string if not connected
  QString localAddress() const;
  QString remoteAddress() const;

  // returns 0 if not connected
  uint16_t localPort() const;
  uint16_t remotePort() const;

  // returns ANY if not connected
  QAbstractSocket::NetworkLayerProtocol localProtocol() const;
  QAbstractSocket::NetworkLayerProtocol remoteProtocol() const;

signals:
  void error(int socketError, const QString &message);
  void messageAvailable(QString message);

  // connection has been established
  void socketConnected(QString localAddress, QString remoteAddress);
  void unableToConnect(QString remoteAddress);

public slots:

  // sends packet via connection
  void sendPacket(const QString &data);

private slots:
  void receivedMessage();
  void printBytesWritten(qint64 bytes);

  void disconnected();

protected:

  void run();

private:

  bool isConnected() const;

  // connects signals.
  void init();
  void uninit();

  void printError(int socketError, const QString &message);

  // return if succeeded
  bool connectLoop();
  void receiveLoop();
  void sendLoop();

  void bufferToSocket();

  void disconnect();

  std::function<void(QByteArray& data)> outDataCallback_;

  QTcpSocket *socket_;

  bool shouldConnect_;

  QString destination_;
  uint16_t port_;

  qintptr socketDescriptor_;
  std::queue<QString> buffer_;

  QMutex sendMutex_;

  // Indicates whether the connection should be active or disconnected
  bool active_;

  QMutex readWriteMutex_;

  QString leftOvers_;

  bool allowedToSendMessages_;

  // this variable prevents us fom spamming connections
  // if the connections are dropped right after succeeding
  std::chrono::time_point<std::chrono::system_clock> next_connection_attempt_;
};
