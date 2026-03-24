/*
 * InterruptADC.h
 *
 *  Created on: 2021年8月2日
 *      Author: Cr
 */

#ifndef INTERRUPTADC_H_
#define INTERRUPTADC_H_

extern struct  _ADI SADC;
extern struct _VAC Vac;
extern struct _VLOOP Vloop;
extern struct _PLL PLL;
extern struct _FLAG Flag;
extern struct _VBUS Vbus;
extern struct _IAC Iac;
extern struct _ILOOP ILoop;
extern struct _FEED Feed;

extern int sqrttable[1024];

//状态机枚举量
typedef enum
{
    Init,//初始化
    Wait,//空闲等待
    Rise,//软启
    Run,//正常运行
    Err//故障
}STATE_M;

#define PFCokDisable()   GpioDataRegs.GPACLEAR.bit.GPIO22 = 1
#define PFCokEnable()   GpioDataRegs.GPASET.bit.GPIO22 = 1

//DEBUG IO
#define SET_DEBG_GPIO24()  GpioDataRegs.GPASET.bit.GPIO24 = 1
#define CLR_DEBG_GPIO24()  GpioDataRegs.GPACLEAR.bit.GPIO24 = 1
/*****************PFC 故障信息常量定义*****************************/
#define ERR_AC_LOW 0x0001  //AC输入欠压保护
#define ERR_AC_HIGH 0x0002  //AC输入过压保护
#define ERR_OCP 0x0004  //电感硬件过流保护
#define ERR_UVP 0x0008  //BUS欠压保护

//采样变量结构体
struct _ADI
{
    long Imos;//MOS电流变量Q15
    long ImosOffset;//MOS电流偏置变量Q15
    long ImosK;//DCM模式下电流采样矫正系数
    long VacL;//输入电压L变量 Q15，
    long VacN;//输入电压N变量 Q15，
    long Vac;//输入电压变量 Q15
    long VacAbs;//输入电压变量 Q15
    long Vbus;//母线电压变量 Q15，
};

//锁相环结构体
struct _PLL
{
    char Polar;//Vac极性,电压负半边向正半边相互转变标志位
    long PosCnt;//正半周期输入电压计数@每个开关周期累计
    long NegCnt;//负半周期输入电压计数@每个开关周期累计
    long Step;//锁相环步长
    long StepSum;//锁相环输出的step叠加,实际上是锁相环位置量乘以Q12
    long CntErr;//锁相环误差
    long Cnt;//当前锁相环输出的正弦表位置
    long CntFianl;//最终当前锁相环输出的正弦表位置
};
//输入电压结构体
struct _VAC
{
    long Sum;//电压累加，用以计算有效值用
    long Peak;//峰值电压
    long Rms;//输入电压有效值
    long Abs;//输入电压绝对值变量 Q15--对应馒头波
    long Sine;//构建的正弦电压
    long Rmsover2;//输入电压有效值倒数的平方
};
//输入电流结构体
struct _IAC
{
    long Sum;//电流累加，用以计算有效值用
    long Rms;//输入电压有效值
    long Ref;//电流参考值
    long RefssK;//参考电流软起动系数
};
//BUS电压结构体
struct _VBUS
{
    long Avg;//平均值
    long Ref;//电压参考值
};
//前馈结构体
struct _FEED
{
    long ccm;//CCM模式前馈占空比
    long dcm;//DCM模式前馈占空比
    long final;//最终前馈输出
    long dcmK;//系数
    long ssK;//软起动系数，启动时，该值慢慢增加
};
//电流环结构体
struct _ILOOP
{
    long out;//电流环输出
    long Err;//电流环误差
    long kp;
    long ki;
    long Inte;//电流环积分
};
//电压环输出
struct _VLOOP
{
    long out;//电压环输出
    long Err;//电压环误差
    long kp;
    long ki;
    long Inte;//电流环积分

};
//控制标志位结构体
struct _FLAG
{
    unsigned char RmsCalReady;//有效值计算标志位
    unsigned char ssFinsh;//软起结束标志位
    unsigned char PWM;//PWM开启关断标志位
    unsigned char LowP;//低功率
    unsigned char PFCState;//PFC状态机量
    unsigned long Err;//故障标志位
};

#pragma CODE_SECTION(ISR_ADC, "ramfuncs");
#pragma CODE_SECTION(ADCSample, "ramfuncs");
#pragma CODE_SECTION(PhaseLockLoop, "ramfuncs");
#pragma CODE_SECTION(FeedCal, "ramfuncs");
#pragma CODE_SECTION(IrefCal, "ramfuncs");
#pragma CODE_SECTION(CurrentLoop, "ramfuncs");
#pragma CODE_SECTION(RegReflash, "ramfuncs");
#pragma CODE_SECTION(Icalibrate, "ramfuncs");
#pragma CODE_SECTION(HwOcp, "ramfuncs");
__interrupt void ISR_ADC(void);
void ADCSample(void);
void PhaseLockLoop(void);
void FeedCal(void);
void IrefCal(void);
void CurrentLoop(void);
void RegReflash(void);
void Icalibrate(void);
void HwOcp(void);

#endif /* INTERRUPTADC_H_ */
