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
| `Rover:` | 在 Rover 中实例化驱动、开机初始化、以 5Hz 从调度器轮询 |

测量值以 MAVLink `NAMED_VALUE_FLOAT` 上报地面站，同时写入板载日志的 `NVF` 记录。

### 使用前必须设置的参数

| 参数 | 值 | 说明 |
| --- | --- | --- |
| `WQ_ENABLE` | `1` | 启用驱动（本仓库默认为 1） |
| `WQ_SERIAL` | `6` / `5` | 传感器所在串口。**思翼 N7 用 `6`**（=UART7）；**云卓 S3 用 `5`**（=UART7） |
| `SERIALx_PROTOCOL` | `51` | 必须设为 `WaterQuality`。**切勿设为 `None`（-1）** —— 那会禁用该串口引脚，导致一个字节都发不出去，且不报任何错 |
| `WQ_RATE` | `1` | 采样率，单位 Hz |
| `WQ_TIMEOUT` | `2.0` | 命令响应超时，单位秒 |
| `WQ_SEND` | `1` | 是否上报地面站。置 0 会**同时**关闭遥测与日志 |

串口固定 **9600 8N1**（`SERIALx_BAUD` 会被忽略）。接线：飞控 TX → 转换器 RX，飞控 RX → 转换器 TX，并**共地**。

### 编译

```sh
./waf configure --board SIYI_N7        # 思翼 N7
./waf configure --board SkyDroid-S3    # 云卓 S3
./waf rover
```

输出固件：`build/<板子>/bin/ardurover.apj`

### 详细文档

- 驱动说明 / 参数 / 故障排查：[`libraries/AP_WaterQuality/README.md`](libraries/AP_WaterQuality/README.md)
- QGC 地面站对接手册：[`libraries/AP_WaterQuality/QGC_INTEGRATION.md`](libraries/AP_WaterQuality/QGC_INTEGRATION.md)

---