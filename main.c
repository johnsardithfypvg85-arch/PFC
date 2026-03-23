/****************************************************************************************
  *
  * @author  文七电源
  * @淘宝店铺链接：https://shop227154945.taobao.com
  * @file : main.c
  * @brief: 主函数，初始化函数相关
  * @version V1.0
  * @date 08-02-2021
  * @LegalDeclaration ：本文档内容难免存在Bug，仅限于交流学习，禁止用于任何的商业用途
  * @Copyright著作权文七电源所有
  * @attention
  *
  ******************************************************************************
  */

#include "DSP28x_Project.h"
#include "InterruptADC.h"
#include "Interrupt1kHz.h"
#include "Interrupt200Hz.h"
#include "SCIcom.h"
/**
 * main.c
 */
//系统时钟配置初始化
void SysCtrlInit(void);
//定时器0配置初始化 1kHz中断
void Timer0Init(void);
//定时器1配置初始化 200Hz中断
void Timer1Init(void);
//IO口功能配置函数
void GPIOInit(void);
//ePWM1初始化函数
void ePWMInit(void);
//ADC 初始化函数
void ADCInit(void);
//硬件过流，硬件过压TZ封波初始化函数
void TZInit(void) ;
//SCI通信初始化
void SCIInit(void);
//ePWM初始化函数(调试用)
void ePWMDebugInit(void);

extern Uint16 RamfuncsLoadStart;
extern Uint16 RamfuncsLoadSize;
extern Uint16 RamfuncsRunStart;

int main(void)
{
    DINT;//关闭所有中断
    DisableDog();   //关看门狗
    SysCtrlInit(); //初始化系统功能
    InitPieCtrl();//复位中断向量
    InitPieVectTable();//初始化中断向量表

    EALLOW;
    PieVectTable.ADCINT1 = &ISR_ADC;//ADC中断定义
    PieVectTable.TINT0 = &ISR_1kHz;//1kHz中断名定义
    PieVectTable.TINT1 = &ISR_200Hz;//200Hz中断名定义
    PieVectTable.SCIRXINTA=&ISR_SCI;//SCI接收中断
    EDIS;

    memcpy((Uint16 *)&RamfuncsRunStart,(Uint16 *)&RamfuncsLoadStart,(unsigned long)&RamfuncsLoadSize);
    InitFlash();

    Timer0Init();// CPU TIMER0 初始化 1ms(1Khz)
    Timer1Init(); // CPU TIMER1 初始化 5ms(200hz)
    ePWMInit();//ePWM1/初始化函数
    TZInit();//比较器1初始化
    ePWMDebugInit();//ePWM4初始化函数(调试用)
    GPIOInit();//GPIO口初始化
    SCIInit();//SCI初始化
    ADCInit();//ADC采样初始化

    EALLOW;
    PieCtrlRegs.PIEIER1.bit.INTx7 = 1; //Timer0 使能
    PieCtrlRegs.PIEIER1.bit.INTx1 = 1; //ADC中断1使能
    PieCtrlRegs.PIEIER9.bit.INTx1 = 1; // SCI中断使能；
    EDIS;

    PieCtrlRegs.PIEACK.all = 0xFFFF;
    IER = M_INT1|M_INT9|M_INT13;  // 开中断INT1/ INT13
    EINT;  // Enable Global interrupt INTM
    ERTM;  // Enable Global realtime interrupt DBGM

    DELAY_US(5000);//延时
    CpuTimer0Regs.TCR.bit.TSS = 0;          // Timer0使能；
    CpuTimer1Regs.TCR.bit.TSS = 0;          // Timer1使能；

    EALLOW;
    SysCtrlRegs.PCLKCR0.bit.TBCLKSYNC = 1;  // enable TBCLK within the ePWM
    EDIS;

    for(;;)
    {
        //RelayOn();
    }
}

