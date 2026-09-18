/**
 * @file main.cpp
 * @brief 提供DP/SP主动心跳的最小UDP联试模拟程序。
 * @details 本工具只按冻结协议向sock2Internal发送0x0101和0x0303，不接收航迹、不访问CoreDataDomain、
 * 不模拟设备控制或维护业务状态。下游为DCMP的DcmpProtocolAdapter；停止进程即可验证5秒超时转灰。
 * 新人应从命令行--help和encodeHeartbeat()阅读可调状态及固定线布局。
 */

#include "core/protocol/DcmpPayloads.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QHostAddress>
#include <QTimer>
#include <QUdpSocket>
#include <QtEndian>

namespace {

/** @brief 保存一次模拟进程固定使用的目标地址、心跳状态和发送周期。 */
struct SimulatorOptions
{
    QHostAddress targetAddress = QHostAddress::LocalHost; ///< DCMP内部UDP绑定地址。
    quint16 targetPort = 7788; ///< DCMP内部UDP端口。
    quint16 dpOnline = 1; ///< DP在线值，协议只允许0或1。
    quint16 spOnline = 1; ///< SP在线值，协议只允许0或1。
    quint16 fpOnlineMask = 0x0F; ///< DP报告的四阵面FP在线位图。
    quint16 fpDataMask = 0x0F; ///< SP报告的四阵面数据位图。
    int intervalMs = 1000; ///< 两轮主动心跳之间的毫秒数。
    bool sendDp = true; ///< false时不发送DP心跳，用于单独验证DP超时。
    bool sendSp = true; ///< false时不发送SP心跳，用于单独验证SP超时。
    bool once = false; ///< true时只发送一轮并立即退出。
};

/**
 * @brief 把十进制或0x前缀文本解析为有上限的无符号值。
 * @param text 命令行参数文本。
 * @param maximum 允许的闭区间上限。
 * @param value 成功时写入解析结果。
 * @return 格式合法且未超过maximum时返回true。
 */
bool parseUnsigned(const QString &text, quint32 maximum, quint32 &value)
{
    bool ok = false;
    const quint32 parsed = text.toUInt(&ok, 0);
    if (!ok || parsed > maximum) return false;
    value = parsed;
    return true;
}

/**
 * @brief 根据命令行形成一份不可变发送配置。
 * @param application 提供程序名及参数列表的Qt应用对象。
 * @param options 成功时写入完整配置。
 * @return 参数全部有效且至少启用一种心跳时返回true；失败时已输出具体原因。
 */
bool parseOptions(const QCoreApplication &application, SimulatorOptions &options)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("DCMP DP/SP心跳UDP模拟器"));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("target-ip"), QStringLiteral("DCMP目标IPv4地址。"),
                      QStringLiteral("ip"), QStringLiteral("127.0.0.1")});
    parser.addOption({QStringLiteral("target-port"), QStringLiteral("DCMP目标UDP端口。"),
                      QStringLiteral("port"), QStringLiteral("7788")});
    parser.addOption({QStringLiteral("dp-online"), QStringLiteral("DP在线值：0或1。"),
                      QStringLiteral("value"), QStringLiteral("1")});
    parser.addOption({QStringLiteral("sp-online"), QStringLiteral("SP在线值：0或1。"),
                      QStringLiteral("value"), QStringLiteral("1")});
    parser.addOption({QStringLiteral("fp-online-mask"), QStringLiteral("FP在线低四位掩码，例如0x0F。"),
                      QStringLiteral("mask"), QStringLiteral("0x0F")});
    parser.addOption({QStringLiteral("fp-data-mask"), QStringLiteral("FP数据低四位掩码，例如0x05。"),
                      QStringLiteral("mask"), QStringLiteral("0x0F")});
    parser.addOption({QStringLiteral("interval-ms"), QStringLiteral("发送周期，最小100毫秒。"),
                      QStringLiteral("ms"), QStringLiteral("1000")});
    parser.addOption({QStringLiteral("no-dp"), QStringLiteral("不发送DP心跳。")});
    parser.addOption({QStringLiteral("no-sp"), QStringLiteral("不发送SP心跳。")});
    parser.addOption({QStringLiteral("once"), QStringLiteral("只发送一轮后退出。")});
    parser.process(application);

    options.targetAddress = QHostAddress(parser.value(QStringLiteral("target-ip")));
    quint32 port = 0;
    quint32 dpOnline = 0;
    quint32 spOnline = 0;
    quint32 fpOnlineMask = 0;
    quint32 fpDataMask = 0;
    quint32 intervalMs = 0;
    if (options.targetAddress.protocol() != QAbstractSocket::IPv4Protocol
        || !parseUnsigned(parser.value(QStringLiteral("target-port")), 65535U, port) || port == 0
        || !parseUnsigned(parser.value(QStringLiteral("dp-online")), 1U, dpOnline)
        || !parseUnsigned(parser.value(QStringLiteral("sp-online")), 1U, spOnline)
        || !parseUnsigned(parser.value(QStringLiteral("fp-online-mask")), 0x0FU, fpOnlineMask)
        || !parseUnsigned(parser.value(QStringLiteral("fp-data-mask")), 0x0FU, fpDataMask)
        || !parseUnsigned(parser.value(QStringLiteral("interval-ms")), 60000U, intervalMs)
        || intervalMs < 100U) {
        qCritical("命令行参数非法，请运行 --help 查看范围。");
        return false;
    }
    options.targetPort = static_cast<quint16>(port);
    options.dpOnline = static_cast<quint16>(dpOnline);
    options.spOnline = static_cast<quint16>(spOnline);
    options.fpOnlineMask = static_cast<quint16>(fpOnlineMask);
    options.fpDataMask = static_cast<quint16>(fpDataMask);
    options.intervalMs = static_cast<int>(intervalMs);
    options.sendDp = !parser.isSet(QStringLiteral("no-dp"));
    options.sendSp = !parser.isSet(QStringLiteral("no-sp"));
    options.once = parser.isSet(QStringLiteral("once"));
    if (!options.sendDp && !options.sendSp) {
        qCritical("--no-dp与--no-sp不能同时使用。");
        return false;
    }
    return true;
}

