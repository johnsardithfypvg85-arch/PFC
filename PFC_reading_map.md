# PFC Project Reading Map

## 1. 项目一句话概览

这个工程是一个基于 TI F28034 的单相 PFC 控制工程，整体采用“主循环空转 + 中断驱动全部业务”的结构。

- `main.c` 负责初始化外设、绑定中断、启动定时器和 PWM。
- `InterruptADC.c` 负责快环控制。
- `Interrupt1kHz.c` 负责慢环、电压环、RMS 和软启动。
- `Interrupt200Hz.c` 负责状态机、输入保护、通信调度。
- `SCIcom.c` 负责与后级 DCDC 通信。

主循环本身几乎不做业务：

```c
for(;;)
{
    //RelayOn();
}
```

所以看这个项目时，重点不是 `main` 里的死循环，而是“谁触发中断、中断里做了什么”。

## 2. 一张图看调度关系

```text
main()
  |
  +-- SysCtrlInit()
  +-- Timer0Init()     -> 1kHz 中断
  +-- Timer1Init()     -> 200Hz 中断
  +-- ePWMInit()       -> 45kHz PWM
  +-- ADCInit()        -> ePWM1 SOCA 触发 ADC
  +-- TZInit()         -> 硬件过流封波
  +-- SCIInit()        -> 串口通信
  |
  +-- 开总中断

ePWM1 CMPB 触发 ADC -> ADCINT1 -> ISR_ADC()
  采样 -> 锁相 -> 前馈 -> 电流参考 -> 电流环 -> 刷新 PWM -> OCP 处理

CPU Timer0 -> ISR_1kHz()
  Vbus 平均 -> 电压环 -> RMS -> DCM 前馈系数 -> 环路变增益 -> 软启动

CPU Timer1 -> ISR_200Hz()
  状态机 -> AC 输入保护 -> SCI 通信
```

## 3. 先记住这几个核心数据结构

这些结构体集中定义在 `InterruptADC.h`，基本代表整个系统的“状态寄存器”。

### 3.1 `SADC`

采样层变量：

- `Imos`：MOS 电流采样值
- `ImosOffset`：电流零偏
- `ImosK`：DCM 采样矫正系数
- `VacL`、`VacN`：交流输入两路采样
- `Vac`：`VacL - VacN`
- `Vbus`：母线电压采样

### 3.2 `PLL`

锁相相关变量：

- `Polar`：当前正负半周
- `PosCnt`、`NegCnt`：半周计数
- `Step`：锁相步长
- `StepSum`：步长累加
- `Cnt`、`CntFianl`：正弦表索引

### 3.3 `Vac`

输入电压衍生量：

- `Sum`：半周累加值
- `Rms`：输入电压有效值
- `Peak`：输入电压峰值
- `Sine`：由锁相表构建出的正弦瞬时值
- `Rmsover2`：输入电压有效值平方倒数

### 3.4 `Iac`

输入电流相关：

- `Sum`：半周累加值
- `Rms`：输入电流有效值
- `Ref`：瞬时电流参考
- `RefssK`：电流参考软启动系数

### 3.5 `Vbus`

- `Avg`：母线平均电压
- `Ref`：母线参考电压

### 3.6 `Feed`

前馈相关：

- `ccm`：CCM 前馈值
- `dcm`：DCM 前馈值
- `final`：最终前馈输出
- `dcmK`：DCM 系数
- `ssK`：前馈软启动系数

### 3.7 `ILoop` 和 `Vloop`

- `Err`：误差
- `kp`、`ki`：PI 参数
- `Inte`：积分项
- `out`：环路输出

### 3.8 `Flag`

系统状态开关量：

- `RmsCalReady`：允许计算 RMS
- `ssFinsh`：软启动结束
- `PWM`：PWM 开关标志
- `LowP`：低功率模式
- `PFCState`：状态机状态
- `Err`：故障位

## 4. 最关键的两条链路

### 4.1 控制主链

```text
ADC采样
  -> ADCSample()
  -> PhaseLockLoop()
  -> FeedCal()
  -> IrefCal()
  -> CurrentLoop()
  -> RegReflash()
```

