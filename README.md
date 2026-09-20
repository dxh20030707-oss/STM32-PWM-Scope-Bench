# STM32F103C8T6 PWM 示波器学习实验台

用一个 STM32F103C8T6 同时做 **PWM 信号源** 和 **PWM 参数测量仪**，把波形引到引脚上用
**真实示波器**观察；再用串口读回 MCU 自己测到的参数，通过
「**设定值 / 示波器读数 / MCU 捕获值**」三方对照来学习示波器与定时器。

> 本工程**不使用 CubeMX 重新生成**，外设全部手写初始化（`.ioc` 保持不动）。

---

## 1. 当前实现状态

| 里程碑 | 内容 | 状态 |
|---|---|---|
| M1 | 手写 TIM1 PWM 初始化，工程可编译 | ✅ 代码完成（静态检查通过，**待你在 Keil 编译**） |
| M2 | 频率/占空比可调 | ✅ `PWM_SetFreqDuty()` 自动选 PSC/ARR/CCR |
| M3 | 串口命令 + printf 重定向 | ✅ `cli` 模块（寄存器直写 USART1） |
| M4 | 输入捕获测量 PWM | ✅ `pwm_cap` 模块（TIM3 PWM Input） |
| M5 | 主循环三方对照 | ✅ `?` 命令打印 SET / CAP |

**未验证项**：本机无 ARM 工具链，代码只做了 `gcc -fsyntax-only` 静态检查 + 算法/链路的
Python 验证；**真实编译、下载、上机波形与串口实测需要你完成**。

---

## 2. 系统架构

```
                 ┌─────────────────── STM32F103C8T6 ───────────────────┐
 串口命令  ──►   │ USART1 ─► cli ─► pwm_gen ─► TIM1(PWM) ─► PA8 ─┐      │
 (PC 上位机)     │                                      │        │      │
 打印结果  ◄──   │ USART1 ◄─ cli_report ◄─ pwm_cap ◄─ TIM3 ◄────┘      │
                 └──────────────────────────────────────────────────────┘
                                          │
                                          └──► 真实示波器探头（观察 PA8 波形）
```

数据流：`命令设定 → TIM1 输出 PWM → ①示波器观察 ②TIM3 捕获测回 → 串口打印对照`。

---

## 3. 硬件连接

输入为 **0~3.3V、可直连 MCU 引脚**的信号，无需分压/偏置。

| 连接 | 说明 |
|---|---|
| `PA8` → 示波器探头 | PWM 输出，观察波形 |
| `PA8` ──杜邦线── `PA6` | 自环：MCU 测量自己产生的信号 |
| `PA9`(TX) / `PA10`(RX) → USB-TTL | 串口命令与打印 |
| `GND` → 示波器地夹 + USB-TTL 地 | **必须共地** |

示波器探头用 **×1 档**；探头地夹只接 GND，切勿接到 5V。

---

## 4. 引脚分配

| 引脚 | 外设 / 功能 | 备注 |
|---|---|---|
| PA8 | TIM1_CH1 — PWM 输出 | `AF_PP`，Speed=HIGH |
| PA6 | TIM3_CH1 — PWM 输入捕获 | 输入 + 下拉；同时是 ADC_IN6，本项目不用 ADC |
| PA9 | USART1_TX | 115200 8N1 |
| PA10 | USART1_RX | 115200 8N1 |
| PA13 / PA14 | SWD 下载口 | **不得占用** |
| PD0 / PD1 | HSE 8MHz 晶振 | 保持；系统时钟 72MHz |

---

## 5. 源码结构

```
Core/Src/pwm_gen.c    // TIM1 PWM 输出：PWM_Gen_Init / PWM_SetFreqDuty / PWM_SetFreq / PWM_SetDuty
Core/Inc/pwm_gen.h
Core/Src/pwm_cap.c    // TIM3 输入捕获：PWM_Cap_Init / PWM_Cap_Get / PWM_Cap_SetPrescaler
Core/Inc/pwm_cap.h
Core/Src/cli.c        // USART1 寄存器直写 + printf 重定向 + 命令解析
Core/Inc/cli.h
Core/Src/main.c       // 初始化 + 主循环 CLI_Task()
Core/Src/stm32f1xx_it.c  // USART1_IRQHandler（在 USER CODE 区）
```

已删除 CubeMX 生成的 `tim.c/tim.h`（TIM1 逻辑迁入 `pwm_gen`）。

**关于 USART1**：本工程的 HAL 库是**按需裁剪**的，`Drivers/.../Src` 里**没有
`stm32f1xx_hal_uart.c`**，因此 USART1 采用**寄存器直写**，无需改 `stm32f1xx_hal_conf.h`、
无需添加库文件。（若日后想用 HAL UART，需先从 STM32Cube FW_F1 包补齐 `hal_uart.c/.h`，
再打开 `HAL_UART_MODULE_ENABLED` 并把源文件加入工程。）

---

## 6. 关键算法

### 6.1 PWM 输出（`pwm_gen.c`）

```
CK_CNT   = 72MHz / (PSC + 1)      // TIM1 在 APB2，预分频=1，故计数时钟 72MHz
输出频率 = CK_CNT / (ARR + 1)
占空比   = CCR / (ARR + 1)
```

