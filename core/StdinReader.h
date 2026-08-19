#pragma once
/**
 * @file StdinReader.h
 * @brief 声明不阻塞 Qt 主事件循环的标准输入读取线程。
 * @details 核心进程用它接收人工输入的工作、待机和退出命令。线程只负责读取和转码，
 *          命令含义由主线程中的命令处理函数解释，不在读取线程中操作业务对象。
 */


#include <QThread>
#include <iostream>
#include <string>

/**
 * @brief 在独立线程中阻塞读取标准输入并逐行通知主线程。
 * @details `std::getline` 可能长期阻塞，因此不能放在 Qt 主线程；收到完整非空行后发射
 *          lineRead()，Qt 的跨线程连接把业务处理排入接收者线程。EOF 会自然结束线程。
 * @warning QThread::terminate() 只能作为进程退出阶段解除阻塞的兜底手段，调用方应在对象析构前等待线程结束。
 */
class StdinReader : public QThread
{
    Q_OBJECT

public:
    explicit StdinReader(QObject* parent = nullptr) : QThread(parent) {}

signals:
    /**
     * @brief 通知接收者标准输入中出现一条有效命令。
     * @param line 按系统本地编码转为 QString 且已去除首尾空白的非空文本。
     */
    void lineRead(const QString& line);

protected:
    /**
     * @brief 执行阻塞式逐行读取循环。
     * @details 空行被丢弃；读到 EOF 或输入流失败后返回并结束线程。
     */
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
