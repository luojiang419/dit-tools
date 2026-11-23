#pragma once

#include <QString>

class Logger {
public:
    static bool initialize(const QString &logFilePath, QString *errorMessage);
    static void info(const QString &message);
    static void warn(const QString &message);
    static void error(const QString &message);
    static QString currentLogFile();
    static void shutdown();

private:
    static void write(const QString &level, const QString &message);
};