按目标频率**自动选最小 PSC**，使 ARR 落在 16 位范围内；`PWM_GetFreq/Duty()` 返回的是
**实际量化后**的值（用于精确对照）。

| 频率 | PSC | ARR | 说明 |
|---|---|---|---|
| 50 Hz | 21 | 65453 | 低频，大 PSC |
| 1 kHz | 1 | 35999 | 常用基准 |
| 10 kHz | 0 | 7199 | |
| 1 MHz | 0 | 71 | 接近上限，占空比量化变粗 |

可用范围约 **0.017 Hz ~ 36 MHz**（受 16 位 ARR/PSC 与 IO 翻转速度限制）。

### 6.2 输入捕获（`pwm_cap.c`）

TIM3 用 **PWM Input 模式**：CH1 捕上升沿（direct）、CH2 捕下降沿（indirect），
并用 **从模式 Reset**（触发源 TI1FP1）在上升沿复位计数器。于是：

```
CCR1 = 一个周期的计数个数       （周期计数）
CCR2 = 高电平的计数个数         （脉宽计数）
计数频率 = 72MHz / (PSC + 1)    // 默认 PSC=71 → 1MHz

频率   = 计数频率 / CCR1
占空比 = CCR2 / CCR1
脉宽   = CCR2 / 计数频率
```

`PWM_Cap_Get()` 会**连读两次 CCR1/CCR2 并要求一致**，避免读到硬件更新中的半截数据。

### 6.3 范围与量化限制

- 时间分辨率 = 1/计数频率（PSC=71 时 **1µs**；`PWM_Cap_SetPrescaler(0)` 时为 13.9ns）。
- 从模式复位法要求「一个周期计数 < 65536」：PSC=71 时**下限约 15Hz**，更低频需加大 PSC
  （命令 `r <psc>`）。
- **高频时占空比量化变粗**：例如 1MHz 一个周期只有 72 个计数，占空比只能到 ~1.4% 的台阶。
  这是 16 位定时器的物理极限，不是 bug。

---

## 7. 串口命令（USART1，115200 8N1）

```
f <hz>      设置 PWM 输出频率（Hz），例如 f 1000
d <pct>     设置占空比（%），例如 d 30
r <psc>     设置输入捕获量程（预分频），0=72MHz，71=1MHz
start       启动 PWM 输出
stop        停止 PWM 输出
?           打印一次 SET vs CAP 对照
help        命令列表
```

`?` 输出示例：

```
SET: f=1000.00Hz d=30.00%
CAP: f=1000.00Hz d=30.00% T=1000.00us W=300.00us
```

---

## 8. Keil 使用须知

1. 用 **MDK-ARM/10MS.uvprojx** 打开工程（`tim.c` 已移出，`pwm_gen.c/pwm_cap.c/cli.c` 已加入）。
2. 必须勾选 **Options for Target → Target → Use MicroLIB**（否则 `printf` 无法重定向）。
3. 编译、下载。串口用 115200 8N1 打开（如 XCOM、PuTTY、串口助手）。
4. 若 Keil 提示工程文件已改变，正常 —— 是源文件列表被更新。

> 注意：IAR 工程（`EWARM/`）**未同步**本次改动，仅维护 Keil。

---

## 9. 用这个项目练示波器

1. **时基（Time/div）**：调到屏幕显示 1~2 个完整周期。
2. **触发（Trigger）**：上升沿触发、电平调到 50%，波形立刻稳定 —— 理解"触发"为何是示波器最核心的旋钮。
3. **垂直（V/div）**：调到波形占屏幕 3~5 格。
4. **三方对照**：串口发 `?`，把 SET、示波器自带的 Freq/Duty 读数、CAP 三者比对。
5. **探带宽极限**：用 `f` 把频率从 1kHz 推到 1MHz，观察上升时间变缓、振铃、混叠。
6. **探头档位**：对比 ×1 / ×10 对幅度与带宽的影响。

---

## 10. 已知限制与注意

- ⚠️ 前提是 **HSE 8MHz 正常起振**；否则 72MHz 假设不成立，所有频率都会偏。
- ⚠️ **共地**：示波器地夹、USB-TTL 地、板子 GND 必须连通。
- ⚠️ 不用 MicroLIB 时 `printf` 不会输出（或在半主机模式下卡住）。
- ⚠️ `TIM3->SMCR` 被直接写为「TI1FP1 + Reset」，改回普通捕获时需清掉。
- ⚠️ `PWM_Cap_SetPrescaler()` 在运行中改 PSC 会有一次更新延迟，切换瞬间测量值可能跳一下。
- ℹ️ 工程同时存在 `MDK-ARM/` 与 `EWARM/`，本方案只维护 **Keil MDK-ARM**。

---

## 11. 后续可扩展方向

- ADC + 定时器触发 + DMA 采样模拟波形（真·数字示波器路线）。
- 双通道输入捕获对比两路 PWM 的相位差。
- 输入调理电路（分压 + 肖特基钳位），把测量范围扩到 3.3V 以上或双极性信号。
- 把测量结果编码成 PWM 波形输出，用示波器"看数字"。
