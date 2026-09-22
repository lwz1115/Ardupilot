# QGC 对接文档 — 水质传感器数据

本文档描述 `AP_WaterQuality` 驱动发往地面站的全部数据格式与行为，供 QGC 侧做
接收、显示开发使用。

---

## 1. 数据通道：MAVLink `NAMED_VALUE_FLOAT`

驱动**不新增自定义消息**（那需要修改 mavlink 子模块并同步 QGC），而是使用
MAVLink 标准消息 `NAMED_VALUE_FLOAT`。

### 1.1 消息定义

来源：`modules/mavlink/message_definitions/v1.0/common.xml`

```xml
<message id="251" name="NAMED_VALUE_FLOAT">
  <description>Send a key-value pair as float. The use of this message is
  discouraged for normal packets, but a quite efficient way for testing new
  messages and getting experimental debug output.</description>
  <field type="uint32_t" name="time_boot_ms" units="ms">Timestamp (time since system boot).</field>
  <field type="char[10]" name="name" instance="true">Name of the debug variable</field>
  <field type="float" name="value">Floating point value</field>
</message>
```

| 字段 | 类型 | 单位 | 长度 |
|---|---|---|---|
| `time_boot_ms` | `uint32_t` | ms | 4 字节 |
| `name` | `char[10]` | — | 10 字节 |
| `value` | `float` (IEEE-754 单精度) | 见第 2 节 | 4 字节 |

- **msgid**：`251`
- **payload 长度**：18 字节
- **CRC_EXTRA**：由 MAVLink 代码生成器处理，QGC 侧无需关注
- **`name` 字段说明**：定长 10 字节，**不保证以 `\0` 结尾**。比较名称时
  必须按长度截断或补零后比较，不能直接当 C 字符串使用

### 1.2 为何选用该消息

规范原文说明它"不推荐用于常规数据包，但适用于实验性调试输出"。本场景
（自定义传感器数据上报）正属此类。ArduPilot 生态中已有同类先例，例如
`libraries/AP_Scripting/examples/SN-GCJA5-particle-sensor.lua` 用同样的方式
上报颗粒物浓度。

---

## 2. 参数名 → 物理量映射表

驱动**只上报探头实际配置的测量项**（通过 `para` 命令动态发现），因此以下
名称**不保证全部出现**。QGC 侧应对缺失的名称做容错，不要假设 7 个都存在。

| MAVLink `name` | 物理量 | 单位 | 合理范围 | YSI 参数码 | 建议显示精度 |
|---|---|---|---|---|---|
| `COND`  | 电导率 | μS/cm | 0 – 100000 | 5   | 2 位小数 |
| `PH`    | 酸碱度 | — | 0 – 14 | 18  | 2 位小数 |
| `DO`    | 光学溶解氧 | mg/L | 0 – 50 | 212 | 2 位小数 |
| `NH4`   | 铵离子 | mg/L | 0 – 100 | 48  | 3 位小数 |
| `CHLOR` | 叶绿素 | μg/L | 0 – 500 | 193 | 2 位小数 |
| `TALPC` | 总藻类-藻蓝蛋白 | cells/mL | 0 – 100000 | 215 | 2 位小数 |
| `TURB`  | 浊度 | NTU | 0 – 4000 | 37  | 2 位小数 |

名称长度均 ≤ 5 字符，远低于 MAVLink 的 10 字符上限。

> 若探头上新增了映射表之外的测量项，驱动会**跳过**该项（不发送），不会
> 发送未定义名称的数据。

---

## 3. 发送行为

### 3.1 发送时机

每次成功解析一行传感器数据后立即发送。发送频率由 `WQ_RATE` 参数控制
（默认 `1.0` Hz，范围 `0.1 – 5`）。

- 一个采样周期内，驱动会为**每个**有效测量项各发一条 `NAMED_VALUE_FLOAT`
- 7 个测量项在 1 Hz 下 = 每秒 7 条消息
- 参数 `WQ_SEND = 0` 时完全不发送，只写机载日志

### 3.2 目标通道（重要）

