# ADC 中断流程图


## 1. ADC 中断触发链

```mermaid
flowchart TD
    A["ePWM1 运行<br/>EPwm1Regs.CMPB = 650"] --> B["SOCA event<br/>当计数到CMPB时产生SOCA触发"]
    B --> C["ADCSOC0 and ADCSOC1<br/>同步采样电感电流和母线电压"]
    B --> D["ADCSOC2 and ADCSOC3<br/>同步采样输入电压L和N"]
    C --> E["EOC0 and EOC1<br/>结果写入 ADCRESULT0 和 ADCRESULT1"]
    D --> F["EOC2 and EOC3<br/>结果写入 ADCRESULT2 和 ADCRESULT3"]
    E --> G["INT1SEL = EOC3<br/>等待SOC3结束"]
    F --> G
    G --> H["ADCINT1<br/>触发 ADC 中断"]
    H --> I["PIE Group1<br/>进入 ISR_ADC()"]
```

## 2. ADC 中断主流程

```mermaid
flowchart TD
    A["ISR_ADC()<br/>ADC中断服务函数"] --> B["ADCSample()<br/>读取并整理采样值"]
    B --> C["PhaseLockLoop()<br/>锁相环更新正弦位置"]
    C --> D["FeedCal()<br/>计算CCM和DCM前馈"]
    D --> E["IrefCal()<br/>计算电流参考值"]
    E --> F["CurrentLoop()<br/>电流环PI计算"]
    F --> G["RegReflash()<br/>刷新PWM比较寄存器"]
    G --> H["Icalibrate()<br/>DCM电流采样矫正"]
    H --> I["HwOcp()<br/>硬件过流检测"]
    I --> J["Clear ADCINT1 flag<br/>清除ADC中断标志"]
    J --> K["PIEACK_GROUP1<br/>应答PIE中断"]
    K --> L["Return<br/>退出ADC中断"]
```

## 3. ADCSample 采样处理流程

```mermaid
flowchart TD
    A["ADCSample()<br/>采样值整理"] --> B["ADCRESULT0 左移3位<br/>减去 ImosOffset"]
    B --> C{"SADC.Imos 小于 0 ?<br/>电流是否为负?"}
    C -->|Yes 是| D["SADC.Imos = 0<br/>电流下限钳位"]
    C -->|No 否| E["保留当前电流值"]
    D --> F["SADC.Imos = SADC.Imos 乘 ImosK<br/>DCM采样矫正"]
    E --> F
    F --> G["ADCRESULT1 左移3位<br/>得到 SADC.Vbus"]
    G --> H["ADCRESULT2 左移3位<br/>得到 SADC.VacL"]
    H --> I["ADCRESULT3 左移3位<br/>得到 SADC.VacN"]
    I --> J["SADC.Vac = SADC.VacL - SADC.VacN<br/>得到输入电压瞬时值"]
```

中文注释：

- `ADCRESULT0`：MOS 电流采样结果。
- `ADCRESULT1`：BUS 电压采样结果。
- `ADCRESULT2`：输入电压 L 端采样结果。
- `ADCRESULT3`：输入电压 N 端采样结果。
- 所有 ADC 结果都通过左移 3 位统一换算到 Q15 格式。
- `ImosOffset` 是初始化阶段测得的电流零偏。
- `ImosK` 是 DCM 模式下的电流采样矫正系数。

## 4. PhaseLockLoop 锁相环流程

