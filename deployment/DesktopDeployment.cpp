#include "DesktopDeployment.h"
#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkRequest>
#include <QNetworkProxy>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QStyle>
#include <QUuid>
#include <QUrl>
#include <vector>
#include <utility>

/**
 * @brief 遵循 Windows argv 引号规则；空格、引号及结尾反斜杠均作为参数本体传递，不执行 Shell。
 */
static QString quoteArgument(const QString &value)
{
    QString result = QStringLiteral("\"");
    int backslashes = 0;
    for (const QChar character : value) {
        if (character == QLatin1Char('\\')) { ++backslashes; continue; }
        if (character == QLatin1Char('"')) {
            result += QString(backslashes * 2 + 1, QLatin1Char('\\'));
        } else {
            result += QString(backslashes, QLatin1Char('\\'));
        }
        backslashes = 0;
        result += character;
    }
    return result + QString(backslashes * 2, QLatin1Char('\\')) + QLatin1Char('"');
}

/**
 * @brief 保留 Windows 错误码对应的本机诊断；格式化缓冲区仅在本调用内存在。
 */
static QString windowsError(DWORD code)
{
    wchar_t *buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t *>(&buffer), 0, nullptr);
    const QString message = buffer ? QString::fromWCharArray(buffer).trimmed() : QStringLiteral("未知 Windows 错误");
    if (buffer) LocalFree(buffer);
    return message + QStringLiteral(" (%1)").arg(static_cast<qulonglong>(code));
}

/**
 * @brief 只组装托盘和启动期计时，不启动进程；Core 归档关闭始终由其既有运行时执行。
 */
DesktopDeployment::DesktopDeployment(QString channel, QObject *parent)
    : QObject(parent), m_root(QCoreApplication::applicationDirPath()), m_channel(std::move(channel)),
      m_token(QUuid::createUuid().toString(QUuid::WithoutBraces)), m_server(this), m_network(this), m_tray(this)
{
    m_logPath = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("deployment.log"));
    m_network.setProxy(QNetworkProxy::NoProxy);
    m_tray.setIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon));
    m_tray.setToolTip(QStringLiteral("NewAntiDrone：正在启动"));
    m_tray.setContextMenu(&m_menu);
    auto *open = m_menu.addAction(QStringLiteral("打开界面"));
    auto *quit = m_menu.addAction(QStringLiteral("退出系统"));
    QObject::connect(open, &QAction::triggered, this, [this] { openBrowser(true); });
    QObject::connect(quit, &QAction::triggered, this, &DesktopDeployment::shutdown);
    QObject::connect(&m_server, &QLocalServer::newConnection, this, &DesktopDeployment::acceptConnections);
    m_healthTimer.setInterval(200);
    QObject::connect(&m_healthTimer, &QTimer::timeout, this, &DesktopDeployment::checkWeb);
    m_startupTimer.setSingleShot(true);
    QObject::connect(&m_startupTimer, &QTimer::timeout, this, [this] { fail(QStringLiteral("启动超时：Core 或 Web 尚未就绪")); });
    m_shutdownTimer.setSingleShot(true);
    QObject::connect(&m_shutdownTimer, &QTimer::timeout, this, [this] {
        if (m_error.isEmpty()) m_error = QStringLiteral("Core 在 15 秒内未完成正常退出，已强制清理；归档排空未确认");
        log(m_error);
        finishShutdown();
    });
}

/**
 * @brief 所有通知器先于原生句柄释放；异常离开事件循环也会关闭本次 Job，避免受管进程残留。
 */
DesktopDeployment::~DesktopDeployment()
{
    for (auto &process : m_processes) {
        delete process.notifier;
        if (process.handle) CloseHandle(process.handle);
    }
    if (m_job) CloseHandle(m_job);
}

/**
 * @brief 配置必须明确 DP/SP 启用状态；先验证全量启用路径，再执行任何外部启动副作用。
 */
