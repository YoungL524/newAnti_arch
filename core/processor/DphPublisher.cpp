#include "DphPublisher.h"
#include "CommunicationManager.h"

#include <QDateTime>
#include <QDebug>
#include <cstring>

/**
 * @brief 建立默认 50 ms 定时器，并使 timeout 在本对象线程调用 sendOnce()。
 */
DphPublisher::DphPublisher(CommunicationManager* commManager, QObject* parent)
    : QObject(parent)
    , m_commManager(commManager)
{
    Q_ASSERT(m_commManager != nullptr);
    m_timer.setInterval(50);
    connect(&m_timer, &QTimer::timeout, this, &DphPublisher::sendOnce);
}

/** @brief 停止定时器，防止对象销毁过程中继续访问非拥有的 CommunicationManager。 */
DphPublisher::~DphPublisher()
{
    stop();
    qDebug() << QStringLiteral("[DphPublisher] 销毁：累计发送=%1, 当前景幅=%2")
                .arg(m_sendCount)
                .arg(m_header.F_UI_Scene_Num);
}

/**
 * @brief 分别更新非空端点 ID 和目标名，避免部分配置把有效默认值清空。
 */
void DphPublisher::setRoute(const QString& endpointId, const QString& targetName)
{
    if (!endpointId.isEmpty()) {
        m_endpointId = endpointId;
    }
    if (!targetName.isEmpty()) {
        m_targetName = targetName;
    }
}

/**
 * @brief 接受正毫秒周期并交给 QTimer；非正值保持当前周期不变。
 */
void DphPublisher::setInterval(int intervalMs)
{
    if (intervalMs > 0) {
        m_timer.setInterval(intervalMs);
    }
}

/**
 * @brief 幂等启动周期发送；不会在启动瞬间额外发送一包，首次发送等待一个完整周期。
 */
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

/**
 * @brief 幂等停止定时器，同时保留景幅号、运行参数和累计尝试次数供下次恢复。
 */
void DphPublisher::stop()
{
    if (!m_timer.isActive()) {
        return;
    }
    m_timer.stop();
    qDebug() << QStringLiteral("[DphPublisher] 已停止");
}

/**
 * @brief 更新 DPH 中的频率和波形控制字段，不重置景幅号或定时器相位。
 */
void DphPublisher::applyRuntimeSettings(const RadarRuntimeSettings& settings)
{
    m_header.F_UI_Freq_Ctrl = settings.frequencyControl;
    m_header.F_UI_Wave_Ctrl = settings.waveControl;
    qDebug() << "[DphPublisher] 应用运行时参数: freq=" << settings.frequencyControl
             << "wave=" << settings.waveControl;
}

/**
 * @brief 刷新每周期字段、递增景幅号，并按协议内存布局发送一个完整 DataPackageHeader。
 * @details 时间码取 Unix 毫秒低 32 位；保存路径每周期清零。sendCount 记录发送尝试，因此日志中的状态
 *          必须结合 ok 判断，不能把该计数当作成功包数。
 */
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
