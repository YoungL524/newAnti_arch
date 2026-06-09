#include "DphPublisher.h"
#include "CommunicationManager.h"

#include <QDateTime>
#include <QDebug>
#include <cstring>

DphPublisher::DphPublisher(CommunicationManager* commManager, QObject* parent)
    : QObject(parent)
    , m_commManager(commManager)
{
    Q_ASSERT(m_commManager != nullptr);
    m_timer.setInterval(50);
    connect(&m_timer, &QTimer::timeout, this, &DphPublisher::sendOnce);
}

DphPublisher::~DphPublisher()
{
    stop();
    qDebug() << QStringLiteral("[DphPublisher] 销毁：累计发送=%1, 当前景幅=%2")
                .arg(m_sendCount)
                .arg(m_header.F_UI_Scene_Num);
}

void DphPublisher::setRoute(const QString& endpointId, const QString& targetName)
{
    if (!endpointId.isEmpty()) {
        m_endpointId = endpointId;
    }
    if (!targetName.isEmpty()) {
        m_targetName = targetName;
    }
}

void DphPublisher::setInterval(int intervalMs)
{
    if (intervalMs > 0) {
        m_timer.setInterval(intervalMs);
    }
}

void DphPublisher::start()
{
    if (m_timer.isActive()) {
        return;
    }
    m_timer.start();
    qDebug() << QStringLiteral("[DphPublisher] 已启动: %1/%2, interval=%3ms")
                .arg(m_endpointId, m_targetName)
                .arg(m_timer.interval());
}

void DphPublisher::stop()
{
    if (!m_timer.isActive()) {
        return;
    }
    m_timer.stop();
    qDebug() << QStringLiteral("[DphPublisher] 已停止");
}

void DphPublisher::applyRuntimeSettings(const RadarRuntimeSettings& settings)
{
    m_header.F_UI_Freq_Ctrl = settings.frequencyControl;
    m_header.F_UI_Wave_Ctrl = settings.waveControl;
    qDebug() << "[DphPublisher] 应用运行时参数: freq=" << settings.frequencyControl
             << "wave=" << settings.waveControl;
}

void DphPublisher::sendOnce()
{
    if (!m_commManager) {
        return;
    }

    m_header.F_UI_Package_Head = 0xA1A1A1A1;
    m_header.F_UI_Time_Code = static_cast<unsigned int>(QDateTime::currentMSecsSinceEpoch());
    m_header.F_UI_Work_Mode = 0;
    m_header.F_UI_Scene_Num++;
    memset(m_header.F_CR_Save_Path, 0, sizeof(m_header.F_CR_Save_Path));

    const QByteArray headerData(reinterpret_cast<const char*>(&m_header), sizeof(DataPackageHeader));
    const bool ok = m_commManager->sendTo(m_endpointId, m_targetName, headerData);

    ++m_sendCount;
    if (m_sendCount == 1 || m_sendCount % 1000 == 0) {
        qDebug() << QStringLiteral("[DphPublisher] DPH 已发送 %1 次, 景幅=%2, 大小=%3字节, 状态=%4")
                    .arg(m_sendCount)
                    .arg(m_header.F_UI_Scene_Num)
                    .arg(headerData.size())
                    .arg(ok ? QStringLiteral("成功") : QStringLiteral("失败"));
    }
}
