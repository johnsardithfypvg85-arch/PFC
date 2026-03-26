# PFC 工程说明

本仓库为基于 TI C2000 F28034 的单相 PFC 闭环控制工程。

## 文档导航

- 项目整体说明：[`项目代码总结.md`](./项目代码总结.md)
- 锁相环函数说明：[`PhaseLockLoop说明.md`](./PhaseLockLoop说明.md)
- 主流程图：[`main中断触发流程图.md`](./main中断触发流程图.md)
- 锁相环流程图：[`PhaseLockLoop流程图.svg`](./PhaseLockLoop流程图.svg)
- ADC 触发流程图：[`adc触发流程图.svg`](./adc触发流程图.svg)

## 核心源码

- 入口与外设初始化：`main.c`
- ADC 快速中断（快环）：`InterruptADC.c`
- 1kHz 中断（慢环）：`Interrupt1kHz.c`
- 200Hz 中断（状态机）：`Interrupt200Hz.c`
- SCI 通信：`SCIcom.c`

> 若你在 GitHub 页面未看到 `PhaseLockLoop说明.md`，可从本 README 的“文档导航”直接进入。