如果你只追这一条，已经能看懂这个项目的 70%。

### 4.2 启动状态链

```text
Init -> Wait -> Rise -> Run -> Err
```

状态机在 `Interrupt200Hz.c` 中实现：

- `Init`：初始化变量、标定电流零偏
- `Wait`：等待母线自然整流充电稳定
- `Rise`：闭环软启动，把 `Vbus.Ref` 拉到目标值
- `Run`：正常运行
- `Err`：进入故障态，关 PWM，等待恢复

## 5. 快环：`InterruptADC.c` 应该怎么读

这个文件是整个工程最核心的部分。

### 5.1 `ISR_ADC()`

执行顺序非常固定：

```text
ADCSample
 -> PhaseLockLoop
 -> FeedCal
 -> IrefCal
 -> CurrentLoop
 -> RegReflash
 -> Icalibrate
 -> HwOcp
```

建议你第一次看时，只盯每一步“输入是什么、输出改了哪个变量”。

### 5.2 `ADCSample()`

作用：把 ADC 原始结果转换成统一的 Q15 量。

主要输出：

- `SADC.Imos`
- `SADC.Vbus`
- `SADC.VacL`
- `SADC.VacN`
- `SADC.Vac`

关键理解：

- 电流采样先减零偏，再乘 `ImosK` 做 DCM 补偿。
- 电压是通过 `VacL - VacN` 得到交流输入瞬时值。

### 5.3 `PhaseLockLoop()`

作用：根据输入电压过零信息调整 `PLL.Step`，生成一个稳定的正弦表位置。

你可以把它先理解为：

```text
根据 Vac 正负半周切换
  -> 统计半周长度
  -> 修正 PLL 步长
  -> 输出当前正弦表索引
```

这个函数还顺手做了两件重要的事：

- 正半周内累加 `Vac.Sum` 和 `Iac.Sum`
- 进入负半周一定时间后，置位 `Flag.RmsCalReady`

所以它不只是“锁相”，也在给 RMS 计算提供时序基准。

### 5.4 `FeedCal()`

作用：计算电流环前馈。

计算思路：

```text
PLL表索引
 -> 查正弦表
 -> 构造 Vac.Sine
 -> 算 CCM 前馈 Feed.ccm
 -> 算 DCM 前馈 Feed.dcm
 -> 取两者较小值
 -> 再乘软启动系数 Feed.ssK
 -> 得到 Feed.final
```

这是改善 THD 和动态的重要部分。

### 5.5 `IrefCal()`

作用：生成输入电流参考。

核心关系：

```text
Iac.Ref = Vloop.out * Vac.Sine * (1 / Vac.Rms^2) * 软启动系数
```

物理意义可以这样记：

- `Vloop.out` 决定“要多少功率”
- `Vac.Sine` 决定“当前这一时刻应该跟随的波形”
- `1 / Vac.Rms^2` 用来做输入电压归一化

最终得到一个跟电网正弦同相的电流参考。

### 5.6 `CurrentLoop()`

作用：电流 PI 计算，占空比核心就在这里。

主逻辑：

```text
误差 = Iac.Ref - SADC.Imos
  -> 误差限幅
  -> 积分
  -> PI输出
  -> 加前馈 Feed.final
  -> 占空比限幅
```

最终结果写到：

- `ILoop.out`

### 5.7 `RegReflash()`

作用：把 `ILoop.out` 写入 `EPwm1Regs.CMPA`。

这是“软件控制量”真正落到硬件 PWM 的地方。

注意它还乘了 `Flag.PWM`：

- `Flag.PWM = 1` 才允许正常输出
- `Flag.PWM = 0` 就会关掉 PWM

所以很多保护不是直接改寄存器，而是先改 `Flag.PWM`。

### 5.8 `Icalibrate()`

作用：DCM 模式下电流采样矫正。

它会根据当前占空比和查表结果更新 `SADC.ImosK`，低功率时又会退化成 1 倍系数。

