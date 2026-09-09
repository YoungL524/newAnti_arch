#include "ApplicationRuntime.h"
namespace core::app {
/**
 * @brief 建立唯一同步接收路径，绑定和协议解释交给各自owner。
 */
ApplicationRuntime::ApplicationRuntime()
    : m_domain(4096,1024), m_heartbeat(m_domain.deviceState()),
      m_adapter(
          [this](const auto&,const auto& p,qint64 u,qint64 s){m_heartbeat.accept(false,p.dpOnline,p.fpOnlineMask,u,s);},
          [this](const auto&,const auto& p,qint64 u,qint64 s){m_heartbeat.accept(true,p.spOnline,p.fpDataMask,u,s);}),
      m_internal([this](const char* p,qint64 n,const QHostAddress&,quint16,qint64 u,qint64 s){m_adapter.onDatagram(p,n,u,s);}),
      m_web(m_domain.deviceState(),m_lane)
{
    m_heartbeat.stop();
}
/**
 * @brief 成员逆序析构前停止接收，确保回调依赖仍存活。
 */
ApplicationRuntime::~ApplicationRuntime() { stop(); }
/**
 * @brief Web失败立即解绑UDP；全部成功后启用期限检查，不留下半运行服务。
 */
bool ApplicationRuntime::start(quint16 internalPort,quint16 statePort)
{
    if (m_running) { m_error=QStringLiteral("运行时已启动"); return false; }
    if (!m_internal.start(QHostAddress::LocalHost,internalPort)) {
        m_error=QStringLiteral("内部UDP启动失败：")+m_internal.lastError(); return false;
    }
    if (!m_web.start(statePort)) {
        m_error=QStringLiteral("Web状态出口启动失败：")+m_web.lastError();
        m_internal.stop(); return false;
    }
    m_heartbeat.start(); m_running=true; m_error.clear(); return true;
}
/**
 * @brief 阻止新报文后关闭计时和Web；清理不依赖后续事件循环。
 */
void ApplicationRuntime::stop()
{
    m_internal.stop(); m_heartbeat.stop(); m_web.stop(); m_running=false;
}
}
