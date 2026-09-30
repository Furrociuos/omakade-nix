#pragma once

class QQuickWindow;
class ControllerInput;

// Runs against the live QML scene and the production SDL input path, with mock games.
bool runCouchNavigationContract(QQuickWindow* window, ControllerInput& controller);
bool runStartupNavigationContract(QQuickWindow* window, ControllerInput& controller);
