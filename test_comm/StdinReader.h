#pragma once
/**
 * @file StdinReader.h
 * @brief 声明通信测试工具的非阻塞标准输入读取线程。
 * @details 测试工具需要持续处理 UDP 和回放定时器，因此把可能永久阻塞的 getline 放到独立线程，
 *          再通过 Qt 信号把完整命令交回 TestComm 所在线程。
 */


#include <QThread>
#include <iostream>
#include <string>

/**
 * @brief 在后台线程逐行读取测试人员输入。
 * @details 输入按系统本地编码转换并去除首尾空白，空行不会发出信号；EOF 会结束线程。
 */
class StdinReader : public QThread
{
    Q_OBJECT

public:
    explicit StdinReader(QObject* parent = nullptr) : QThread(parent) {}

signals:
    /**
     * @brief 通知测试程序收到一条非空控制命令。
     * @param line 已完成本地编码转换并去除首尾空白的文本。
     */
    void lineRead(const QString& line);

protected:
    /** @brief 执行阻塞读取循环，直到标准输入 EOF 或读取失败。 */
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