/*****************************************************************************
 *  函    数:void SysCtrlInit(void)
 *  功    能:系统时钟初始化
 *  配置内容：
 *
 *  矫正时钟信号（必须先使能ADC时钟信号）
 *  禁用时钟输出功能
 *  系统时钟配置如下：
 *  系统时钟1使用内部时钟1
 *  系统时钟2使用外部时钟（实际不起作用）
 *  看门狗使用内部时钟1
 *  CUP定时器2使用内部时钟1
 *  CPU定时器2不分频
 *  使能内容部晶振1（时钟1）
 *  暂停时，时钟1信号暂停（默认）
 *  关闭内部晶振2（时钟2）
 *  暂停时，时钟2信号暂停（默认）
 *  暂停时，看门狗时钟信号暂停（默认）
 *  禁止外部时钟输入功能
 *  禁止外部晶振功能
 *  时钟丢失时，立即无延时复位（默认）
 *
 * PLL设置如下：
 * 确认系统时钟正常，系统时钟丢失将停止PLL设置；
 *  确保PLL分频为0，如果非0，将其写0；
 *  关闭丢失时钟检测 PLL切换时防止检测到系统时钟报错
 *  9分频，设置时钟频率，CLKIN*6=60Mhz   CLKIN = 10Mhz;
 *  一直等待，直到PLL时钟锁存；
 *  丢失时钟检测开启；
 *  最终系统时钟： CLKIN*6/1 = 60Mhz;SYSCLKOUT=60Mhz
 *
*****************************************************************************/
void SysCtrlInit(void)
{
    EALLOW;
    //使能ADC时钟
    SysCtrlRegs.PCLKCR0.bit.ADCENCLK = 1;
    //时钟矫正函数，在这之前要使能ADC时钟
    (*Device_cal)();
    //关闭ADC时钟
    SysCtrlRegs.PCLKCR0.bit.ADCENCLK = 0;

    // 禁用外部时钟功能
    SysCtrlRegs.XCLK.bit.XCLKOUTDIV = 3;

    //时钟控制寄存器配置
    //系统时钟1使用内部时钟1
    SysCtrlRegs.CLKCTL.bit.OSCCLKSRCSEL = 0;
    //系统时钟2使用外部时钟（实际不起作用）
    SysCtrlRegs.CLKCTL.bit.OSCCLKSRC2SEL = 0;
    //看门狗使用内部时钟1
    SysCtrlRegs.CLKCTL.bit.WDCLKSRCSEL = 0;
    //CUP定时器2使用内部时钟1
    SysCtrlRegs.CLKCTL.bit.TMR2CLKSRCSEL = 2;
    // CPU定时器2不分频
    SysCtrlRegs.CLKCTL.bit.TMR2CLKPRESCALE = 0;
    //使能内容部晶振1（时钟1）
    SysCtrlRegs.CLKCTL.bit.INTOSC1OFF = 0;
    //暂停时，时钟1信号暂停（默认）
    SysCtrlRegs.CLKCTL.bit.INTOSC1HALTI = 0;
    //关闭内部晶振2（时钟2）
    SysCtrlRegs.CLKCTL.bit.INTOSC2OFF = 1;
    //暂停时，时钟2信号暂停（默认）
    SysCtrlRegs.CLKCTL.bit.INTOSC2HALTI = 0;
    //暂停时，看门狗时钟信号暂停（默认）
    SysCtrlRegs.CLKCTL.bit.WDHALTI = 0;
    //禁止外部时钟输入功能
    SysCtrlRegs.CLKCTL.bit.XCLKINOFF = 1;
    //禁止外部晶振功能
    SysCtrlRegs.CLKCTL.bit.XTALOSCOFF = 1;
    //时钟丢失时，立即无延时复位（默认）
    SysCtrlRegs.CLKCTL.bit.NMIRESETSEL = 0;

    // PLL设置
    // 先将PLL分频寄存器清0，关闭系统时钟检测，修改PLL倍频寄存器，等待PLL时钟锁存；

    //确认系统时钟正常，系统时钟丢失将停止PLL设置；
    if(SysCtrlRegs.PLLSTS.bit.MCLKSTS)
    {
        // 清除丢时钟标志位
        SysCtrlRegs.PLLSTS.bit.MCLKCLR = 1;
        // 系统停止工作
        ESTOP0;
    }
    // 确保PLL分频为0，如果非0，将其写0；
    if((SysCtrlRegs.PLLSTS.bit.DIVSEL == 2) || (SysCtrlRegs.PLLSTS.bit.DIVSEL == 3))
        // PLL分频寄存器写0；
        SysCtrlRegs.PLLSTS.bit.DIVSEL  = 0;
    // 关闭丢失时钟检测 PLL切换时防止检测到系统时钟报错
    SysCtrlRegs.PLLSTS.bit.MCLKOFF = 1;
    // CLKIN*6=60Mhz   CLKIN = 10Mhz;
    SysCtrlRegs.PLLCR.bit.DIV  = 6;
    // 一直等待，直到PLL时钟锁存；
    while(!SysCtrlRegs.PLLSTS.bit.PLLLOCKS);
    // 丢失时钟检测开启；
    SysCtrlRegs.PLLSTS.bit.MCLKOFF = 0;
    // CLKIN*6/1 = 60Mhz;SYSCLKOUT=60Mhz
    SysCtrlRegs.PLLSTS.bit.DIVSEL = 3;

    // ADC CLK开启
    SysCtrlRegs.PCLKCR0.bit.ADCENCLK = 1;
    // EPWM7 CLK关闭
    SysCtrlRegs.PCLKCR1.bit.EPWM7ENCLK = 0;
    // EPWM6 CLK关闭
    SysCtrlRegs.PCLKCR1.bit.EPWM6ENCLK = 0;
    // EPWM5 CLK开启
    SysCtrlRegs.PCLKCR1.bit.EPWM5ENCLK = 0;
    // EPWM4 CLK开启
    SysCtrlRegs.PCLKCR1.bit.EPWM4ENCLK = 1;
    // EPWM3 CLK开启
    SysCtrlRegs.PCLKCR1.bit.EPWM3ENCLK = 0;
    // EPWM2 CLK开启
    SysCtrlRegs.PCLKCR1.bit.EPWM2ENCLK = 0;
    // EPWM1 CLK开启
    SysCtrlRegs.PCLKCR1.bit.EPWM1ENCLK = 1;
    // CPU Timer1 CLK开启
    SysCtrlRegs.PCLKCR3.bit.CPUTIMER1ENCLK = 1;
    // CPU Timer0 CLK开启
    SysCtrlRegs.PCLKCR3.bit.CPUTIMER0ENCLK = 1;
    // COMP1 CLK开启
    SysCtrlRegs.PCLKCR3.bit.COMP1ENCLK = 1;
    // ECANA CLK关闭
    SysCtrlRegs.PCLKCR0.bit.ECANAENCLK = 0;
    // SCIA CLK关闭
    SysCtrlRegs.PCLKCR0.bit.SCIAENCLK = 1;
    // SPIA CLK关闭
    SysCtrlRegs.PCLKCR0.bit.SPIAENCLK = 0;
    // SPIB CLK关闭
    SysCtrlRegs.PCLKCR0.bit.SPIBENCLK = 0;
    // I2C CLK关闭
    SysCtrlRegs.PCLKCR0.bit.I2CAENCLK = 0;
    // TBC CLK关闭
    SysCtrlRegs.PCLKCR0.bit.TBCLKSYNC = 0;
    // LINA CLK关闭
    SysCtrlRegs.PCLKCR0.bit.LINAENCLK = 0;
    // HRPWM CLK关闭
    SysCtrlRegs.PCLKCR0.bit.HRPWMENCLK = 0;
    // EQEP CLK关闭
    SysCtrlRegs.PCLKCR1.bit.EQEP1ENCLK = 0;
    // ECAP2 CLK关闭
    SysCtrlRegs.PCLKCR1.bit.ECAP1ENCLK = 0;
    // CPU Timer2 CLK关闭
    SysCtrlRegs.PCLKCR3.bit.CPUTIMER2ENCLK = 0;
    // COMP3 CLK关闭
    SysCtrlRegs.PCLKCR3.bit.COMP3ENCLK = 0;
    // COMP2 CLK关闭
    SysCtrlRegs.PCLKCR3.bit.COMP2ENCLK =0;
    EDIS;
}

