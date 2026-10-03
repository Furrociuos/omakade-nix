#include "guidebutton/GuidePress.h"

#include <QStringList>

void GuidePress::opened(const QString& device, qint64 nowMs) {
  m_devices.insert(device, State{.armedAt = nowMs + kArmDelayMs});
}

void GuidePress::closed(const QString& device) { m_devices.remove(device); }

void GuidePress::dropped(const QString& device) {
  auto state = m_devices.find(device);
  if (state != m_devices.end()) {
    state->heldSince = -1;
    state->chord = false;
  }
}

bool GuidePress::event(const QString& device, int type, int code, int value, qint64 nowMs) {
  auto state = m_devices.find(device);
  if (state == m_devices.end()) {
    return false;
  }
  if (type == kEvKey && code == kBtnMode) {
    if (value == 1) {
      // A button already down when the controller connected, or the press that switched it on,
      // was not seen starting here and does not count.
      state->heldSince = nowMs >= state->armedAt ? nowMs : -1;
      state->chord = false;
      return false;
    }
    if (value != 0 || state->heldSince < 0) {
      return false;
    }
    const bool shortPress = nowMs - state->heldSince <= kMaxHoldMs;
    const bool alone = !state->chord;
    state->heldSince = -1;
    state->chord = false;
    const bool samePress = m_lastRelease >= 0 && nowMs - m_lastRelease < kSameReleaseMs;
    m_lastRelease = nowMs;
    return shortPress && alone && !samePress;
  }
  if (state->heldSince < 0) {
    return false;
  }
  // Holding Guide with another button is a chord for Steam or an emulator hotkey, not a press.
  if ((type == kEvKey && value == 1) ||
      (type == kEvAbs && code >= kAbsHat0X && code <= kAbsHat3Y && value != 0)) {
    state->chord = true;
  }
  return false;
}

bool GuidePress::hasBit(const QString& bitmap, int bit, int wordBits) {
  const QStringList words = bitmap.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
  const int index = words.size() - 1 - bit / wordBits;
  if (index < 0) {
    return false;
  }
  bool ok = false;
  const quint64 word = words.at(index).toULongLong(&ok, 16);
  return ok && ((word >> (bit % wordBits)) & 1U) != 0;
}

bool GuidePress::isController(const QString& keyCapabilities) {
  return hasBit(keyCapabilities, kBtnMode) && hasBit(keyCapabilities, kBtnSouth);
}
