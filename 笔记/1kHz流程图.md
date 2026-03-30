# 1kHz 流程图

说明：这份文件适配 Typora 的 Mermaid 渲染，采用较稳妥的写法：

- `flowchart TD` 单独占一行
- 节点文本使用引号包裹
- 中文说明直接写入节点中，便于截图和阅读

## 1. 1kHz 中断主流程

```mermaid
flowchart TD
    A["Timer0 1ms / 1kHz<br/>定时器0每1ms触发一次"] --> B["ISR_1kHz()<br/>进入1kHz中断服务函数"]
    B --> C["Enable INT1 / ACK / EINT<br/>开中断并完成中断应答"]
    C --> D["VbusAvgCal()<br/>母线电压滑动平均"]
    D --> E["VloopKpKiCal()<br/>电压环KP KI变增益"]
    E --> F["VrefCal()<br/>BUS参考电压递增"]
    F --> G["VoltageLoop()<br/>电压环PI计算"]
    G --> H["VbusLimit()<br/>母线过压限制"]
    H --> I["VbusUVP()<br/>母线欠压保护"]
    I --> J["VIacRmsCal()<br/>输入电压电流有效值计算"]
    J --> K["DCMFeedCal()<br/>DCM前馈参数计算"]
    K --> L["IloopKpKiCal()<br/>电流环KP KI变增益"]
    L --> M["Feedss()<br/>前馈软启动"]
    M --> N["Irefss()<br/>电流参考软启动"]
    N --> O["Clear Timer0 TIF<br/>清除Timer0中断标志"]
    O --> P["Exit ISR<br/>退出1kHz中断"]
```

## 2. 电压环主流程

```mermaid
flowchart TD
    A["1kHz电压环处理"] --> B["VbusAvgCal()<br/>Vbus.Avg = 母线电压滑动平均"]
    B --> C["VloopKpKiCal()<br/>根据Vbus.Avg与Vbus.Ref偏差调整KP KI"]
    C --> D["VrefCal()<br/>在Run或Rise状态下逐步抬升Vbus.Ref"]
    D --> E["VoltageLoop()<br/>计算电压误差和积分"]
    E --> F["Limit Vloop.Inte<br/>限制积分范围避免溢出"]
    F --> G["Calculate Vloop.out<br/>得到电压环输出"]
    G --> H["Limit Vloop.out<br/>限制输出范围到Q15"]
```

中文注释：

- `VbusAvgCal()`：对 ADC 采样得到的母线电压做滑动平均，降低纹波影响。
- `VloopKpKiCal()`：只有软启动结束后才根据 BUS 偏差做变增益，提高动态响应。
- `VrefCal()`：在 `Rise` 或 `Run` 状态下，将 `Vbus.Ref` 逐步增加到 380V。
- `VoltageLoop()`：执行电压环 PI 运算，输出给后续电流参考计算使用。

## 3. VloopKpKiCal 变增益流程

```mermaid
flowchart TD
    A["VloopKpKiCal()<br/>电压环变增益"] --> B{"ssFinsh == 1 ?<br/>软启动是否已完成?"}
    B -->|No 否| C["KP = 默认值<br/>KI = 默认值"]
    B -->|Yes 是| D{"Vbus.Avg 位于 Ref 正负20V内?<br/>是否处于正常调节范围?"}
    D -->|Yes 是| E["KP = 正常值<br/>KI = 正常值"]
    D -->|No 否| F{"Vbus.Avg 高于 Ref 正负30V上限?<br/>是否明显过压?"}
    F -->|Yes 是| G["KP = 4倍<br/>KI = 4倍<br/>快速拉回BUS电压"]
    F -->|No 否| H["KP = 2倍<br/>KI = 2倍<br/>加快低压恢复"]
```

## 4. VrefCal 参考电压递增流程

```mermaid
flowchart TD
    A["VrefCal()<br/>BUS参考电压计算"] --> B{"PFCState 是 Run 或 Rise ?<br/>当前是否允许运行或软启动?"}
    B -->|No 否| C["保持当前 Vbus.Ref"]
    B -->|Yes 是| D{"Vbus.Ref 小于 380V ?<br/>是否还未达到目标参考?"}
    D -->|No 否| E["Vbus.Ref 保持 380V"]
    D -->|Yes 是| F["Vbus.Ref = Vbus.Ref + 10<br/>每1ms缓慢抬升参考值"]
    F --> G{"Vbus.Ref 超过 380V ?"}
    G -->|No 否| H["保留当前参考值"]
    G -->|Yes 是| I["限幅到 380V"]
```

## 5. BUS 保护流程