/*****************************************************************************
 * 函    数:void Timer0Init(void)
 * 功    能:1mS定时器，1kHz中断
 *  配置情况：
 *  关闭定时器0
 *  清楚定时器0中断标志位
 *  分频30,时钟分频为TPR+1;TDDR寄存器为29(0x1D）
 *   时钟60Mhz , PRD = 60Mhz/30/1Khz =  2000（0x7D0）
 *  将PRD数据载入TIM,TDDR数据载入PSC；
 *  将PRD数据载入TIM,TDDR数据载入PSC；
 *  Timer0 中断使能开启
*****************************************************************************/
void Timer0Init(void)
{
   //关闭定时器0
    CpuTimer0Regs.TCR.bit.TSS = 1;
    //清除定时器0中断标志位
    CpuTimer0Regs.TCR.bit.TIF = 1;
    // 分频30,时钟分频为TPR+1;TDDR寄存器为29(0x001D）
    CpuTimer0Regs.TPRH.bit.TDDRH = 0x00;
    CpuTimer0Regs.TPR.bit.TDDR = 0x1D;
    // 时钟60Mhz , PRD = 60Mhz/30/1Khz =  2000（0x7D0）
    CpuTimer0Regs.PRD.all = 0x000007D0;
    // 将PRD数据载入TIM,TDDR数据载入PSC；
    CpuTimer0Regs.TCR.bit.TRB = 1;
    // Timer0 中断使能开启
    CpuTimer0Regs.TCR.bit.TIE = 1;
}
/*****************************************************************************
 * 函    数:void Timer1Init(void)
 * 功    能:5mS定时器，200Hz中断
 *  配置情况：
 *  关闭定时器0
 *  清楚定时器0中断标志位
 *  分频30,时钟分频为TPR+1;TDDR寄存器为29(0x1D）
 *   时钟60Mhz , PRD = 60Mhz/30/200hz = 15000（0x2710）
 *  将PRD数据载入TIM,TDDR数据载入PSC；
 *  将PRD数据载入TIM,TDDR数据载入PSC；
 *  Timer0 中断使能开启
*****************************************************************************/
void Timer1Init(void)
{
    //关闭定时器1
     CpuTimer1Regs.TCR.bit.TSS = 1;
     //清楚定时器1中断标志位
     CpuTimer1Regs.TCR.bit.TIF = 1;
     // 分频30,时钟分频为TPR+1;TDDR寄存器为29(0x1D）
     CpuTimer1Regs.TPRH.bit.TDDRH = 0x00;
     CpuTimer1Regs.TPR.bit.TDDR = 0x1D;
     // 时钟60Mhz , PRD = 60Mhz/30/200hz = 10000（0x2710）
     CpuTimer1Regs.PRD.all = 0x00002710;
     // 将PRD数据载入TIM,TDDR数据载入PSC；
     CpuTimer1Regs.TCR.bit.TRB = 1;
     // Timer1 中断使能开启
     CpuTimer1Regs.TCR.bit.TIE = 1;
}

