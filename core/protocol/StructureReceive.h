#pragma once
/**
 * @file StructureReceive.h
 * @brief 接收数据结构体定义文件（CPU/雷达 → 上位机）
 * @details 纯数据结构体，不包含任何业务逻辑。
 *          保持现有的雷达协议结构不变，与硬件端协议一一对应。
 *          本文件为协议定义的唯一正式版本。
 */
#include <iostream>
#include <vector>
#include <QMetaType>
#include "StructureSend.h"
#pragma pack(push, 1) // 强制单字节对齐

/**
 * @brief 目标类型结构体 -- 航迹
 */
struct TargetType
{
    unsigned int   TargetID;    // 目标识别号
    int     wave_row;           // 波位行
    int     wave_col;           // 波位列
    int     wave;               // 波位
    double   Range;             // 目标距离（单位：米）
    double   Azimuth;           // 目标方位角（单位：弧度）
    double   Elevation;         // 目标俯仰角（单位：弧度）
    double   RadialSpeed;       // 目标径向速度（单位：米/秒）
    double   RCS;               // 目标雷达截面积
    double   Altitude;          // 目标高度
    double   CourseAngle;       // 目标方位角速度
    double   PitchAngle;        // 目标俯仰角速度
    double   TargetSize;        // 目标大小
    double   x;                 // X坐标
    double   y;                 // Y坐标
    double   z;                 // Z坐标
    double   vx;                // vx速度
    double   vy;                // vy速度
    double   vz;                // vz速度
    int cluster_id;             // 聚类ID
    unsigned int count = 0;     // 计数
};

/**
 * @brief 给上位机的航迹结构体
 */
struct host_computer
{
    TargetType TrackInfo;                    // 航迹信息
    unsigned int time_accumulate_;           // 积累时间
    double Track_recognition_mark;           // 航迹识别标志
    double Coming_and_going_signs;           // 出现和消失标记
    double Point_position;                   // 第几个点
    double Actual_or_supplementary_points;   // 实际点或补充点标记
    int Extrapolation_frequency = 0;         // 航迹未用实测点更新次数
    bool Update_Flag;                        // 更新标志
    int view = 0;                            // 景幅
};

/**
 * @brief CPU发来的总包
 */
struct GeneralPackage
{
    DataPackageHeader datapackageheader;    // 数据包头
    workStandby workstandby;                // 工作模式
    host_computer h_c;                      // 航迹数据
};

#pragma pack(pop) // 恢复之前的对齐
