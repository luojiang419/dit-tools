#pragma once

#include <QObject>
#include <QString>

class LocalImageUrlHelper : public QObject {
    Q_OBJECT

public:
    explicit LocalImageUrlHelper(QObject *parent = nullptr);

    Q_INVOKABLE QString sourceForInput(const QString &input) const;

    static QString sourceForInputString(const QString &input);
};
