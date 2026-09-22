# AP_WaterQuality

ArduPilot 水质传感器驱动。

## 支持的传感器

### YSI EXO DCP 适配器

YSI EXO DCP（数据采集平台）适配器将水质探头（sonde）的测量值以 ASCII
串口数据流的形式输出。将适配器的 RS232 输出经 RS232 转 TTL 转换器接到飞控
的空闲串口即可。

| 项目   | 值   |
| ------ | ---- |
| 波特率 | 9600 |
| 数据位 | 8    |
| 校验位 | 无   |
| 停止位 | 1    |

适配器上电后需要至少 19 秒才开始接受命令，且所有命令前必须加一个 CR
字符，否则命令的第一个字符会丢失。

## 参数

| 参数         | 说明             | 默认值    |
| ------------ | ---------------- | --------- |
| `WQ_ENABLE`  | 启用驱动         | 1（启用） |
| `WQ_SERIAL`  | 传感器所在串口号 | 6         |
| `WQ_TIMEOUT` | 响应超时时间     | 2.0 秒    |
| `WQ_RATE`    | 采样率           | 1.0 Hz    |
| `WQ_SEND`    | 向地面站发送数据 | 1         |

## 配置步骤

1. 将传感器接到 `WQ_SERIAL` 指定的串口。
2. 把该串口的 `SERIALx_PROTOCOL` 设为 **`WaterQuality`（51）**。若
   `WQ_SERIAL = 6`，则设置 `SERIAL6_PROTOCOL = 51`。
3. 确认 `WQ_ENABLE = 1`（默认已启用）。
4. 重启飞控。

`SERIALx_BAUD` 参数会被忽略，驱动始终以 9600 波特率打开串口。

### 为什么不能把 SERIALx_PROTOCOL 设为 None

`AP_SerialManager::init()` 对协议为 `None` 的串口会调用 `uart->disable_rxtx()`，
把该口的 TX/RX 引脚设成高阻输入，用于把它们让给其他外设（例如共用同一
引脚的 RCIN）。这个动作**没有任何代码会撤销**：

- HAL 里只有 `disable_rxtx()`，没有对应的 `enable_rxtx()`；
- ChibiOS 的串口底层驱动（`hal_serial_lld.c`）完全不配置引脚，引脚只在
  开机时由 `hwdef/common/board.c` 的 `pal_default_config` 配置一次；
- ArduPilot 的 `begin()` 只对 RTS/CTS 调 `palSetLineMode`，对 TX/RX 只调
  `palLineSetPushPull()`，而后者只改 PUPDR，不恢复 MODER/AFR。

因此协议为 `None` 的串口**永远发不出任何数据**：驱动会正常运行、不报错、
`hal.serial()` 也返回非空指针，但线上一个字节都没有，只会反复出现
`WQ: para timeout`。`WaterQuality`（51）这个分支只 `break`，既不 `begin()`
也不 `disable_rxtx()`，引脚保持可用，由驱动自己以 9600 打开串口。

### 各板串口对应关系并不通用

`WQ_SERIAL` 用的是 **SERIALn 编号**，不是 UART 编号，两者在不同板子上
并不一致，请以各板 `hwdef` 的 `SERIAL_ORDER` 为准（`SERIALn` 取
`SERIAL_ORDER[n]`，从 0 开始数）：

| 飞控                | UART7 对应  | 说明                                                        |
| ------------------- | ----------- | ----------------------------------------------------------- |
| SIYI N7（思翼 N7）  | `SERIAL6`   | 默认值 6 正确                                               |
| SkyDroid-S3（云卓） | `SERIAL5`   | 该板 `SERIAL6` 是 UART4（SBUS / RC 输入），不能作传感器口   |

在 SkyDroid-S3 上应把 `WQ_SERIAL` 设为 5，并设置 `SERIAL5_PROTOCOL = 51`。

## 测量项

驱动先发送 `para` 命令获取探头当前配置的测量项列表，之后循环发送 `data`
命令读取数值，因此不依赖固定的测量项顺序。

YSI EXO 常用参数码：

| 参数码 | 测量项          | 单位     |
| ------ | --------------- | -------- |
| 5      | 电导率          | μS/cm    |
| 18     | pH              | -        |
| 212    | 光学溶解氧      | mg/L     |
| 48     | 铵离子          | mg/L     |
| 193    | 叶绿素          | μg/L     |
| 215    | 总藻类-藻蓝蛋白 | cells/mL |
| 37     | 浊度            | NTU      |

