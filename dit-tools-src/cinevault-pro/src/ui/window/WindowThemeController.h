#pragma once

#include <QColor>
#include <QObject>

class QWindow;

class WindowThemeController : public QObject {
    Q_OBJECT

public:
    explicit WindowThemeController(QObject *parent = nullptr);

    Q_INVOKABLE void apply(QWindow *window, const QColor &captionColor, const QColor &textColor, bool darkMode);
};

