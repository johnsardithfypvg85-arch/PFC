# FeedCal 数值来源总结

## 1. 函数作用

`FeedCal()` 的作用是计算电流环前馈量。

它同时给出两套前馈结果：

- `Feed.ccm`：CCM 模式前馈
- `Feed.dcm`：DCM 模式前馈

最后取两者中较小的一个，再乘以前馈软启动系数 `Feed.ssK`，得到最终前馈输出 `Feed.final`。

调用链如下：

```c
ISR_ADC() -> ADCSample() -> PhaseLockLoop() -> FeedCal() -> IrefCal() -> CurrentLoop()
```

其中：

- `PhaseLockLoop()` 给出正弦表索引 `PLL.CntFianl`
- `FeedCal()` 根据这个索引生成同步正弦量 `Vac.Sine`
- `CurrentLoop()` 把 `Feed.final` 直接加到电流环输出中

## 2. 对应代码位置

### 2.1 `FeedCal()` 本体

`InterruptADC.c` 第 201~230 行：

```c
#define FEED_K  31214
#define VBUS_OVER 10550

void FeedCal(void)
{
    SinePoint = PLL.CntFianl + sinetable;
    Vac.Sine=((long)(*SinePoint))*Vac.Peak>>12;

    Feed.ccm =32767 - (Vac.Sine * VBUS_OVER>>13);
    Feed.ccm = Feed.ccm*FEED_K>>15;

    sqrt_point = sqrttable + (Feed.ccm>>5);
    Feed.dcm= Feed.dcmK * (*sqrt_point)>>10;

    if(Feed.dcm >  Feed.ccm)
        Feed.final =  Feed.ccm * Feed.ssK>>15;
    else
        Feed.final= Feed.dcm * Feed.ssK>>15;
}
```

### 2.2 `Feed.final` 在哪里使用

`CurrentLoop()` 中：

```c
ILoop.out = ((ILoop.kp * ILoop.Err + ILoop.Inte )>>13) + Feed.final;
```

对应：

- `InterruptADC.c` 第 286~287 行

这说明 `FeedCal()` 计算出来的结果不是参考值，而是直接加到电流环输出上的一个前馈占空比补偿量。

## 3. 输入输出和依赖量

### 3.1 输入

`FeedCal()` 自己没有形式参数，但实际输入来自全局变量：

- `PLL.CntFianl`
  - 由 `PhaseLockLoop()` 计算出的正弦表索引
- `Vac.Peak`
  - 输入电压峰值
- `Feed.dcmK`
  - DCM 前馈系数
- `Feed.ssK`
  - 前馈软启动系数

### 3.2 输出

- `Vac.Sine`
  - 按 PLL 相位重构出来的输入电压瞬时正弦量
- `Feed.ccm`
  - CCM 前馈
- `Feed.dcm`
  - DCM 前馈
- `Feed.final`
  - 最终加入电流环的前馈量

## 4. `Vac.Sine` 怎么来的

### 4.1 先由 PLL 定位正弦表点

代码：

```c
SinePoint = PLL.CntFianl + sinetable;
```

含义：

- `PLL.CntFianl` 是锁相环输出的表索引
- `sinetable[1024]` 是 1024 点正弦表
- `*SinePoint` 就是当前时刻的单位正弦值

相关定义：

- `Table.h` 第 12 行：`int sinetable[1024]`

这个正弦表是 Q12 格式，峰值接近 `4095`。

### 4.2 再乘输入峰值得到时刻电压

代码：

```c
Vac.Sine = ((long)(*SinePoint)) * Vac.Peak >> 12;
```

含义：

- `*SinePoint`：单位正弦，Q12
- `Vac.Peak`：输入电压峰值，Q15
- 乘完后右移 12 位，得到 `Vac.Sine`，仍然是 Q15

也就是：

```text
Vac.Sine = sin(theta) * Vac.Peak
```

只是这里用的是定点实现。

### 4.3 `Vac.Peak` 从哪里来

`Vac.Peak` 不是在 `FeedCal()` 中算的，而是在 1kHz 中断里由 RMS 值换算：