若适配器未响应 `para` 命令，驱动会回退使用上述参数码顺序。

## 查看数据

每个测量项都通过 MAVLink `NAMED_VALUE_FLOAT` 消息发布：

| 名称    | 测量项          |
| ------- | --------------- |
| `COND`  | 电导率          |
| `PH`    | pH              |
| `DO`    | 溶解氧          |
| `NH4`   | 铵离子          |
| `CHLOR` | 叶绿素          |
| `TALPC` | 总藻类-藻蓝蛋白 |
| `TURB`  | 浊度            |

在 QGC 的 **MAVLink Inspector** 中可以查看这些数据，它们不会自动显示在
QGC 的仪表盘上。

`GCS::send_named_float()` 同时会向机载日志写入一条 `NVF` 记录，因此任务
结束后仍可回放分析这些数据。注意 `NVF` 位于 ArduPilot 的 `.BIN` 日志中，
需要用 Mission Planner、MAVExplorer 等能解析 `.BIN` 的工具查看，QGC 本身
不能直接打开该日志。

### 无读数时的取值

若某次采样中某个字段无法解析（传感器返回了非数值、`NaN` 或字段缺失），
该测量项会被置为 `NaN` 而不是 `0`，也不会保留上一次的旧值。这样在 QGC
中会直接显示为异常值，避免把"没有数据"误读成真实的 `0`。

### 健康状态与告警

`healthy()` 在以下任一条件成立时返回 `false`：

- 驱动未启用（`WQ_ENABLE` 为 0）
- 尚未收到过任何数据行
- 最近 10 秒内没有新的数据（数据过期）
- 最近一次解析出的**所有**测量值都是 `NaN`

以下情况会通过 `GCS_SEND_TEXT` 向地面站发出警告。警告做了限流（最快每
30 秒一条），避免持续故障刷屏：

| 告警文本                                       | 含义                                                                                                              |
| ---------------------------------------------- | ----------------------------------------------------------------------------------------------------------------- |
| `WQ: para timeout, using default param codes`  | 适配器未响应 `para` 命令，已回退到默认参数码顺序。**若探头实际配置不同，参数码与数值会错位**，请检查接线与波特率。 |
| `WQ: parameter/value count mismatch`           | `data` 行中的数值个数与 `para` 报告的测量项个数不一致，本次采样不完整（缺失项为 `NaN`）。                         |

### 上报哪些测量项

驱动只上报**探头实际配置**的测量项：它遍历 `para` 返回的参数码，在名称
映射表中查到对应名称后再发送，映射表之外的参数码会被跳过。

因此在探头上增删测量项不需要改代码。但若新增的测量项不在映射表中，
需要往 `AP_WaterQuality.cpp` 的 `wq_param_names[]` 里补一条记录。

### 遥测带宽注意事项

`WQ_SEND` 为 1 时，每个测量项都会单独发送一条 `NAMED_VALUE_FLOAT` 消息。
该路径走的是 `GCS::send_to_active_channels()`，**不受 `SRx_` 流控参数限制**，
每调用一次就立即发送一次。

单条 `NAMED_VALUE_FLOAT` 约 30 字节（MAVLink2）。7 个测量项在 1 Hz 下约
占用 210 字节/秒：

| 数传波特率 | 可用带宽（约） | 1 Hz 占用 | 5 Hz 占用 |
| ---------- | -------------- | --------- | --------- |
| 9600       | ~960 B/s       | 22%       | 链路饱和  |
| 19200      | ~1.9 kB/s      | 11%       | 55%       |
| 57600      | ~5.8 kB/s      | 3.6%      | 18%       |
| 115200     | ~11.5 kB/s     | 1.8%      | 9%        |

低速数传（9600 / 19200）下建议保持 `WQ_RATE` ≤ 1，或把 `WQ_SEND` 设为 0，
只在机载日志中记录数据。

## 在车辆代码中读取数据

```cpp
#if AP_WATERQUALITY_ENABLED
AP_WaterQuality *wq = AP_WaterQuality::get_singleton();
if (wq != nullptr && wq->healthy()) {
    const float ph = wq->ph();
    const float turbidity = wq->turbidity();
}
#endif
```

## 适配其他飞控板

`AP_WaterQuality_config.h` 中的 `WQ_DEFAULT_SERIAL_PORT` 决定默认串口号。
若目标板卡的 5 号口已被占用，可在该板卡的 `hwdef.dat` 中覆盖此宏定义。