bool DesktopDeployment::loadConfiguration()
{
    QFile file(QDir(m_root).filePath(QStringLiteral("deployment.json")));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) {
        m_error = QStringLiteral("无法读取有效的 deployment.json"); return false;
    }
    const auto document = QJsonDocument::fromJson(file.readAll());
    const auto object = document.object();
    const int port = object.value(QStringLiteral("webPort")).toInt(-1);
    const int timeout = object.value(QStringLiteral("startupTimeoutMs")).toInt(-1);
    if (!document.isObject() || object.value(QStringLiteral("schemaVersion")).toDouble() != 1
        || port < 1024 || port > 65535 || object.value(QStringLiteral("webPort")).toDouble() != port
        || timeout < 5000 || timeout > 120000 || object.value(QStringLiteral("startupTimeoutMs")).toDouble() != timeout) {
        m_error = QStringLiteral("部署配置版本、端口或启动期限无效"); return false;
    }
    m_port = static_cast<quint16>(port);
    m_startupTimeout = timeout;
    m_processes[0].name = QStringLiteral("Core");
    m_processes[0].enabled = true;
    m_processes[0].executable = QDir(m_root).filePath(QStringLiteral("runtime/core/NewAntiDroneCore.exe"));
    m_processes[0].directory = m_root;
    m_processes[0].arguments = QStringList {QStringLiteral("--launcher-channel"), m_channel, QStringLiteral("--launcher-token"), m_token};
    m_processes[3].name = QStringLiteral("Web");
    m_processes[3].enabled = true;
    m_processes[3].executable = QDir(m_root).filePath(QStringLiteral("runtime/node.exe"));
    m_processes[3].directory = m_root;
    const QString webScript = QDir(m_root).filePath(QStringLiteral("runtime/web/server/production.mjs"));
    m_processes[3].arguments = QStringList {webScript};
    if (!QFileInfo(webScript).isFile() || !QFileInfo(QDir(m_root).filePath(QStringLiteral("runtime/web/dist/index.html"))).isFile()) {
        m_error = QStringLiteral("部署包缺少生产 Web 服务或已构建的网页"); return false;
    }
    for (std::size_t index = 1; index <= 2; ++index) {
        auto &process = m_processes[index];
        process.name = index == 1 ? QStringLiteral("DP") : QStringLiteral("SP");
        const auto entry = object.value(index == 1 ? QStringLiteral("dp") : QStringLiteral("sp"));
        const auto configuration = entry.toObject();
        if (!entry.isObject() || !configuration.value(QStringLiteral("enabled")).isBool()) {
            m_error = process.name + QStringLiteral(" 启用配置无效"); return false;
        }
        process.enabled = configuration.value(QStringLiteral("enabled")).toBool();
        if (!process.enabled) continue;
        const auto path = configuration.value(QStringLiteral("executable"));
        const auto directory = configuration.value(QStringLiteral("workingDirectory"));
        const auto arguments = configuration.value(QStringLiteral("arguments"));
        if (!path.isString() || path.toString().isEmpty() || path.toString().contains(QChar(0))
            || !directory.isString() || directory.toString().isEmpty() || directory.toString().contains(QChar(0)) || !arguments.isArray()) {
            m_error = process.name + QStringLiteral(" 路径、参数或工作目录无效"); return false;
        }
        process.executable = QDir::cleanPath(QDir(m_root).absoluteFilePath(path.toString()));
        process.directory = QDir::cleanPath(QDir(m_root).absoluteFilePath(directory.toString()));
        for (const auto argument : arguments.toArray()) {
            if (!argument.isString() || argument.toString().contains(QChar(0))) {
                m_error = process.name + QStringLiteral(" 参数必须是不含 NUL 的字符串"); return false;
            }
            process.arguments.append(argument.toString());
        }
    }
    for (const auto &process : m_processes) {
        if (process.enabled && (!QFileInfo(process.executable).isFile()
            || !process.executable.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)
            || !QFileInfo(process.directory).isDir())) {
            m_error = process.name + QStringLiteral(" EXE 或工作目录不存在"); return false;
        }
    }
    return true;
}

/**
 * @brief 初始化失败不留下半部署；就绪由 IPC 与健康令牌证明，不用固定延迟冒充启动成功。
 */
