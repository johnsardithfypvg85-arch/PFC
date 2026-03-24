/*
 * Interrupt1kHz.h
 *
 *  Created on: 2021Äê8ÔÂ3ÈÕ
 *      Author: chenge-bjb06
 */

#ifndef INTERRUPT1KHZ_H_
#define INTERRUPT1KHZ_H_

__interrupt void ISR_1kHz(void);
void VbusAvgCal(void);
void VloopKpKiCal(void);
void VrefCal(void);
void VoltageLoop(void);
void VbusLimit(void);
void VbusUVP(void);
void VIacRmsCal(void);
void DCMFeedCal(void);
void IloopKpKiCal(void);
void Feedss(void);
void Irefss(void);

#endif /* INTERRUPT1KHZ_H_ */
