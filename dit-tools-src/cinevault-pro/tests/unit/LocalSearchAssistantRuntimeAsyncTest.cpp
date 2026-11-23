#include <QtTest>

#include "infrastructure/search/LocalSearchAssistantRuntime.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstdio>

namespace {
int argumentValue(const QStringList &arguments, const QString &name)
{
    const auto index = arguments.indexOf(name);
    if (index < 0 || index + 1 >= arguments.size()) {
        return 0;
    }
    return arguments.at(index + 1).toInt();
}

int runFakeRuntime(QCoreApplication *application)
{
    const auto port = argumentValue(application->arguments(), QStringLiteral("--port"));
    QTcpServer server;
    if (port <= 0 || !server.listen(QHostAddress::LocalHost, static_cast<quint16>(port))) {
        return 2;
    }
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server]() {
        auto *socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket]() {
            socket->readAll();
            const QByteArray body = QByteArrayLiteral("{\"status\":\"ok\"}");
            socket->write(QByteArrayLiteral(
                              "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
                          + QByteArray::number(body.size())
                          + QByteArrayLiteral("\r\nConnection: close\r\n\r\n")
                          + body);
            socket->disconnectFromHost();
        });
    });
    return application->exec();
}
}

class LocalSearchAssistantRuntimeAsyncTest : public QObject {
    Q_OBJECT

private slots:
    void startStopAndImmediateRestartNeverBlockCaller()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto modelPath = temporary.filePath(QStringLiteral("fake-model.gguf"));
        QFile model(modelPath);
        QVERIFY(model.open(QIODevice::WriteOnly));
        QVERIFY(model.write("fake") == 4);
        model.close();

        LocalSearchAssistantRuntime runtime(QCoreApplication::applicationFilePath(), modelPath);
        QSignalSpy readySpy(&runtime, &LocalSearchAssistantRuntime::ready);

        QElapsedTimer callTimer;
        callTimer.start();
        QVERIFY(runtime.start());
        QVERIFY2(callTimer.elapsed() < 200, "start() 不得同步等待 GPU 探测");
        QVERIFY(runtime.isStarting());
        QTRY_VERIFY_WITH_TIMEOUT(runtime.isReady(), 5000);
        QVERIFY(!runtime.gpuDeviceName().isEmpty());

        callTimer.restart();
        runtime.stop();
        QVERIFY2(callTimer.elapsed() < 200, "stop() 不得同步等待子进程退出");
        QVERIFY(!runtime.isReady());
        QVERIFY(runtime.endpoint().isEmpty());

        callTimer.restart();
        QVERIFY(runtime.start());
        QVERIFY2(callTimer.elapsed() < 200, "停止中的重启请求不得阻塞调用线程");
        QTRY_VERIFY_WITH_TIMEOUT(runtime.isReady(), 5000);
        QVERIFY(readySpy.count() >= 2);
    }
};

int main(int argc, char **argv)
{
    const QStringList arguments = [&]() {
        QStringList values;
        for (int index = 0; index < argc; ++index) {
            values.append(QString::fromLocal8Bit(argv[index]));
        }
        return values;
    }();
    if (arguments.contains(QStringLiteral("--list-devices"))) {
        QTextStream(stdout) << "Available devices:\n0: CineVault Fake GPU\n";
        return 0;
    }

    QCoreApplication application(argc, argv);
    if (arguments.contains(QStringLiteral("--model"))) {
        return runFakeRuntime(&application);
    }
    LocalSearchAssistantRuntimeAsyncTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "LocalSearchAssistantRuntimeAsyncTest.moc"