### 5.9 `HwOcp()`

作用：处理硬件过流封波。

逻辑很重要：

- 如果 `TZFLG.CBC` 置位，说明本周期发生硬件封波
- 统计 10ms 内封波次数
- 超过阈值则进入 `Err`

也就是说，这里不是“瞬时一次比较器动作就永久报错”，而是做了一个时间窗计数。

## 6. 慢环：`Interrupt1kHz.c` 怎么看

这个文件主要负责“外环”和“参数更新”。

### 6.1 `VbusAvgCal()`

对 `SADC.Vbus` 做 4 次滑动平均，输出：

- `Vbus.Avg`

### 6.2 `VloopKpKiCal()`

电压环变增益。

启动后如果 `Vbus.Avg` 偏离 `Vbus.Ref` 太多，就动态放大 `kp`、`ki`。

你可以把它理解成：

- 偏差小：正常增益
- 电压过高：更激进地往下拉
- 电压过低：更快往上顶

### 6.3 `VrefCal()`

作用：把母线参考值慢慢拉到目标 380V。

这一步很关键，因为 `Rise` 状态里只负责打开 PWM 和初始化 `Vbus.Ref`，真正持续上升参考值的是这里。

### 6.4 `VoltageLoop()`

外环 PI，输出：

- `Vloop.out`

这是整机功率需求的来源，后面会进入 `IrefCal()`。

### 6.5 `VbusLimit()`

这是软件母线过压限制，不直接进入 `Err`。

处理动作是：

- `Flag.PWM = 0`
- `Iac.RefssK = 0`
- `ILoop.Inte = 0`

等母线恢复后再重新打开 PWM，并重新软启动电流参考。

所以它更像一个“可恢复的软保护”。

### 6.6 `VbusUVP()`

这是更严厉的欠压保护。

当：

```text
Vbus.Avg < Vac.Rms - 50V
```

则：

- 关 PWM
- 置 `ERR_UVP`
- 状态转 `Err`
- 拉低 `PFCOK`

### 6.7 `VIacRmsCal()`

作用：计算输入电压和输入电流的有效值。

注意它不是每 1ms 无脑计算一次，而是依赖：

- `Flag.RmsCalReady`

这个标志来自快环里的 `PhaseLockLoop()`，所以 RMS 计算本质上是“按工频半周节奏”完成的。

输出：

- `Vac.Rms`
- `Iac.Rms`
- `Vac.Peak`

### 6.8 `DCMFeedCal()`

作用：估算 DCM 前馈系数和低功率状态。

输出：

- `Feed.dcmK`
- `Vac.Rmsover2`
- `Flag.LowP`

这是连接“RMS 信息”和“前馈控制”的桥梁。

### 6.9 `IloopKpKiCal()`

作用：根据输入电流有效值动态调整电流环 PI 参数。

意思很简单：

- 负载、电流变化时，内环参数跟着变
- 避免一种固定参数覆盖全工况

### 6.10 `Feedss()` 和 `Irefss()`

两个软启动：

- `Feed.ssK` 负责前馈慢慢加进去
- `Iac.RefssK` 负责电流参考慢慢加进去

这是启动平滑的重要来源。

## 7. 状态机：`Interrupt200Hz.c` 怎么看

这个文件是“系统管理层”。

### 7.1 `StateMInit()`

做两件事：

- 全部变量初始化
- 采样电流零偏

只有 `ImosOffsetCal()` 完成后，才转去 `Wait`。

### 7.2 `StateMWait()`

这一步很容易看漏，但实际非常关键。

它不是立刻软启动，而是先等母线通过整流自然充电，判断标准是：

- `Vbus.Avg` 一段时间内变化不大

稳定约 500ms 后：

- 吸合继电器 `RelayOn()`
- 状态切到 `Rise`

这说明工程里有预充过程。

### 7.3 `StateMRise()`

只在第一次进入时做一次：

- 清环路
- 开 PWM
- `Vbus.Ref = Vbus.Avg`

然后等待 `Vbus.Ref` 被 1kHz 慢慢拉到 380V，且母线实际电压跟上后：

