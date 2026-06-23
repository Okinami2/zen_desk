#include "asr.h"
extern "C"{ void * __dso_handle = 0 ;}
#include "setup.h"
#include "HardwareSerial.h"

uint32_t snid;
void ASR_CODE();
void app();

//{speak:小蝶-清新女声,vol:1,speed:10,platform:haohaodada}
//{playid:10001,voice:，}
//{playid:10002,voice:，}
/*
 * Zen Desk ASRPRO 离线语音识别固件代码
 * 这里利用了“同义词泛化”策略，为每个动作设定了足足 20 个极度口语化的语义别名
 * 它们在 C 语言中利用 switch case 穿透，映射到同一个单字节指令。
 */
void ASR_CODE() {
  // 语音识别成功时自动调用，保持唤醒状态 15 秒
  set_state_enter_wakeup(15000);
  
  // 通过串口发送单字节 Hex 指令，匹配海鸥派 Linux 端的 asr_controller 解析规则
  switch (snid) {
    // ============ 0. 唤醒词 ============
    case 0:
    case 1:
    case 2:
      Serial.write(0x00);
      break;

    // ============ 1. 灯光控制系列 ============
    // 开灯系列 (0x11)
    case 10: case 11: case 12: case 13: case 14: 
    case 15: case 16: case 17: case 18: case 19: 
    case 20: case 21: case 22: case 23: case 24:
    case 25: case 26: case 27: case 28: case 29:
      Serial.write(0x11);
      break;
    
    // 关灯系列 (0x12)
    case 30: case 31: case 32: case 33: case 34: 
    case 35: case 36: case 37: case 38: case 39: 
    case 40: case 41: case 42: case 43: case 44:
    case 45: case 46: case 47: case 48: case 49:
      Serial.write(0x12);
      break;
    
    // 调高亮度系列 (0x13)
    case 50: case 51: case 52: case 53: case 54: 
    case 55: case 56: case 57: case 58: case 59: 
    case 60: case 61: case 62: case 63: case 64:
    case 65: case 66: case 67: case 68: case 69:
      Serial.write(0x13);
      break;
    
    // 调低亮度系列 (0x14)
    case 70: case 71: case 72: case 73: case 74: 
    case 75: case 76: case 77: case 78: case 79: 
    case 80: case 81: case 82: case 83: case 84:
    case 85: case 86: case 87: case 88: case 89:
      Serial.write(0x14);
      break;

    // ============ 2. 专注与学习系列 ============
    // 开始专注系列 (普通正计时) (0x21)
    case 90: case 91: case 92: case 93: case 94: 
    case 95: case 96: case 97: case 98: case 99: 
    case 100: case 101: case 102: case 103: case 104:
    case 105: case 106: case 107: case 108: case 109:
      Serial.write(0x21);
      break;
    
    // 结束专注系列 (0x22)
    case 110: case 111: case 112: case 113: case 114: 
    case 115: case 116: case 117: case 118: case 119: 
    case 120: case 121: case 122: case 123: case 124:
    case 125: case 126: case 127: case 128: case 129:
      Serial.write(0x22);
      break;
    
    // 暂停专注系列 (0x23)
    case 130: case 131: case 132: case 133: case 134: 
    case 135: case 136: case 137: case 138: case 139: 
    case 140: case 141: case 142: case 143: case 144:
    case 145: case 146: case 147: case 148: case 149:
      Serial.write(0x23);
      break;
    
    // 恢复专注系列 (0x24)
    case 150: case 151: case 152: case 153: case 154: 
    case 155: case 156: case 157: case 158: case 159: 
    case 160: case 161: case 162: case 163: case 164:
    case 165: case 166: case 167: case 168: case 169:
      Serial.write(0x24);
      break;

    // ============ 3. 定时专注系列 ============
    // 25分钟番茄钟 (0x25)
    case 170: case 171: case 172: case 173: case 174: 
    case 175: case 176: case 177: case 178: case 179: 
    case 180: case 181: case 182: case 183: case 184:
    case 185: case 186: case 187: case 188: case 189:
      Serial.write(0x25);
      break;
    
    // 45分钟一节课 (0x26)
    case 190: case 191: case 192: case 193: case 194: 
    case 195: case 196: case 197: case 198: case 199: 
    case 200: case 201: case 202: case 203: case 204:
    case 205: case 206: case 207: case 208: case 209:
      Serial.write(0x26);
      break;
    
    // 60分钟长时效 (0x27)
    case 210: case 211: case 212: case 213: case 214: 
    case 215: case 216: case 217: case 218: case 219: 
    case 220: case 221: case 222: case 223: case 224:
    case 225: case 226: case 227: case 228: case 229:
      Serial.write(0x27);
      break;

    // ============ 4. 屏幕控制系列 ============
    // 查看数据页 (0x31)
    case 230: case 231: case 232: case 233: case 234: 
    case 235: case 236: case 237: case 238: case 239: 
    case 240: case 241: case 242: case 243: case 244:
    case 245: case 246: case 247: case 248: case 249:
      Serial.write(0x31);
      break;
    
    // 回到主页 (0x32)
    case 250: case 251: case 252: case 253: case 254: 
    case 255: case 256: case 257: case 258: case 259: 
    case 260: case 261: case 262: case 263: case 264:
    case 265: case 266: case 267: case 268: case 269:
      Serial.write(0x32);
      break;
  }
}

