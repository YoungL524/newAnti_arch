#include <QtTest>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include "ConfigManager.h"

class TestConfigManager : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        qRegisterMetaType<QString>("QString");
    }

    // ── endpoints 校验 ──
    void validate_endpoints_empty_array_returns_warning() {
        QJsonObject cfg;
        cfg["endpoints"] = QJsonArray();
        auto errors = ConfigManager::validateConfig("endpoints", cfg);
        QVERIFY(errors.contains("endpoints.json: endpoints 数组为空"));
    }

    void validate_endpoints_missing_id_returns_warning() {
        QJsonObject ep;
        ep["id"] = "";
        ep["localPort"] = 7788;
        QJsonArray arr;
        arr.append(ep);
        QJsonObject cfg;
        cfg["endpoints"] = arr;
        auto errors = ConfigManager::validateConfig("endpoints", cfg);
        bool found = false;
        for (const auto& e : errors)
            if (e.contains("id 为空")) found = true;
        QVERIFY(found);
    }

    void validate_endpoints_port_zero_returns_warning() {
        QJsonObject ep;
        ep["id"] = "test";
        ep["localPort"] = 0;
        QJsonArray arr;
        arr.append(ep);
        QJsonObject cfg;
        cfg["endpoints"] = arr;
        auto errors = ConfigManager::validateConfig("endpoints", cfg);
        bool found = false;
        for (const auto& e : errors)
            if (e.contains("localPort")) found = true;
        QVERIFY(found);
    }

    void validate_endpoints_ok_returns_empty() {
        QJsonObject ep;
        ep["id"] = "test";
        ep["localPort"] = 7788;
        QJsonArray arr;
        arr.append(ep);
        QJsonObject cfg;
        cfg["endpoints"] = arr;
        auto errors = ConfigManager::validateConfig("endpoints", cfg);
        QVERIFY(errors.isEmpty());
    }

    // ── processor 校验 ──
    void validate_processor_missing_target_returns_warning() {
        QJsonObject cfg;
        auto errors = ConfigManager::validateConfig("processor", cfg);
        bool found = false;
        for (const auto& e : errors)
            if (e.contains("reverseTargetDevice")) found = true;
        QVERIFY(found);
    }

    void validate_processor_interval_zero_returns_warning() {
        QJsonObject dph;
        dph["intervalMs"] = 0;
        QJsonObject cfg;
        cfg["reverseTargetDevice"] = 1;
        cfg["dphPublisher"] = dph;
        auto errors = ConfigManager::validateConfig("processor", cfg);
        bool found = false;
        for (const auto& e : errors)
            if (e.contains("intervalMs")) found = true;
        QVERIFY(found);
    }

    void validate_processor_interval_5000_returns_warning() {
        QJsonObject dph;
        dph["intervalMs"] = 5000;
        QJsonObject cfg;
        cfg["reverseTargetDevice"] = 1;
        cfg["dphPublisher"] = dph;
        auto errors = ConfigManager::validateConfig("processor", cfg);
        bool found = false;
        for (const auto& e : errors)
            if (e.contains("intervalMs")) found = true;
        QVERIFY(found);
    }

    void validate_processor_ok_returns_empty() {
        QJsonObject dph;
        dph["intervalMs"] = 50;
        QJsonObject cfg;
        cfg["reverseTargetDevice"] = 1;
        cfg["dphPublisher"] = dph;
        auto errors = ConfigManager::validateConfig("processor", cfg);
        QVERIFY(errors.isEmpty());
    }

    void validate_unknown_name_returns_empty() {
        QJsonObject cfg;
        auto errors = ConfigManager::validateConfig("unknown", cfg);
        QVERIFY(errors.isEmpty());
    }

    void saveConfig_roundTripsThroughAtomicFile() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        ConfigManager manager;
        QVERIFY(manager.loadConfigDir(dir.path()));
        QJsonObject cfg;
        cfg["value"] = 42;
        cfg["name"] = "atomic";

        QVERIFY(manager.saveConfig("roundtrip", cfg));
        manager.clearCache();
        QCOMPARE(manager.getConfig("roundtrip"), cfg);
    }
};

/**
 * @brief 验证 QTEST_MAIN 场景的验收条件并防止对应功能回归。
 * @details 测试通过明确的输入、可观察结果和边界断言锁定既定行为。
 */
QTEST_MAIN(TestConfigManager)
#include "tst_ConfigManager.moc"
