/****************************************************************************************
  *
  * @author  文七电源设计
  * @淘宝店铺链接：https://shop227154945.taobao.com
  * @file : Interrupt200Hz.c
  * @brief: 200Hz中断，主要用以状态机等运算
  * @version V1.0
  * @date 08-02-2021
  * @LegalDeclaration ：本文档内容难免存在Bug，仅限于交流学习，禁止用于任何的商业用途
  * @Copyright著作权文七电源所有
  * @attention
  *
  ******************************************************************************
  */
#include "Interrupt200Hz.h"
#include "InterruptADC.h"
#include "SCIcom.h"
#include "DSP28x_Project.h"
/** ===================================================================
**     Funtion Name : __interrupt void ISR_200HZ(void)
**     Description :
**    5mS定时中断函数
**    中断优先级为Level3
**     Parameters  :
**     Returns     :
** ===================================================================*/
__interrupt void ISR_200Hz(void)
{
    IER = M_INT1;  // 开中断INT1
    PieCtrlRegs.PIEACK.all = PIEACK_GROUP1;
    __asm("  NOP");
    EINT;  // Enable Global interrupt INTM

    //PFC状态机函数
    StateM();
    //输入电压保护
    VacCheck();
    //SCI内部通信
    SciCom();

    //请中断标志位
    CpuTimer1Regs.TCR.bit.TIF = 1;
}