```mermaid
flowchart TD
    A["PhaseLockLoop()<br/>输入锁相环"] --> B{"SADC.Vac positive ?<br/>输入电压是否为正半周?"}

    B -->|Yes 是| C{"Polar was 0 and NegCnt above 100 ?<br/>是否发生负转正过零?"}
    C -->|Yes 是| D["PosCnt = 0<br/>NegCnt = 0<br/>Polar = 1<br/>StepSum = 0"]
    D --> E["CntErr = MAX_SINE_CNT - PLL.Cnt<br/>Step = Step + CntErr 乘 PLL_K"]
    E --> F["Clamp Step to valid range<br/>限制PLL步长范围"]
    C -->|No 否| G["保持当前极性和步长"]
    F --> H["PosCnt plus plus<br/>正半周计数加一"]
    G --> H
    H --> I["Vac.Sum += SADC.Vac<br/>Iac.Sum += SADC.Imos"]

    B -->|No 否| J{"SADC.Vac negative ?<br/>是否为负半周?"}
    J -->|No 否| K["保持当前状态"]
    J -->|Yes 是| L{"Polar was 1 and PosCnt above 100 ?<br/>是否发生正转负过零?"}
    L -->|Yes 是| M["NegCnt = 0<br/>Polar = 0<br/>StepSum = 0"]
    M --> N["CntErr = MAX_SINE_CNT - PLL.Cnt<br/>Step = Step + CntErr 乘 PLL_K"]
    N --> O["Clamp Step to valid range<br/>限制PLL步长范围"]
    L -->|No 否| P["保持当前极性和步长"]
    O --> Q["NegCnt plus plus<br/>负半周计数加一"]
    P --> Q
    Q --> R{"NegCnt equals 100 ?<br/>是否到达有效值计算时刻?"}
    R -->|Yes 是| S["Flag.RmsCalReady = 1<br/>允许1kHz中断计算有效值"]
    R -->|No 否| T["等待后续计数"]

    I --> U["StepSum = StepSum + Step"]
    K --> U
    S --> U
    T --> U
    U --> V["PLL.Cnt = StepSum right shift 12"]
    V --> W{"PLL.Cnt above MAX_SINE_CNT ?<br/>是否超出正弦表范围?"}
    W -->|Yes 是| X["PLL.CntFianl = PLL.Cnt - MAX_SINE_CNT"]
    W -->|No 否| Y["PLL.CntFianl = PLL.Cnt"]
```

## 5. FeedCal 前馈计算流程

```mermaid
flowchart TD
    A["FeedCal()<br/>前馈计算"] --> B["根据 PLL.CntFianl 查正弦表"]
    B --> C["Vac.Sine = 正弦表值 乘 Vac.Peak<br/>构建输入电压正弦瞬时值"]
    C --> D["Feed.ccm = 1 - Vac.Sine 除以 BUS<br/>得到CCM前馈"]
    D --> E["Feed.ccm = Feed.ccm 乘 FEED_K<br/>优化THD"]
    E --> F["根据 Feed.ccm 查根号表"]
    F --> G["Feed.dcm = Feed.dcmK 乘 sqrt(Feed.ccm)<br/>得到DCM前馈"]
    G --> H{"Feed.dcm greater than Feed.ccm ?<br/>DCM前馈是否更大?"}
    H -->|Yes 是| I["Feed.final = Feed.ccm 乘 Feed.ssK<br/>取较小前馈并乘软启动系数"]
    H -->|No 否| J["Feed.final = Feed.dcm 乘 Feed.ssK<br/>取较小前馈并乘软启动系数"]
```

## 6. IrefCal 与 CurrentLoop 流程

```mermaid
flowchart TD
    A["IrefCal()<br/>电流参考计算"] --> B["Iac.Ref = Vloop.out 乘 Vac.Sine<br/>再乘 Vac.Rmsover2 和 IREF_KM"]
    B --> C["Iac.Ref = Iac.Ref 乘 Iac.RefssK<br/>加入电流软启动系数"]
    C --> D{"Iac.Ref above max ?<br/>是否超过5A上限?"}
    D -->|Yes 是| E["Iac.Ref = 10920<br/>限流保护"]
    D -->|No 否| F["保留当前电流参考"]
    E --> G["CurrentLoop()<br/>电流环计算"]
    F --> G
    G --> H["ILoop.Err = Iac.Ref - SADC.Imos"]
    H --> I["Clamp ILoop.Err<br/>限制电流误差范围"]
    I --> J["ILoop.Inte += ILoop.ki 乘 ILoop.Err<br/>积分更新"]
    J --> K["Clamp ILoop.Inte<br/>限制积分上下限"]
    K --> L["ILoop.out = KP项 + 积分项 + Feed.final"]
    L --> M{"ILoop.out too small ?<br/>是否低于最小输出阈值?"}
    M -->|Yes 是| N["ILoop.out = 0"]
    M -->|No 否| O{"ILoop.out above MAX_DUTY ?<br/>是否超过最大占空比?"}
    O -->|Yes 是| P["ILoop.out = MAX_DUTY"]
    O -->|No 否| Q["保留当前 ILoop.out"]
```

