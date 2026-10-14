#include "ApplicationRuntime.h"
#include "core/app/StorageRuntime.h"

#include <QMetaObject>
#include <QDebug>
#include <utility>
namespace core::app {
/**
 * @brief 建立唯一同步接收路径，绑定和协议解释交给各自owner。
 */
ApplicationRuntime::ApplicationRuntime(QString databasePath, QString applicationVersion)
    : m_domain(4096,1024),
      m_databasePath(std::move(databasePath)),
      m_applicationVersion(std::move(applicationVersion)),
      m_deviceEventPublisher(m_domain.eventTopic()),
      m_deviceController(m_domain.deviceState(),
          [this](const QByteArray &bytes, const QHostAddress &address, quint16 port) {
              return m_sock2DpSp.send(bytes, address, port);
          }, &m_deviceEventPublisher),
      m_trackIngress(m_domain.trackTopic()),
      m_applicationController(m_deviceController, m_trackIngress),
      m_adapter(
          [this](const auto&,const auto& p,qint64 u,qint64 s){++m_dpReceived; m_deviceController.acceptHeartbeat(false,p.dpOnline,p.fpOnlineMask,u,s,&p);},
          [this](const auto&,const auto& p,qint64 u,qint64 s){++m_spReceived; m_deviceController.acceptHeartbeat(true,p.spOnline,p.fpDataMask,u,s);},
          [this](const auto&,quint8 face,quint32 sequence,const auto& track,qint64 u,qint64 s){
              if (m_trackIngress.accept(face,sequence,track,u,s)
                  == core::producer::TrackIngressProducer::Result::Published) ++m_trackPublished;
              else ++m_trackRejected;
          }),
      m_sock2DpSp([this](const char* p,qint64 n,const QHostAddress& a,quint16 port,qint64 u,qint64 s){acceptDatagram(p,n,a,port,u,s);}),
      m_stateWeb(m_domain.deviceState(),m_lane,m_gateway,m_stableIds),
      m_trackWeb(m_domain.trackTopic(),m_domain.deviceState(),m_lane,m_gateway,m_stableIds,256),
      m_eventWeb(m_domain.eventTopic(),m_lane,m_gateway,m_stableIds)
{
    QObject::connect(&m_sock2DpSp, &core::comm::UdpEndpoint::transportError, &m_sock2DpSp,
        [this, lastReported = QString()](const QString &error) mutable {
            if (error == lastReported) return;
            lastReported = error;
            qWarning().noquote() << "[UDP传输错误]" << error << m_sock2DpSp.receiveDiagnostics();
        });
    m_deviceController.stop();
}

/**
 * @brief 保留唯一同步解析栈，以累计值低频定位停读或拒收，不逐包打印、不等待任何 Consumer。
 * @details 汇总由实际来包触发；停止出现汇总而心跳超时日志继续时，可区分接收入口停滞与整个主线程停滞。
 * 终端诊断不替代 EventTopic 的业务事实，定位完成后可删除此处汇总及计数，不改变协议或所有权。
 */
void ApplicationRuntime::acceptDatagram(const char *data, qint64 size, const QHostAddress &sender,
                                      quint16 senderPort, qint64 utcMs, qint64 steadyNs)
{
    ++m_udpReceived;
    const auto result = m_adapter.onDatagram(data, size, utcMs, steadyNs);
    if (result != core::protocol::DcmpProtocolAdapter::Result::Accepted) {
        ++m_protocolRejected;
        m_lastProtocolRejection = result;
    }
    if (m_lastUdpReportSteadyNs != 0 && steadyNs - m_lastUdpReportSteadyNs < 5'000'000'000LL) return;
    m_lastUdpReportSteadyNs = steadyNs;
    qInfo().noquote() << "[UDP接收]" << "utcMs=" << utcMs
        << "source=" << sender.toString() << senderPort << "total=" << m_udpReceived
        << "DP=" << m_dpReceived << "SP=" << m_spReceived
        << "trackPublished=" << m_trackPublished << "trackRejected=" << m_trackRejected
        << "protocolRejected=" << m_protocolRejected
        << "lastProtocolCode=" << static_cast<int>(m_lastProtocolRejection)
        << m_sock2DpSp.receiveDiagnostics();
}
/**
 * @brief 成员逆序析构前停止接收，确保回调依赖仍存活。
 */
ApplicationRuntime::~ApplicationRuntime() { stop(); }
/**
 * @brief 在回环地址接收航迹与心跳；Web失败立即解绑UDP，不留下半运行服务。
 */
bool ApplicationRuntime::start(quint16 internalPort,quint16 webSocketPort,quint16 deviceHttpPort)
{
    if (m_running) { m_error=QStringLiteral("运行时已启动"); return false; }
    if (!startStorage()) return false;
    if (!m_sock2DpSp.start(QHostAddress::LocalHost,internalPort)) {
        m_error=QStringLiteral("内部UDP启动失败：")+m_sock2DpSp.lastError();
        stopStorage(); return false;
    }
    if (!m_gateway.start(webSocketPort)) {
        m_error=QStringLiteral("WebSocket Gateway启动失败");
        m_sock2DpSp.stop(); stopStorage(); return false;
    }
    if (!m_deviceHttp.start(deviceHttpPort, *m_storageRuntime, m_applicationController)) {
        m_error=QStringLiteral("设备与任务HTTP启动失败：")+m_deviceHttp.lastError();
        m_gateway.stop(); m_sock2DpSp.stop(); stopStorage(); return false;
    }
    m_trackWeb.refresh(); m_eventWeb.refresh(); m_deviceController.start(); m_running=true; m_error.clear(); return true;
}
/**
 * @brief 先收口已接受 HTTP 的提交和回滚，再停止接收、计时和 Web，最后排空归档。
 * @details HTTP 关停只定向完成已接受请求，不启动嵌套事件循环；此时 Socket 和设备 Owner 仍存活，
 * 避免已创建会话尚未绑定就关闭存储，或在工作/待机提交前提前关闭唯一 UDP 端点。
 */
void ApplicationRuntime::stop()
{
    m_deviceHttp.stop(); m_sock2DpSp.stop(); m_deviceController.stop(); m_gateway.stop(); m_running=false; stopStorage();
}

/**
 * @brief 先启动唯一存储事件循环，再阻塞调用初始化，保证 UDP Producer 启动前身份注册域已经完整。
 */
bool ApplicationRuntime::startStorage()
{
    if (m_storageRuntime || m_storageThread.isRunning()) {
        m_error = QStringLiteral("存储运行时状态不一致或已经启动");
        return false;
    }
    m_stableIds.clear();
    m_storageRuntime = new StorageRuntime(
        m_databasePath, m_applicationVersion, m_stableIds,
        m_domain.trackTopic(), m_domain.eventTopic(), m_domain.taskState());
    m_storageRuntime->moveToThread(&m_storageThread);
    QObject::connect(&m_storageThread, &QThread::finished,
                     m_storageRuntime, &QObject::deleteLater);
    m_storageThread.start();
    bool initialized = false;
    const bool invoked = QMetaObject::invokeMethod(
        m_storageRuntime, "initialize", Qt::BlockingQueuedConnection,
        Q_RETURN_ARG(bool, initialized));
    if (invoked && initialized) return true;

    m_error = invoked ? m_storageRuntime->lastError()
                      : QStringLiteral("无法调用存储运行时初始化");
    stopStorage();
    return false;
}

/**
 * @brief 在停止实时入口后同步闭合数据库，再结束线程；失败只保留诊断，不跳过资源释放。
 */
void ApplicationRuntime::stopStorage()
{
    if (!m_storageRuntime) {
        if (m_storageThread.isRunning()) {
            m_storageThread.quit();
            m_storageThread.wait();
        }
        m_stableIds.clear();
        return;
    }
    if (m_storageThread.isRunning()) {
        bool closed = false;
        const bool invoked = QMetaObject::invokeMethod(
            m_storageRuntime, "shutdown", Qt::BlockingQueuedConnection,
            Q_RETURN_ARG(bool, closed));
        if ((!invoked || !closed) && m_error.isEmpty()) {
            m_error = invoked ? m_storageRuntime->lastError()
                              : QStringLiteral("无法调用存储运行时关闭");
        }
        m_storageThread.quit();
        m_storageThread.wait();
    }
    m_storageRuntime = nullptr;
    m_stableIds.clear();
}
}