void app() {
  // 主循环，处理后台任务
  while (1) {
    delay(100);
  }
  vTaskDelete(NULL);
}

void hardware_init() {
  // 操作系统启动后初始化
  // 将底层硬件功放音量调到最低 (通常1是最小声音)
  vol_set(1);
  // 这里将 RX(13) TX(14) 设为串口模式 (根据实际板子修改引脚)
  setPinFun(13, SECOND_FUNCTION);
  setPinFun(14, SECOND_FUNCTION);
  
  // 海鸥派 Linux 端设定的波特率是 9600
  Serial.begin(9600);
  
  xTaskCreate(app, "app", 128, NULL, 4, NULL);
  vTaskDelete(NULL);
}

void setup() {
  // ==============================================================
  // 核心！天问Block要求每个词条的 ID 必须唯一！
  // 此处为每个指令泛化了足足 20 种高频口语表达，且共用同一组回应
  // ==============================================================
  
  // ---- 唤醒词 ----
  //{ID:0,keyword:"唤醒词",ASR:"慧学引擎",ASRTO:"我在呢"}
  //{ID:1,keyword:"唤醒词",ASR:"智能管家",ASRTO:"很高兴为您服务"}
  //{ID:2,keyword:"唤醒词",ASR:"小艺小艺",ASRTO:"我在呢"}

  // ---- 场景 1: 开灯 (20个别名) ----
  //{ID:10,keyword:"命令词",ASR:"打开台灯",ASRTO:"已为您打开台灯"}
  //{ID:11,keyword:"命令词",ASR:"开灯",ASRTO:"已为您打开台灯"}
  //{ID:12,keyword:"命令词",ASR:"太暗了",ASRTO:"已为您打开台灯"}
  //{ID:13,keyword:"命令词",ASR:"给我点光",ASRTO:"已为您打开台灯"}
  //{ID:14,keyword:"命令词",ASR:"我看不清了",ASRTO:"已为您打开台灯"}
  //{ID:15,keyword:"命令词",ASR:"把灯打开",ASRTO:"已为您打开台灯"}
  //{ID:16,keyword:"命令词",ASR:"光线太差",ASRTO:"已为您打开台灯"}
  //{ID:17,keyword:"命令词",ASR:"点亮台灯",ASRTO:"已为您打开台灯"}
  //{ID:18,keyword:"命令词",ASR:"我需要光照",ASRTO:"已为您打开台灯"}
  //{ID:19,keyword:"命令词",ASR:"这里好黑",ASRTO:"已为您打开台灯"}
  //{ID:20,keyword:"命令词",ASR:"光线暗了",ASRTO:"已为您打开台灯"}
  //{ID:21,keyword:"命令词",ASR:"帮我打开灯",ASRTO:"已为您打开台灯"}
  //{ID:22,keyword:"命令词",ASR:"开启照明",ASRTO:"已为您打开台灯"}
  //{ID:23,keyword:"命令词",ASR:"帮我开下灯",ASRTO:"已为您打开台灯"}
  //{ID:24,keyword:"命令词",ASR:"启动灯光",ASRTO:"已为您打开台灯"}
  //{ID:25,keyword:"命令词",ASR:"房间太黑了",ASRTO:"已为您打开台灯"}
  //{ID:26,keyword:"命令词",ASR:"怎么这么黑",ASRTO:"已为您打开台灯"}
  //{ID:27,keyword:"命令词",ASR:"快点开灯",ASRTO:"已为您打开台灯"}
  //{ID:28,keyword:"命令词",ASR:"打开灯光",ASRTO:"已为您打开台灯"}
  //{ID:29,keyword:"命令词",ASR:"恢复照明",ASRTO:"已为您打开台灯"}

  // ---- 场景 1: 关灯 (20个别名) ----
  //{ID:30,keyword:"命令词",ASR:"关闭台灯",ASRTO:"已为您关闭台灯"}
  //{ID:31,keyword:"命令词",ASR:"关灯",ASRTO:"已为您关闭台灯"}
  //{ID:32,keyword:"命令词",ASR:"太亮了",ASRTO:"已为您关闭台灯"}
  //{ID:33,keyword:"命令词",ASR:"把灯关了",ASRTO:"已为您关闭台灯"}
  //{ID:34,keyword:"命令词",ASR:"不需要光",ASRTO:"已为您关闭台灯"}
  //{ID:35,keyword:"命令词",ASR:"有点刺眼",ASRTO:"已为您关闭台灯"}
  //{ID:36,keyword:"命令词",ASR:"关掉台灯",ASRTO:"已为您关闭台灯"}
  //{ID:37,keyword:"命令词",ASR:"帮我关灯",ASRTO:"已为您关闭台灯"}
  //{ID:38,keyword:"命令词",ASR:"熄灭灯光",ASRTO:"已为您关闭台灯"}
  //{ID:39,keyword:"命令词",ASR:"停止照明",ASRTO:"已为您关闭台灯"}
  //{ID:40,keyword:"命令词",ASR:"光线太强了",ASRTO:"已为您关闭台灯"}
  //{ID:41,keyword:"命令词",ASR:"眼睛被晃到了",ASRTO:"已为您关闭台灯"}
  //{ID:42,keyword:"命令词",ASR:"现在不需要台灯了",ASRTO:"已为您关闭台灯"}
  //{ID:43,keyword:"命令词",ASR:"把台灯关上",ASRTO:"已为您关闭台灯"}
  //{ID:44,keyword:"命令词",ASR:"帮我关下灯",ASRTO:"已为您关闭台灯"}
  //{ID:45,keyword:"命令词",ASR:"房间太亮了",ASRTO:"已为您关闭台灯"}
  //{ID:46,keyword:"命令词",ASR:"帮我把灯关了",ASRTO:"已为您关闭台灯"}
  //{ID:47,keyword:"命令词",ASR:"关闭灯光",ASRTO:"已为您关闭台灯"}
  //{ID:48,keyword:"命令词",ASR:"晃到眼睛了",ASRTO:"已为您关闭台灯"}
  //{ID:49,keyword:"命令词",ASR:"可以关灯了",ASRTO:"已为您关闭台灯"}

  // ---- 场景 1: 调高亮度 (20个别名) ----
  //{ID:50,keyword:"命令词",ASR:"调高亮度",ASRTO:"已为您调高亮度"}
  //{ID:51,keyword:"命令词",ASR:"亮一点",ASRTO:"已为您调高亮度"}
  //{ID:52,keyword:"命令词",ASR:"再亮一点",ASRTO:"已为您调高亮度"}
  //{ID:53,keyword:"命令词",ASR:"把灯调亮",ASRTO:"已为您调高亮度"}
  //{ID:54,keyword:"命令词",ASR:"不够亮",ASRTO:"已为您调高亮度"}
  //{ID:55,keyword:"命令词",ASR:"增加亮度",ASRTO:"已为您调高亮度"}
  //{ID:56,keyword:"命令词",ASR:"灯光太微弱了",ASRTO:"已为您调高亮度"}
  //{ID:57,keyword:"命令词",ASR:"把光线调亮",ASRTO:"已为您调高亮度"}
  //{ID:58,keyword:"命令词",ASR:"灯不够亮",ASRTO:"已为您调高亮度"}
  //{ID:59,keyword:"命令词",ASR:"亮度再高一点",ASRTO:"已为您调高亮度"}
  //{ID:60,keyword:"命令词",ASR:"再加点亮度",ASRTO:"已为您调高亮度"}
  //{ID:61,keyword:"命令词",ASR:"稍微亮一点",ASRTO:"已为您调高亮度"}
  //{ID:62,keyword:"命令词",ASR:"把台灯调亮",ASRTO:"已为您调高亮度"}
  //{ID:63,keyword:"命令词",ASR:"需要更亮的光",ASRTO:"已为您调高亮度"}
  //{ID:64,keyword:"命令词",ASR:"调高点亮度",ASRTO:"已为您调高亮度"}
  //{ID:65,keyword:"命令词",ASR:"还可以再亮一点吗",ASRTO:"已为您调高亮度"}
  //{ID:66,keyword:"命令词",ASR:"加大亮度",ASRTO:"已为您调高亮度"}
  //{ID:67,keyword:"命令词",ASR:"把亮度调到最高",ASRTO:"已为您调高亮度"}
  //{ID:68,keyword:"命令词",ASR:"台灯太暗了",ASRTO:"已为您调高亮度"}
  //{ID:69,keyword:"命令词",ASR:"我需要最亮的光",ASRTO:"已为您调高亮度"}

  // ---- 场景 1: 调低亮度 (20个别名) ----
  //{ID:70,keyword:"命令词",ASR:"调低亮度",ASRTO:"已为您调低亮度"}
  //{ID:71,keyword:"命令词",ASR:"暗一点",ASRTO:"已为您调低亮度"}
  //{ID:72,keyword:"命令词",ASR:"再暗一点",ASRTO:"已为您调低亮度"}
  //{ID:73,keyword:"命令词",ASR:"把灯调暗",ASRTO:"已为您调低亮度"}
  //{ID:74,keyword:"命令词",ASR:"亮度太高了",ASRTO:"已为您调低亮度"}
  //{ID:75,keyword:"命令词",ASR:"降低亮度",ASRTO:"已为您调低亮度"}
  //{ID:76,keyword:"命令词",ASR:"灯太刺眼了",ASRTO:"已为您调低亮度"}
  //{ID:77,keyword:"命令词",ASR:"把光线调暗",ASRTO:"已为您调低亮度"}
  //{ID:78,keyword:"命令词",ASR:"亮度降一点",ASRTO:"已为您调低亮度"}
  //{ID:79,keyword:"命令词",ASR:"稍微暗一点",ASRTO:"已为您调低亮度"}
  //{ID:80,keyword:"命令词",ASR:"把台灯调暗",ASRTO:"已为您调低亮度"}
  //{ID:81,keyword:"命令词",ASR:"减少一点光",ASRTO:"已为您调低亮度"}
  //{ID:82,keyword:"命令词",ASR:"太亮了调暗点",ASRTO:"已为您调低亮度"}
  //{ID:83,keyword:"命令词",ASR:"降低点亮度",ASRTO:"已为您调低亮度"}
  //{ID:84,keyword:"命令词",ASR:"调低点亮度",ASRTO:"已为您调低亮度"}
  //{ID:85,keyword:"命令词",ASR:"可以稍微暗一点吗",ASRTO:"已为您调低亮度"}
  //{ID:86,keyword:"命令词",ASR:"减小亮度",ASRTO:"已为您调低亮度"}
  //{ID:87,keyword:"命令词",ASR:"把亮度调到最低",ASRTO:"已为您调低亮度"}
  //{ID:88,keyword:"命令词",ASR:"台灯太刺眼了",ASRTO:"已为您调低亮度"}
  //{ID:89,keyword:"命令词",ASR:"降点光",ASRTO:"已为您调低亮度"}

  // ---- 场景 2: 开始专注 (20个别名) ----
  //{ID:90,keyword:"命令词",ASR:"开始专注",ASRTO:"专注计时已开始"}
  //{ID:91,keyword:"命令词",ASR:"开始学习",ASRTO:"专注计时已开始"}
  //{ID:92,keyword:"命令词",ASR:"计时开始",ASRTO:"专注计时已开始"}
  //{ID:93,keyword:"命令词",ASR:"我要学习了",ASRTO:"专注计时已开始"}
  //{ID:94,keyword:"命令词",ASR:"开始工作",ASRTO:"专注计时已开始"}
  //{ID:95,keyword:"命令词",ASR:"进入学习模式",ASRTO:"专注计时已开始"}
  //{ID:96,keyword:"命令词",ASR:"记录我的时间",ASRTO:"专注计时已开始"}
  //{ID:97,keyword:"命令词",ASR:"正计时开始",ASRTO:"专注计时已开始"}
  //{ID:98,keyword:"命令词",ASR:"我要看书了",ASRTO:"专注计时已开始"}
  //{ID:99,keyword:"命令词",ASR:"开始记录时间",ASRTO:"专注计时已开始"}
  //{ID:100,keyword:"命令词",ASR:"准备学习了",ASRTO:"专注计时已开始"}
  //{ID:101,keyword:"命令词",ASR:"帮我计时",ASRTO:"专注计时已开始"}
  //{ID:102,keyword:"命令词",ASR:"开始专注模式",ASRTO:"专注计时已开始"}
  //{ID:103,keyword:"命令词",ASR:"我要开始看书了",ASRTO:"专注计时已开始"}
  //{ID:104,keyword:"命令词",ASR:"进入专注状态",ASRTO:"专注计时已开始"}
  //{ID:105,keyword:"命令词",ASR:"开始记录",ASRTO:"专注计时已开始"}
  //{ID:106,keyword:"命令词",ASR:"进入工作状态",ASRTO:"专注计时已开始"}
  //{ID:107,keyword:"命令词",ASR:"开启正计时",ASRTO:"专注计时已开始"}
  //{ID:108,keyword:"命令词",ASR:"专注模式启动",ASRTO:"专注计时已开始"}
  //{ID:109,keyword:"命令词",ASR:"现在开始学习",ASRTO:"专注计时已开始"}

  // ---- 场景 2: 结束专注 (20个别名) ----
  //{ID:110,keyword:"命令词",ASR:"结束专注",ASRTO:"已结束专注计时"}
  //{ID:111,keyword:"命令词",ASR:"停止学习",ASRTO:"已结束专注计时"}
  //{ID:112,keyword:"命令词",ASR:"不学了",ASRTO:"已结束专注计时"}
  //{ID:113,keyword:"命令词",ASR:"结束工作",ASRTO:"已结束专注计时"}
  //{ID:114,keyword:"命令词",ASR:"下课",ASRTO:"已结束专注计时"}
  //{ID:115,keyword:"命令词",ASR:"停止计时",ASRTO:"已结束专注计时"}
  //{ID:116,keyword:"命令词",ASR:"结束计时",ASRTO:"已结束专注计时"}
  //{ID:117,keyword:"命令词",ASR:"学习结束",ASRTO:"已结束专注计时"}
  //{ID:118,keyword:"命令词",ASR:"今天学完了",ASRTO:"已结束专注计时"}
  //{ID:119,keyword:"命令词",ASR:"结束学习模式",ASRTO:"已结束专注计时"}
  //{ID:120,keyword:"命令词",ASR:"我学完了",ASRTO:"已结束专注计时"}
  //{ID:121,keyword:"命令词",ASR:"停止专注模式",ASRTO:"已结束专注计时"}
  //{ID:122,keyword:"命令词",ASR:"帮我停止计时",ASRTO:"已结束专注计时"}
  //{ID:123,keyword:"命令词",ASR:"关闭计时器",ASRTO:"已结束专注计时"}
  //{ID:124,keyword:"命令词",ASR:"结算我的时间",ASRTO:"已结束专注计时"}
  //{ID:125,keyword:"命令词",ASR:"关闭专注模式",ASRTO:"已结束专注计时"}
  //{ID:126,keyword:"命令词",ASR:"工作结束了",ASRTO:"已结束专注计时"}
  //{ID:127,keyword:"命令词",ASR:"我要下班了",ASRTO:"已结束专注计时"}
  //{ID:128,keyword:"命令词",ASR:"取消计时",ASRTO:"已结束专注计时"}
  //{ID:129,keyword:"命令词",ASR:"清除计时",ASRTO:"已结束专注计时"}

  // ---- 场景 2: 稍微休息 (20个别名) ----
  //{ID:130,keyword:"命令词",ASR:"稍微休息",ASRTO:"专注已暂停"}
  //{ID:131,keyword:"命令词",ASR:"暂停一下",ASRTO:"专注已暂停"}
  //{ID:132,keyword:"命令词",ASR:"暂停学习",ASRTO:"专注已暂停"}
  //{ID:133,keyword:"命令词",ASR:"我要喝水",ASRTO:"专注已暂停"}
  //{ID:134,keyword:"命令词",ASR:"暂停计时",ASRTO:"专注已暂停"}
  //{ID:135,keyword:"命令词",ASR:"休息一会",ASRTO:"专注已暂停"}
  //{ID:136,keyword:"命令词",ASR:"中场休息",ASRTO:"专注已暂停"}
  //{ID:137,keyword:"命令词",ASR:"让我歇会",ASRTO:"专注已暂停"}
  //{ID:138,keyword:"命令词",ASR:"暂停专注模式",ASRTO:"专注已暂停"}
  //{ID:139,keyword:"命令词",ASR:"暂停记录时间",ASRTO:"专注已暂停"}
  //{ID:140,keyword:"命令词",ASR:"我去上个厕所",ASRTO:"专注已暂停"}
  //{ID:141,keyword:"命令词",ASR:"我出去一下",ASRTO:"专注已暂停"}
  //{ID:142,keyword:"命令词",ASR:"暂时停止计时",ASRTO:"专注已暂停"}
  //{ID:143,keyword:"命令词",ASR:"帮我暂停一下",ASRTO:"专注已暂停"}
  //{ID:144,keyword:"命令词",ASR:"休息五分钟",ASRTO:"专注已暂停"}
  //{ID:145,keyword:"命令词",ASR:"暂停工作",ASRTO:"专注已暂停"}
  //{ID:146,keyword:"命令词",ASR:"歇一口气",ASRTO:"专注已暂停"}
  //{ID:147,keyword:"命令词",ASR:"中断一下",ASRTO:"专注已暂停"}
  //{ID:148,keyword:"命令词",ASR:"暂时离开",ASRTO:"专注已暂停"}
  //{ID:149,keyword:"命令词",ASR:"帮我挂起计时",ASRTO:"专注已暂停"}

  // ---- 场景 2: 恢复学习 (20个别名) ----
  //{ID:150,keyword:"命令词",ASR:"恢复学习",ASRTO:"专注已恢复"}
  //{ID:151,keyword:"命令词",ASR:"继续专注",ASRTO:"专注已恢复"}
  //{ID:152,keyword:"命令词",ASR:"接着学",ASRTO:"专注已恢复"}
  //{ID:153,keyword:"命令词",ASR:"继续工作",ASRTO:"专注已恢复"}
  //{ID:154,keyword:"命令词",ASR:"继续计时",ASRTO:"专注已恢复"}
  //{ID:155,keyword:"命令词",ASR:"恢复计时",ASRTO:"专注已恢复"}
  //{ID:156,keyword:"命令词",ASR:"我回来了",ASRTO:"专注已恢复"}
  //{ID:157,keyword:"命令词",ASR:"开始继续学习",ASRTO:"专注已恢复"}
  //{ID:158,keyword:"命令词",ASR:"接着看书",ASRTO:"专注已恢复"}
  //{ID:159,keyword:"命令词",ASR:"继续我的计时",ASRTO:"专注已恢复"}
  //{ID:160,keyword:"命令词",ASR:"恢复专注模式",ASRTO:"专注已恢复"}
  //{ID:161,keyword:"命令词",ASR:"帮我继续计时",ASRTO:"专注已恢复"}
  //{ID:162,keyword:"命令词",ASR:"接着刚刚的进度",ASRTO:"专注已恢复"}
  //{ID:163,keyword:"命令词",ASR:"继续开始",ASRTO:"专注已恢复"}
  //{ID:164,keyword:"命令词",ASR:"学习继续",ASRTO:"专注已恢复"}
  //{ID:165,keyword:"命令词",ASR:"恢复工作",ASRTO:"专注已恢复"}
  //{ID:166,keyword:"命令词",ASR:"取消暂停",ASRTO:"专注已恢复"}
  //{ID:167,keyword:"命令词",ASR:"接着倒计时",ASRTO:"专注已恢复"}
  //{ID:168,keyword:"命令词",ASR:"继续正计时",ASRTO:"专注已恢复"}
  //{ID:169,keyword:"命令词",ASR:"从刚才开始计时",ASRTO:"专注已恢复"}

  // ---- 场景 3: 番茄钟 (20个别名) ----
  //{ID:170,keyword:"命令词",ASR:"定时二十五分钟",ASRTO:"二十五分钟番茄钟已开启"}
  //{ID:171,keyword:"命令词",ASR:"番茄钟",ASRTO:"二十五分钟番茄钟已开启"}
  //{ID:172,keyword:"命令词",ASR:"来个番茄钟",ASRTO:"二十五分钟番茄钟已开启"}
  //{ID:173,keyword:"命令词",ASR:"二十五分钟倒计时",ASRTO:"二十五分钟番茄钟已开启"}
  //{ID:174,keyword:"命令词",ASR:"开始番茄钟",ASRTO:"二十五分钟番茄钟已开启"}
  //{ID:175,keyword:"命令词",ASR:"定个番茄钟",ASRTO:"二十五分钟番茄钟已开启"}
  //{ID:176,keyword:"命令词",ASR:"二十五分钟计时",ASRTO:"二十五分钟番茄钟已开启"}
  //{ID:177,keyword:"命令词",ASR:"倒计时二十五分钟",ASRTO:"二十五分钟番茄钟已开启"}
  //{ID:178,keyword:"命令词",ASR:"开启番茄工作法",ASRTO:"二十五分钟番茄钟已开启"}
  //{ID:179,keyword:"命令词",ASR:"番茄钟开始",ASRTO:"二十五分钟番茄钟已开启"}
  //{ID:180,keyword:"命令词",ASR:"帮我定个番茄钟",ASRTO:"二十五分钟番茄钟已开启"}
  //{ID:181,keyword:"命令词",ASR:"设定二十五分钟",ASRTO:"二十五分钟番茄钟已开启"}
  //{ID:182,keyword:"命令词",ASR:"二十五分钟专注",ASRTO:"二十五分钟番茄钟已开启"}
  //{ID:183,keyword:"命令词",ASR:"一个番茄钟",ASRTO:"二十五分钟番茄钟已开启"}
  //{ID:184,keyword:"命令词",ASR:"番茄工作法启动",ASRTO:"二十五分钟番茄钟已开启"}
  //{ID:185,keyword:"命令词",ASR:"帮我定个二十五分钟的闹钟",ASRTO:"二十五分钟番茄钟已开启"}
  //{ID:186,keyword:"命令词",ASR:"二十五分钟学习",ASRTO:"二十五分钟番茄钟已开启"}
  //{ID:187,keyword:"命令词",ASR:"定时二十五",ASRTO:"二十五分钟番茄钟已开启"}
  //{ID:188,keyword:"命令词",ASR:"开启番茄时间",ASRTO:"二十五分钟番茄钟已开启"}
  //{ID:189,keyword:"命令词",ASR:"给我一个番茄钟",ASRTO:"二十五分钟番茄钟已开启"}

  // ---- 场景 3: 45分钟一节课 (20个别名) ----
  //{ID:190,keyword:"命令词",ASR:"定时四十五分钟",ASRTO:"四十五分钟倒计时已开启"}
  //{ID:191,keyword:"命令词",ASR:"一节课",ASRTO:"四十五分钟倒计时已开启"}
  //{ID:192,keyword:"命令词",ASR:"来一节课",ASRTO:"四十五分钟倒计时已开启"}
  //{ID:193,keyword:"命令词",ASR:"四十五分钟倒计时",ASRTO:"四十五分钟倒计时已开启"}
  //{ID:194,keyword:"命令词",ASR:"开始一节课",ASRTO:"四十五分钟倒计时已开启"}
  //{ID:195,keyword:"命令词",ASR:"定一节课时间",ASRTO:"四十五分钟倒计时已开启"}
  //{ID:196,keyword:"命令词",ASR:"四十五分钟计时",ASRTO:"四十五分钟倒计时已开启"}
  //{ID:197,keyword:"命令词",ASR:"倒计时四十五分钟",ASRTO:"四十五分钟倒计时已开启"}
  //{ID:198,keyword:"命令词",ASR:"开启一节课模式",ASRTO:"四十五分钟倒计时已开启"}
  //{ID:199,keyword:"命令词",ASR:"一节课倒计时",ASRTO:"四十五分钟倒计时已开启"}
  //{ID:200,keyword:"命令词",ASR:"设定四十五分钟",ASRTO:"四十五分钟倒计时已开启"}
  //{ID:201,keyword:"命令词",ASR:"帮我定一节课",ASRTO:"四十五分钟倒计时已开启"}
  //{ID:202,keyword:"命令词",ASR:"学习四十五分钟",ASRTO:"四十五分钟倒计时已开启"}
  //{ID:203,keyword:"命令词",ASR:"四十五分钟专注",ASRTO:"四十五分钟倒计时已开启"}
  //{ID:204,keyword:"命令词",ASR:"上课了",ASRTO:"四十五分钟倒计时已开启"}
  //{ID:205,keyword:"命令词",ASR:"帮我定个四十五分钟的闹钟",ASRTO:"四十五分钟倒计时已开启"}
  //{ID:206,keyword:"命令词",ASR:"四十五分钟学习",ASRTO:"四十五分钟倒计时已开启"}
  //{ID:207,keyword:"命令词",ASR:"定时四十五",ASRTO:"四十五分钟倒计时已开启"}
  //{ID:208,keyword:"命令词",ASR:"开启一堂课的时间",ASRTO:"四十五分钟倒计时已开启"}
  //{ID:209,keyword:"命令词",ASR:"给我一节课的时间",ASRTO:"四十五分钟倒计时已开启"}

  // ---- 场景 3: 60分钟长时效 (20个别名) ----
  //{ID:210,keyword:"命令词",ASR:"定时一个小时",ASRTO:"六十分钟长时专注已开启"}
  //{ID:211,keyword:"命令词",ASR:"定时六十分钟",ASRTO:"六十分钟长时专注已开启"}
  //{ID:212,keyword:"命令词",ASR:"专注一个小时",ASRTO:"六十分钟长时专注已开启"}
  //{ID:213,keyword:"命令词",ASR:"一个小时倒计时",ASRTO:"六十分钟长时专注已开启"}
  //{ID:214,keyword:"命令词",ASR:"六十分钟倒计时",ASRTO:"六十分钟长时专注已开启"}
  //{ID:215,keyword:"命令词",ASR:"开始一个小时",ASRTO:"六十分钟长时专注已开启"}
  //{ID:216,keyword:"命令词",ASR:"倒计时一个小时",ASRTO:"六十分钟长时专注已开启"}
  //{ID:217,keyword:"命令词",ASR:"倒计时六十分钟",ASRTO:"六十分钟长时专注已开启"}
  //{ID:218,keyword:"命令词",ASR:"一个小时专注",ASRTO:"六十分钟长时专注已开启"}
  //{ID:219,keyword:"命令词",ASR:"设定一个小时",ASRTO:"六十分钟长时专注已开启"}
  //{ID:220,keyword:"命令词",ASR:"设定六十分钟",ASRTO:"六十分钟长时专注已开启"}
  //{ID:221,keyword:"命令词",ASR:"帮我定一个小时",ASRTO:"六十分钟长时专注已开启"}
  //{ID:222,keyword:"命令词",ASR:"学习一个小时",ASRTO:"六十分钟长时专注已开启"}
  //{ID:223,keyword:"命令词",ASR:"六十分钟计时",ASRTO:"六十分钟长时专注已开启"}
  //{ID:224,keyword:"命令词",ASR:"长时间专注",ASRTO:"六十分钟长时专注已开启"}
  //{ID:225,keyword:"命令词",ASR:"帮我定个一个小时的闹钟",ASRTO:"六十分钟长时专注已开启"}
  //{ID:226,keyword:"命令词",ASR:"六十分钟学习",ASRTO:"六十分钟长时专注已开启"}
  //{ID:227,keyword:"命令词",ASR:"定时六十",ASRTO:"六十分钟长时专注已开启"}
  //{ID:228,keyword:"命令词",ASR:"开启一小时专注",ASRTO:"六十分钟长时专注已开启"}
  //{ID:229,keyword:"命令词",ASR:"给我一个小时的时间",ASRTO:"六十分钟长时专注已开启"}

  // ---- 场景 4: 屏幕查看数据 (20个别名) ----
  //{ID:230,keyword:"命令词",ASR:"查看数据",ASRTO:"正在为您展示今日数据"}
  //{ID:231,keyword:"命令词",ASR:"今天学了多久",ASRTO:"正在为您展示今日数据"}
  //{ID:232,keyword:"命令词",ASR:"我的数据",ASRTO:"正在为您展示今日数据"}
  //{ID:233,keyword:"命令词",ASR:"学习记录",ASRTO:"正在为您展示今日数据"}
  //{ID:234,keyword:"命令词",ASR:"打开数据看板",ASRTO:"正在为您展示今日数据"}
  //{ID:235,keyword:"命令词",ASR:"展示数据",ASRTO:"正在为您展示今日数据"}
  //{ID:236,keyword:"命令词",ASR:"数据面板",ASRTO:"正在为您展示今日数据"}
  //{ID:237,keyword:"命令词",ASR:"我的学习记录",ASRTO:"正在为您展示今日数据"}
  //{ID:238,keyword:"命令词",ASR:"查看今日数据",ASRTO:"正在为您展示今日数据"}
  //{ID:239,keyword:"命令词",ASR:"打开面板",ASRTO:"正在为您展示今日数据"}
  //{ID:240,keyword:"命令词",ASR:"我今天专注了多久",ASRTO:"正在为您展示今日数据"}
  //{ID:241,keyword:"命令词",ASR:"今日统计",ASRTO:"正在为您展示今日数据"}
  //{ID:242,keyword:"命令词",ASR:"学习统计",ASRTO:"正在为您展示今日数据"}
  //{ID:243,keyword:"命令词",ASR:"显示数据",ASRTO:"正在为您展示今日数据"}
  //{ID:244,keyword:"命令词",ASR:"切换到数据",ASRTO:"正在为您展示今日数据"}
  //{ID:245,keyword:"命令词",ASR:"让我看看数据",ASRTO:"正在为您展示今日数据"}
  //{ID:246,keyword:"命令词",ASR:"打开数据报表",ASRTO:"正在为您展示今日数据"}
  //{ID:247,keyword:"命令词",ASR:"我今天表现如何",ASRTO:"正在为您展示今日数据"}
  //{ID:248,keyword:"命令词",ASR:"看看学习成果",ASRTO:"正在为您展示今日数据"}
  //{ID:249,keyword:"命令词",ASR:"展示我的数据",ASRTO:"正在为您展示今日数据"}

  // ---- 场景 4: 回到主页 (20个别名) ----
  //{ID:250,keyword:"命令词",ASR:"回到主页",ASRTO:"屏幕已切回主页"}
  //{ID:251,keyword:"命令词",ASR:"退出数据",ASRTO:"屏幕已切回主页"}
  //{ID:252,keyword:"命令词",ASR:"返回桌面",ASRTO:"屏幕已切回主页"}
  //{ID:253,keyword:"命令词",ASR:"显示主页",ASRTO:"屏幕已切回主页"}
  //{ID:254,keyword:"命令词",ASR:"切换到主页",ASRTO:"屏幕已切回主页"}
  //{ID:255,keyword:"命令词",ASR:"退出面板",ASRTO:"屏幕已切回主页"}
  //{ID:256,keyword:"命令词",ASR:"回到桌面",ASRTO:"屏幕已切回主页"}
  //{ID:257,keyword:"命令词",ASR:"关闭数据",ASRTO:"屏幕已切回主页"}
  //{ID:258,keyword:"命令词",ASR:"退出看板",ASRTO:"屏幕已切回主页"}
  //{ID:259,keyword:"命令词",ASR:"返回主页面",ASRTO:"屏幕已切回主页"}
  //{ID:260,keyword:"命令词",ASR:"主屏幕",ASRTO:"屏幕已切回主页"}
  //{ID:261,keyword:"命令词",ASR:"桌面显示",ASRTO:"屏幕已切回主页"}
  //{ID:262,keyword:"命令词",ASR:"关闭统计",ASRTO:"屏幕已切回主页"}
  //{ID:263,keyword:"命令词",ASR:"退出记录",ASRTO:"屏幕已切回主页"}
  //{ID:264,keyword:"命令词",ASR:"恢复主页",ASRTO:"屏幕已切回主页"}
  //{ID:265,keyword:"命令词",ASR:"回去主页",ASRTO:"屏幕已切回主页"}
  //{ID:266,keyword:"命令词",ASR:"关掉看板",ASRTO:"屏幕已切回主页"}
  //{ID:267,keyword:"命令词",ASR:"不看数据了",ASRTO:"屏幕已切回主页"}
  //{ID:268,keyword:"命令词",ASR:"退到主页",ASRTO:"屏幕已切回主页"}
  //{ID:269,keyword:"命令词",ASR:"切回桌面",ASRTO:"屏幕已切回主页"}
  
  // LED 提示灯初始化
  setPinFun(4, FIRST_FUNCTION);
  pinMode(4, output);
  digitalWrite(4, 0);
}
