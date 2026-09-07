#include "tracking/AppNotify.h"

#include "tracking/SessionDatabase.h"

#include <QLocalSocket>

namespace AppNotify {

bool send(const QByteArray& command, int timeoutMs) {
  QLocalSocket socket;
  socket.connectToServer(SessionDatabase::appServerName(), QIODevice::WriteOnly);
  if (!socket.waitForConnected(timeoutMs)) {
    return false;
  }
  socket.write(command);
  socket.flush();
  socket.waitForBytesWritten(timeoutMs);
  return true;
}

} // namespace AppNotify