/*****************************************************************************
 * 函    数:void GPIOInit(void)
 * 功    能:IO口功能配置函数
 * 配置情况：
 * 如程序中所示
*****************************************************************************/
void GPIOInit(void)
{
    EALLOW;
    GpioDataRegs.GPADAT.all = 0ul; // GPIOA DATA数据全清0
    GpioDataRegs.GPBDAT.all = 0ul; // GPIOB DATA数据全清0
    //继电器控制
    GpioCtrlRegs.GPBMUX1.bit.GPIO32=0; //普通IO口
    GpioCtrlRegs.GPBDIR.bit.GPIO32=1; //输出配置
    GpioDataRegs.GPBCLEAR.bit.GPIO32 = 1;
    //PFC-OK信号输出
    GpioCtrlRegs.GPAMUX2.bit.GPIO22=0; //普通IO口
    GpioCtrlRegs.GPADIR.bit.GPIO22=1; //输出配置
    //控制板上LED
    GpioCtrlRegs.GPAMUX2.bit.GPIO23=0; //普通IO口
    GpioCtrlRegs.GPADIR.bit.GPIO23=1; //输出配置
    //调试端口
    GpioCtrlRegs.GPAMUX2.bit.GPIO24=0; //普通IO口
    GpioCtrlRegs.GPADIR.bit.GPIO24=1; //输出配置
    GpioCtrlRegs.GPBMUX1.bit.GPIO33=0; //普通IO口
    GpioCtrlRegs.GPBDIR.bit.GPIO33=1; //输出配置
    EDIS;
}