```mermaid
flowchart TD
    A["BUS保护处理"] --> B["VbusLimit()<br/>过压限制"]
    B --> C{"PFCState 是 Run 或 Rise ?"}
    C -->|No 否| D["跳过过压处理"]
    C -->|Yes 是| E{"Vbus.Avg 超过过压门限?"}
    E -->|Yes 是| F["置 OVPFlag = 1<br/>PWM = 0<br/>Iac.RefssK = 0<br/>ILoop.Inte = 0"]
    E -->|No 否| G{"OVPFlag == 1 且<br/>Vbus.Avg 已回到恢复门限以下?"}
    F --> G
    G -->|Yes 是| H["OVPFlag = 0<br/>PWM = 1<br/>Iac.RefssK = 0<br/>ILoop.Inte = 0"]
    G -->|No 否| I["保持当前状态"]
    H --> J["VbusUVP()<br/>欠压保护"]
    I --> J
    D --> J
    J --> K{"PFCState == Run ?<br/>当前是否处于正常运行?"}
    K -->|No 否| L["跳过欠压保护"]
    K -->|Yes 是| M{"Vbus.Avg 小于 Vac.Rms 减去50V ?<br/>是否发生BUS欠压?"}
    M -->|No 否| N["保持运行"]
    M -->|Yes 是| O["PWM = 0<br/>置 ERR_UVP<br/>PFCState = Err<br/>PFCokDisable()"]
```

## 6. 有效值计算流程

```mermaid
flowchart TD
    A["VIacRmsCal()<br/>电压电流有效值计算"] --> B{"RmsCalReady == 1 ?<br/>是否到达计算时机?"}
    B -->|No 否| C["本周期不更新有效值"]
    B -->|Yes 是| D["根据 PLL.PosCnt 计算平均系数"]
    D --> E["Vac.Sum 和 Iac.Sum<br/>换算为 VacRmsTemp 和 IacRmsTemp"]
    E --> F["清 Vac.Sum / Iac.Sum / PLL.PosCnt"]
    F --> G["Vac.Rms 滑动平均"]
    G --> H["Iac.Rms 滑动平均"]
    H --> I["Vac.Peak = Vac.Rms 乘 1.414"]
    I --> J["RmsCalReady = 0<br/>等待下一次半周更新"]
```

## 7. DCM 前馈与电流环参数流程

```mermaid
flowchart TD
    A["DCMFeedCal()<br/>DCM前馈参数计算"] --> B["查表求 SqrtVac 和 SqrtIac"]
    B --> C["计算功率开根号并滑动平均"]
    C --> D["限制 Vac.Peak 最小值<br/>避免倒数过大"]
    D --> E["计算 VacPeakInv 和 Feed.dcmK"]
    E --> F["限制 Vac.Rms 最小值<br/>计算 Vac.Rmsover2"]
    F --> G["计算 PowerAc = Vac.Rms 乘 Iac.Rms"]
    G --> H{"PowerAc 小于低功率门限?"}
    H -->|Yes 是| I["Flag.LowP = 1<br/>Feed.ssK = 0<br/>取消前馈"]
    H -->|No 否| J{"LowP 已置位且功率恢复?"}
    J -->|Yes 是| K["Flag.LowP = 0"]
    J -->|No 否| L["保持当前低功率状态"]
```

```mermaid
flowchart TD
    A["IloopKpKiCal()<br/>电流环KP KI变增益"] --> B["Iac.Rms 滑动平均"]
    B --> C["根据平均电流计算 ILoop.ki"]
    C --> D["限制 ILoop.ki 最大最小值"]
    D --> E["根据平均电流计算 ILoop.kp"]
    E --> F["限制 ILoop.kp 最大最小值"]
```

## 8. 软启动系数流程

```mermaid
flowchart TD
    A["Feedss()<br/>前馈软启动"] --> B{"ssFinsh == 1 ?<br/>软启动是否完成?"}
    B -->|No 否| C["保持 Feed.ssK"]
    B -->|Yes 是| D{"Feed.ssK 小于最大值?"}
    D -->|No 否| E["保持 Feed.ssK 最大值"]
    D -->|Yes 是| F["Feed.ssK = Feed.ssK + 100"]
    F --> G["若超过上限则钳位到最大值"]
```

```mermaid
flowchart TD
    A["Irefss()<br/>电流参考软启动"] --> B{"Iac.RefssK 小于最大值?"}
    B -->|No 否| C["保持 Iac.RefssK 最大值"]
    B -->|Yes 是| D["Iac.RefssK = Iac.RefssK + 200"]
    D --> E["若超过上限则钳位到最大值"]
```

## 9. 说明

- 1kHz 中断主要负责慢速环路、BUS 保护、有效值计算、前馈参数和软启动参数更新。
- `Vbus.Ref` 在这里每 1ms 递增一次，因此它和 200Hz 状态机中的 `Rise` 状态是联动关系。
- 主电流环实时计算、PLL、采样校正和 PWM 刷新仍主要在 ADC 中断中完成。