## 7. RegReflash 占空比刷新流程

```mermaid
flowchart TD
    A["RegReflash()<br/>PWM寄存器刷新"] --> B["使用 ILoop.out 和 Flag.PWM<br/>计算新的CMPA"]
    B --> C["EPwm1Regs.CMPA = function of ILoop.out and PWM flag<br/>写入PWM比较寄存器"]
    C --> D["若 Flag.PWM = 0<br/>则实际输出被关闭"]
```

## 8. Icalibrate 电流采样矫正流程

```mermaid
flowchart TD
    A["Icalibrate()<br/>DCM电流采样矫正"] --> B{"ssFinsh == 1 ?<br/>软启动是否完成?"}
    B -->|No 否| C["不更新 ImosK"]
    B -->|Yes 是| D["ImosK = ILoop.out 乘 DIV_TABLE 对应值<br/>计算DCM矫正系数"]
    D --> E{"LowP == 1 ?<br/>当前是否低功率?"}
    E -->|Yes 是| F["ImosK = 32767<br/>低功率下不做DCM矫正"]
    E -->|No 否| G["保留计算结果"]
    F --> H{"ImosK above threshold ?<br/>是否接近1?"}
    G --> H
    H -->|Yes 是| I["ImosK = 32767<br/>直接钳位为1"]
    H -->|No 否| J["保留当前 ImosK"]
```

## 9. HwOcp 硬件过流保护流程

```mermaid
flowchart TD
    A["HwOcp()<br/>硬件过流检测"] --> B{"TZFLG.CBC == 1 and state not Init ?<br/>是否发生周期封波?"}
    B -->|No 否| C["跳过封波计数"]
    B -->|Yes 是| D["OpcCnt plus plus<br/>封波计数加一"]
    D --> E["清除 TZ CBC 标志<br/>等待下一个开关周期"]
    E --> F{"OpcCnt above 100 ?<br/>短时间内封波次数是否过多?"}
    C --> F
    F -->|Yes 是| G["OpcCnt = 0<br/>PFCokDisable()<br/>PFCState = Err<br/>Err |= ERR_OCP<br/>PWM = 0"]
    F -->|No 否| H["保持当前工作状态"]
    G --> I{"OpcCnt above 0 ?<br/>是否存在封波事件?"}
    H --> I
    I -->|No 否| J["结束本次检测"]
    I -->|Yes 是| K["OcpTimeCnt plus plus<br/>封波计时器加一"]
    K --> L{"OcpTimeCnt above 450 ?<br/>是否超过约10ms窗口?"}
    L -->|No 否| M["继续累计"]
    L -->|Yes 是| N["OpcCnt = 0<br/>OcpTimeCnt = 0<br/>重新开始统计"]
```

## 10. 说明

- ADC 中断是主实时控制中断，负责采样、PLL、电流前馈、电流参考、电流环和 PWM 刷新。
- `Vloop.out`、`Vac.Peak`、`Vac.Rmsover2`、`Feed.dcmK`、`Iac.RefssK` 等慢变量由 `1kHz` 中断维护，再在 ADC 中断中参与实时控制计算。
- `Flag.PWM` 由 200Hz 和 1kHz 中断中的状态机与保护逻辑共同决定，ADC 中断中的 `RegReflash()` 只是将结果刷新到 PWM 寄存器。