/*****************************************************************************
 * 函    数:void ePWMInit(void)
 * 功    能:ePWM初始化函数
 *  配置情况：
 *  ePWM1A配置
 *  配置周期值，45kHz 计数上升下降方式 EPwm1Regs.TBPRD=60M/45K/2=667
 *  不相移，计数器与时钟信号同步
 *  计数上升下降模式
 *  禁用相移功能 Master方式
 *  周期寄存器使用镜像寄存器，不立即使用
 *  同步模式，同步信号来自系统时钟，且在计数器为0时同步
 *  使用系统时钟作为TB信号时钟
 *  CMPA使用镜像寄存器
 *  CMPA在计数器为0时装载PWM
 *  向上计数时，计数器=CMPA时，置高电平
 *  向下计数时，计数器=CMPA时，置低电平
*****************************************************************************/
#define PWM_PERIOD  667
void ePWMInit(void)
{
    EALLOW;
    // GPIO0 <-> EPWM1A
    GpioCtrlRegs.GPAMUX1.bit.GPIO0 = 1;

    //配置周期值，45kHz 计数上升下降方式 EPwm1Regs.TBPRD=60M/45K/2=667
    EPwm1Regs.TBPRD = PWM_PERIOD;
    //不相移，计数器与时钟信号同步
    EPwm1Regs.TBPHS.half.TBPHS = 0;
    //计数上升下降模式
    EPwm1Regs.TBCTL.bit.CTRMODE = TB_COUNT_UPDOWN;
    //禁用相移功能 Master方式
    EPwm1Regs.TBCTL.bit.PHSEN = TB_DISABLE;
    //周期寄存器使用镜像寄存器，不立即使用
    EPwm1Regs.TBCTL.bit.PRDLD = TB_SHADOW;
    //同步模式，同步信号来自系统时钟，且在计数器为0时同步
    EPwm1Regs.TBCTL.bit.SYNCOSEL = TB_CTR_ZERO;
    // 相移后，Count UP模式；
    EPwm1Regs.TBCTL.bit.PHSDIR = TB_UP;
    //使用系统时钟作为TB信号时钟
    EPwm1Regs.TBCTL.bit.CLKDIV=TB_DIV1;
    EPwm1Regs.TBCTL.bit.HSPCLKDIV=TB_DIV1;
    //CMPA使用镜像寄存器
    EPwm1Regs.CMPCTL.bit.SHDWAMODE = CC_SHADOW;
    //CMPA在计数器为0时装载PWM
    EPwm1Regs.CMPCTL.bit.LOADAMODE = CC_CTR_ZERO;
    //向上计数时，计数器=CMPA时，置高电平
    EPwm1Regs.AQCTLA.bit.CAU = AQ_SET;
    //向下计数时，计数器=CMPA时，置低电平
    EPwm1Regs.AQCTLA.bit.CAD = AQ_CLEAR;
    //向上计数时，计数器=CMPB时，置高电平，主要用来触发ADC采样
    EPwm1Regs.AQCTLB.bit.CBU = AQ_SET;
    //向下计数时，计数器=CMPB时，置低电平
    EPwm1Regs.AQCTLB.bit.CBD = AQ_CLEAR;
    //采样触发点
    EPwm1Regs.CMPB = 650;

    //使能SOCA触发功能
    EPwm1Regs.ETSEL.bit.SOCAEN  = 1;
    //Cnt=CPMB信号
    EPwm1Regs.ETSEL.bit.SOCASEL = 2;
    //立即触发Generate pulse on 1st event
    EPwm1Regs.ETPS.bit.SOCAPRD  = 1;

    // adjust duty=0 for output EPWM1A
    EPwm1Regs.CMPA.half.CMPA = 667;
    EDIS;
}

/*****************************************************************************
 * 函    数:void ePWMDebugInit(void)
 * 功    能:ePWM用以配置用来模拟DAC的效果，用以调试用
 *  配置情况：
 *  ePWM4A配置
 *  配置周期值，200kHz 计数上升下降方式 EPwm1Regs.TBPRD=60M/200k=300
 *  不相移，计数器与时钟信号同步
 *  计数上升下降模式
 *  禁用相移功能 Master方式
 *  周期寄存器使用镜像寄存器，不立即使用
 *  同步模式，同步信号来自系统时钟，且在计数器为0时同步
 *  使用系统时钟作为TB信号时钟
 *  CMPA使用镜像寄存器
 *  CMPA在计数器为0时装载PWM
 *  向上计数时，计数器=0时，置高电平
 *  向下计数时，计数器=CMPA时，置低电平
 *  *****************************************************************************/
