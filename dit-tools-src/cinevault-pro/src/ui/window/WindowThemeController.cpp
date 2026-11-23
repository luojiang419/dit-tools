#include "ui/window/WindowThemeController.h"

#include <QWindow>

#ifdef Q_OS_WIN
#include <dwmapi.h>
#include <windows.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif

namespace {
COLORREF colorRef(const QColor &color)
{
    return RGB(color.red(), color.green(), color.blue());
}
}
#endif

WindowThemeController::WindowThemeController(QObject *parent)
    : QObject(parent)
{
}

void WindowThemeController::apply(QWindow *window, const QColor &captionColor, const QColor &textColor, bool darkMode)
{
    if (!window) {
        return;
    }

#ifdef Q_OS_WIN
    const auto hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd) {
        return;
    }

    BOOL useDarkMode = darkMode ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));

    const COLORREF caption = colorRef(captionColor);
    const COLORREF text = colorRef(textColor);
    const COLORREF border = colorRef(captionColor);
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &text, sizeof(text));
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
#else
    Q_UNUSED(captionColor)
    Q_UNUSED(textColor)
    Q_UNUSED(darkMode)
#endif
}

