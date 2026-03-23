/*
 * Interrupt200Hz.h
 *
 *  Created on: 2021年8月3日
 *      Author: chenge-bjb06
 */

#ifndef INTERRUPT200HZ_H_
#define INTERRUPT200HZ_H_

#define RelayOn()   GpioDataRegs.GPBSET.bit.GPIO32 = 1
#define RelayOff()   GpioDataRegs.GPBCLEAR.bit.GPIO32 = 1

/****************************函数声明***************************/
__interrupt void ISR_200Hz(void);
void StateM(void);
void StateMInit(void);
void StateMWait(void);
void StateMRise(void);
void StateMRun(void);
void StateMErr(void);
void VariableInit(void);
void ResetVILoop(void);
char ImosOffsetCal(void);
void VacCheck(void);
void PWMDAC(long para);

#endif /* INTERRUPT200HZ_H_ */
