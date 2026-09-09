/**
 * @file api-track.h
 * @brief 航迹字段评审来源，保留旧结构供对照，不作为可直接包含的交付头文件。
 * @details 已确认字段现已落实到 core/protocol/DcmpPayloads.h 的 DpToDcmpTrackPayload::DpTrack，
 * 对外使用 toDP.h；航迹点224字节，完整0x0100载荷608字节，声明长度644，整包656字节。
 */
//目标类型--航迹
struct TargetType
{
    int    TargetID;    // 目标识别号
    int		wave_row;           // 波位行
    int     wave_col;			// 波位列
    int     wave;				// 波位
    int     raw_wave_position;  // 原始包头波位号
    float   Range;             ///< 雷达到目标的斜距（单位：米）z。
    float   Azimuth;           // 目标方位角（单位：弧度）x
    float   Elevation;         // 目标俯仰角（单位：弧度）y
    float   RadialSpeed;       // 目标径向速度（单位：米/秒）
    float   RCS;               // 幅度
    float   Altitude;          // 目标高度
    float   CourseAngle;       // 目标方位角速度
    float   PitchAngle;        // 目标俯仰角速度
    float   TargetSize;        // 信噪比
    float   x;                 // X坐标
    float   y;                 // y坐标BackgroundChangeCBox
    float   z;                 // z坐标
    float   vx;				// vx速度
    float   vy;				// vy速度
    float   vz;				// vz速度
    int cluster_id;             // 聚类
    int count = 0;
    int Clutter_Diagram_Markers = 0;//杂波图标志位
    int A_or_B_mark; // a,b波形标志 
    //============= 后添加的 ======================
    float a;
    float b;
    float c;
    int tier_level = 0;
    quint64 GPUTimeStart;
    quint64 GPUTimeEnd;
    int view;
    quint32 pack_flag; // 消亡标志
    float B_FL_Azi_H_saved; // 保存方位指向中心（从header中复制）
    float B_FL_Ele_H_saved; // 保存俯仰指向中心（从header中复制）
    // 滤波后的值（仅用于CSV记录对比）
    float Azimuth_filtered;      // 滤波后的方位角
    float Elevation_filtered;    // 滤波后的俯仰角
    float Range_filtered;        // 滤波后的距离
    float RadialSpeed_filtered;  // 滤波后的径向速度
    // 运动学一致性验证
    float kinematics_margin;     // 运动学余量（米）：>0表示在预测区间内，<0表示超出区间
    float kinematics_diff;
    int data_source = 0;  // 数据来源标识：0-探测数据，1-跟踪数据
    int assoc_state;
    int point_id = -1;   // 原始点ID：对应RadarTrack中的原始点编号，用于绑定航迹与原始点
    int Multiply_range = 0;    // 广域距离扩展倍数，原始点默认0
    int Multiply_doppler = 0;  // 广域速度扩展倍数，原始点默认0
    float bak[9]; // 备份字（减少1个以容纳data_source）

};

//给上位机的航迹结构体
struct host_computer
{
    TargetType TrackInfo;
    unsigned int time_accumulate_; //积累时间
    float Track_recognition_mark; // 航迹识别标志--航迹标号等同于targetID
    float Coming_and_going_signs; // 出现和消失标记 -- 离雷达越来越近还是越远---！！！先用来记一下接收到吴翀数据的时间戳
    float Point_position;         // 第几个点 -- 类似于Tnn
    float Actual_or_supplementary_points; // 实际点或补充点标记 -- 补点为1 实际点为0
    int Extrapolation_frequency = 0;// 航迹未用实测点更新次数 -- 外推点更新次数
    bool Update_Flag; //更新标志
    int view = 0; // 景幅
};





#pragma once

#include <QtGlobal>