/**
 * @brief 编码一个具有84字节心跳载荷的完整统一报文。
 * @param messageType 只允许DpHeartbeat或SpHeartbeat。
 * @param online 对应进程在线值，调用前已限制为0或1。
 * @param faceMask 对应四阵面状态低四位，调用前已限制为0～15。
 * @return 132字节小端报文；20个备份字和未明确系统信息保持零。
 */
QByteArray encodeHeartbeat(core::protocol::DcmpMessageType messageType, quint16 online, quint16 faceMask)
{
    using namespace core::protocol;
    QByteArray frame(static_cast<int>(DpHeartbeatPayload::frameWireSize), '\0');
    qToLittleEndian<quint32>(DcmpFrameHeader::expectedMagic,
                             frame.data() + DcmpFrameHeader::magicOffset);
    qToLittleEndian<quint32>(DpHeartbeatPayload::frameWireSize - DcmpFrameHeader::lengthExcludedSize,
                             frame.data() + DcmpFrameHeader::totalLengthOffset);
    qToLittleEndian<quint32>(static_cast<quint32>(messageType),
                             frame.data() + DcmpFrameHeader::messageTypeOffset);
    qToLittleEndian<quint32>(DcmpFrameHeader::expectedVersion,
                             frame.data() + DcmpFrameHeader::versionOffset);
    char *const payload = frame.data() + DcmpFrameHeader::payloadOffset;
    qToLittleEndian<quint16>(online, payload + DpHeartbeatPayload::onlineOffset);
    qToLittleEndian<quint16>(faceMask, payload + DpHeartbeatPayload::faceMaskOffset);
    qToLittleEndian<quint32>(DcmpFrameTail::expectedMagic,
                             frame.data() + frame.size() - DcmpFrameTail::wireSize);
    return frame;
}

/**
 * @brief 发送当前配置启用的一轮DP/SP心跳。
 * @param socket 由main拥有且与本线程同生命周期的UDP套接字。
 * @param options 启动时冻结的目标及状态配置。
 * @param dpFrame 已编码且可重复发送的DP心跳字节。
 * @param spFrame 已编码且可重复发送的SP心跳字节。
 * @return 所有启用报文均被QUdpSocket接受时返回true。
 */
bool sendRound(QUdpSocket &socket, const SimulatorOptions &options,
               const QByteArray &dpFrame, const QByteArray &spFrame)
{
    bool ok = true;
    if (options.sendDp) {
        ok = socket.writeDatagram(dpFrame, options.targetAddress, options.targetPort) == dpFrame.size() && ok;
    }
    if (options.sendSp) {
        ok = socket.writeDatagram(spFrame, options.targetAddress, options.targetPort) == spFrame.size() && ok;
    }
    if (!ok) qWarning().noquote() << QStringLiteral("心跳发送失败：") + socket.errorString();
    return ok;
}

} // namespace

/**
 * @brief 解析固定状态后立即发送首轮，周期模式复用同一份132字节报文直到进程退出。
 * @return 参数或单次发送失败返回1；周期模式进入Qt事件循环。
 */
int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("DcmpHeartbeatSimulator"));
    SimulatorOptions options;
    if (!parseOptions(application, options)) return 1;

    const QByteArray dpFrame = encodeHeartbeat(
        core::protocol::DcmpMessageType::DpHeartbeat, options.dpOnline, options.fpOnlineMask);
    const QByteArray spFrame = encodeHeartbeat(
        core::protocol::DcmpMessageType::SpHeartbeat, options.spOnline, options.fpDataMask);
    QUdpSocket socket;
    const bool firstRoundOk = sendRound(socket, options, dpFrame, spFrame);
    qInfo().noquote() << QStringLiteral("发送到 %1:%2；DP=%3 mask=0x%4；SP=%5 mask=0x%6；周期=%7ms")
        .arg(options.targetAddress.toString()).arg(options.targetPort)
        .arg(options.sendDp ? QString::number(options.dpOnline) : QStringLiteral("停发"))
        .arg(options.fpOnlineMask, 0, 16)
        .arg(options.sendSp ? QString::number(options.spOnline) : QStringLiteral("停发"))
        .arg(options.fpDataMask, 0, 16).arg(options.intervalMs);
    if (options.once) return firstRoundOk ? 0 : 1;

    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &application,
                     [&socket, &options, &dpFrame, &spFrame] {
        sendRound(socket, options, dpFrame, spFrame);
    });
    timer.start(options.intervalMs);
    return application.exec();
}
