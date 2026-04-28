#pragma once
#include <QString>
#include <QMap>

enum class Protocol {
    UDP,
    TCP
};

struct Target {
    QString address;
    quint16 port = 0;
};

struct EndpointConfig {
    QString   id;
    Protocol  protocol = Protocol::UDP;
    QString   localAddress  = QStringLiteral("127.0.0.1");
    quint16   localPort     = 0;
    QMap<QString, Target> targets;
    QString   description;
};