namespace core::protocol {

/**
 * @brief DP -> DCMP 单条航迹数据载荷。
 *
 * @details
 * 由原 TargetType + host_computer 中的航迹相关字段整理而来。
 *
 * 本结构体表示协议字段语义，不代表最终线协议内存布局。
 * 编解码应由 ProtocolCodec 按字段顺序完成，
 * 禁止直接发送 sizeof(DcmpTrackPayload) 对应的对象内存。
 */
struct DcmpTrackPayload
{
    // =========================================================
    // 1. 目标及波位基本信息
    // =========================================================
    qint32 targetId = 0;              ///< 目标识别号
    qint32 waveRow = 0;               ///< 波位行
    qint32 waveCol = 0;               ///< 波位列
    qint32 wave = 0;                  ///< 波位
    qint32 rawWavePosition = 0;       ///< 原始包头波位号
    // =========================================================
    // 2. 目标极坐标及基本运动信息
    // =========================================================
    float range = 0.0;               ///< 雷达到目标的斜距，m
    float azimuth = 0.0;             ///< 目标方位角，度.度
    float elevation = 0.0;           ///< 目标俯仰角，度.度
    float radialSpeed = 0.0;         ///< 目标径向速度，m/s，远离为负，抵近为正
    float amplitude = 0.0;           ///< 幅度
    float altitude = 0.0;            ///< 目标高度,相对雷达高度，m
    float courseAngle = 0.0;         ///< 目标方位角速度,度.度/s
    float pitchAngle = 0.0;          ///< 目标俯仰角速度,度.度/s
    float targetSNR = 0.0;           ///< 信噪比
    // =========================================================
    // 3. 笛卡尔坐标及三维速度，雷达为坐标原点
    // =========================================================
    float x = 0.0;                   ///< X 坐标
    float y = 0.0;                   ///< Y 坐标
    float z = 0.0;                   ///< Z 坐标
    float vx = 0.0;                  ///< X 方向速度
    float vy = 0.0;                  ///< Y 方向速度
    float vz = 0.0;                  ///< Z 方向速度
    // =========================================================
    // 4. 聚类及波形相关信息
    // =========================================================
    qint32 clusterId = 0;             ///< 聚类 ID
    qint32 count = 0;                 ///< 原 count
    qint32 clutterDiagramMarkers = 0; ///< 杂波图标志位
    qint32 waveformMark = 0;          ///< A/B 波形标志
    // =========================================================
    // 5. 算法内部参数
    // =========================================================
    float a = 0.0F;
    float b = 0.0F;
    float c = 0.0F;
    qint32 tierLevel = 0;             ///< 层级
    // =========================================================
    // 6. 景幅、生命周期及波束中心
    // =========================================================
    qint32 targetView = 0;            ///< 原 TargetType::view
    quint32 packFlag = 0;             ///< 消亡标志，1-消失，2-不消失
    float savedAzimuthCenter = 0.0F;  ///< 保存方位指向中心
    float savedElevationCenter = 0.0F;///< 保存俯仰指向中心
    // =========================================================
    // 7. 运动学一致性验证
    // =========================================================
    float kinematicsMargin = 0.0F;    ///< >0 在预测区间内，<0 超出
    float kinematicsDiff = 0.0F;      ///< 运动学差值
    // =========================================================
    // 8. 数据来源及关联状态
    // =========================================================
    qint32 dataSource = 0;            ///< 0: 探测数据，1: 跟踪数据
    qint32 assocState = 0;            ///< 关联状态
    qint32 pointId = -1;              ///< 原始点 ID
    qint32 multiplyRange = 0;         ///< 广域距离扩展倍数
    qint32 multiplyDoppler = 0;       ///< 广域速度扩展倍数
    // =========================================================
    // 9. host_computer 附加航迹信息
    // =========================================================
    quint32 accumulatedTime = 0;      ///< 积累时间
    float trackRecognitionMark = 0.0;///< 航迹识别标志
    float comingAndGoingSigns = 0.0; ///< 出现/消失相关标记
    float pointPosition = 0.0;       ///< 第几个点
    float pointType = 0.0;           ///< 实际点/补充点：补点1，实际点0
    qint32 extrapolationCount = 0;    ///< 未用实测点更新次数 / 外推次数
    quint32 updateFlag = 0; ///< 0 = 未更新；1 = 已更新；
    qint32 hostView = 0;    ///< 景幅
    // =========================================================
    // 10. 备份字
    // =========================================================
    float reserved[9] {};             ///< 原 bak[9]

    // 已落实的 DpTrack::wireSize = 224U；此处只保留评审来源，不另维护正式类型。
};

} // namespace core::protocol
