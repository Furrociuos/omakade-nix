#pragma once

#include <QByteArray>

// One-shot client for Omakade's single-instance socket, the same channel the
// launcher uses for "play <key>" commands. Best effort: a closed Omakade window
// simply misses the notification.
namespace AppNotify {

bool send(const QByteArray& command, int timeoutMs = 200);

} // namespace AppNotify