```c
Vac.Peak = Vac.Rms * RMS_TO_PEAK >> 14;
```

对应：

- `Interrupt1kHz.c` 第 284~285 行

其中：

```c
#define RMS_TO_PEAK 23166
```

因为：

```text
23166 / 16384 = 1.4142
```

所以它本质上就是：

```text
Vac.Peak = Vac.Rms * 1.414
```

## 5. `Feed.ccm` 怎么来的

### 5.1 原始公式

代码：

```c
Feed.ccm = 32767 - (Vac.Sine * VBUS_OVER >> 13);
```

注释写的是：

```text
CCM 模式下前馈 = 1 - Vin / Vbus
```

所以这一步的数学意义是：

```text
Feed.ccm ≈ 1 - Vac.Sine / Vbus
```

只是全部都用定点实现。

### 5.2 `VBUS_OVER = 10550` 怎么来的

代码：

```c
#define VBUS_OVER 10550
```

注释写的是：

```text
380V 母线电压倒数，Q13
```

而 1kHz 文件里又定义了：

```c
#define VBUS_REF 25448 //380V
```

对应：

- `Interrupt1kHz.c` 第 122 行

这两个量其实是能对上的。

因为 `380V` 对应的数字量是 `25448`，所以：

```text
32768 / 25448 = 1.2876
```

若把它写成 Q13：

```text
1.2876 * 8192 = 10548.4 ≈ 10550
```

也就是说：

```text
VBUS_OVER ≈ (1 / VBUS_REF) 的定点表示
```

更准确地说，它让下面这个式子成立：

```text
Vac.Sine * VBUS_OVER >> 13
≈ Vac.Sine / 25448 * 32768
```

而 `25448` 在工程标定里正好对应 `380V`。

所以 `Feed.ccm` 这一项本质上是在算：

```text
Feed.ccm ≈ 1 - Vin / 380V
```

注意这里用的是固定参考 `380V`，不是实时 `Vbus.Avg`。

### 5.3 `FEED_K = 31214` 怎么来的

代码：

```c
#define FEED_K 31214
Feed.ccm = Feed.ccm * FEED_K >> 15;
```

注释写的是“前馈减小系数 0.95”。

因为：

```text
31214 / 32768 = 0.9526
```

所以这一项本质上就是：

```text
Feed.ccm = Feed.ccm * 0.95
```

它的作用是把理论前馈稍微降一点，留一点环路调节余量，通常是为了让 THD、稳定性和动态之间更容易折中。

## 6. `Feed.dcm` 怎么来的

### 6.1 代码形式

```c
sqrt_point = sqrttable + (Feed.ccm >> 5);
Feed.dcm = Feed.dcmK * (*sqrt_point) >> 10;
```

### 6.2 `sqrttable` 是什么

`Table.h` 中定义：

```c
int sqrttable[1024]
```

对应：

- `Table.h` 第 68 行

由于 `Feed.ccm` 是 Q15，范围约 `0 ~ 32767`，右移 5 位以后：

```text
32767 >> 5 = 1023
```

刚好可以作为 `sqrttable[1024]` 的索引。

所以：

```text
Feed.ccm >> 5
```

就是把 Q15 的前馈量压缩成 0~1023 的查表地址。

### 6.3 数学意义

这一步的本质是：

```text
Feed.dcm ≈ Feed.dcmK * sqrt(Feed.ccm)
```

其中：

- `*sqrt_point`：近似 `sqrt(Feed.ccm)` 的查表结果
- `Feed.dcmK`：在 `DCMFeedCal()` 中计算出的 DCM 系数

`Feed.dcmK` 的计算位置：

```c
Feed.dcmK = (((SqrtPAvg * VacPeakInv) >> 15) * DCM_FEED_COEFF) >> 15;
```

对应：

- `Interrupt1kHz.c` 第 333~334 行

也就是说，`FeedCal()` 本身不负责生成 `Feed.dcmK`，它只是使用这个系数。

## 7. 为什么最后取较小值

代码：

