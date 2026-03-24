
#ifndef SCICOM_H_
#define SCICOM_H_

/************SCI_REQ_FLAG**********************/
#define     NEED_SENT 0x0001//需要发送信息标志位
#define     TX_FINISH            0x0002//信息正在发送中标志位
/************SCI_TX_S**********************/
#define     SCI_TX1                1
#define     SCI_TX2                2
/********************MESSAGE**********************/
#define     MESSAGE1 0x0001
#define     MESSAGE2 0x0002
#define     RCMD1 0b00010001
#define     RCMD2 0b00110011

typedef struct
{
    unsigned char      high;
    unsigned char       low;
} sci_dg;

struct _SCI_Reg
{
    unsigned int    RXCnt;
    unsigned int    TXCnt;
    unsigned int    RxBuffer;
    unsigned int    cmd;
    unsigned int    FlagS;
    unsigned int    RxFrame[6];
};

extern __interrupt void ISR_SCI(void);
void MesRxCmd(int RxCmd);
void MesStore(unsigned int mesId);
void TxDataRecord(void);
void FrameSent(void);
void FrameBuild(unsigned short *frame,sci_dg *data1,sci_dg *data2);
void SCI_TX(unsigned short *tx_pointer);
void SciCom(void);
void SCIValueInit(void);

#define setRegBits(reg, mask)                                   (reg |= (unsigned int)(mask))
#define getRegBits(reg, mask)                                   (reg & (unsigned int)(mask))
#define clrRegBits(reg, mask)                                   (reg &= (unsigned int)(~(unsigned int)(mask)))

#endif /* SCICOM_H_ */
