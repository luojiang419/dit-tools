#include "app/AppBootstrap.h"

#include "app/AppContext.h"
#include "infrastructure/logging/Logger.h"
#include "shared/Paths.h"
#include "ui/imaging/LocalImageProvider.h"

#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickStyle>
#include <QDir>
#include <QMessageLogContext>
#include <QCoreApplication>
#include <QTimer>
#include <QWindow>

namespace {
QString applicationLogFilePath()
{
    return QDir(Paths::logsRoot()).filePath(QStringLiteral("app.log"));
}

void appendQmlStartupLog(const QString &message)
{
    Logger::info(message);
}

QString messageTypeLabel(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("debug");
    case QtInfoMsg:
        return QStringLiteral("info");
    case QtWarningMsg:
        return QStringLiteral("warning");
    case QtCriticalMsg:
        return QStringLiteral("critical");
    case QtFatalMsg:
        return QStringLiteral("fatal");
    }
    return QStringLiteral("unknown");
}

QtMessageHandler g_previousMessageHandler = nullptr;

void qmlRuntimeMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    QStringList parts;
    parts << QStringLiteral("[qt-%1]").arg(messageTypeLabel(type));
    if (context.category && *context.category) {
        parts << QStringLiteral("[category:%1]").arg(QString::fromUtf8(context.category));
    }
    if (context.file && *context.file) {
        parts << QStringLiteral("[file:%1:%2]").arg(QString::fromUtf8(context.file)).arg(context.line);
    }
    parts << message;
    const auto formattedMessage = parts.join(' ');
    if (type == QtWarningMsg) {
        Logger::warn(formattedMessage);
    } else if (type == QtCriticalMsg || type == QtFatalMsg) {
        Logger::error(formattedMessage);
    } else {
        Logger::info(formattedMessage);
    }

    if (g_previousMessageHandler && g_previousMessageHandler != qmlRuntimeMessageHandler) {
        g_previousMessageHandler(type, context, message);
    }
}

void installQmlRuntimeLogger()
{
    static bool installed = false;
    if (installed) {
        return;
    }
    QString loggerError;
    if (!Logger::initialize(applicationLogFilePath(), &loggerError)) {
        return;
    }
    g_previousMessageHandler = qInstallMessageHandler(qmlRuntimeMessageHandler);
    installed = true;
    appendQmlStartupLog(QStringLiteral("[qt-info] Installed Qt/QML runtime message handler."));
}
}

AppBootstrap::AppBootstrap()
    : m_context(std::make_unique<AppContext>())
{
}

AppBootstrap::~AppBootstrap() = default;