void DesktopDeployment::start()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) { fail(QStringLiteral("系统托盘不可用")); return; }
    if (!loadConfiguration()) { fail(m_error); return; }
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server.listen(m_channel)) { fail(QStringLiteral("部署 IPC 无法监听：") + m_server.errorString()); return; }
    m_job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!m_job || !SetInformationJobObject(m_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        fail(QStringLiteral("无法建立进程树所有权：") + windowsError(GetLastError())); return;
    }
    m_tray.show();
    for (std::size_t index = 0; index < m_processes.size(); ++index) {
        if (m_processes[index].enabled && !launchProcess(index)) { fail(m_error); return; }
    }
    log(QStringLiteral("已创建启用进程，等待 Core/Web 就绪"));
    m_startupTimer.start(m_startupTimeout);
    m_healthTimer.start();
    checkWeb();
}

/**
 * @brief 原生挂起创建消除纳入 Job 前子进程派生的窗口；继承列表只包含 NUL，避免泄漏其他模块句柄。
 */
bool DesktopDeployment::launchProcess(std::size_t index)
{
    auto &process = m_processes[index];
    SECURITY_ATTRIBUTES security {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE nullFile = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (nullFile == INVALID_HANDLE_VALUE) { m_error = windowsError(GetLastError()); return false; }
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<unsigned char> storage(bytes);
    auto *attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &bytes)) {
        const DWORD error = GetLastError(); CloseHandle(nullFile); m_error = windowsError(error); return false;
    }
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &nullFile, sizeof(nullFile), nullptr, nullptr)) {
        const DWORD error = GetLastError(); DeleteProcThreadAttributeList(attributes); CloseHandle(nullFile);
        m_error = windowsError(error); return false;
    }
    STARTUPINFOEXW startup {};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = nullFile;
    startup.StartupInfo.hStdOutput = nullFile;
    startup.StartupInfo.hStdError = nullFile;
    startup.lpAttributeList = attributes;
    QString command = quoteArgument(QDir::toNativeSeparators(process.executable));
    for (const auto &argument : process.arguments) command += QLatin1Char(' ') + quoteArgument(argument);
    auto line = command.toStdWString();
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("NEWANTI_WEB_PORT"), QString::number(m_port));
    environment.insert(QStringLiteral("NEWANTI_DEPLOYMENT_TOKEN"), m_token);
    environment.insert(QStringLiteral("NEWANTI_DEPLOYMENT_ROOT"), m_root);
    QStringList values = environment.toStringList();
    values.sort(Qt::CaseInsensitive);
    std::wstring block;
    for (const auto &value : values) { block += value.toStdWString(); block.push_back(L'\0'); }
    block.push_back(L'\0');
    PROCESS_INFORMATION created {};
    const BOOL success = CreateProcessW(reinterpret_cast<const wchar_t *>(process.executable.utf16()), line.data(),
        nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
        block.data(), reinterpret_cast<const wchar_t *>(process.directory.utf16()), &startup.StartupInfo, &created);
    const DWORD creationError = GetLastError();
    DeleteProcThreadAttributeList(attributes);
    CloseHandle(nullFile);
    if (!success) { m_error = process.name + QStringLiteral(" 启动失败：") + windowsError(creationError); return false; }
    if (!AssignProcessToJobObject(m_job, created.hProcess) || ResumeThread(created.hThread) == static_cast<DWORD>(-1)) {
        const DWORD error = GetLastError();
        TerminateProcess(created.hProcess, 1);
        CloseHandle(created.hThread); CloseHandle(created.hProcess);
        m_error = process.name + QStringLiteral(" 无法纳入受管进程树：") + windowsError(error); return false;
    }
    CloseHandle(created.hThread);
    process.handle = created.hProcess;
    process.notifier = new QWinEventNotifier(process.handle, this);
    QObject::connect(process.notifier, &QWinEventNotifier::activated, this, [this, index] { processExited(index); });
    log(process.name + QStringLiteral(" 已启动"));
    return true;
}

/**
 * @brief 当前用户 IPC 不承载实时数据；每条消息最多 4096 字节，未认证连接五秒后释放。
 */
