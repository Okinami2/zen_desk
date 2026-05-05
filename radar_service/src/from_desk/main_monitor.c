/**
 * =========================================================================================
 * 项目名称：基于 HLK-LD2420 毫米波雷达的智能桌面人员状态监测系统
 * 核心架构：空间分治策略 (Spatial Divide & Conquer) + 漏桶容错有限状态机 (Leaky Bucket FSM)
 * 编译标准：严格 C89 / C90 (变量局部声明全部置顶)
 * * * 【工程物理痛点与算法解决之道】
 * * 1. 痛点一：极近场能量饱和 (0~1门，0-140cm)。雷达波天线自耦和桌面静态反射，导致能量均值长期爆表。
 * -> 【解决】：在 0 和 1 门，人为将均值阈值拔高至 50.0 进行“旁路(Bypass)”，彻底弃用均值判定，
 * 【100% 依靠方差 (Variance)】 来捕捉生命体征打破“死水”的瞬间波动。
 * * * 2. 痛点二：远距离多径效应与旁瓣泄露导致的“幽灵方差” (2门以上)。
 * -> 【解决】：引入【空间分治】与【独立方差阈值数组】。针对 2 门及以上，不再迷信极低方差，
 * 而是回归真实的能量绝对值突破来判定入座；同时为每个门设置独立的微动方差阈值，防止远处门被干扰。
 * * * 3. 痛点三：入座判定条件过于苛刻（一票否决制），人稍有停顿倒计时即被打断清零。
 * -> 【解决】：引入网络流量控制中著名的【漏桶算法 (Leaky Bucket)】。入座判定中断时不再断崖式清零，
 * 而是采用“轻微扣分制”，允许入座动作有短暂的 1~2 秒停顿，极大提升系统鲁棒性。
 * =========================================================================================
 */

#include <stdio.h>
#include <windows.h>
#include <conio.h>
#include <time.h> 
#include "serial_utils.h"
#include "radar_protocol.h"

/* ================== 核心参数宏定义区 ================== */
#define MAX_DISTANCE_GATE 3        /* 雷达底层最大上报门限 (0-12门，约覆盖9米范围)，建议改这一项 */
#define PORT_NAME "\\\\.\\COM9"    /* Windows 串口路径 (需根据设备管理器实际情况修改) */

#define VAR_TH_FIDGET 40.0  /* 【大幅乱动全局方差阈值】大于此值判定为剧烈动作 (如起立、换座位) */

/* --- 非对称积分器时间常数 (基于雷达 10Hz 刷新率，10帧 = 1秒) --- */
#define TIME_IN_FRAMES 100  /* 严进入座积分：需累计 10 秒 (100帧) 体征，过滤瞬间路过的人影 */
#define TIME_OUT_FRAMES 300 /* 宽出防抖积分：需连续 30 秒 (300帧) 彻底静默，才判定人员彻底离开 */
/* ====================================================== */

/* 定义人员状态的 3 级有限状态机 (FSM) 枚举 */
typedef enum {
    STATE_AWAY,   /* 状态：没人 (目标彻底消失) */
    STATE_NORMAL, /* 状态：安静微动 (人在座位上，有日常呼吸或轻微微动) */
    STATE_FIDGET  /* 状态：乱动 (目标截面积发生急剧变化) */
} PersonState;

