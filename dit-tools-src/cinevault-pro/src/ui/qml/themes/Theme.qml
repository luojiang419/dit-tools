pragma Singleton
import QtQuick

QtObject {
    readonly property int modeSystem: 0
    readonly property int modeDark: 1
    readonly property int modeLight: 2
    property int mode: modeSystem

    property SystemPalette systemPalette: SystemPalette {
        colorGroup: SystemPalette.Active
    }

    function luminance(colorValue) {
        return (0.2126 * colorValue.r) + (0.7152 * colorValue.g) + (0.0722 * colorValue.b)
    }

    readonly property bool systemIsDark: luminance(systemPalette.window) < luminance(systemPalette.windowText)
    readonly property bool isDark: mode === modeDark || (mode === modeSystem && systemIsDark)

    readonly property color bg: isDark ? "#0E1014" : "#F5F7FB"
    readonly property color panel: isDark ? "#151922" : "#FFFFFF"
    readonly property color panel2: isDark ? "#1B202B" : "#EEF2F7"
    readonly property color card: isDark ? "#222837" : "#FFFFFF"
    readonly property color line: isDark ? "#2E3647" : "#D5DCE8"
    readonly property color text: isDark ? "#F4F7FB" : "#172033"
    readonly property color muted: isDark ? "#9AA4B2" : "#5E6B7E"
    readonly property color weak: isDark ? "#626D7D" : "#8A96A8"
    readonly property color blue: isDark ? "#4F8CFF" : "#2563EB"
    readonly property color green: isDark ? "#22C55E" : "#16A34A"
    readonly property color orange: isDark ? "#F59E0B" : "#D97706"
    readonly property color red: isDark ? "#EF4444" : "#DC2626"

    readonly property color inputBg: isDark ? "#151922" : "#FFFFFF"
    readonly property color inputHover: isDark ? "#1B202B" : "#F8FAFC"
    readonly property color inputPressed: isDark ? "#222837" : "#EEF2F7"
    readonly property color popupBg: isDark ? "#151922" : "#FFFFFF"
    readonly property color popupHover: isDark ? "#222837" : "#EEF2F7"
    readonly property color topBar: isDark ? "#10131A" : "#FFFFFF"
    readonly property color mediaSurface: isDark ? "#10131A" : "#E7ECF4"
    readonly property color selectedBg: Qt.rgba(blue.r, blue.g, blue.b, isDark ? 0.16 : 0.12)
    readonly property color selectedLine: Qt.rgba(blue.r, blue.g, blue.b, isDark ? 0.46 : 0.34)
    readonly property color buttonBg: isDark ? card : "#FFFFFF"
    readonly property color buttonHover: isDark ? "#333D50" : "#E8EEF8"
    readonly property color buttonPressed: isDark ? "#2A3242" : "#DDE6F4"
    readonly property color primaryBg: isDark ? "#3F7FF0" : "#2563EB"
    readonly property color primaryHover: isDark ? "#4F8CFF" : "#1D4ED8"
    readonly property color primaryPressed: isDark ? "#2F6FE0" : "#1E40AF"
    readonly property color primaryText: "#FFFFFF"
    readonly property color feedbackIncomingBg: isDark ? "#222837" : "#FFFFFF"
    readonly property color feedbackIncomingBorder: isDark ? "#2E3647" : "#D5DCE8"
    readonly property color feedbackOutgoingBg: isDark ? "#353D4C" : "#E7ECF4"
    readonly property color feedbackOutgoingBorder: isDark ? "#4A5364" : "#CDD5E2"
    readonly property color feedbackOutgoingText: isDark ? "#F4F7FB" : "#172033"
    readonly property color feedbackOutgoingAttachmentBg: isDark ? "#414A5A" : "#F3F6FA"
}
