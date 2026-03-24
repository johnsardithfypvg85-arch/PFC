/****************************************************************************************
  *
  * @author  文七电源设计
  * @淘宝店铺链接：https://shop227154945.taobao.com
  * @file : Interrupt1kHz.c
  * @brief: 1kHz中断，主要用以电压环类的相关计算
  * @version V1.0
  * @date 08-02-2021
  * @LegalDeclaration ：本文档内容难免存在Bug，仅限于交流学习，禁止用于任何的商业用途
  * @Copyright著作权文七电源所有
  * @attention
  *
  ******************************************************************************
  */
#include "Interrupt1kHz.h"
#include "InterruptADC.h"
#include "DSP28x_Project.h"

__interrupt void ISR_1kHz(void)
{
    IER = M_INT1;  // 开中断INT1
    PieCtrlRegs.PIEACK.all = PIEACK_GROUP1;
    __asm("  NOP");
    EINT;

    //输出电压采样求平均计算
    VbusAvgCal();
    //电压环变增益函数计算
    VloopKpKiCal();
    //BUS参考电压处理
    VrefCal();
    //电压环函数
    VoltageLoop();
    //OVP函数-母线电压限制
    VbusLimit();
    //UVP函数
    VbusUVP();
    //计算AC有效值计算函数
    VIacRmsCal();
    //DCM前馈参数计算
    DCMFeedCal();
    //电流环便增益
    IloopKpKiCal();
    //前馈软启
    Feedss();
    //电流参考值软启
    Irefss();
    //请中断标志位，重新赋值中断向量
    CpuTimer0Regs.TCR.bit.TIF = 1;
    PieCtrlRegs.PIEACK.all = PIEACK_GROUP1;
}
/*
** ===================================================================
**     Funtion Name : void VbusAvgCal(void)
**     Description : 对采样后的bus电压求平均计算
**     电压环为慢速环路
**     Parameters  : none
**     Returns     : none
** ===================================================================
*/
void VbusAvgCal(void)
{
    //滑动平均求和量，正BUS滑动平均求和量,负BUS滑动平均求和量,双边电压平均量
    static long VSum=0;

    //母线电压采样4次滑动平均
    VSum = VSum + SADC.Vbus - (VSum>>2);
    Vbus.Avg = VSum>>2;
}
/*
** ===================================================================
**     Funtion Name : void VloopKpKiCal(void)
**     Description : 电压环Kp和Ki变增益，当BUS电压过高或者过低时能够快速调节会参考值
**     Parameters  : none
**     Returns     : none
** ===================================================================
*/
#define VLOOP_KP 1000//3079
#define VLOOP_KI  100//162
#define V_OFFSET 2009//参考值+30V
#define V_OFFSET_RE 1339//参考值+20V
void VloopKpKiCal(void)
{
    //只有在启动结束后才进行电压环变增益
    if(Flag.ssFinsh == 1)
    {
        //当正BUS电压在正常范围内，退出变增益 上下正负20V
        if( (Vbus.Avg < (Vbus.Ref   + V_OFFSET_RE))  &&  (Vbus.Avg > (Vbus.Ref   - V_OFFSET_RE)) )
        {
            Vloop.kp = VLOOP_KP;//正常KP KI
            Vloop.ki  = VLOOP_KI;//正常KP KI
        }
        //电压过大变增益
        else if(Vbus.Avg > ( Vbus.Ref + V_OFFSET ))
        {
            Vloop.kp = VLOOP_KP*4;//*倍变增益KP
            Vloop.ki  = VLOOP_KI*4;//*倍变增益KI
        }
        //电压过小变增益
        else if(Vbus.Avg < (Vbus.Ref   - V_OFFSET))
        {
            Vloop.kp = VLOOP_KP*2;//*倍变增益KP
            Vloop.ki  = VLOOP_KI*2;//*倍变增益KI
        }
    }
    else
    {
        Vloop.kp = VLOOP_KP;
        Vloop.ki = VLOOP_KI;
    }
    //Vloop.kp =2639;
    //Vloop.ki = 1391;
}
/*
** ===================================================================
**     Funtion Name : void VrefCal(void)
**     Description : 输出参考电压计算，启动时参考电压慢慢增加
**     Parameters  : none
**     Returns     : none
** ===================================================================
*/
#define VBUS_REF 25448 //380V
void VrefCal(void)
{
    //只有在PFC运行状态下处理
    if(( Flag.PFCState == Run) || (Flag.PFCState ==Rise))
    {
        //当参考电压小于VBUS_REF 缓慢增加
        if( Vbus.Ref < VBUS_REF)
            Vbus.Ref  = Vbus.Ref +10;
        if( Vbus.Ref  > VBUS_REF )
            Vbus.Ref  = VBUS_REF;
    }
}
/*
** ===================================================================
**     Funtion Name : void VoltageLoop(void)
**     Description : 恒压环路
**     Parameters  : none
**     Returns     : none
** ===================================================================
*/
void VoltageLoop(void)
{
    //BUS电压环路计算，计算算bus电压与参考量之间误差    Q15
    Vloop.Err = Vbus.Ref  - Vbus.Avg ;
    //计算BUS电压环积分量=误差*KI
    Vloop.Inte += (Vloop.Err  * Vloop.ki);
    //对积分尽量进行最大最小值限制，防止溢出
    if(Vloop.Inte<0)
        Vloop.Inte=0;
    if(Vloop.Inte>33550000)//Q10*Q15
        Vloop.Inte=33550000;
    //计算BUS电压环输出量= 积分量+误差*KP
    Vloop.out = (Vloop.Inte + Vloop.Err * Vloop.kp) >> 10;
    //对环路输出最大最小进行限制
    if(Vloop.out<0)
        Vloop.out=0;
    if(Vloop.out>32767)//Q15
        Vloop.out=32767;
}
/*
** ===================================================================
**     Funtion Name : void VbusLimit(void)
**     Description : 母线电压限制
**     Parameters  : none
**     Returns     : none
** ===================================================================
*/
#define VBUS_LIMIT 28797//*V
#define VBUS_LIMIT_RE 26788//*V
void VbusLimit(void)
{
    static char OVPFlag=0;
    //只有在PFC运行状态下处理
    if(( Flag.PFCState == Run) || (Flag.PFCState ==Rise))
    {
        //当bus出现过压限制
        if(Vbus.Avg> VBUS_LIMIT)
        {
            //标志位置位
            OVPFlag=1;
            //关闭PWM波
            Flag.PWM = 0;
            //当出现过压时，电流参考量重新软起
            Iac.RefssK = 0;
            //当出现过压时，电流环积分量=0
            ILoop.Inte = 0;
        }
        //当发生过压后
        if(OVPFlag==1)
        {
            //当出现过压后，电压恢复到理想值后复位
            if(Vbus.Avg < VBUS_LIMIT_RE)
            {
                //标志位清零
                OVPFlag=0;
                //关闭PWM波
                Flag.PWM = 1;
                //当出现过压时，电流参考量重新软起
                Iac.RefssK = 0;
                //当出现过压时，电流环积分量=0
                ILoop.Inte = 0;
            }
        }
    }
}
/*
** ===================================================================
**     Funtion Name : void VbusUVP(void)
**     Description : 母线欠压保护，防止驱动损坏的情况下输出电压不能满足要求
**     Parameters  : none
**     Returns     : none
** ===================================================================
*/
#define VBUS_UVP_OFFSET 3349//50V
void VbusUVP(void)
{
    //只有在PFC运行状态下处理
    if(Flag.PFCState ==Run)
    {
        //当BUS电压< 线电压计算的BUS整流电压-50V时，发生欠压保护，PFC关闭PWM
        if(Vbus.Avg < (Vac.Rms - VBUS_UVP_OFFSET))
        {
            //关闭PWM波
            Flag.PWM = 0;
            //硬件过压标志位置位
            Flag.Err |=ERR_UVP;
            //进入故障状态
            Flag.PFCState = Err;
            //告知LLCPFC有故障
            PFCokDisable() ;
        }
    }
}
/*
** ===================================================================
**     Funtion Name : void VIacRmsCal(void)
**     Description : 输入电压，输入电流有效值计算
**     Parameters  : none
**     Returns     : none
** ===================================================================
*/
//正弦平均值至有效值转换系数  Q14 PI/(2*1.414)
#define AVG_TO_RMS  18198
//正弦有效值至最大值转换系数 Q14 1.414
#define RMS_TO_PEAK 23166
void VIacRmsCal(void)
{
    //累加标定，常规数字平均值累加求平均公式
    long couter=0;
    //电压电流有效值中间量
    long VacRmsTemp=0,IacRmsTemp=0;
    //电压电流有效值的平均值滑动滤波求和
    static long VacRmsSum=0,IacRmsSum=0;

    //有效值计算准备标志位置1？在进输入进入正半周期电压电流累加完成，进入负半周期>100次计数后该标志位置1
    //程序要正半周期进入数值累加，在负半周期进行计算
    if(Flag.RmsCalReady == 1)
    {
        //累加标定，常规数字平均值累加求平均计算
        couter= (long)65536/PLL.PosCnt;
        //清计数器
        PLL.PosCnt=0;
        //限幅
        if(couter<131)//45Hz
            couter=131;
        if(couter>204)//70Hz
            couter=204;
        //电压平均值 = 电压累加值/累加计数量 。电压有效值 = 电压平均值*平均值至有效值转换系数
        VacRmsTemp = (Vac.Sum *couter>>16)*AVG_TO_RMS>>14;
        //电流平均值 = 电流累加值/累加计数量 。电流有效值 = 电流平均值*平均值至有效值转换系数
        IacRmsTemp = (Iac.Sum *couter>>16)*AVG_TO_RMS>>14;
        //清电压累加量
        Vac.Sum = 0;
        //清电流累加量
        Iac.Sum = 0;
        //电压有效值滑动求平均（电压有效值的平均值）
        VacRmsSum = VacRmsSum + VacRmsTemp - (VacRmsSum>>3);
        Vac.Rms = VacRmsSum>>3;
        //计算R相电流有效值滑动求平均（电流有效值的平均值）
        IacRmsSum = IacRmsSum + IacRmsTemp - (IacRmsSum>> 3);
        Iac.Rms = IacRmsSum>>3;
        //电压最大值（正弦量）=R相电压有效值*1.414
        Vac.Peak= Vac.Rms * RMS_TO_PEAK>>14;
        //清有效值计算标志位
        Flag.RmsCalReady =0;
    }
}
/*
** ===================================================================
**     Funtion Name : void DCMFeedCal(void)
**     Description : DCM模式下前馈系数计算
**     Parameters  : none
**     Returns     : none
** ===================================================================
*/
//DCM情况下计算电流前馈系数Q10
#define DCM_FEED_COEFF  2946
//前馈加入功率恢复点Q30 50W
#define MIN_POWER_RE 7314816
//当输出功率小时，去前馈功率动作点Q30 25W
#define MIN_POWER   3657408
void DCMFeedCal(void)
{
    static long SqrtVac=0,SqrtIac=0,SqrtP=0,SqrtPAvg;//电压有效值、电流有效值、功率开根号,功率开根号平均,
    static long SqrtPSum=0;//功率开根号求平均和
    static long VacPeakTemp=0,VacPeakInv=0;
    static long TempQ15 = 32767;
    static long VacRmsTemp=0;
    static long PowerAc=0;
    static long temp=1073741824;
    //static long temp=11385;  //AC170V数字量

    //计算电压有效值开根号，查表法Q15
    SqrtVac=  *(sqrttable + (int)(Vac.Rms>>5));
    //计算电流有效值开根号，查表法Q15
    SqrtIac=  *(sqrttable + (int)(Iac.Rms>>5));
    //计算功率开根号，查表法Q15
    SqrtP = SqrtVac*SqrtIac>>15;

    //三相功率开根号求平均
    SqrtPSum = SqrtPSum + SqrtP - (SqrtPSum>>5);
    SqrtPAvg = SqrtPSum>>5;

    //计算电压峰值倒数限制，防止电压过小，倒数计算过大，限制170的峰值
    if(Vac.Peak <16098)
        VacPeakTemp = 16098;
    else
        VacPeakTemp = Vac.Peak;
    //计算倒数Q15
    VacPeakInv = (TempQ15<<15)/VacPeakTemp;
    //计算DCM前馈系数Q10
    Feed.dcmK = ( ((SqrtPAvg*VacPeakInv)>>15)*DCM_FEED_COEFF)>>15;

    //计算过程对电压进行限幅170V,避免求倒数值过大 AC电压最低值有效值Q15
    if(Vac.Rms< 11385)
        VacRmsTemp = 11385;
    else
        VacRmsTemp = Vac.Rms;
    //Vac.Rmsover2=1/(average phase voltage)^2
    Vac.Rmsover2= ((temp/VacRmsTemp)<<12) / VacRmsTemp;
    //Vac.Rmsover2= (((temp<<15) /VacRmsTemp)<<15) / VacRmsTemp;

    //计算单相功率平均值
    PowerAc = Vac.Rms*Iac.Rms;
    //当输出功率较小时，取消前馈
    if(PowerAc < MIN_POWER)
    {
        Flag.LowP = 1;//标志位置1
        Feed.ssK = 0;//取消前馈
    }
    if( Flag.LowP == 1)
    {
        if(PowerAc > MIN_POWER_RE)
        {
            Flag.LowP =0;
        }
    }
}
/*
** ===================================================================
**     Funtion Name : void IloopKpKiCal(void)
**     Description : 电流环KP KI变增益
**     Parameters  : none
**     Returns     : none
** ===================================================================
*/
//kp变增益计算函数K值Q12
#define DSP_ILOOP_KP_K  -2850
//kp变增益计算函数B值
#define DSP_ILOOP_KP_B  7979
 //电流环KP最大值限制