void ePWMDebugInit(void)
{
    EALLOW;
    // GPIO0 <-> EPWM4A
    GpioCtrlRegs.GPAMUX1.bit.GPIO6 = 1;

    //配置周期值，200kHz 计数上升下降方式 EPwm1Regs.TBPRD=60M/200k=300
    EPwm4Regs.TBPRD = 300;
    //不相移，计数器与时钟信号同步
    EPwm4Regs.TBPHS.half.TBPHS = 0;
    //计数上升下降模式
    EPwm4Regs.TBCTL.bit.CTRMODE = TB_COUNT_UP;
    //禁用相移功能 Master方式
    EPwm4Regs.TBCTL.bit.PHSEN = TB_DISABLE;
    //周期寄存器使用镜像寄存器，不立即使用
    EPwm4Regs.TBCTL.bit.PRDLD = TB_SHADOW;
    //同步模式，同步信号来自系统时钟，且在计数器为0时同步
    EPwm4Regs.TBCTL.bit.SYNCOSEL = TB_CTR_ZERO;
    //使用系统时钟作为TB信号时钟
    EPwm4Regs.TBCTL.bit.CLKDIV=TB_DIV1;
    EPwm4Regs.TBCTL.bit.HSPCLKDIV=TB_DIV1;
    //CMPA使用镜像寄存器
    EPwm4Regs.CMPCTL.bit.SHDWAMODE = CC_SHADOW;
    //CMP4在计数器为0时装载PWM
    EPwm4Regs.CMPCTL.bit.LOADAMODE = CC_CTR_PRD;
    //向上计数时，计数器=ZRO 时，置高电平
    EPwm4Regs.AQCTLA.bit.ZRO = AQ_SET;
    //向上计数时，计数器=CMPA时，置低电平
    EPwm4Regs.AQCTLA.bit.CAU = AQ_CLEAR;
    //向上计数时，计数器=ZRO 时，置高电平
    EPwm4Regs.AQCTLB.bit.ZRO = AQ_NO_ACTION;
    //向下计数时，计数器=CMPA时，置低电平
    EPwm4Regs.AQCTLB.bit.CBU = AQ_NO_ACTION;
    // adjust duty for output EPWM4A
    EPwm4Regs.CMPA.half.CMPA =0;
    EDIS;
}

/*****************************************************************************
 * 函    数:void ADCInit(void)
 * 功    能:ADC初始化函数
 *  配置情况：
 *

*****************************************************************************/
void ADCInit(void)
{
    EALLOW;
    // ADC 采集数据允许倍覆盖；
    AdcRegs.ADCCTL2.bit.ADCNONOVERLAP = 0;
    // ADCCLK = SYSCLKOUT(60Mhz)
    AdcRegs.ADCCTL2.bit.CLKDIV2EN=0;
    // ADC ONESHOT不启动
    AdcRegs.SOCPRICTL.bit.ONESHOT = 0;

    // SOC0~1，SOC2~3,同时采样；
    AdcRegs.ADCSAMPLEMODE.bit.SIMULEN0=1;
    AdcRegs.ADCSAMPLEMODE.bit.SIMULEN2=1;

    // SOC0~3优先级最高，其他的采样轮询顺序优先级
    AdcRegs.SOCPRICTL.bit.SOCPRIORITY = 0x04;

    //SOC0<->ADCINTA0  电感电流
    //SOC1<->ADCINTB0  母线电压
    AdcRegs.ADCSOC0CTL.bit.CHSEL = 0;
    // ePWM1B SOCA触发采样
    AdcRegs.ADCSOC0CTL.bit.TRIGSEL = 5;
    // 采样窗口8*CLK
    AdcRegs.ADCSOC0CTL.bit.ACQPS   = 8;

    // SOC2<->ADCINTA1  输入电压L
    // SOC3<->ADCINTB1  输入电压N
    AdcRegs.ADCSOC2CTL.bit.CHSEL = 1;
    // ePWM1B SOCA触发采样
    AdcRegs.ADCSOC2CTL.bit.TRIGSEL = 5;
    // 采样窗口8*CLK
    AdcRegs.ADCSOC2CTL.bit.ACQPS   = 8;

    //当采样数值保存至Resualt寄存器后触发中断
    AdcRegs.ADCCTL1.bit.INTPULSEPOS = 1;
    //ADCINT1使能，用来触发中断
    AdcRegs.INTSEL1N2.bit.INT1E     = 1;
    //只有在清中断标志位后才会触发下一次中断
    AdcRegs.INTSEL1N2.bit.INT1CONT  = 0;
    //EOC3采样结束触发中断
    AdcRegs.INTSEL1N2.bit.INT1SEL   = 3;

    // ADC 模拟电路内部核模块上电
    AdcRegs.ADCCTL1.bit.ADCPWDN = 1;
    // ADC 参考电路内部核模块上电
    AdcRegs.ADCCTL1.bit.ADCREFPWD = 1;
    // ADC Bandgap电路内部核模块上电
    AdcRegs.ADCCTL1.bit.ADCBGPWD = 1;
    // ADC启动；
    AdcRegs.ADCCTL1.bit.ADCENABLE = 1;

    EDIS;
}

