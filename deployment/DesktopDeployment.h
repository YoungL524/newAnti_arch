/**
 * @file DesktopDeployment.h
 * @brief 定义 Windows 单机启动器的具体进程生命周期 owner。
 * @details 上游是部署配置和托盘操作，下游是 Core、Node Web、DP/SP 与默认浏览器。
 * 不读 Topic、不解析雷达协议、不代替 DeviceController 发送控制。新人从 start() 阅读启动与回滚。
 */
#pragma once
#include <QObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QTimer>
#include <QWinEventNotifier>
#include <QStringList>
#include <array>
#include <windows.h>

/**
 * @brief 在 GUI 线程串行管理单次部署，进程由 Windows Job Object 兜底释放。
 * @note 配置加载和进程创建仅在启动期执行；运行期用进程事件通知，不轮询业务 Topic。
 */
class DesktopDeployment final : public QObject
{
    Q_OBJECT
public:
    /** @brief 借用应用事件循环并保存当前用户 IPC 名称；根目录取启动 EXE 所在目录。 */
    explicit DesktopDeployment(QString channel, QObject *parent = nullptr);
    /** @brief 释放通知器和进程句柄，关闭 Job 后仅清理本次创建的进程树。 */
    ~DesktopDeployment() override;
    /** @brief 校验布局并异步启动；失败时先停止已启动部分，再通过 finished 返回非零退出码。 */
    void start();
Q_SIGNALS:
    /** @brief 本次生命周期已结束；零表示用户正常退出，非零表示启动或运行失败。 */
    void finished(int exitCode);
private:
    /** @brief 一个固定进程槽位保存配置与原生资源，不保存业务数据或消息队列。 */
    struct Process {
        QString name; ///< 诊断名称，固定为 Core/DP/SP/Web。
        QString executable; ///< 已解析绝对 EXE 路径；不通过 Shell 解释。
        QStringList arguments; ///< 直接传给进程的参数，禁止内嵌 NUL。
        QString directory; ///< 工作目录，生命周期与槽位一致。
        bool enabled = false; ///< DP/SP 尚未配置时显式禁用。
        HANDLE handle = nullptr; ///< 本对象唯一拥有的进程句柄；关闭前先删除 notifier。
        QWinEventNotifier *notifier = nullptr; ///< 本对象拥有的 GUI 线程进程退出监听，可空。
    };
    /** @brief 读取部署 JSON 并校验全部启用进程路径，失败不产生子进程。 */
    bool loadConfiguration();
    /** @brief 挂起创建并先纳入 Job，再恢复线程；只继承 NUL 标准句柄，控制台隐藏。 */
    bool launchProcess(std::size_t index);
    /** @brief 接纳受当前用户访问控制的 IPC；Core 令牌与重复启动 OPEN 使用不同消息合同。 */
    void acceptConnections();
    /** @brief 启动期最多一个本机健康请求，确认随机令牌匹配后才打开网页。 */
    void checkWeb();
    /** @brief Core/Web 均就绪后停止启动期检查，自动打开网页一次。 */
    void becomeReady();
    /** @brief 根据本次进程句柄报告退出；意外退出触发整体停止，不自动重启。 */
    void processExited(std::size_t index);
    /** @brief 聚合第一个失败原因并请求整体停止，避免递归弹窗或重复退出。 */
    void fail(const QString &message);
    /** @brief 禁止新业务请求，通知 Core 退出并等待最多 15 秒，再结束其余受管进程。 */
    void shutdown();
    /** @brief 删除通知器、关闭 Job 和句柄，再报告已保存的失败原因或正常完成。 */
    void finishShutdown();
    /** @brief 默认浏览器独立于受管进程；重复启动不自动增加页面，托盘显式操作可以重开。 */
    void openBrowser(bool explicitRequest);
    /** @brief 保存有界启动诊断：1 MiB 当前文件加一个旧文件；不收集 DP/SP 数据输出。 */
    void log(const QString &message);

    QString m_root; ///< 启动 EXE 所在 dcmp 目录，不依赖用户当前工作目录。
    QString m_channel; ///< 主实例本地 IPC 名称，调用者持有对应单实例锁。
    QString m_token; ///< 本次启动随机身份，仅在本地 IPC 和健康请求使用。
    QString m_error; ///< 首个失败原因，用于最终提示和退出码。
    QString m_logPath; ///< 当前用户应用数据目录中的有界诊断文件。
    std::array<Process, 4> m_processes; ///< 固定 Core/DP/SP/Web 槽位，唯一进程资源集合。
    HANDLE m_job = nullptr; ///< 本对象唯一拥有的 Windows Job，关闭时终止仍存活的受管进程。
    QLocalServer m_server; ///< 当前用户的 IPC 监听者，不接受设备命令。
    QPointer<QLocalSocket> m_core; ///< 已认证 Core 控制连接，由本对象通过 Qt parent 拥有，可空。
    QNetworkAccessManager m_network; ///< 启动期健康请求 owner，运行期不轮询。
    QPointer<QNetworkReply> m_healthReply; ///< 同时最多一条未完成的健康请求，可空。
    QMenu m_menu; ///< 托盘菜单，生命周期覆盖 tray 的借用。
    QSystemTrayIcon m_tray; ///< 唯一托盘入口，没有主窗口。
    QTimer m_healthTimer; ///< 仅启动期触发健康检查，周期 200 ms。
    QTimer m_startupTimer; ///< 本次启动总期限，默认 30 秒。
    QTimer m_shutdownTimer; ///< 等待 Core 正常退出的 15 秒上限。
    quint16 m_port = 8080; ///< 对浏览器开放的唯一回环 Web 端口。
    int m_startupTimeout = 30000; ///< 配置的启动期限，单位毫秒，范围 5～120 秒。
    bool m_coreReady = false; ///< 只在 Core 报告完成初始化后置位。
    bool m_webReady = false; ///< 只在健康令牌匹配后置位。
    bool m_ready = false; ///< 两条就绪证据成立后的部署运行态。
    bool m_stopping = false; ///< 防止重复进入退出流程。
    bool m_finished = false; ///< 资源已释放与完成信号已发出的幂等门禁。
    bool m_browserOpened = false; ///< 自动打开页面只执行一次，重复启动不新增页面。
};
