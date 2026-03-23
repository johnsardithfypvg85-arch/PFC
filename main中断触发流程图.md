```mermaid
flowchart TD
    A["上电进入 main()"] --> B["系统时钟/PIE/向量表初始化"]
    B --> C["挂接 ISR<br/>ADCINT1 -> ISR_ADC<br/>TINT0 -> ISR_1kHz<br/>TINT1 -> ISR_200Hz<br/>SCIRXINTA -> ISR_SCI"]
    C --> D["初始化 Timer0/Timer1/ePWM1/ADC/SCI/TZ"]
    D --> E["使能 PIE 与 CPU 中断"]
    E --> F["启动 Timer0/Timer1"]
    F --> G["打开 TBCLKSYNC"]
    G --> H["ePWM1 开始计数输出"]

    H --> I["ePWM1 CMPB 事件产生 SOCA"]
    I --> J["ADC SOC0/SOC2 触发采样"]
    J --> K["EOC3 完成"]
    K --> L["ADCINT1"]
    L --> M["ISR_ADC 快环<br/>采样 -> 锁相 -> 前馈 -> 电流环 -> PWM更新"]

    F --> N["Timer0 周期到达 1ms"]
    N --> O["TINT0 -> PIE Group1 INTx7"]
    O --> P["ISR_1kHz 慢环<br/>电压环/RMS/软启动"]

    F --> Q["Timer1 周期到达 5ms"]
    Q --> R["CPU INT13 / TINT1"]
    R --> S["ISR_200Hz<br/>状态机/保护/通信调度"]

    T["SCI 收到 1 个字节"] --> U["SCIRXINTA -> PIE Group9 INTx1"]
    U --> V["ISR_SCI<br/>接收解析帧"]

    W["比较器/Trip Zone 检测过流"] --> X["硬件直接拉低 PWM"]
    X --> Y["ISR_ADC 中读取 TZFLG 并升级为故障处理"]

```