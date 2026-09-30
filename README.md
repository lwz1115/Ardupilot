# ArduPilot Project

[![Discord](https://img.shields.io/discord/674039678562861068.svg)](https://ardupilot.org/discord)

[![Test Copter](https://github.com/ArduPilot/ardupilot/workflows/test%20copter/badge.svg?branch=master)](https://github.com/ArduPilot/ardupilot/actions/workflows/test_sitl_copter.yml) [![Test Plane](https://github.com/ArduPilot/ardupilot/workflows/test%20plane/badge.svg?branch=master)](https://github.com/ArduPilot/ardupilot/actions/workflows/test_sitl_plane.yml) [![Test Rover](https://github.com/ArduPilot/ardupilot/workflows/test%20rover/badge.svg?branch=master)](https://github.com/ArduPilot/ardupilot/actions/workflows/test_sitl_rover.yml) [![Test Sub](https://github.com/ArduPilot/ardupilot/workflows/test%20sub/badge.svg?branch=master)](https://github.com/ArduPilot/ardupilot/actions/workflows/test_sitl_sub.yml) [![Test Tracker](https://github.com/ArduPilot/ardupilot/workflows/test%20tracker/badge.svg?branch=master)](https://github.com/ArduPilot/ardupilot/actions/workflows/test_sitl_tracker.yml)

[![Test AP_Periph](https://github.com/ArduPilot/ardupilot/workflows/test%20ap_periph/badge.svg?branch=master)](https://github.com/ArduPilot/ardupilot/actions/workflows/test_sitl_periph.yml) [![Test Chibios](https://github.com/ArduPilot/ardupilot/workflows/test%20chibios/badge.svg?branch=master)](https://github.com/ArduPilot/ardupilot/actions/workflows/test_chibios.yml) [![Test Linux SBC](https://github.com/ArduPilot/ardupilot/workflows/test%20Linux%20SBC/badge.svg?branch=master)](https://github.com/ArduPilot/ardupilot/actions/workflows/test_linux_sbc.yml) [![Test Replay](https://github.com/ArduPilot/ardupilot/workflows/test%20replay/badge.svg?branch=master)](https://github.com/ArduPilot/ardupilot/actions/workflows/test_replay.yml)

[![Test Unit Tests](https://github.com/ArduPilot/ardupilot/workflows/test%20unit%20tests%20and%20sitl%20building/badge.svg?branch=master)](https://github.com/ArduPilot/ardupilot/actions/workflows/test_unit_tests.yml)[![test size](https://github.com/ArduPilot/ardupilot/actions/workflows/test_size.yml/badge.svg)](https://github.com/ArduPilot/ardupilot/actions/workflows/test_size.yml)

[![Test Environment Setup](https://github.com/ArduPilot/ardupilot/actions/workflows/test_environment.yml/badge.svg?branch=master)](https://github.com/ArduPilot/ardupilot/actions/workflows/test_environment.yml)

[![Cygwin Build](https://github.com/ArduPilot/ardupilot/actions/workflows/cygwin_build.yml/badge.svg)](https://github.com/ArduPilot/ardupilot/actions/workflows/cygwin_build.yml) [![Macos Build](https://github.com/ArduPilot/ardupilot/actions/workflows/macos_build.yml/badge.svg)](https://github.com/ArduPilot/ardupilot/actions/workflows/macos_build.yml)

[![Coverity Scan Build Status](https://scan.coverity.com/projects/5331/badge.svg)](https://scan.coverity.com/projects/ardupilot-ardupilot)

[![Test Coverage](https://github.com/ArduPilot/ardupilot/actions/workflows/test_coverage.yml/badge.svg?branch=master)](https://github.com/ArduPilot/ardupilot/actions/workflows/test_coverage.yml)

[![Autotest Status](https://autotest.ardupilot.org/autotest-badge.svg)](https://autotest.ardupilot.org/)

[![OpenSSF Best Practices](https://www.bestpractices.dev/projects/10598/badge)](https://www.bestpractices.dev/projects/10598)

## 本仓库说明（无人船水质采样定制版）

本仓库基于 [ArduPilot](https://github.com/ArduPilot/ardupilot) `master`（提交 `af2a1ba`，`ArduRover V4.8.0-dev`）修改，用于**无人船水质自动采样**项目。

### 相对上游的修改

| 提交前缀 | 内容 |
| --- | --- |
| `AP_WaterQuality:` | 新增 `libraries/AP_WaterQuality/`：YSI EXO DCP 水质传感器驱动（电导率 / pH / 溶解氧 / 铵离子 / 叶绿素 / 总藻类-藻蓝蛋白 / 浊度） |
| `AP_SerialManager:` | 新增串口协议 `SerialProtocol_WaterQuality`，取值 **51** |
| `Rover:` | 在 Rover 中实例化两个驱动、开机初始化、从调度器轮询（水质 5Hz，采样杆 10Hz）|
| `hwdef:` | 新增/修改 `SIYI_N7` 与 `SkyDroid-S3` 的 `defaults.parm`，随固件一起打包 |
| `Tools:` | `check_param_groupidx.py` 增加参数名长度校验（参数名超过 16 字符会让飞控开机即 panic）|

测量值以 MAVLink `NAMED_VALUE_FLOAT` 上报地面站，同时写入板载日志的 `NVF` 记录。
采样机构每一步的状态以 MAVLink `STATUSTEXT` 上报，见下面的「采样全流程」。

### 水质传感器参数（`WQ_`）

| 参数 | 值 | 说明 |
| --- | --- | --- |
| `WQ_ENABLE` | `1` | 启用驱动（本仓库默认为 1） |
| `WQ_SERIAL` | `6` / `5` | 传感器所在串口。**思翼 N7 用 `6`**（=UART7）；**云卓 S3 用 `5`**（=UART7） |
| `SERIALx_PROTOCOL` | `51` | 必须设为 `WaterQuality`。**切勿设为 `None`（-1）** —— 那会禁用该串口引脚，导致一个字节都发不出去，且不报任何错 |
| `WQ_RATE` | `1` | 采样率，单位 Hz |
| `WQ_TIMEOUT` | `2.0` | 命令响应超时，单位秒 |
| `WQ_SEND` | `1` | 是否上报地面站。置 0 会**同时**关闭遥测与日志 |

串口固定 **9600 8N1**（`SERIALx_BAUD` 会被忽略）。接线：飞控 TX → 转换器 RX，飞控 RX → 转换器 TX，并**共地**。

## 水质采样机构（`AP_WaterSampler`）

`libraries/AP_WaterSampler/` 控制整套采样机构：**采样杆 + 水泵 + 电磁阀（选瓶）**。
地面站只发一次「开始」，剩下全部由飞控自己跑完。

### 地面站要发什么

只有一条 MAVLink 命令，`MAV_CMD_DO_SET_SERVO`（命令号 **183**），往三个「信箱通道」写值：

```
① 选瓶：  param1 = 15   param2 = 1900（2 号瓶）/ 1500（1 号瓶）
② 容量：  param1 = 16   param2 = 1000（直接填毫升数）
③ 开始：  param1 = 14   param2 = 1900（上升沿触发）
```

三个通道都**不接任何线**，只是信箱；它们的 `SERVOx_FUNCTION` 必须是 **0（Disabled）**，
否则命令会被拒绝。**必须先写瓶号和容量，再拉触发** —— 飞控只在触发那一刻读一次。

触发是**上升沿**，所以两次采样之间必须把通道拉回 `1500`（建议地面站在 200ms 后自动补发）。

### 采样全流程

飞控检测到上升沿后，把瓶号和容量**一次性锁存**，然后自动执行：

| # | 飞控动作 | 实际输出 | 时长 | 参数 |
| --- | --- | --- | --- | --- |
| ① | 选通瓶号 | 继电器吸合（2 号瓶）/ 释放（1 号瓶） | 瞬间 | — |
| ② | 等电磁阀动作到位 | 采样杆保持 `1500`（停止） | **5 s** | `WS_VALVE_DELAY` |
| ③ | **仅开机后第一次采样**：采样杆回零 | 杆通道给 `1900` | **1 s** | `WS_ROD_HOME` |
| ④ | 放下采样杆 | 前 1s `1500` → 中间 8s `1100` → 后 1s `1500` | **10 s** | `WS_ROD_GUARD` + `WS_OPEN_TIME` |
| ⑤ | 水泵正转抽水 | 正转线拉低、反转线拉高 | **容量 ÷ 流速** | `WS_FLOW_RATE` |
| ⑥ | 停泵 | 两路都拉高 | 瞬间 | — |
| ⑦ | 等管路里的水稳定 | 采样杆保持 `1500` | **3 s** | `WS_CLOSE_DELAY` |
| ⑧ | 收起采样杆 | 前 1s `1500` → 中间 8s `1900` → 后 1s `1500` | **10 s** | `WS_ROD_GUARD` + `WS_CLOSE_TIME` |
| ⑨ | 完成 | 泵停；**继电器保持不动**（2 号瓶继续导通） | — | — |

**总时长**（以 1000 ml、流速 10 ml/s 为例）

| | 计算 | 总时长 |
| --- | --- | --- |
| 开机后第一次 | `5 + 1 + 10 + 100 + 3 + 10` | **129 秒** |
| 之后每次 | `5 + 10 + 100 + 3 + 10` | **128 秒** |

`WS_OPEN_TIME` / `WS_CLOSE_TIME` 只是**行程时间**，前后的 `WS_ROD_GUARD`（默认 1s）
是单独加的保护时间，用来吃掉电机驱动器的启动/停止延迟。

飞控每一步都发一条 `STATUSTEXT`，地面站据此显示进度：

| 时机 | 文本 |
| --- | --- |
| 触发（①） | `WS: bottle 2 1000ml selected` |
| 开始放杆（④） | `WS: rod lowering` |
| 开始抽水（⑤） | `WS: pumping 100s` |
| 容量抽够（⑥） | `WS: volume reached` |
| 开始收杆（⑧） | `WS: rod retracting` |
| 收杆完成（⑨） | `WS: sample complete` |

### 为什么第 ③ 步只在开机后第一次做

采样杆的电机驱动器**每次上电后，在收到过一次「收回」指令之前，会忽略「伸出」指令**。

表现就是：**只要不断电，开机后第一次采样放杆没反应，第二次以后全部正常；断电重启必然重现。**

所以第一次采样前先用 `WS_ROD_HOME`（默认 1s）的「收回」脉冲帮它回零。杆本来就在上限位，
这个脉冲不会让它移动。设成 `0` 可以关掉这一步。

### 分板通道对照

两块板子的输出编号不一样，各自的 `defaults.parm` 已随固件打包：

| 用途 | 思翼 N7（有 IOMCU）| 云卓 S3（无 IOMCU）| `SERVOx_FUNCTION` |
| --- | --- | --- | --- |
| 采样杆电机（PWM）| `SERVO9` / AUX1 / GPIO 50 | `SERVO6` / PWM6 / GPIO 55 | **0** |
| 电磁阀继电器（GPIO）| `SERVO10` / AUX2 / GPIO **51** | `SERVO2` / PWM2 / GPIO **51** | **-1** |
| 水泵正转（GPIO）| `SERVO11` / AUX3 / GPIO 52 | `SERVO4` / PWM4 / GPIO 53 | **-1** |
| 水泵反转（GPIO）| `SERVO12` / AUX4 / GPIO 53 | `SERVO5` / PWM5 / GPIO 54 | **-1** |
| `SERVO_GPIO_MASK` | `3584` | `26` | — |

> 云卓 S3 的 `SERVO1` 是 Rover 转向、`SERVO3` 是油门，**不能占用**。

### 采样机构参数

| 参数 | 默认 | 说明 |
| --- | --- | --- |
| `WS_ENABLE` | `1` | 采样杆总开关。置 0 后完全不碰电机输出（水泵和阀有自己的开关）|
| `WS_MOTOR_CHAN` | `9`（N7）/ `6`（S3）| 采样杆电机通道 |
| `WS_TRIG_CHAN` / `WS_BOTTLE_CHAN` / `WS_VOLUME_CHAN` | `14` / `15` / `16` | 三个信箱通道，**不接线** |
| `WS_MAX_VOLUME` | `2000` | 船体最大容量（毫升），超出的请求会被拒绝 |
| `WS_FLOW_RATE` | `10.0` | 泵流速（毫升/秒），**必须实测标定** |
| `WS_OPEN_TIME` / `WS_CLOSE_TIME` | `8.0` / `8.0` | 放杆 / 收杆的行程时间（秒）|
| `WS_ROD_GUARD` | `1.0` | 每次动杆前后各停这么久（秒）|
| `WS_ROD_HOME` | `1.0` | 开机后第一次采样前的回零脉冲时长（秒）|
| `WS_VALVE_DELAY` | `5.0` | 选完瓶等电磁阀动作到位（秒），选 1 号瓶也要等 |
| `WS_CLOSE_DELAY` | `3.0` | 关泵后延时（秒）|
| `WS_OPEN_PWM` / `WS_CLOSE_PWM` | `1100` / `1900` | 放杆 / 收杆 PWM |
| `WS_VALVE_PIN` / `WS_VALVE_ACTLOW` | `51` / `1` | 继电器 GPIO 编号 / 低电平吸合 |
| `WS_PUMP_ENABLE` | `1` | 水泵总开关 |
| `WS_PUMP_FWD_PIN` / `WS_PUMP_REV_PIN` | `52`/`53`（N7）、`53`/`54`（S3）| 正转 / 反转 GPIO |
| `WS_PUMP_CMD_CH` | `13` | **手动**泵方向命令信箱（三键用）|

### 两个必须知道的坑

**① GPIO 引脚必须显式标记，否则电平「一直不变」**

水泵和电磁阀走的是普通 GPIO，不是舵机 PWM。对应通道的 `SERVOx_FUNCTION` **必须设成
`-1`（GPIO）**，或者把该位加进 `SERVO_GPIO_MASK`。**设成 `0`（Disabled）是不够的** ——
引脚仍然归 PWM 定时器所有，`hal.gpio->write()` 会被**静默丢弃**，表现为引脚电平
**永远不变**（低电平触发的继电器就是「一直导通」）。**改完必须重启**。

**② 思翼 N7 的 `BRD_SAFETY_DEFLT` 会屏蔽 PWM 输出**

N7 的 hwdef 里有 `PE12 SAFETY_IN`，所以该参数**默认是 `1`**。这时**整个 FMU 的 PWM 输出
都被钳到 0**，但 **GPIO 完全不受影响**。

症状很迷惑人：**继电器和水泵（GPIO）正常，但采样杆（舵机 PWM）一点输出都没有**。
更坑的是给采样杆通道发 `DO_SET_SERVO` 时飞控**仍然回 `ACCEPTED`**。

检查方法：把 `BRD_SAFETY_DEFLT` 改成 `0` 重启。云卓 S3 没有安全开关引脚，默认就是 0，不会遇到。

## 编译

```sh
./waf configure --board SIYI_N7        # 思翼 N7
./waf configure --board SkyDroid-S3    # 云卓 S3
./waf rover
```

输出固件：`build/<板子>/bin/ardurover.apj`

## 详细文档

- 驱动说明 / 参数 / 故障排查：[`libraries/AP_WaterQuality/README.md`](libraries/AP_WaterQuality/README.md)
- 采样机构说明 / 参数 / 接线：[`libraries/AP_WaterSampler/README.md`](libraries/AP_WaterSampler/README.md)

---