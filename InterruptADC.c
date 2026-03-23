/****************************************************************************************
  *
  * @author  文七电源设计
  * @淘宝店铺链接：https://shop227154945.taobao.com
  * @file : InterruptADC.c
  * @brief: ADC中断函数
  * @version V1.0
  * @date 08-02-2021
  * @LegalDeclaration ：本文档内容难免存在Bug，仅限于交流学习，禁止用于任何的商业用途
  * @Copyright著作权文七电源所有
  * @attention
  *
  ******************************************************************************
  */

#include "InterruptADC.h"
#include "DSP28x_Project.h"
#include "Table.h"
#include "Interrupt200Hz.h"

//采样变量结构体
struct  _ADI SADC={0};
//锁相环结构体
struct _PLL PLL={0};
//输入电压结构体
struct _VAC Vac={0};
//输入电流结构体
struct _IAC Iac={0};
//BUS电压结构体
struct _VBUS Vbus={0};
//前馈结构体
struct _FEED Feed={0};
//电流环结构图
struct _ILOOP ILoop={0};
//电压环结构体
struct _VLOOP Vloop={0};
//控制标志位结构体
struct _FLAG Flag={0,0,0,0,0,0};

__interrupt void ISR_ADC(void)
{
    //SET_DEBG_GPIO24();

    //ADC采样函数，获取电感电流和输入电压
    ADCSample();
    //锁相环处理
    PhaseLockLoop();
    //前馈环计算
    FeedCal();
    // 参考电流计算
    IrefCal();
    //电流环计算
    CurrentLoop();
    //PWM寄存器更新,占空比
    RegReflash();
    //DMC模式电流采样矫正
    Icalibrate();
    //硬件过流保护
    HwOcp();

    //CLR_DEBG_GPIO24();//
    //清中断标志位
    AdcRegs.ADCINTFLGCLR.bit.ADCINT1 = 1;
    // Acknowledge interrupt to PIE
    PieCtrlRegs.PIEACK.all = PIEACK_GROUP1;
    return;
}

/*
** ===================================================================
**     Funtion Name :   void ADCSample(void)
**     Description :    采样MOS电流和输入电压,BUS电压
**     Parameters  :
**     Returns     :
** ===================================================================
*/
void ADCSample(void)
{
    //Mos电流,ADC本身为Q12位，统一将采样更换成Q15 （左移3位）
    //SADC.ImosOffset为电流偏置，参照MOS电流采样硬件电路，有0.87V的偏置
    SADC.Imos = (AdcResult.ADCRESULT0<<3) - SADC.ImosOffset;
    //对MOS电流限制
    if(SADC.Imos<0)
        SADC.Imos = 0;
    //PWMDAC(SADC.Imos);
    //DCM模式下电流采样矫正
    SADC.Imos = SADC.Imos*SADC.ImosK>>15;
    //母线电压 Q15
    SADC.Vbus = AdcResult.ADCRESULT1<<3;
    //输入电压L Q15
    SADC.VacL = AdcResult.ADCRESULT2<<3;
    //输入电压N Q15
    SADC.VacN = AdcResult.ADCRESULT3<<3;
    //计算输入电压VAC
    SADC.Vac = SADC.VacL -  SADC.VacN;

/*
    //求取输入电压绝对值
    if(SADC.Vac<0)
        SADC.VacAbs =  -SADC.Vac;
    else
        SADC.VacAbs = SADC.Vac;

    PWMDAC(SADC.VacAbs);
    */
}
/*
** ===================================================================
**     Funtion Name :  void PhaseLockLoop(void)
**     Description : 输入锁相环函数，求取合适的步长，输出正弦表的位置
**     Parameters  :
**     Returns     :
** ===================================================================
*/
//锁相环路表为1023表
#define MAX_SINE_CNT  1023
//锁相环路计算K值
#define PLL_K  3
//有效值开始计算标志位，=1表明可以进行有效值计算
char RmsCalReadyFlag=0;
void PhaseLockLoop(void)
{
    //输入电压>0 ，输入相电压进入正半周期
    if(SADC.Vac  > 0)
    {
        //输入电压之前为负，由负转正，且前期负半周期技术值>100
        if((PLL.Polar == 0)&&(PLL.NegCnt>100))
        {
            //计数器清零
            PLL.PosCnt=0;
            PLL.NegCnt=0;
            //输入电压标志位为正
            PLL.Polar = 1;
            //PLL求和清掉
            PLL.StepSum= 0;
            //计算PLL误差
            PLL.CntErr = MAX_SINE_CNT  - PLL.Cnt;
            //闭环计算PLL的步长
            PLL.Step += PLL.CntErr * PLL_K;
            if(PLL.Step >13005)
                PLL.Step = 13005;
            else if(PLL.Step < 8389)
                PLL.Step = 8389;
        }
        //正半周计数器计数累加
        PLL.PosCnt ++;
        //电压,电流值累加Q15
        Vac.Sum += SADC.Vac;
        Iac.Sum += SADC.Imos;
    }
    else if(SADC.Vac< 0)//当输入电压<0 为输入相电压进入负半周期
     {
         //输入电压首次由正转负，且前期正半周期技术值>100
         if((PLL.Polar == 1)&&(PLL.PosCnt>100))
         {
             //计数器
             PLL.NegCnt=0;
             //输入电压首次由负转正标志位清零
             PLL.Polar = 0;
             //PLL求和清掉
             PLL.StepSum = 0;
             //计算PLL误差，
             PLL.CntErr= MAX_SINE_CNT  - PLL.Cnt;
             //闭环计算PLL的步长
             PLL.Step += PLL.CntErr* PLL_K;

             if(PLL.Step >13005)
                 PLL.Step = 13005;
             else if(PLL.Step < 8389)
                 PLL.Step = 8389;
         }
         //负半周计数器计数累加
         PLL.NegCnt++;
         //当进入负半周期后，在100次控制中断周期后，有效值计算标志位置1，开始计算有效值
         if(PLL.NegCnt == 100)
             //有效值计算标志位置1    ，可以计算有效值
             Flag.RmsCalReady = 1;
     }

    //锁相环累加 = 锁相环输出+步长
    PLL.StepSum+= PLL.Step;
    //最终锁相值
    PLL.Cnt=  PLL.StepSum >> 12;

    //超出1024数表时，返回从0开始
    if(PLL.Cnt> MAX_SINE_CNT)
       PLL.CntFianl = PLL.Cnt - MAX_SINE_CNT;
   else
       PLL.CntFianl  = PLL.Cnt;

    //PWMDAC(PLL.CntFianl*16);
}
/*
** ===================================================================
**     Funtion Name :  vvoid FeedCal(void)
**     Description : DCM与CCM前馈计算
**     Parameters  :
**     Returns     :
** ===================================================================
*/
//前馈减小系数 0.95
#define FEED_K  31214
//380V母线电压倒数 V Q13
#define VBUS_OVER 10550
//正弦表变量
static int *SinePoint;
//开根号查表变量
int *sqrt_point=sqrttable;

