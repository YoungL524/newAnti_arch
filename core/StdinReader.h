#pragma once

#include <QThread>
#include <iostream>
#include <string>

/**
 * @brief stdin 读取线程
 * @details 独立线程中阻塞读取 stdin，读到一行后通过信号传回主线程
 *          这样不会阻塞 Qt 事件循环，定时器和 UDP 接收正常工作
 *
 * 注意：此类故意在头文件中全量实现，避免 core 目录额外新增 .cpp 负担；
 *       test_comm/StdinReader.h 保持独立副本，两项目各持一份。
 */
class StdinReader : public QThread
{
    Q_OBJECT

public:
    explicit StdinReader(QObject* parent = nullptr) : QThread(parent) {}

signals:
    /**
     * @brief 读取到一行输入
     * @param line 输入内容（已去除首尾空白）
     */
    void lineRead(const QString& line);

protected:
    void run() override {
        std::string line;
        while (std::getline(std::cin, line)) {
            QString qline = QString::fromLocal8Bit(line.c_str()).trimmed();
            if (!qline.isEmpty()) {
                emit lineRead(qline);
            }
        }
    }
};
