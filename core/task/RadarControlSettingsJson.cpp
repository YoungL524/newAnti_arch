#include "core/task/RadarControlSettingsJson.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

#include <cmath>
#include <limits>

namespace core::task {
namespace {

/** @brief 判断控制字是否属于四态二进制编码集合。 */
/**
 * @brief 判断协议字是否属于0x00、0x01、0x10、0x11四种已冻结编码。
 */
bool isFourStateWord(quint32 value)
{
    return value == 0x00U || value == 0x01U || value == 0x10U || value == 0x11U;
}

/**
 * @brief 从JSON读取无符号32位整数，拒绝浮点、负数和越界。
 */
bool readUnsigned(const QJsonObject &object, const char *key, quint32 &value, QString &error)
{
    const QJsonValue json = object.value(QString::fromLatin1(key));
    const double number = json.toDouble(std::numeric_limits<double>::quiet_NaN());
    if (!json.isDouble() || !std::isfinite(number) || std::floor(number) != number
        || number < 0.0 || number > static_cast<double>(std::numeric_limits<quint32>::max())) {
        error = QStringLiteral("%1必须是uint32整数").arg(QString::fromLatin1(key));
        return false;
    }
    value = static_cast<quint32>(number);
    return true;
}

/**
 * @brief 从JSON读取有符号32位整数，拒绝浮点和越界。
 */
bool readSigned(const QJsonObject &object, const char *key, qint32 &value, QString &error)
{
    const QJsonValue json = object.value(QString::fromLatin1(key));
    const double number = json.toDouble(std::numeric_limits<double>::quiet_NaN());
    if (!json.isDouble() || !std::isfinite(number) || std::floor(number) != number
        || number < static_cast<double>(std::numeric_limits<qint32>::min())
        || number > static_cast<double>(std::numeric_limits<qint32>::max())) {
        error = QStringLiteral("%1必须是int32整数").arg(QString::fromLatin1(key));
        return false;
    }
    value = static_cast<qint32>(number);
    return true;
}

/**
 * @brief 从JSON读取有限双精度数。
 */
bool readDouble(const QJsonObject &object, const char *key, double &value, QString &error)
{
    const QJsonValue json = object.value(QString::fromLatin1(key));
    value = json.toDouble(std::numeric_limits<double>::quiet_NaN());
    if (!json.isDouble() || !std::isfinite(value)) {
        error = QStringLiteral("%1必须是有限数值").arg(QString::fromLatin1(key));
        return false;
    }
    return true;
}

/**
 * @brief 从JSON读取显式布尔值，禁止0/1代替。
 */
bool readBool(const QJsonObject &object, const char *key, bool &value, QString &error)
{
    const QJsonValue json = object.value(QString::fromLatin1(key));
    if (!json.isBool()) {
        error = QStringLiteral("%1必须是布尔值").arg(QString::fromLatin1(key));
        return false;
    }
    value = json.toBool();
    return true;
}

/**
 * @brief 把单阵面运行参数转换为JSON，不写入设备安装姿态。
 */
QJsonObject faceToJson(const RadarFaceSettings &face)
{
    return QJsonObject {
        {QStringLiteral("frequencyBand"), static_cast<qint64>(face.frequencyBand)},
        {QStringLiteral("frequencyIndex"), static_cast<qint64>(face.frequencyIndex)},
        {QStringLiteral("rfAttenuationDb"), static_cast<qint64>(face.rfAttenuationDb)},
        {QStringLiteral("ifAttenuationDb"), static_cast<qint64>(face.ifAttenuationDb)},
        {QStringLiteral("antennaMode"), static_cast<qint64>(face.antennaMode)},
        {QStringLiteral("fan1Powered"), face.fan1Powered},
        {QStringLiteral("fan2Powered"), face.fan2Powered},
        {QStringLiteral("trModulePowered"), face.trModulePowered}
    };
}

/** @brief 解析单阵面完整参数；缺失任何字段即失败。 */
/**
 * @brief 完整读取一个阵面参数，任一字段缺失时保持候选对象不可提交。
 */
bool faceFromJson(const QJsonObject &object, RadarFaceSettings &face, QString &error)
{
    return readUnsigned(object, "frequencyBand", face.frequencyBand, error)
        && readUnsigned(object, "frequencyIndex", face.frequencyIndex, error)
        && readUnsigned(object, "rfAttenuationDb", face.rfAttenuationDb, error)
        && readUnsigned(object, "ifAttenuationDb", face.ifAttenuationDb, error)
        && readUnsigned(object, "antennaMode", face.antennaMode, error)
        && readBool(object, "fan1Powered", face.fan1Powered, error)
        && readBool(object, "fan2Powered", face.fan2Powered, error)
        && readBool(object, "trModulePowered", face.trModulePowered, error);
}

} // namespace

/**
 * @brief 校验合同已经明确的枚举和值域；模式禁用的字段仍保留，不擅自清零。
 */
bool validateRadarControlSettings(const RadarControlSettings &settings, QString &error)
{
    const auto &common = settings.common;
    const auto &frame = settings.frame;
    if (common.combinationMode > 0x01U) error = QStringLiteral("combinationMode超出0x00～0x01");
    else if (common.frameWorkMode != 0x00U && common.frameWorkMode != 0x02U)
        error = QStringLiteral("frameWorkMode只允许0x00或0x02");
    else if (common.systemWorkMode > 0x04U) error = QStringLiteral("systemWorkMode超出0x00～0x04");
    else if (common.rangeMode > 0x01U) error = QStringLiteral("rangeMode只允许0x00或0x01");
    else if (!isFourStateWord(common.stcMode)) error = QStringLiteral("stcMode编码无效");
    else if (common.cfarMode != 0x00 && common.cfarMode != 0x01) error = QStringLiteral("cfarMode编码无效");
    else if (!std::isfinite(frame.beamAzimuthDeg) || frame.beamAzimuthDeg < -45.0 || frame.beamAzimuthDeg > 45.0)
        error = QStringLiteral("beamAzimuthDeg超出[-45,45]");
    else if (!std::isfinite(frame.beamElevationDeg) || frame.beamElevationDeg < -45.0 || frame.beamElevationDeg > 45.0)
        error = QStringLiteral("beamElevationDeg超出[-45,45]");
    else if (frame.bandwidthMode > 0x02U) error = QStringLiteral("bandwidthMode超出0x00～0x02");
    else if (frame.waveformWritePosition < 0x11U || frame.waveformWritePosition > 0x99U)
        error = QStringLiteral("waveformWritePosition超出0x11～0x99");
    else {
        for (qsizetype index = 0; index < static_cast<qsizetype>(settings.faces.size()); ++index) {
            const auto &face = settings.faces[static_cast<std::size_t>(index)];
            if (face.frequencyBand > 0x01U || face.frequencyIndex > 39U
                || face.rfAttenuationDb > 31U || face.ifAttenuationDb > 31U
                || !isFourStateWord(face.antennaMode)) {
                error = QStringLiteral("faces[%1]的频段、频点、衰减或天线模式无效").arg(index);
                return false;
            }
        }
        error.clear();
        return true;
    }
    return false;
}

/**
 * @brief 使用冻结的英文内部键形成完整对象，供HTTP和SQLite共同复用。
 */
QJsonObject radarControlSettingsToJson(const RadarControlSettings &settings)
{
    const auto &c = settings.common;
    const auto &f = settings.frame;
    QJsonArray faces;
    for (const auto &face : settings.faces) faces.append(faceToJson(face));
    return QJsonObject {
        {QStringLiteral("common"), QJsonObject {
            {QStringLiteral("combinationMode"), static_cast<qint64>(c.combinationMode)},
            {QStringLiteral("frameWorkMode"), static_cast<qint64>(c.frameWorkMode)},
            {QStringLiteral("systemWorkMode"), static_cast<qint64>(c.systemWorkMode)},
            {QStringLiteral("rangeMode"), static_cast<qint64>(c.rangeMode)},
            {QStringLiteral("stcEnabled"), c.stcEnabled},
            {QStringLiteral("stcMode"), static_cast<qint64>(c.stcMode)},
            {QStringLiteral("excitationDisabled"), c.excitationDisabled},
            {QStringLiteral("cfarMode"), c.cfarMode},
            {QStringLiteral("cfarThreshold1"), c.cfarThreshold1},
            {QStringLiteral("cfarThreshold2"), c.cfarThreshold2},
            {QStringLiteral("velocityThreshold"), c.velocityThreshold},
            {QStringLiteral("adRecordEnabled"), c.adRecordEnabled},
            {QStringLiteral("pointRecordEnabled"), c.pointRecordEnabled},
            {QStringLiteral("trackInitiationPointCount"), static_cast<qint64>(c.trackInitiationPointCount)},
            {QStringLiteral("clusterRecordEnabled"), c.clusterRecordEnabled},
            {QStringLiteral("associationRecordEnabled"), c.associationRecordEnabled}
        }},
        {QStringLiteral("frame"), QJsonObject {
            {QStringLiteral("screenWaveMode"), static_cast<qint64>(f.screenWaveMode)},
            {QStringLiteral("screenTrackEnabled"), f.screenTrackEnabled},
            {QStringLiteral("beamAzimuthDeg"), f.beamAzimuthDeg},
            {QStringLiteral("beamElevationDeg"), f.beamElevationDeg},
            {QStringLiteral("bandwidthMode"), static_cast<qint64>(f.bandwidthMode)},
            {QStringLiteral("bitEnabled"), f.bitEnabled},
            {QStringLiteral("waveformCode"), static_cast<qint64>(f.waveformCode)},
            {QStringLiteral("modulationMode"), static_cast<qint64>(f.modulationMode)},
            {QStringLiteral("modulationBandwidthWord"), static_cast<qint64>(f.modulationBandwidthWord)},
            {QStringLiteral("pulseWidthWord"), static_cast<qint64>(f.pulseWidthWord)},
            {QStringLiteral("repetitionPeriodWord"), static_cast<qint64>(f.repetitionPeriodWord)},
            {QStringLiteral("frameWaitPulseCount"), static_cast<qint64>(f.frameWaitPulseCount)},
            {QStringLiteral("effectivePulseCount"), static_cast<qint64>(f.effectivePulseCount)},
            {QStringLiteral("sampleStartWord"), static_cast<qint64>(f.sampleStartWord)},
            {QStringLiteral("sampleEndOrLengthWord"), static_cast<qint64>(f.sampleEndOrLengthWord)},
            {QStringLiteral("waveformWriteEnabled"), f.waveformWriteEnabled},
            {QStringLiteral("waveformWritePosition"), static_cast<qint64>(f.waveformWritePosition)}
        }},
        {QStringLiteral("faces"), faces}
    };
}

/**
 * @brief 在局部候选对象中完成解析和校验，保证失败不会部分修改调用者状态。
 */
bool radarControlSettingsFromJson(
    const QJsonObject &object, RadarControlSettings &settings, QString &error)
{
    if (!object.value(QStringLiteral("common")).isObject()
        || !object.value(QStringLiteral("frame")).isObject()
        || !object.value(QStringLiteral("faces")).isArray()) {
        error = QStringLiteral("controlSettings必须包含common、frame和faces");
        return false;
    }
    RadarControlSettings candidate;
    const QJsonObject c = object.value(QStringLiteral("common")).toObject();
    const QJsonObject f = object.value(QStringLiteral("frame")).toObject();
    if (!readUnsigned(c, "combinationMode", candidate.common.combinationMode, error)
        || !readUnsigned(c, "frameWorkMode", candidate.common.frameWorkMode, error)
        || !readUnsigned(c, "systemWorkMode", candidate.common.systemWorkMode, error)
        || !readUnsigned(c, "rangeMode", candidate.common.rangeMode, error)
        || !readBool(c, "stcEnabled", candidate.common.stcEnabled, error)
        || !readUnsigned(c, "stcMode", candidate.common.stcMode, error)
        || !readBool(c, "excitationDisabled", candidate.common.excitationDisabled, error)
        || !readSigned(c, "cfarMode", candidate.common.cfarMode, error)
        || !readSigned(c, "cfarThreshold1", candidate.common.cfarThreshold1, error)
        || !readSigned(c, "cfarThreshold2", candidate.common.cfarThreshold2, error)
        || !readSigned(c, "velocityThreshold", candidate.common.velocityThreshold, error)
        || !readBool(c, "adRecordEnabled", candidate.common.adRecordEnabled, error)
        || !readBool(c, "pointRecordEnabled", candidate.common.pointRecordEnabled, error)
        || !readUnsigned(c, "trackInitiationPointCount", candidate.common.trackInitiationPointCount, error)
        || !readBool(c, "clusterRecordEnabled", candidate.common.clusterRecordEnabled, error)
        || !readBool(c, "associationRecordEnabled", candidate.common.associationRecordEnabled, error)
        || !readUnsigned(f, "screenWaveMode", candidate.frame.screenWaveMode, error)
        || !readBool(f, "screenTrackEnabled", candidate.frame.screenTrackEnabled, error)
        || !readDouble(f, "beamAzimuthDeg", candidate.frame.beamAzimuthDeg, error)
        || !readDouble(f, "beamElevationDeg", candidate.frame.beamElevationDeg, error)
        || !readUnsigned(f, "bandwidthMode", candidate.frame.bandwidthMode, error)
        || !readBool(f, "bitEnabled", candidate.frame.bitEnabled, error)
        || !readUnsigned(f, "waveformCode", candidate.frame.waveformCode, error)
        || !readUnsigned(f, "modulationMode", candidate.frame.modulationMode, error)
        || !readUnsigned(f, "modulationBandwidthWord", candidate.frame.modulationBandwidthWord, error)
        || !readUnsigned(f, "pulseWidthWord", candidate.frame.pulseWidthWord, error)
        || !readUnsigned(f, "repetitionPeriodWord", candidate.frame.repetitionPeriodWord, error)
        || !readUnsigned(f, "frameWaitPulseCount", candidate.frame.frameWaitPulseCount, error)
        || !readUnsigned(f, "effectivePulseCount", candidate.frame.effectivePulseCount, error)
        || !readUnsigned(f, "sampleStartWord", candidate.frame.sampleStartWord, error)
        || !readUnsigned(f, "sampleEndOrLengthWord", candidate.frame.sampleEndOrLengthWord, error)
        || !readBool(f, "waveformWriteEnabled", candidate.frame.waveformWriteEnabled, error)
        || !readUnsigned(f, "waveformWritePosition", candidate.frame.waveformWritePosition, error)) return false;

    const QJsonArray faces = object.value(QStringLiteral("faces")).toArray();
    if (faces.size() != 4) {
        error = QStringLiteral("controlSettings.faces必须固定包含4项");
        return false;
    }
    for (qsizetype index = 0; index < faces.size(); ++index) {
        if (!faces[index].isObject()
            || !faceFromJson(faces[index].toObject(), candidate.faces[static_cast<std::size_t>(index)], error)) {
            if (error.isEmpty()) error = QStringLiteral("faces[%1]必须是完整对象").arg(index);
            return false;
        }
    }
    if (!validateRadarControlSettings(candidate, error)) return false;
    settings = candidate;
    return true;
}

/**
 * @brief 序列化默认构造值，确保Schema内置模板与运行模型使用同一真源。
 */
QString defaultRadarControlSettingsJson()
{
    return QString::fromUtf8(QJsonDocument(radarControlSettingsToJson(RadarControlSettings {}))
                                 .toJson(QJsonDocument::Compact));
}

} // namespace core::task