bool AppBootstrap::run()
{
    QString errorMessage;
    if (!Paths::ensureBaseDirectories(&errorMessage)) {
        return false;
    }

    installQmlRuntimeLogger();
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    m_engine = std::make_unique<QQmlApplicationEngine>();
    m_engine->addImageProvider(QStringLiteral("cinevault-local"), new LocalImageProvider);
    QObject::connect(m_engine.get(), &QQmlApplicationEngine::warnings, [](const QList<QQmlError> &warnings) {
        for (const auto &warning : warnings) {
            Logger::warn(warning.toString());
        }
    });
    m_context->expose(*m_engine);
    m_engine->loadFromModule("CineVault", "Main");
    const auto loaded = !m_engine->rootObjects().isEmpty();
    if (!loaded) {
        Logger::error(QStringLiteral("QML root object load failed."));
        return false;
    }

    if (QCoreApplication::arguments().contains(QStringLiteral("--qml-startup-probe"),
                                               Qt::CaseInsensitive)) {
        appendQmlStartupLog(QStringLiteral("[qml-startup-probe] root-loaded"));
        QTimer::singleShot(0, m_engine.get(), []() {
            QCoreApplication::exit(0);
        });
        return true;
    }

    m_context->startInteractiveServices();

    if (QCoreApplication::arguments().contains(QStringLiteral("--quick-search-probe"),
                                               Qt::CaseInsensitive)) {
        QTimer::singleShot(250, m_engine.get(), [this]() {
            auto *controller = m_engine->rootContext()
                                   ->contextProperty(QStringLiteral("quickSearchController"))
                                   .value<QObject *>();
            if (controller) {
                QMetaObject::invokeMethod(controller, "requestQuickSearch", Qt::QueuedConnection);
            }
        });
        QTimer::singleShot(500, m_engine.get(), [this]() {
            auto *rootObject = m_engine->rootObjects().isEmpty()
                ? nullptr
                : m_engine->rootObjects().constFirst();
            auto *searchField = rootObject
                ? rootObject->findChild<QObject *>(QStringLiteral("quickSearchField"))
                : nullptr;
            if (searchField) {
                searchField->setProperty("text", QStringLiteral("视频"));
            }
            auto *materialCenter = m_engine->rootContext()
                                       ->contextProperty(QStringLiteral("materialCenterVm"))
                                       .value<QObject *>();
            if (materialCenter) {
                QMetaObject::invokeMethod(materialCenter,
                                          "setSearchText",
                                          Qt::QueuedConnection,
                                          Q_ARG(QString, QStringLiteral("视频")));
            }
        });
        QTimer::singleShot(1500, m_engine.get(), [this]() {
            auto *rootObject = m_engine->rootObjects().isEmpty()
                ? nullptr
                : m_engine->rootObjects().constFirst();
            auto *quickSearchWindow = rootObject
                ? rootObject->findChild<QWindow *>(QStringLiteral("quickSearchWindow"))
                : nullptr;
            auto *pinButton = rootObject
                ? rootObject->findChild<QObject *>(QStringLiteral("quickSearchPinButton"))
                : nullptr;
            if (quickSearchWindow) {
                quickSearchWindow->setProperty("pinned", true);
            }
            const auto visible = quickSearchWindow && quickSearchWindow->isVisible();
            const auto pinned = quickSearchWindow
                && quickSearchWindow->property("pinned").toBool();
            auto *materialCenter = m_engine->rootContext()
                                       ->contextProperty(QStringLiteral("materialCenterVm"))
                                       .value<QObject *>();
            appendQmlStartupLog(QStringLiteral("[quick-search-probe] windowFound=%1 visible=%2 pinFound=%3 pinned=%4 title=%5 folders=%6 assets=%7 frames=%8")
                                    .arg(quickSearchWindow ? 1 : 0)
                                    .arg(visible ? 1 : 0)
                                    .arg(pinButton ? 1 : 0)
                                    .arg(pinned ? 1 : 0)
                                    .arg(quickSearchWindow ? quickSearchWindow->title() : QString())
                                    .arg(materialCenter ? materialCenter->property("folderCount").toInt() : -1)
                                    .arg(materialCenter ? materialCenter->property("assetCount").toInt() : -1)
                                    .arg(materialCenter ? materialCenter->property("frameCount").toInt() : -1));
            QCoreApplication::exit(visible && pinButton && pinned ? 0 : 6);
        });
    }
    if (QCoreApplication::arguments().contains(
            QStringLiteral("--search-assistant-startup-probe"),
            Qt::CaseInsensitive)) {
        auto *probeTimer = new QTimer(m_engine.get());
        probeTimer->setInterval(250);
        probeTimer->setProperty("attempts", 0);
        QObject::connect(probeTimer, &QTimer::timeout, m_engine.get(), [this, probeTimer]() {
            const auto attempts = probeTimer->property("attempts").toInt() + 1;
            probeTimer->setProperty("attempts", attempts);
            auto *settings = m_engine->rootContext()
                                 ->contextProperty(QStringLiteral("settingsVm"))
                                 .value<QObject *>();
            const auto status = settings
                ? settings->property("localSearchAssistantStatusText").toString()
                : QString();
            if (status.contains(QStringLiteral("已就绪"))) {
                appendQmlStartupLog(
                    QStringLiteral("[search-assistant-startup-probe] ready attempts=%1 status=%2")
                        .arg(attempts)
                        .arg(status));
                probeTimer->stop();
                QCoreApplication::exit(0);
                return;
            }
            if (status.contains(QStringLiteral("失败"))
                || status.contains(QStringLiteral("资产缺失"))
                || status.contains(QStringLiteral("已关闭"))
                || attempts >= 140) {
                appendQmlStartupLog(
                    QStringLiteral("[search-assistant-startup-probe] failed attempts=%1 status=%2")
                        .arg(attempts)
                        .arg(status));
                probeTimer->stop();
                QCoreApplication::exit(7);
            }
        });
        probeTimer->start();
    }
    return true;
}

bool AppBootstrap::activateMainWindow()
{
    if (!m_engine || m_engine->rootObjects().isEmpty()) {
        return false;
    }

    auto *window = qobject_cast<QWindow *>(m_engine->rootObjects().constFirst());
    if (!window) {
        return false;
    }

    window->showNormal();
    window->raise();
    window->requestActivate();
    return true;
}