```c
if (Feed.dcm > Feed.ccm)
    Feed.final = Feed.ccm * Feed.ssK >> 15;
else
    Feed.final = Feed.dcm * Feed.ssK >> 15;
```

这等价于：

```text
Feed.final = min(Feed.ccm, Feed.dcm) * Feed.ssK
```

物理意义可以理解为：

- CCM 和 DCM 各自都给出一个“建议前馈占空比”
- 为了避免前馈给得过大，程序取较小的一边
- 再乘以软启动系数，避免上电时前馈一下子加满

## 8. `Feed.ssK` 怎么来的

`Feed.ssK` 在 1kHz 中断中由 `Feedss()` 逐步拉升：

```c
#define FEED_SS_STEP 100
#define MAX_FEED_SSK 32767

Feed.ssK = Feed.ssK + FEED_SS_STEP;
```

对应：

- `Interrupt1kHz.c` 第 423~438 行

所以：

- `Feed.ssK = 32767` 时，相当于乘 1
- 启动初期 `Feed.ssK` 较小，前馈会被压低
- 低功率时，`DCMFeedCal()` 还会把 `Feed.ssK` 清零，暂时取消前馈

对应：

- `Interrupt1kHz.c` 第 347~352 行

## 9. `FeedCal()` 和整个控制链的关系

从控制链上看：

1. `PhaseLockLoop()` 给出相位索引 `PLL.CntFianl`
2. `FeedCal()` 用它重构 `Vac.Sine`
3. `IrefCal()` 用 `Vac.Sine` 生成电流参考
4. `CurrentLoop()` 再把 `Feed.final` 直接加到电流环输出

对应代码：

- `Vac.Sine` 在 `FeedCal()` 中生成
- `Iac.Ref = ((Vloop.out * Vac.Sine >> 15) * Vac.Rmsover2 >> 12) * IREF_KM >> 15`
  - `InterruptADC.c` 第 242~246 行
- `ILoop.out = (...) + Feed.final`
  - `InterruptADC.c` 第 286~287 行

所以 `FeedCal()` 的本质作用是：

- 给电流环提供一个“按输入电压相位预先补偿”的占空比前馈
- 减轻 PI 电流环的调节压力
- 改善电流跟踪和 THD

## 10. 一张总表

| 数值 | 来源 | 含义 |
| --- | --- | --- |
| `Vac.Sine` | `sinetable * Vac.Peak >> 12` | 重构输入电压瞬时正弦量 |
| `RMS_TO_PEAK = 23166` | `1.414 * 2^14` | RMS 转峰值系数 |
| `VBUS_REF = 25448` | 工程标定 `380V` | 380V 对应数字量 |
| `VBUS_OVER = 10550` | `Q13(32768 / 25448)` | 380V 倒数的定点形式 |
| `FEED_K = 31214` | `0.9526 * 2^15` | CCM 前馈衰减系数，约 0.95 |
| `Feed.ccm >> 5` | `Q15 -> 0~1023` | 开根号查表索引 |
| `Feed.dcmK` | `DCMFeedCal()` 计算 | DCM 前馈系数 |
| `Feed.ssK` | `Feedss()` 软启动 | 前馈软启动系数 |

## 11. 最终总结

`FeedCal()` 可以概括成下面这条链：

```text
PLL.CntFianl
-> 查 1024 点正弦表
-> 用 Vac.Peak 还原 Vac.Sine
-> 算出 CCM 前馈：0.95 * (1 - Vin / 380V)
-> 再根据 sqrt(CCM前馈) 算 DCM 前馈
-> 取 min(CCM, DCM)
-> 乘前馈软启动系数 Feed.ssK
-> 得到 Feed.final
```

所以它的核心不是“重新算电流参考”，而是“提前给电流环一个合适的占空比补偿”。

如果后续还要继续深挖，可以再往下追两步：

- `Feed.dcmK` 的物理来源和 `DCM_FEED_COEFF` 的推导
- `VBUS_REF = 25448 // 380V` 与原理图采样分压/ADC 标定的对应关系