驱动走的是 `GCS::send_named_float()` → `GCS::send_to_active_channels()`。

该路径的行为（源码 `libraries/GCS_MAVLink/GCS.cpp`）：

```cpp
for (uint8_t i=0; i<num_gcs(); i++) {
    GCS_MAVLINK &c = *chan(i);
    if (c.is_private())            continue;   // 跳过私有通道
    if (!c.is_active())            continue;   // 跳过非活动通道
    if (c.is_high_latency_link)    continue;   // 跳过 HighLatency 链路
    c.send_message(pkt, entry);
}
```

由此可得三条对 QGC 有影响的结论：

1. **不受 `SRx_` 流控参数约束** —— 底层 `send_message()` 只做长度检查，
   没有速率控制。设了 `SRx_EXTRA1 = 0` 也不会减少这些消息
2. **会发给所有活动通道** —— 若同时连接 USB 与数传，QGC 会**收到重复消息**
   （相同 `time_boot_ms` 与 `name`），需要按通道或时间去重
3. **HighLatency 链路收不到** —— 通过铱星等低速链路连接时不会收到数据

### 3.3 带宽占用

单条消息约 30 字节（MAVLink2）。7 个测量项：

| 数传波特率 | 可用带宽（约） | `WQ_RATE=1` | `WQ_RATE=5` |
|---|---|---|---|
| 9600   | ~960 B/s | 22% | 链路饱和 |
| 19200  | ~1.9 kB/s | 11% | 55% |
| 57600  | ~5.8 kB/s | 3.6% | 18% |
| 115200 | ~11.5 kB/s | 1.8% | 9% |

---

## 4. 异常值语义（QGC 必须处理）

驱动以 **`NaN`** 表示"该测量项本次无有效读数"。触发条件：

- 该字段无法解析成数字（传感器返回了文本或字段缺失）
- 传感器明确返回 `NaN` 或 `inf`
- 该字段在数据行中缺失（行尾字段不全）

**`NaN` 不代表 0**。这一点对水质数据尤其重要：浊度 `0 NTU`、铵离子
`0 mg/L` 都是合法读数，因此驱动**刻意不用 0** 表示缺失，以避免混淆。

QGC 侧建议：

| 场景 | 建议显示 |
|---|---|
| `isnan(value)` 为真 | 显示 `--` 或 `N/A`，不要当作 0 参与绘图 |
| 绘图 | `NaN` 应产生**断线**，不要插值为 0 |
| 告警逻辑 | 可选：连续 N 秒全部为 `NaN` 时提示传感器失效 |

---

## 5. 告警消息（`STATUSTEXT`）

除数值外，驱动还会在异常时通过 `GCS_SEND_TEXT` 发送 `STATUSTEXT`
（severity = `MAV_SEVERITY_WARNING` = 4）。**限流：最快每 30 秒一条**。

| 文本 | 含义 | QGC 建议处理 |
|---|---|---|
| `WQ: para timeout, using default param codes` | 适配器未响应 `para` 命令，已回退到默认参数码顺序。**若探头实际配置不同，参数名与数值会错位** | 提示用户检查接线与波特率，并警惕后续数据可信度 |
| `WQ: parameter/value count mismatch` | `data` 行的数值个数与 `para` 报告的测量项个数不一致，本次采样不完整 | 提示数据不完整；缺失项为 `NaN` |

这两条属于普通 `STATUSTEXT`，QGC 会在消息面板自动显示，无需额外开发。

---

## 6. 机载日志（可选对接）

`GCS::send_named_float()` 在发送 MAVLink 的同时，会向 ArduPilot `.BIN` 日志
写入一条 `NVF` 记录（消息名 `NVF`）：

| 字段 | 类型 | 单位 | 说明 |
|---|---|---|---|
| `TimeUS` | `uint64_t` | μs | 系统启动后时间 |
| `Name` | `char[10]` | — | 与 MAVLink 的 `name` 一致 |
| `Value` | `float` | 见第 2 节 | 数值 |

日志中的名称与上表完全一致，因此 QGC 若有 `.BIN` 日志分析功能，可复用同一
套名称映射表。

