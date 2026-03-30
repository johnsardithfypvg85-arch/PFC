# PhaseLockLoop 数值来源总结

## 1. 函数作用

`PhaseLockLoop()` 的作用是根据输入交流电压 `SADC.Vac` 的正负和过零时刻，修正 PLL 步长 `PLL.Step`，再输出正弦表索引 `PLL.CntFianl`，供后续 `FeedCal()` 查表生成同步正弦参考。

调用链如下：

```
ISR_ADC() -> ADCSample() -> PhaseLockLoop() -> FeedCal()
```

对应代码：

- `ISR_ADC()` 中调用 `ADCSample()`、`PhaseLockLoop()`、`FeedCal()`：
  - `InterruptADC.c` 第 40~49 行
- `ADCSample()` 中计算：
  - `SADC.VacL = ADCRESULT2 << 3`
  - `SADC.VacN = ADCRESULT3 << 3`
  - `SADC.Vac  = SADC.VacL - SADC.VacN`
  - `InterruptADC.c` 第 90~95 行

## 2. 关键结论

`PhaseLockLoop()` 里的数值主要分成两类：

- 由软件时序和查表格式严格推导出来的：
  - `1023`
  - `9321`
  - `8389`
  - `13005`
  - `100`
  - `>>12`
- 由控制效果调出来的：
  - `PLL_K = 3`

原理图决定的是输入采样链路的缩放、偏置和噪声水平，会影响过零检测是否稳定；但 `PhaseLockLoop()` 里的大多数常数本身并不是直接由分压电阻值算出来的。

## 3. 45kHz 从哪里来

`PhaseLockLoop()` 每次执行都跟着 ADC 中断走，而 ADC 中断由 `ePWM1` 的 SOCA 触发。

代码中给出的时序关系：

- 系统时钟 `SYSCLKOUT = 60MHz`
  - `main.c` 第 190~196 行
- `ePWM1` 采用上下计数模式，周期设置为：
  - `EPwm1Regs.TBPRD = 667`
  - 注释明确写了 `60M / 45K / 2 = 667`
  - `main.c` 第 340 行、第 352 行、第 359~360 行
- ADC 触发源：
  - `SOCAEN = 1`
  - `SOCASEL = 2`
  - `SOCAPRD = 1`
  - `main.c` 第 391~396 行
- ADC 完成后触发 `INT1`，进入 `ISR_ADC`
  - `main.c` 第 498~505 行

所以 `PhaseLockLoop()` 的运行频率就是约 `45kHz`。

## 4. `MAX_SINE_CNT = 1023` 怎么来的

代码中定义：

```c
#define MAX_SINE_CNT 1023
```

对应：

- `InterruptADC.c` 第 115~118 行

而正弦表定义为：

```c
int sinetable[1024]
```

对应：

- `Table.h` 第 11~12 行

因此：

- 正弦表总点数是 `1024`
- 索引范围是 `0 ~ 1023`
- `MAX_SINE_CNT = 1023` 本质上就是“最大索引值”

注意这里的 `1023` 不是表长，而是表长减 1。

## 5. `PLL.Step = 9321` 怎么来的

初始化代码：

```c
PLL.Step = 9321;
```

对应：

- `Interrupt200Hz.c` 第 252~259 行

这个值可以严格从“采样频率 + 表长 + Q12 格式”推导出来。

### 5.1 先看相位累加格式

代码里每次中断做：

```c
PLL.StepSum += PLL.Step;
PLL.Cnt = PLL.StepSum >> 12;
```

对应：

- `InterruptADC.c` 第 180~183 行

这说明：

- `PLL.StepSum` 和 `PLL.Step` 是 Q12 定点量
- 右移 12 位之后，才得到正弦表索引

所以，每推进 1 个表点，内部其实对应增加 `4096`

### 5.2 50Hz 工频下半周有多少个中断点

若输入频率按 `50Hz` 计算：

- 一个周期 `20ms`
- 半个周期 `10ms`
- ISR 频率约 `45kHz`

则每个半周的控制中断次数：

```text
Nhalf = 45000 * 10ms = 450
```

### 5.3 每个半周要走完整个 1024 点表

这个项目的 `sinetable[1024]` 在 `FeedCal()` 中被当作单个半周使用：

```c
SinePoint = PLL.CntFianl + sinetable;
Vac.Sine = (*SinePoint) * Vac.Peak >> 12;
```

对应：

- `InterruptADC.c` 第 210~215 行

因此 PLL 的目标是：

- 每个输入半周，走完 `1024` 个表点

### 5.4 步长公式

每个中断应该推进的 Q12 步长为：

```text
Step = 1024 * 4096 / 450
     = 9320.6756
     ≈ 9321
```

所以 `PLL.Step = 9321` 是按 `50Hz`、`45kHz`、`1024点表`、`Q12` 直接算出来的。

## 6. `8389` 和 `13005` 怎么来的

代码中在每次过零校正后都会钳位步长：

```c
if (PLL.Step > 13005)
    PLL.Step = 13005;
else if (PLL.Step < 8389)
    PLL.Step = 8389;
```

对应：

- `InterruptADC.c` 第 139~143 行
- `InterruptADC.c` 第 165~170 行