- 进入 `Run`
- `PFCokEnable()`
- `Flag.ssFinsh = 1`

### 7.4 `StateMRun()`

这个函数本身几乎是空的。

这是个很重要的阅读信号：

- 真正运行逻辑不在 `Run()` 里
- 而是在 ADC/1kHz 中断里持续执行

### 7.5 `StateMErr()`

进入故障态后会：

- 清电压环和电流环
- 关 PWM
- 清软启动完成标志
- 拉低 `PFCOK`

当故障恢复后，会重新回到 `Wait`。

### 7.6 `VacCheck()`

这是交流输入过压欠压保护。

它依据 `Vac.Rms` 判断：

- 低于下限，置 `ERR_AC_LOW`
- 高于上限，置 `ERR_AC_HIGH`

恢复时也有回差，避免来回抖动。

## 8. 通信层：`SCIcom.c`

这个文件不是先读主控闭环必须的，但可以最后补。

通信模式大致是：

- 收到后级 DCDC 请求帧
- 校验帧正确
- 置发送标志
- 在 200Hz 调度里回传状态数据

回传内容主要有两类：

- 故障位 + 状态机状态
- 输入电压有效值 + 输入电流有效值

所以你可以把它理解成“状态上报通道”。

## 9. 推荐阅读顺序

如果你要在 30 到 60 分钟内快速看懂，建议顺序如下：

1. `main.c`
2. `InterruptADC.h`
3. `InterruptADC.c`
4. `Interrupt1kHz.c`
5. `Interrupt200Hz.c`
6. `SCIcom.c`

读每个函数时只做一件事：

- 写一句“它改了哪些全局变量”

这样速度会非常快。

## 10. 一页式变量追踪法

建议你在纸上或者注释里只追下面这几条：

### 10.1 采样闭环

```text
SADC.Vac / SADC.Imos / SADC.Vbus
```

### 10.2 锁相链

```text
SADC.Vac -> PLL.Step -> PLL.CntFianl -> Vac.Sine
```

### 10.3 外环链

```text
SADC.Vbus -> Vbus.Avg -> Vbus.Ref -> Vloop.out
```

### 10.4 内环链

```text
Vac.Sine + Vloop.out + Vac.Rmsover2
  -> Iac.Ref
  -> ILoop.Err
  -> ILoop.out
  -> EPwm1Regs.CMPA
```

### 10.5 保护链

```text
Vac.Rms / Vbus.Avg / TZFLG
  -> Flag.Err / Flag.PWM / Flag.PFCState
```

## 11. 最值得重点盯的风险点

### 11.1 `Flag.PWM`

这个量是“软件总闸”。

很多保护不是直接写死 PWM 寄存器，而是通过它来统一关断。

### 11.2 共享全局变量

三个中断都在改全局结构体：

- ADC 快环改采样和内环
- 1kHz 改外环和参数
- 200Hz 改状态和故障

所以调试时一定要有“跨中断共享变量”的意识。

### 11.3 中断嵌套

`ISR_1kHz()` 和 `ISR_200Hz()` 里都重新开了 `INT1`，意味着 ADC 快环可以抢占慢环。

这对实时性是好事，但对调试理解会更复杂。

### 11.4 `Run()` 很空

不要误以为“运行逻辑还没写完”，这是该工程本来的架构。

真正控制都在高频和中频中断里。

## 12. 最后怎么判断自己已经看懂了

如果你已经能不看代码，自己复述出下面这句话，就算主链打通了：

```text
PWM 触发 ADC 采样，ADC 中断里完成采样、PLL、前馈、电流参考和电流环，
1kHz 中断里完成母线电压环、RMS 和软启动，
200Hz 中断里跑状态机和保护，
最后由 ILoop.out 刷新 PWM 占空比。
```

如果你下一步要继续深挖，优先建议继续啃这三个函数：

1. `PhaseLockLoop()`
2. `IrefCal()`
3. `CurrentLoop()`

因为它们最能决定你是否真正理解了这个 PFC 的控制思想。