/*****************************************************************************
  * 函    数: void TZInit(void)
  * 功    能:硬件过压封波，硬件过流封波保护
  * 用以判定过流
  * 配置如下
  *
 *****************************************************************************/
void TZInit(void)
{
    EALLOW;
    //COMP_OUT GPIO42 <-> OPP_COMP1_OUT
    GpioCtrlRegs.GPBMUX1.bit.GPIO42 = 3;
    // AI02<-> COPM1+
    GpioCtrlRegs.AIOMUX1.bit.AIO2 = 2;
    // GPIO15 <-> TZ1
    GpioCtrlRegs.GPAMUX1.bit.GPIO15 = 1;

    // 接受数据连续五个SYSCLKOUT时钟一致，COMPOUT才输出；
    Comp1Regs.COMPCTL.bit.QUALSEL = 5;
    // 输出信号饭庄；COMP+>COMP-,输出低电平；反之，输出高；
    Comp1Regs.COMPCTL.bit.CMPINV = 1;
    // COMP-链接内部DAC模块
    Comp1Regs.COMPCTL.bit.COMPSOURCE = 0;
    // 内部DAC有DAC Valu寄存器控制；
    Comp1Regs.DACCTL.bit.DACSOURCE = 0;
    //保护点
    Comp1Regs.DACVAL.bit.DACVAL =1000;
    // 比较器和DAC使能；
    Comp1Regs.COMPCTL.bit.COMPDACEN = 1;

    //清寄存器
    EPwm1Regs.TZCLR.bit.CBC=1;
    //OCP-TZ1 will be one shot signal for EPWM1
    EPwm1Regs.TZSEL.bit.CBC1=TZ_ENABLE;
    //TZ信号使能后，强制拉低PWM1A
    EPwm1Regs.TZCTL.bit.TZA=TZ_FORCE_LO;
    // TZ中断标志位全部清0；
    EPwm1Regs.TZCLR.all = 0xFFFF;
    // 中断均不使能；
    EPwm1Regs.TZEINT.all = 0;
    EDIS;
}
/*****************************************************************************
  * 函    数: void SCIInit(void)
  * 功    能:SCI通信初始化
  * 与后级DCDC通信
  * 配置如下
  *
 *****************************************************************************/
void SCIInit(void)
{
    EALLOW;
    //SCI通信
    GpioCtrlRegs.GPAMUX1.bit.GPIO7 = 2; // GPIO12<-> SCI_TX
    GpioCtrlRegs.GPAMUX1.bit.GPIO12 = 2; // GPIO7 <-> SCI_RX

    //1位停止位
    SciaRegs.SCICCR.bit.STOPBITS = 0x0;
    //8字节数据
    SciaRegs.SCICCR.bit.SCICHAR = 0x7;
    //No parity
    SciaRegs.SCICCR.bit.PARITYENA = 0x0;
    // 禁止Loop模式
    SciaRegs.SCICCR.bit.LOOPBKENA = 0x0;
    // 添加地址为
    SciaRegs.SCICCR.bit.ADDRIDLE_MODE = 0x1;
    //使能RX接收端
    SciaRegs.SCICTL1.bit.RXENA = 0x1;
    //使能TX接收端
    SciaRegs.SCICTL1.bit.TXENA = 0x1;
    //禁止接受错误中断功能
    SciaRegs.SCICTL1.bit.RXERRINTENA = 0x0;
    //禁止睡眠模式
    SciaRegs.SCICTL1.bit.SLEEP = 0x0;
    //允许接收中断使能
    SciaRegs.SCICTL2.bit.RXBKINTENA = 0x1;
    //禁止发送中断使能
    SciaRegs.SCICTL2.bit.TXINTENA = 0x0;
    //60M/4/((0x186+1)*8)=4800  LOSPCP
    SciaRegs.SCIHBAUD    =0x0001;
    SciaRegs.SCILBAUD    =0x0086;
    // Relinquish SCI from Reset
    SciaRegs.SCICTL1.bit.SWRESET = 0x1;

    EDIS;
}