void DesktopDeployment::acceptConnections()
{
    while (m_server.hasPendingConnections()) {
        QLocalSocket *socket = m_server.nextPendingConnection();
        if (!socket) continue;
        socket->setParent(this);
        if (findChildren<QLocalSocket *>(QString(), Qt::FindDirectChildrenOnly).size() > 8) { socket->abort(); socket->deleteLater(); continue; }
        /** @brief 本地控制连接保留 Core 角色；重复启动消息只影响自动打开标记，不启动第二组进程。 */
        QObject::connect(socket, &QLocalSocket::readyRead, this, [this, socket, buffer = QByteArray()]() mutable {
            if (buffer.size() + socket->bytesAvailable() > 4096) { socket->abort(); return; }
            buffer += socket->readAll();
            while (buffer.contains('\n')) {
                const int end = buffer.indexOf('\n');
                const QByteArray line = buffer.left(end);
                buffer.remove(0, end + 1);
                if (line == "OPEN") { if (!m_stopping) openBrowser(false); socket->disconnectFromServer(); return; }
                const auto message = QJsonDocument::fromJson(line).object();
                if (message.value(QStringLiteral("token")).toString() != m_token || (m_core && m_core != socket)) { socket->abort(); return; }
                const QString type = message.value(QStringLiteral("type")).toString();
                if (type != QStringLiteral("core-connected") && type != QStringLiteral("core-ready")
                    && type != QStringLiteral("core-error") && type != QStringLiteral("core-stop-error")) {
                    socket->abort(); return;
                }
                m_core = socket;
                if (type == QStringLiteral("core-stop-error")) {
                    if (m_error.isEmpty()) m_error = QStringLiteral("Core 停机失败：") + message.value(QStringLiteral("message")).toString();
                    log(m_error);
                    if (!m_stopping) shutdown();
                    continue;
                }
                if (m_stopping) { socket->write("STOP\n"); socket->flush(); continue; }
                if (type == QStringLiteral("core-error")) { fail(QStringLiteral("Core 启动失败：") + message.value(QStringLiteral("message")).toString()); return; }
                if (type == QStringLiteral("core-ready")) { m_coreReady = true; becomeReady(); }
            }
        });
        QObject::connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            if (m_core == socket) { m_core.clear(); if (!m_stopping) fail(QStringLiteral("Core 管理连接断开")); }
            socket->deleteLater();
        });
        QTimer::singleShot(5000, socket, [this, socket] { if (m_core != socket) { socket->abort(); socket->deleteLater(); } });
        if (socket->bytesAvailable() > 0) QMetaObject::invokeMethod(socket, "readyRead", Qt::QueuedConnection);
    }
}

/**
 * @brief 启动期健康请求不叠加；短暂拒绝连接只等候启动总期限，不保存重试队列。
 */
void DesktopDeployment::checkWeb()
{
    if (m_stopping || m_ready || m_webReady || m_healthReply) return;
    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1/deployment/health").arg(m_port)));
    request.setRawHeader("X-Deployment-Token", m_token.toUtf8());
    auto *reply = m_network.get(request);
    reply->setReadBufferSize(4097);
    m_healthReply = reply;
    /** @brief 就绪证据必须属于本次实例，端口已有其他应用时不会误判成功。 */
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply] {
        m_healthReply.clear();
        const QByteArray body = reply->readAll();
        if (!m_stopping && reply->error() == QNetworkReply::NoError && body.size() <= 4096
            && QJsonDocument::fromJson(body).object().value(QStringLiteral("token")).toString() == m_token) {
            m_webReady = true; becomeReady();
        }
        reply->deleteLater();
    });
    QTimer::singleShot(1000, reply, [reply] { if (!reply->isFinished()) reply->abort(); });
}

/**
 * @brief 只有 Core 的真实初始化和 Web 实例健康均确认后，用户才看到自动打开的页面。
 */
void DesktopDeployment::becomeReady()
{
    if (m_stopping || m_ready || !m_coreReady || !m_webReady) return;
    m_ready = true;
    m_healthTimer.stop(); m_startupTimer.stop();
    m_tray.setToolTip(QStringLiteral("NewAntiDrone：已运行"));
    log(QStringLiteral("Core/Web 已就绪"));
    openBrowser(false);
}

/**
 * @brief 进程对象的退出信号是生命周期依据；DP/SP 心跳在线仍归 DeviceController，二者不混淆。
 */