#define MAX_ILOOP_KP   6839
 //电流环KP最小限制
#define MIN_ILOOP_KP    3420
//ki变增益计算函数K值Q12
#define DSP_ILOOP_KI_K  -2898
//kI变增益计算函数B值
#define DSP_ILOOP_KI_B  8114
 //电流环KI最大值限制
#define MAX_ILOOP_KI    6955
 //电流环KI最小限制
#define MIN_ILOOP_KI     3478
void IloopKpKiCal(void)
{
    //static long VacRmsAvg=0,VacRmsSum=0;
    static long IacRmsAvg=0,IacRmsSum=0;

    //滑动求平均-8次
    //VacRmsSum=VacRmsSum+Vac.Rms-(VacRmsSum>>3);
    //VacRmsAvg=VacRmsSum>>3;
    IacRmsSum=IacRmsSum+Iac.Rms-(IacRmsSum>>3);
    IacRmsAvg=IacRmsSum>>3;

     //计算电流环KI变增益，随着电压越大，kp值越大
     ILoop.ki = ((IacRmsAvg*DSP_ILOOP_KI_K)>>12) + DSP_ILOOP_KI_B;
     //kI最大最小值限制
     if(ILoop.ki  > MAX_ILOOP_KI)
         ILoop.ki  = MAX_ILOOP_KI;
     if(ILoop.ki  < MIN_ILOOP_KI)
         ILoop.ki = MIN_ILOOP_KI;

     //计算电流环KP变增益，随着电压越大，kp值越大
     ILoop.kp = ((IacRmsAvg*DSP_ILOOP_KP_K)>>12) + DSP_ILOOP_KP_B;
     //kp最大最小值限制
     if(ILoop.kp > MAX_ILOOP_KP)
         ILoop.kp = MAX_ILOOP_KP;
     if(ILoop.kp < MIN_ILOOP_KP)
         ILoop.kp = MIN_ILOOP_KP;

     //ILoop.kp = 3700;
     //ILoop.ki  = 453;
}
/*
** ===================================================================
**     Funtion Name : void Feedss(void)
**     Description : 前馈系数软起
**     Parameters  : none
**     Returns     : none
** ===================================================================
*/
//前馈软启增量减量
#define FEED_SS_STEP 100
//电流环前馈软启系数最大值限制Q15 系数为1
#define MAX_FEED_SSK  32767
void Feedss(void)
{
    //软启动过程结束后，软启动系数逐渐增加
    if(Flag.ssFinsh == 1)
    {
        //软启动系数逐渐增加
        if(Feed.ssK < MAX_FEED_SSK)
        {
            Feed.ssK = Feed.ssK + FEED_SS_STEP;
            if(Feed.ssK> MAX_FEED_SSK)
                Feed.ssK = MAX_FEED_SSK;
        }
    }
}
/*
** ===================================================================
**     Funtion Name : void Irefss(void)
**     Description : 参考电流软起
**     Parameters  : none
**     Returns     : none
** ===================================================================
*/
//前馈软启增量减量
#define IREF_SS_STEP 200
//电流环前馈软启系数最大值限制Q15 系数为1
#define MAX_IREF_SSK  32767
void Irefss(void)
{
    if(Iac.RefssK < MAX_IREF_SSK)
    {
        Iac.RefssK = Iac.RefssK + IREF_SS_STEP;
        if(Iac.RefssK>MAX_IREF_SSK)
            Iac.RefssK =MAX_IREF_SSK;
    }
}