/** ===================================================================
**     Funtion Name :void StateM()(void)
**     Description :   状态机函数
**     初始化状态
**     等外启动状态
**     启动状态
**     运行状态
**     故障状态
**     Parameters  :
**     Returns     :
** ===================================================================*/
void StateM(void)
{
    //判断状态类型
    switch(Flag.PFCState)
    {
        //初始化状态
        case  Init :StateMInit();
        break;
        //等待状态
        case  Wait :StateMWait();
        break;
        //软启动状态
        case  Rise :StateMRise();
        break;
        //运行状态
        case  Run :StateMRun();
        break;
        //故障状态
        case  Err :StateMErr();
        break;
    }
}
/** ===================================================================
**     Funtion Name :void ValInit(void)
**     Description :   相关参数初始化函数
**     Parameters  :无
**     Returns     :无
** ===================================================================*/
void StateMInit(void)
{
    static char FirstFlag=0;
    //告知后级DC-DC,PFC并未准备好
    PFCokDisable();
    //变量初始化一次
    if(FirstFlag==0)
    {
        FirstFlag=1;
        //所有变量初始化
        VariableInit();
        //SCI通信变量初始化
        SCIValueInit();
    }
    //电流采样偏置采样求平均已经完成，则跳转状态
    if(ImosOffsetCal()==1)
        Flag.PFCState  = Wait;//状态机跳转至等待软启状态

    //PWMDAC(0);
}
/** ===================================================================
**     Funtion Name :void StateMWait(void)
**     Description :   等待状态机
**     Parameters  :无
**     Returns     :无
** ===================================================================*/
void StateMWait(void)
{
    static unsigned char CapChargedCnt=0;
    static long VbusAvgPre=0;

    //PWMDAC(5000);
    //清软启动标志位,准备软启
    Flag.ssFinsh=0;
    //PWM关机状态
    Flag.PWM=0;
    //初始化电压参考值
    Vbus.Ref =0;
    //环路控制量初始化
    ResetVILoop();
    //当检测到BUS电压变化不大后，说明母线电容已充电至满，两次检测电压相差不超过10V
    if((Vbus.Avg>(VbusAvgPre + 669)) || (Vbus.Avg<(VbusAvgPre- 669)))
        CapChargedCnt=0;        //BUS电容自然整流充电保持计时器清0
    else
    {
         //PWMDAC(10000);
        //BUS电容自然整流充电保持计时
        CapChargedCnt++;
        //保持500MS未快速增长，可认为BUS电容已接近充满电
        if(CapChargedCnt>100)
        {
            //BUS电容自然整流充电保持计时器清0
            CapChargedCnt=0;
            //继电器吸合
            RelayOn();
            //状态机跳转至等待软启状态
            Flag.PFCState  = Rise;
        }

    }
    //BUS电压前值 = 当前值
    VbusAvgPre = Vbus.Avg;
}
/*
** ===================================================================
**     Funtion Name : void StateMRise(void)
**     Description :软启阶段
**     Parameters  : none
**     Returns     : none
** ===================================================================
*/
#define INIT_BUS_REF 25448 //380V
#define BUS_V_OFSSET 1339//20V
void StateMRise(void)
{
    static char FirstFlag=0;

    //PWMDAC(15000);
    //运行一次幅值
    if(FirstFlag ==0)
    {
        FirstFlag =1;
        //环路控制量初始化
        ResetVILoop();
        //PWM开启
        Flag.PWM=1;
        //初始化电压参考量，从当前BUS电压递增
        Vbus.Ref = Vbus.Avg;
        //初始电压参考量限幅
        if(Vbus.Ref>= INIT_BUS_REF )
            Vbus.Ref= INIT_BUS_REF;
    }

    //电压软启动结束后，且bus电压达到参考电压的余量范围内，状态机进入正常运行状态，启动状态机复位
    if((Vbus.Ref==INIT_BUS_REF)&&(Vbus.Avg>(INIT_BUS_REF - BUS_V_OFSSET )))
    {
        //状态机进入正常运行状态
        Flag.PFCState  = Run;
        //PFC ok
        PFCokEnable();
        //软启标志位置位
        Flag.ssFinsh=1;
        //  复位
        FirstFlag =0;
    }
}
/*
** ===================================================================
**     Funtion Name :void StateMRun(void)
**     Description :正常运行（空），主函数进程在中断中运行
**     Parameters  : none
**     Returns     : none
** ===================================================================
*/
void StateMRun(void)
{
    //PWMDAC(20000);
}
/*
** ===================================================================
**     Funtion Name :void StateMErr(void)
**     Description :故障状态
**     Parameters  : none
**     Returns     : none
** ===================================================================
*/
void StateMErr(void)
{
    //PWMDAC(30000);
    //环路控制量初始化
    ResetVILoop();
    //PWM关闭
    Flag.PWM=0;
    //软启动标志位清0
    Flag.ssFinsh=0;
    //PFC 不OK
    PFCokDisable();

    //若所有故障已恢复，且等待大于1S
    if((Flag.Err&0x07)==0)
    {
        //跳转至空闲等待状态,重新启动
        Flag.PFCState  = Wait;
        //清空欠压保护标志位,保护后BUS电压降得很低，一般会发生bus 欠压保护
        if(Flag.Err&ERR_UVP)
            Flag.Err &=~ ERR_UVP;
        //所有变量初始化
        //VariableInit();
    }
}
/*
** ===================================================================
**     Funtion Name :void VariableInit(void)
**     Description :所有变量初始化
**     Parameters  : none
**     Returns     : none
** ===================================================================
*/
void VariableInit(void)
{
    SADC.Imos=0;//MOS电流变量Q15
    SADC.ImosOffset=0;//MOS电流偏置变量Q15
    SADC.ImosK=0;//DCM模式下电流采样矫正系数
    SADC.VacL=0;//输入电压L变量 Q15，
    SADC.VacN=0;//输入电压N变量 Q15，
    SADC.Vac=0;//输入电压变量 Q15
    SADC.Vbus=0;//母线电压变量 Q15，

    PLL.Polar=0;//Vac极性,电压负半边向正半边相互转变标志位
    PLL.PosCnt=0;//正半周期输入电压计数@每个开关周期累计
    PLL.NegCnt=0;//负半周期输入电压计数@每个开关周期累计
    PLL.Step=9321;//锁相环步长Q12
    PLL.StepSum=0;//锁相环输出的step叠加,实际上是锁相环位置量乘以Q12
    PLL.CntErr=0;//锁相环误差
    PLL.Cnt=0;//当前锁相环输出的正弦表位置
    PLL.CntFianl=0;//最终当前锁相环输出的正弦表位置

    Vac.Sum=0;//电压累加，用以计算有效值用
    Vac.Peak=0;//峰值电压
    Vac.Rms=0;//输入电压有效值
    Vac.Abs=0;//输入电压绝对值变量 Q15--对应馒头波
    Vac.Sine=0;//构建的正弦电压
    Vac.Rmsover2=0;//输入电压有效值倒数的平方

    Iac.Sum=0;//电流累加，用以计算有效值用
    Iac.Rms=0;//输入电压有效值
    Iac.Ref=0;//电流参考值
    Iac.RefssK=0;//参考电流软起动系数

   Vbus.Avg=0;//平均值
   Vbus.Ref=0;//电压参考值

    Feed.ccm=0;//CCM模式前馈占空比
    Feed.dcm=0;//DCM模式前馈占空比
    Feed.final=0;//最终前馈输出
    Feed.dcmK=0;//系数
    Feed.ssK=0;//软起动系数，启动时，该值慢慢增加

    ILoop.out=0;//电流环输出
    ILoop.Err=0;//电流环误差
    ILoop.kp=0;
    ILoop.ki=0;
    ILoop.Inte=0;//电流环积分

    Vloop.out=0;//电压环输出
    Vloop.Err=0;//电压环误差
    Vloop.kp=0;
    Vloop.ki=0;
    Vloop.Inte=0;//电流环积分

    Flag.RmsCalReady=0;//有效值计算标志位
    Flag.ssFinsh=0;//软起结束标志位
    Flag.PWM=0;//PWM开启关断标志位
    Flag.LowP=0;//低功率
    Flag.PFCState=0;//PFC状态机量
    Flag.Err=0;//故障标志位
}
/*
** ===================================================================
**     Funtion Name :void ResetVILoop(void)
**     Description :电压电流环相关参数复位
**     Parameters  : none
**     Returns     :
** ===================================================================
*/
void ResetVILoop(void)
{
    ILoop.out=0;//电流环输出
    ILoop.Err=0;//电流环误差
    ILoop.Inte=0;//电流环积分

    Vloop.out=0;//电压环输出
    Vloop.Err=0;//电压环误差
    Vloop.Inte=0;//电流环积分

    Iac.Ref=0;//电流参考值
    Iac.RefssK=0;//电流参考值慢慢增加
    Vbus.Ref=0;//电压参考值
    Feed.ssK=0;//软起动系数，启动时，该值慢慢增加
}
/*
** ===================================================================
**     Funtion Name :char ImosOffsetCal(void)
**     Description :电流采样偏置
**     Parameters  : none
**     Returns     : 计算完成返回1，否则返回0
** ===================================================================
*/
char ImosOffsetCal(void)
{
    //计算电流零偏计数变量
    static int Cnt=0;
    //零偏矫正结束标志位
    static  char FlagC=0;
    //滑动平均和
    static long ImosOffsetSum=0;

    //采样累加
    ImosOffsetSum += AdcResult.ADCRESULT0<<3;
    //零偏累加
    Cnt++;
    //累加256次
    if(Cnt>=256)
    {
        //累加计时清零
        Cnt=0;
        //求R相电流采样OFFSET 的256次平均
        SADC.ImosOffset = ImosOffsetSum>>8;
        FlagC=1;
    }
    //返回零偏矫正结束标志位置位
    return FlagC;
}
/*
** ===================================================================
**     Funtion Name :void VacCheck(void)
**     Description :输入电压检测与保护
**     Parameters  : none
**     Returns     :
** ===================================================================
*/
//AC欠压保护恢复点电压
#define MIN_AC_RMS   12523
//AC欠压保护恢复点电压
#define MIN_AC_RMS_RE   13528
//AC过压保护动作点电压
#define MAX_AC_RMS  16943
//AC过压保护恢复点电压
#define MAX_AC_RMS_RE   15939
void VacCheck(void)
{
     //电压有效值小于AC欠压保护值时，为AC欠压保护 ，
     if ( (Vac.Rms<MIN_AC_RMS)&&( Flag.PFCState  != Init) )
     {
         //欠压故障标志位置位
         Flag.Err |=ERR_AC_LOW;
         //进入故障保护状态，
         Flag.PFCState = Err;
         //PWM关闭
         Flag.PWM=0;
         //PFC 不OK
         PFCokDisable();
     }
     //电压有效值大于AC过压保护值时，为AC过压保护
     if ( (Vac.Rms>MAX_AC_RMS)&&( Flag.PFCState  != Init) )
     {
         //过压故障标志位置位
         Flag.Err |=ERR_AC_HIGH;
         //进入故障保护状态，
         Flag.PFCState = Err;
         //PWM关闭
         Flag.PWM=0;
         //PFC 不OK
         PFCokDisable();
     }

     //但发生AC欠压或者AC过压保护时
     if((Flag.Err & ERR_AC_LOW) || (Flag.Err & ERR_AC_HIGH))
     {
         //当三相AC电压均大于欠压恢复值且小于过压恢复值时，表明AC正常电压正常。
         if ( (Vac.Rms>MIN_AC_RMS_RE)&&(Vac.Rms<MAX_AC_RMS_RE) )
         {
             //清除AC欠压标志位
             Flag.Err &=~ ERR_AC_LOW;
             //清除AC过压标志位
             Flag.Err &=~ ERR_AC_HIGH;
         }
     }
}
/*
** ===================================================================
**     Funtion Name :void PWMDAC(unsigned int para)
**     Description : 用PWM口模拟DAC输出
**     输入：数字量Q15代表3.3V，标定方式
**     Parameters  : none
**     Returns     : none
** ===================================================================
*/
#define PWMDAC_PERIOD 300
void PWMDAC(long para)
{
    static long Period=0;

    Period = para * PWMDAC_PERIOD>>15;
    EALLOW;
    EPwm4Regs.CMPA.half.CMPA =Period; // adjust duty for output EPWM4A
    EDIS;
}