void FeedCal(void)
{
    //根据锁相环输出点位查表获取输入正弦量
      SinePoint = PLL.CntFianl + sinetable;
      //计算构建输入电压时刻值= 锁相环输出正弦表位置*采样输入电压最大值Q12*Q15>>12=Q15 绝对值
      Vac.Sine=((long)(*SinePoint))*Vac.Peak>>12;
      //PWMDAC(Vac.Sine);
      //计算CCM模式下电流环电流前馈= 1 - 电网电压/bus电压    Q15-((Q15)*Q13)>>13=Q15
      Feed.ccm =32767 - (Vac.Sine * VBUS_OVER>>13);
      //PWMDAC(Feed.ccm);
      //电流环前馈参考量 = 电流前馈参考量*FEED_K   为了获取更好的THD值
      Feed.ccm = Feed.ccm*FEED_K>>15;
      //计算DCM模式下R相电流环前馈值2 = K*开根号CCM占空比Q15
      sqrt_point = sqrttable + (Feed.ccm>>5);
      Feed.dcm= Feed.dcmK * (*sqrt_point)>>10;
      //比较R相CCM模式下前馈值与DCM模式下前馈值，取前馈值 = 较小前馈之*前馈软启动系数，FeedSoftstart为前馈软启动系数
      if(Feed.dcm >  Feed.ccm)
          Feed.final =  Feed.ccm * Feed.ssK>>15;
      else
          Feed.final= Feed.dcm * Feed.ssK>>15;
}
/*
** ===================================================================
**     Funtion Name :  void IrefCal(void)
**     Description : 电流环计算
**     Parameters  :
**     Returns     :
** ===================================================================
*/
#define IREF_KM  3189//Km Q15
void IrefCal(void)
{
    //Current reference= (voltage loop output)*Vac*(1/Vac.rms^2)*Iref_softstart
    Iac.Ref = ((Vloop.out  * Vac.Sine>>15) * Vac.Rmsover2>>12)*IREF_KM>>15 ;
    //Iac.Ref = (Vloop.out  * Vac.Sine>>15) * Vac.Rmsover2>>15 ;
    //参考电流软启和在过流的情况下软启
    Iac.Ref = Iac.Ref * Iac.RefssK>>15;

    //最大参考电流限制-5A
    if(Iac.Ref >10920)
        Iac.Ref =10920;
    //PWMDAC(Iac.Ref);
}
/*
** ===================================================================
**     Funtion Name :  void CurrentLoop(void)
**     Description : 电流环计算
**     Parameters  :
**     Returns     :
** ===================================================================
*/
//电流环电流误差最大限制，输入电压最低，输出满载时电流的0.15A
#define MAX_IEER 328
#define MIN_IEER -328
//电流环积分最大值限制，防止积分溢出，50%最大占空比 扩大Q13
#define MAX_ILOOP_IN    134217728
//电流环积分最小值限制，防止积分溢出，50%最大占空比 扩大Q13
#define MIN_ILOOP_IN    -134217728
//环路最大输出Q15
#define MAX_DUTY 31130
void CurrentLoop(void)
{
    //电流误差= 电流参考量 - 电流值
    ILoop.Err = Iac.Ref - SADC.Imos;
    //电流误差计算最大值限制，大误差在动态时会导致占空比突变过大，造成电流过冲
    if(ILoop.Err>MAX_IEER)
        ILoop.Err = MAX_IEER;
    if(ILoop.Err< MIN_IEER)
        ILoop.Err =MIN_IEER;
    //电流环积分量 = 电流误差*KI + 电流环输出误差*退饱和系数 Q15*Q13+Q15*Q13=Q28
    ILoop.Inte +=  ILoop.ki * ILoop.Err ;
    //积分量限制
    if(ILoop.Inte > MAX_ILOOP_IN)
        ILoop.Inte = MAX_ILOOP_IN;
    if(ILoop.Inte < MIN_ILOOP_IN)
        ILoop.Inte = MIN_ILOOP_IN;
    //电流环环路输出 = 电流环路输出 +前馈量;
    ILoop.out = ((ILoop.kp * ILoop.Err + ILoop.Inte )>>13)+ Feed.final;

    //PWMDAC(Feed.final);
    //环路输出最大最小值限制
    if( ILoop.out < 100)   ILoop.out = 0;
   else if( ILoop.out> MAX_DUTY)   ILoop.out = MAX_DUTY;
}
/*
** ===================================================================
**     Funtion Name :  void RegReflash(void)
**     Description : PWM寄存器更新,占空比更新
**     Parameters  :
**     Returns     :
** ===================================================================
*/
void RegReflash(void)
{
    //关PWM量 = 软件OVP状态量* PFC开关机状态量，关PWM
    EALLOW;
    //Flag.PWM=0;
    //ILoop.out=16383;
    // adjust duty for output EPWM1A
    EPwm1Regs.CMPA.half.CMPA = (long)(32767-  ILoop.out*Flag.PWM) *667>>15;
    EDIS;
}
/*
** ===================================================================
**     Funtion Name :  void Icalibrate(void)
**     Description : DCM模式下电流采样矫正
**     Parameters  :
**     Returns     :
** ===================================================================
*/
void Icalibrate(void)
{
    //软启动结束后才软件过流保护才起作用,和DCM采样补偿参数计算
     if(Flag.ssFinsh == 1)
     {
         //计算矫正系数
         SADC.ImosK = ILoop.out * (DIV_TABLE[Feed.ccm>>5])>>5;

         //当输出功率较小时，DCM电流采样不需要矫正，矫正系数 = 1
         if(Flag.LowP==1)
             SADC.ImosK= 32767;
         //if DCM_calibrate>=0.9, set to 1. this process is to decrease the current overshot when DCM step into CCM
         if(SADC.ImosK >30767)
             SADC.ImosK =32767;
     }
}
/*
** ===================================================================
**     Funtion Name :void  HwOcp(void)
**     Description : 硬件过流保护
**     Parameters  :
**     Returns     :
** ===================================================================
*/
void  HwOcp(void)
{
    //硬件周期封波次数计数器
    static int OpcCnt=0;
    //10MS硬件封波计时器
    static long OcpTimeCnt=0;

    //当发生过流周期封波时
    if(( EPwm1Regs.TZFLG.bit.CBC==1)&&(Flag.PFCState!=Init))
    {
        //封波次数计数器累加
        OpcCnt++;
        //清封波标志位，等待下一个开关周期重启
        EALLOW;
        EPwm1Regs.TZCLR.bit.CBC=1;
        EDIS;
    }

    //10ms内连续封波次数大于100次，则发生严重过流故障
    if(OpcCnt > 100)
    {
        //计数器清0
        OpcCnt=0;
        //告知LLCPFC有故障
        PFCokDisable() ;
        //进入故障状态
        Flag.PFCState = Err;
        //硬件过压标志位置位
        Flag.Err |=ERR_OCP;
        //关闭PWM
        Flag.PWM = 0;
    }

    //当发生封波时
    if(OpcCnt >0)
    {
        //10ms计时器计时
        OcpTimeCnt++;
        //10ms计时结束
        if(OcpTimeCnt>450)
        {
            //封波计数器清0
            OpcCnt =0;
            //10ms计时器清0
            OcpTimeCnt=0;
        }
    }
}