---

## 7. QGC 侧集成建议

### 7.1 现状

QGC 已能解析 `NAMED_VALUE_FLOAT`（MAVLink Inspector 中可见），但**不会**
自动为它创建仪表控件 —— QGC 只为硬编码支持的消息生成 UI。

### 7.2 最小改动路径（推荐先做这个）

不改 QGC 代码，用 **MAVLink Inspector** 验证数据通路：连接后搜索
`NAMED_VALUE_FLOAT`，确认 `name` 与 `value` 符合预期。这一步能排除掉
"飞控没发"或"参数没配置"的问题，再动手改 QGC。

### 7.3 增加自定义显示面板的改动点

QGC 中处理 MAVLink 消息的典型位置：

| 位置 | 作用 |
|---|---|
| `src/Vehicle/Vehicle.cc` | 消息分发（`_mavlinkMessageReceived` 的 switch） |
| `src/Vehicle/Vehicle.h` | 声明 `_handleNamedValueFloat` 一类的槽函数 |
| `src/UI/**` | 新增显示控件并绑定到 Vehicle 暴露的属性 |

典型接收代码骨架（对照实际 QGC 版本调整命名）：

```cpp
// Vehicle.h
void _handleNamedValueFloat(mavlink_message_t& message);

// Vehicle.cc 的消息分发 switch 中
case MAVLINK_MSG_ID_NAMED_VALUE_FLOAT:
    _handleNamedValueFloat(message);
    break;

// Vehicle.cc
void Vehicle::_handleNamedValueFloat(mavlink_message_t& message)
{
    mavlink_named_value_float_t namedValue;
    mavlink_msg_named_value_float_decode(&message, &namedValue);

    // 注意: namedValue.name 是定长 char[10]，不保证以 '\0' 结尾
    const QString name = QString::fromLatin1(namedValue.name,
                                             strnlen(namedValue.name, 10));
    const float value = namedValue.value;

    if (name == QStringLiteral("PH")) {
        _waterQualityPH = value;      // 自行定义属性
        // emit 信号通知 UI
    } else if (name == QStringLiteral("COND")) {
        _waterQualityCond = value;
    }
    // ... 其余名称
}
```

**实现要点**：

1. **去重**：第 3.2 节提到多通道会重复发送。建议按 `(name, time_boot_ms)`
   去重，或只处理当前活动链路
2. **缺项容错**：不要假设 7 个名称都存在，缺失时保持"无数据"状态
3. **`NaN` 处理**：进入 UI 前先判 `qIsNaN(value)`，不要让它流入数值控件
4. **不依赖固定顺序**：消息到达顺序不保证与参数码顺序一致，必须按 `name` 匹配

### 7.4 备选方案对比

| 方案 | 改动量 | 说明 |
|---|---|---|
| MAVLink Inspector | 无 | 立即可用，适合验证 |
| 自定义 QGC 面板 | 中 | 需改 QGC 源码，功能最完整 |
| 改用自定义 MAVLink 消息 | 大 | 需改 mavlink 子模块 + 重新生成 QGC 方言，**不推荐** |

---

## 8. 快速核对清单

对接调试时依次确认：

- [ ] 飞控端 `WQ_ENABLE = 1`、`WQ_SERIAL` 指向实际接线的串口
- [ ] 对应 `SERIALx_PROTOCOL = -1`（否则串口被其他驱动占用）
- [ ] 修改后已**重启飞控**（`WQ_ENABLE`/`WQ_SERIAL` 需重启生效）
- [ ] 上电后等待 ≥ 19 秒（DCP 适配器启动时间）
- [ ] QGC 的 MAVLink Inspector 中出现 `NAMED_VALUE_FLOAT`，`name` 为 `PH` 等
- [ ] 若无任何数据：查看是否有 `WQ: para timeout` 告警 → 指向接线/波特率问题
- [ ] 若有数据但数值全为 `NaN`：传感器在响应但读数无效
- [ ] 若提示 `count mismatch`：`para` 与 `data` 字段数不一致，检查探头配置