int main() {
    /* ---------------- C89 标准：局部变量严格集中声明区 ---------------- */
    HANDLE hComm;                     /* 串口系统句柄 */
    unsigned char rx_buffer[256];     /* 串口接收数据底层缓存 */
    int bytes_read;                   /* 单次实际读取到的字节数 */
    RadarData radar_data;             /* 协议解析后的雷达结构体 (包含距离、各门能量) */
    
    time_t rawtime;            /* 用于存储系统绝对时间戳 */
    struct tm * timeinfo;      /* 用于存储转换后的本地时间格式结构体 */

    /* * 门限触发数组 (Trigger_TH): 远场依赖此值过滤“多径幽灵”
     * 【旁路设计】00门(0-70cm)和01门(70-140cm)设为 50.00，远高于满载底噪，
     * 故意让均值条件失效。2门以上则恢复正常的能量突变触发。
     */
    double trigger_th[16] = {
        50.00, 50.00, 22.00, 20.00, 
        28.50, 27.00, 21.00, 20.00, 
        17.50, 18.50, 17.50, 17.00, 
        19.00, 16.50, 17.00, 16.00  
    };

    /* * 门限保持数组 (Maintain_TH): 远场依赖此值进行迟滞保持 (Hysteresis)
     * 配合 30秒 的超长超时机制，只要能量跌破此值，就开启离座倒计时。
     */
    double maintain_th[16] = {
        50.00, 50.00, 16.00, 15.00, 
        26.50, 24.50, 19.50, 18.00, 
        15.50, 16.50, 15.50, 15.00, 
        17.00, 14.50, 15.00, 14.00  
    };

    /* * 微动方差阈值数组 (Var_TH_Motion): 核心修正！
     * 每个门拥有独立的抗干扰底线。
     * 0-1门：底噪极稳，设为 1.5，极度敏锐捕捉呼吸。
     * 2门及以后：易受旁边人员走动或多径反射干扰，拔高至 3.0 ~ 5.0 避免误触发。
     */
    double var_th_motion[16] = {
        1.5, 1.5, 3.0, 5.0, 
        5.0, 5.0, 5.0, 5.0,
        5.0, 5.0, 5.0, 5.0,
        5.0, 5.0, 5.0, 5.0
    };

    int monitor_gate = 0; /* 默认焦点：监视第 0 门 (桌面极近场区) */
    int is_radar_on = 1;  /* 系统软开关机标志位 (1: 工作中, 0: 待机挂起) */
    
    /* * 滑动窗口滤波机制 (Sliding Window FIFO) 
     * 用于缓存过去 1 秒 (10帧) 的历史数据，计算当前的均值与动态方差
     */
    double window[10] = {0}; 
    int win_idx = 0;     /* 环形队列写入指针 */
    int win_count = 0;   /* 队列有效数据量统计，满 10 帧才允许算法介入 */
    
    /* 数学统计计算缓存变量 */
    double sum, mean, var_sum, variance; 
    int i;
    
    /* 状态机抗干扰积分器 (核心时延控制器) */
    int count_in = 0;    /* 入座条件满足的累计帧数 */
    int count_out = 0;   /* 离座条件满足的累计帧数 */
    
    /* 空间分治逻辑的布尔触发器 (1代表满足条件，0代表不满足) */
    int condition_in = 0;
    int condition_out = 0;

    int print_counter = 0; /* 限制 UI 刷新率的计数器 */
    char key;              /* 键盘输入捕获变量 */

    /* 雷达硬件底层通信指令 (基于海凌科官方协议构造) */
    unsigned char cmd_power_off[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFF, 0x00, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01}; /* 挂起射频上报 */
    unsigned char cmd_power_on[]  = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0xFE, 0x00, 0x04, 0x03, 0x02, 0x01}; /* 恢复射频上报 */

    /* 状态机历史留痕变量，用于前后比对以触发跃迁 UI 弹窗 */
    PersonState current_state = STATE_AWAY;
    PersonState last_state = STATE_AWAY;
    /* ------------------------------------------------------------------------ */

    /* 初始化终端为 UTF-8 编码，彻底杜绝控制台中文乱码 */
    SetConsoleOutputCP(65001);
    printf("--- LD2420 智能桌面监测系统 (空间分治 + 抗多径干扰版) ---\n");
    printf("操作提示: [0-9, a-f]切换距离门  [r/R]软重启  [q/Q]待机  [s/S]唤醒\n\n");

    /* 步骤 1：打开并握手底层硬件串口 */
    hComm = open_serial(PORT_NAME, 115200);
    if (hComm == INVALID_HANDLE_VALUE) {
        printf("【致命错误】无法打开串口，请检查端口号或拔插雷达重启！\n");
        return -1;
    }

    /* 步骤 2：下发探测距离参数，并调用 Windows 原生 API 暴力清空启动瞬间产生的杂乱脏数据 */
    radar_configure_all(hComm, MAX_DISTANCE_GATE);
    PurgeComm(hComm, PURGE_RXCLEAR | PURGE_TXCLEAR); 

    printf("\n>>> 软硬件联调成功！系统已锚定观测门: %02d <<<\n\n", monitor_gate);

    /* 步骤 3：进入实时信号处理大循环 */
    while (1) {
        /* ==== 模块一：无阻塞人机交互 (非阻塞检测键盘击键) ==== */
        if (_kbhit()) {
            key = _getch(); 
            /* 注意：无论是切换门、还是重启唤醒，都必须将所有的滑动缓存和历史状态清零，防止数据交叉污染 */
            if (key >= '0' && key <= '9') { monitor_gate = key - '0'; win_idx = win_count = count_in = count_out = 0; current_state = last_state = STATE_AWAY; printf("\n>>> 观测锚点已切换至: %02d 门 <<<\n\n", monitor_gate); }
            else if (key >= 'a' && key <= 'f') { monitor_gate = key - 'a' + 10; win_idx = win_count = count_in = count_out = 0; current_state = last_state = STATE_AWAY; printf("\n>>> 观测锚点已切换至: %02d 门 <<<\n\n", monitor_gate); }
            else if (key == 'r' || key == 'R') { printf("\n[系统保护] 正在执行硬件级重启...\n"); radar_reboot(hComm); Sleep(2000); PurgeComm(hComm, PURGE_RXCLEAR | PURGE_TXCLEAR); radar_configure_all(hComm, MAX_DISTANCE_GATE); win_idx = win_count = count_in = count_out = 0; current_state = last_state = STATE_AWAY; is_radar_on = 1; printf("\n>>> 重启完成！系统重新锚定门: %02d <<<\n\n", monitor_gate); }
            else if (key == 'q' || key == 'Q') { printf("\n[功耗控制] 雷达已挂起待机。\n"); write_serial(hComm, cmd_power_off, sizeof(cmd_power_off)); is_radar_on = 0; }
            else if (key == 's' || key == 'S') { printf("\n[功耗控制] 正在唤醒射频模块...\n"); write_serial(hComm, cmd_power_on, sizeof(cmd_power_on)); Sleep(200); PurgeComm(hComm, PURGE_RXCLEAR | PURGE_TXCLEAR); win_idx = win_count = count_in = count_out = 0; current_state = last_state = STATE_AWAY; is_radar_on = 1; printf("\n>>> 唤醒就绪！系统继续锚定门: %02d <<<\n\n", monitor_gate); }
        }

        /* ==== 模块二：数据剥离与滑动滤波 ==== */
        if (is_radar_on && read_serial(hComm, rx_buffer, sizeof(rx_buffer)) > 0) {
            /* 调用协议层剥离一帧完美的雷达数据 */
            if (parse_radar_frame(rx_buffer, bytes_read, &radar_data)) {
                
                /* 将当前门的瞬时能量挤入滑动窗口 (FIFO 队列) */
                window[win_idx] = radar_data.energy_db[monitor_gate];
                win_idx = (win_idx + 1) % 10; 
                if (win_count < 10) win_count++;

                /* 攒齐 10 帧 (1秒) 数据后，开始执行数学统计与判定算法 */
                if (win_count == 10) {
                    /* 计算均值 (Mean)：反映整体能量大小 */
                    sum = var_sum = 0.0;
                    for (i = 0; i < 10; i++) sum += window[i];
                    mean = sum / 10.0;
                    
                    /* 计算方差 (Variance)：反映信号的起伏剧烈程度，是判断生命体征的核心 */
                    for (i = 0; i < 10; i++) var_sum += (window[i] - mean) * (window[i] - mean);
                    variance = var_sum / 10.0;

                    /* ================= 模块三：空间分治判定引擎 ================= */
                    if (monitor_gate <= 1) {
                        /* 【近场策略】0-1门：因为环境杂波极强，能量均值无效。
                         * 生与死绝对依赖方差，调用对应距离门的独立方差阈值。 */
                        condition_in  = (variance > var_th_motion[monitor_gate]);
                        condition_out = (variance < var_th_motion[monitor_gate]); 
                    } else {
                        /* 【远场策略】2-15门：存在被 0 门目标多径折射产生的高方差“幽灵”。
                         * 必须依靠真实的能量绝对值突破门限来判定入座；离座要求能量暴跌且方差平息（低于独立阈值）。 */
                        condition_in  = (mean > trigger_th[monitor_gate]);
                        condition_out = (mean < maintain_th[monitor_gate] && variance < var_th_motion[monitor_gate]);
                    }
                    /* ========================================================== */

                    /* ==== 模块四：漏桶积分器与三态状态机 ==== */
                    if (current_state == STATE_AWAY) {
                        /* 漏桶算法 (Leaky Bucket)：
                         * 满足条件则积分 +1；不满足时采用“-1”轻度惩罚，代替一票否决清零。
                         * 极大提高了抗干扰连贯性，允许入座动作中有 1-2 秒的停顿。 */
                        if (condition_in) {
                            count_in++;
                        } else {
                            if (count_in > 0) count_in -= 1; 
                            if (count_in < 0) count_in = 0; /* 跌底保护 */
                        }

                        /* 积分满 10 秒即跃迁至“入座” */
                        if (count_in >= TIME_IN_FRAMES) { 
                            current_state = STATE_NORMAL;
                            count_in = 0; 
                        }
                    } 
                    else { 
                        /* 离座判断依然严苛：
                         * 只要不满足离座条件（如稍微喘了口气），瞬间清零倒计时！
                         * 配合 30 秒的超时，形成巨大的“防抖死区”，对抗发呆或屏息。 */
                        if (condition_out) {
                            count_out++;
                        } else {
                            count_out = 0; 
                        }

                        if (count_out >= TIME_OUT_FRAMES) { 
                            current_state = STATE_AWAY;
                            count_out = 0;
                        } else {
                            /* 人在座位上时，通过乱动方差幅度(固定大值)进行动作级别分类 */
                            if (variance > VAR_TH_FIDGET) current_state = STATE_FIDGET;
                            else current_state = STATE_NORMAL;
                        }
                    }

                    /* ==== 模块五：UI 表现层 ==== */
                    /* 事件中断打印：只有状态发生跨越时，才生成醒目的全屏通告日志 */
                    if (current_state != last_state) {
                        printf("\n=======================================================================\n");
                        if (current_state == STATE_AWAY) printf("  [EVENT] 目标区域失去生命体征逾 30 秒，系统决策 ---> 【人员已彻底离开】\n");
                        else if (last_state == STATE_AWAY) printf("  [EVENT] 持续侦测到有效生物活动逾 10 秒，系统决策 ---> 【人员已入座】\n");
                        else if (current_state == STATE_FIDGET) printf("  [EVENT] 目标反射截面积发生剧烈变化，状态更新为   ---> 【大幅乱动】\n");
                        else if (current_state == STATE_NORMAL) printf("  [EVENT] 目标体征参数回落至日常阈值，状态更新为   ---> 【安静微动】\n");
                        printf("=======================================================================\n\n");
                        last_state = current_state; 
                    }

                    /* 常规流水打印：利用 print_counter 取模降频 (10Hz降到1Hz) */
                    print_counter++;
                    if (print_counter >= 10) {
                        print_counter = 0;
                        
                        /* 获取当前系统时间并转换为本地时间结构 */
                        time(&rawtime);
                        timeinfo = localtime(&rawtime);

                        /* 输出格式化带时间戳的精致日志 */
                        printf("[%04d.%02d.%02d %02d:%02d:%02d]  系统判定距: %3d cm  |  观测门: %02d  |  平滑均值: %5.1f dB  |  动态方差: %5.1f  |  当前结论: %s\n",
                               timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                               timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec,
                               radar_data.distance_cm, monitor_gate, mean, variance,
                               current_state == STATE_AWAY ? "没人" : (current_state == STATE_NORMAL ? "微动" : "乱动"));
                    }
                }
            }
        }
        /* 强制让出系统时间片，防止 While 死循环占满单核 CPU 资源 */
        Sleep(10);
    }
    
    close_serial(hComm);
    return 0;
}