这个上下限本质上是 PLL 允许锁定的频率范围。

由前面的公式可反推输入频率：

```text
Fin = Step * Fs / (2 * 1024 * 4096)
Fs  = 45000
```

代入得到：

- `8389 -> 45.00Hz`
- `13005 -> 69.76Hz`

所以这两个值的含义就是：

- 最低允许频率约 `45Hz`
- 最高允许频率约 `70Hz`

作用是防止过零检测异常时 `PLL.Step` 跑飞。

## 7. `100` 怎么来的

代码里有两个地方用到了半周计数门限 `100`：

- 只有 `NegCnt > 100` 才承认负到正过零
  - `InterruptADC.c` 第 126~127 行
- 只有 `PosCnt > 100` 才承认正到负过零
  - `InterruptADC.c` 第 153~155 行
- 当 `NegCnt == 100` 时置位 `Flag.RmsCalReady`
  - `InterruptADC.c` 第 173~177 行

由于 ISR 频率约 `45kHz`，所以：

```text
100 个中断周期 = 100 / 45000
               = 2.22ms
```

这个数不是由电路参数精确算出来的，更像是工程门限，用来：

- 避免零点附近噪声导致误判过零
- 保证已经稳定进入新半周后再做 RMS 处理

所以 `100` 的物理意义可以理解为“约 2.22ms 的过零消抖窗口”。

## 8. `PLL_K = 3` 怎么来的

代码中定义：

```c
#define PLL_K 3
```

对应：

- `InterruptADC.c` 第 117~118 行

过零后步长修正公式为：

```c
PLL.CntErr = MAX_SINE_CNT - PLL.Cnt;
PLL.Step += PLL.CntErr * PLL_K;
```

对应：

- `InterruptADC.c` 第 136~139 行
- `InterruptADC.c` 第 162~165 行

这说明 `PLL_K` 是一个比例增益：

- 增大 `PLL_K`：锁相更快，但更容易抖动或过调
- 减小 `PLL_K`：更稳，但锁定更慢

因此 `PLL_K = 3` 更像是调参结果，不是从原理图分压比直接推导出来的数值。

## 9. `>>12` 怎么来的

代码中：

```c
PLL.Cnt = PLL.StepSum >> 12;
```

对应：

- `InterruptADC.c` 第 181~183 行

这说明内部相位量采用 `Q12` 格式：

- 低 12 位表示小数部分
- 高位表示表索引整数部分

因此：

- `Step` 的单位不是“点/次中断”
- 而是“Q12 点/次中断”

这也是 `9321` 这种看起来很大的数字出现的根本原因。

## 10. 原理图和 `Vac` 的关系

从原理图文件可以确认，输入采样前端不是直接把交流高压送进 ADC，而是先经过运放和偏置网络。

目前能确认的关键信息：

- `LPD04810-4.SchDoc` 中存在：
  - `LM358`
  - `GS8552-SR`
  - `ADC_VADJ`
  - `SGND`
  - `SC+`
  - `SC-`
- `LPD04810-3.SchDoc` 中也能看到：
  - `SC+`
  - `SC-`

这说明 ADC 输入链路里存在：

- 模拟地参考
- ADC 偏置电压
- 运放缓冲/调理

而代码端对应做法是：

```c
SADC.VacL = AdcResult.ADCRESULT2 << 3;
SADC.VacN = AdcResult.ADCRESULT3 << 3;
SADC.Vac  = SADC.VacL - SADC.VacN;
```

即：

- 分别采样 `L` 与 `N`
- 通过相减恢复输入交流电压极性

所以原理图决定了：

- `VacL`、`VacN` 的缩放比例
- ADC 零点偏置位置
- 零点附近噪声大小

但 `PhaseLockLoop()` 中的主常数依然主要由数字控制时序决定。

## 11. 一张总表

| 数值 | 来源 | 含义 |
| --- | --- | --- |
| `1023` | `1024` 点正弦表的最大索引 | `0~1023` |
| `9321` | `1024 * 4096 / 450` | 50Hz 下的理论 Q12 步长 |
| `8389` | 反推约 `45Hz` | PLL 最小步长钳位 |
| `13005` | 反推约 `69.76Hz` | PLL 最大步长钳位 |
| `100` | `100 / 45000 = 2.22ms` | 过零消抖和 RMS 延时门限 |
| `3` | 调参值 | PLL 比例修正增益 |
| `>>12` | Q12 定点格式 | 从相位累加器还原表索引 |

## 12. 最终总结

`PhaseLockLoop()` 里最关键的数学链路是：

```text
45kHz ISR
-> 50Hz 半周对应 450 次中断
-> 半周内走完 1024 点正弦表
-> 相位累加器采用 Q12
-> 理论步长 = 1024 * 4096 / 450 = 9321
```

因此：

- `9321` 是按时序和查表格式算出来的
- `8389/13005` 是按允许输入频率范围反推出来的
- `100` 是工程消抖门限
- `PLL_K = 3` 是软件调参结果

如果后续要继续深挖，可以再往前推两步：

- 从原理图分压/运放参数反推出 `VacL/VacN` 的 ADC 量程
- 结合示波或 ADC 采样值验证零点附近的噪声裕量是否足够
