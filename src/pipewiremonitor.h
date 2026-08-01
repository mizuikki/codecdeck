#pragma once

#include <QObject>
#include <QVariantList>

#include <memory>

class PipeWireMonitorPrivate;

class PipeWireMonitor final : public QObject
{
    Q_OBJECT

public:
    explicit PipeWireMonitor(QObject *parent = nullptr);
    ~PipeWireMonitor() override;

    bool start(QString *error = nullptr);
    void refresh();
    bool selectCodec(const QString &address, const QString &codec, QString *error = nullptr);

signals:
    void devicesChanged(const QVariantList &devices);
    void connectionChanged(bool connected, const QString &error);

private:
    std::unique_ptr<PipeWireMonitorPrivate> d;
};