void DesktopDeployment::processExited(std::size_t index)
{
    auto &process = m_processes[index];
    process.notifier->setEnabled(false);
    DWORD code = 0;
    GetExitCodeProcess(process.handle, &code);
    log(process.name + QStringLiteral(" 已退出，代码 %1").arg(static_cast<qulonglong>(code)));
    if (m_stopping) {
        if (index == 0) {
            if (code != 0 && m_error.isEmpty()) m_error = QStringLiteral("Core 停机返回异常代码 %1，归档排空未确认").arg(static_cast<qulonglong>(code));
            finishShutdown();
        }
        return;
    }
    fail(process.name + QStringLiteral(" 意外退出，代码 %1").arg(static_cast<qulonglong>(code)));
}

/**
 * @brief 保存首个错误后进入同一退出链，不重启设备程序，也不把进程状态发布为设备确认。
 */
void DesktopDeployment::fail(const QString &message)
{
    if (m_finished) return;
    if (m_error.isEmpty()) m_error = message;
    log(m_error);
    shutdown();
}

/**
 * @brief 停止启动期调度再请求 Core 停机；Core 尚在初始化时保留 IPC 接纳，认证后补发 STOP。
 */
void DesktopDeployment::shutdown()
{
    if (m_stopping || m_finished) return;
    m_stopping = true;
    m_healthTimer.stop(); m_startupTimer.stop();
    if (m_healthReply) m_healthReply->abort();
    m_tray.setToolTip(QStringLiteral("NewAntiDrone：正在退出"));
    if (m_processes[0].handle && WaitForSingleObject(m_processes[0].handle, 0) == WAIT_TIMEOUT) {
        if (m_core) { m_core->write("STOP\n"); m_core->flush(); }
        m_shutdownTimer.start(15000);
    } else {
        finishShutdown();
    }
}

/**
 * @brief 正常 Core 退出后才关闭 Job，DP/SP/Web 按用户合同强制结束；超时也明确记录降级。
 */
void DesktopDeployment::finishShutdown()
{
    if (m_finished) return;
    m_finished = true;
    m_shutdownTimer.stop(); m_server.close(); m_tray.hide();
    for (auto &process : m_processes) {
        delete process.notifier; process.notifier = nullptr;
    }
    if (m_job) { CloseHandle(m_job); m_job = nullptr; }
    std::array<HANDLE, 4> handles {};
    DWORD count = 0;
    for (const auto &process : m_processes) if (process.handle) handles[count++] = process.handle;
    if (count && WaitForMultipleObjects(count, handles.data(), TRUE, 3000) != WAIT_OBJECT_0 && m_error.isEmpty()) {
        m_error = QStringLiteral("已请求强制结束，但受管进程未在 3 秒内全部退出");
    }
    for (auto &process : m_processes) {
        if (process.handle) { CloseHandle(process.handle); process.handle = nullptr; }
    }
    log(QStringLiteral("部署退出流程完成"));
    if (!m_error.isEmpty()) QMessageBox::critical(nullptr, QStringLiteral("NewAntiDrone"), m_error + QStringLiteral("\n诊断：") + m_logPath);
    emit finished(m_error.isEmpty() ? 0 : 1);
}

/**
 * @brief 浏览器由系统独立启动；首次自动打开和重复启动通知合并，显式托盘操作可重新打开页面。
 */
void DesktopDeployment::openBrowser(bool explicitRequest)
{
    if (!m_ready || m_stopping || (m_browserOpened && !explicitRequest)) return;
    const QUrl address(QStringLiteral("http://127.0.0.1:%1/").arg(m_port));
    if (QDesktopServices::openUrl(address)) m_browserOpened = true;
    else QMessageBox::warning(nullptr, QStringLiteral("打开界面失败"), QStringLiteral("请在浏览器打开：") + address.toString());
}

/**
 * @brief 有界诊断只记录启动器事实；轮换失败时放弃本条写入，避免无限增长或破坏原文件。
 */
void DesktopDeployment::log(const QString &message)
{
    const QByteArray line = (QDateTime::currentDateTime().toString(Qt::ISODateWithMs) + QLatin1Char(' ') + message.left(1024) + QLatin1Char('\n')).toUtf8();
    if (QFileInfo(m_logPath).size() + line.size() > 1024 * 1024) {
        const QString previous = m_logPath + QStringLiteral(".1");
        if (QFileInfo::exists(previous) && !QFile::remove(previous)) return;
        if (!QFile::rename(m_logPath, previous)) return;
    }
    QFile file(m_logPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append)) file.write(line);
}
