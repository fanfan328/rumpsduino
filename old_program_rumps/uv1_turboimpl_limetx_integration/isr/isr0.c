//------------------------------------------------------------------------------
//

#include "main.h"

// cannot and should not use printf(), data sharing problem
// message() should be reentrant (by default) 

#define TIMINGPILOT_LEN 16
#define FREQPILOT_LEN 2
#define CODEWORD_LEN 768
#define RF_PKTLEN TIMINGPILOT_LEN + FREQPILOT_LEN + CODEWORD_LEN

#define RCOS_FIRLEN 12

unsigned int systick_cnt = 0;
unsigned int ext_0_cnt = 0;
unsigned int ext_1_cnt = 0;
unsigned int ext_2_cnt = 0;
unsigned int ext_3_cnt = 0;
unsigned int ext_4_cnt = 0;
unsigned int ext_5_cnt = 0;
unsigned int ext_6_cnt = 0;
unsigned int ext_7_cnt = 0;
unsigned int ext_8_cnt = 0;
unsigned int ext_9_cnt = 0;
unsigned int ext_10_cnt = 0;
unsigned int ext_11_cnt = 0;
unsigned int ext_12_cnt = 0;
unsigned int ext_13_cnt = 0;
unsigned int ext_14_cnt = 0;
unsigned int ext_15_cnt = 0;

//shared with main0/main.c
#define TX_SETUP 0xa0
#define TX_INITLIME 0xa1
#define TX_IDLE 0xa2
#define TX_ENCODE 0xa3
#define TX_ON 0xa4

volatile uint8_t tx_state; 
unsigned char sendPkt[RF_PKTLEN];

//**for pulse shaping
int rcos_lut[2][RCOS_FIRLEN]; // raised cosine impulse response LUT
int arrctr0, arrctr1, arrctr2, arrctr3, arrctr4; // pulse components counter
unsigned int symctr0, symctr1, symctr2, symctr3, symctr4; // symbol counter
volatile int send_buff;


//-----------------------------------------------------------------------------------
void NMI_Handler(void)
{
  int i;
  
  __SEV();
  
  //write32(0x200, 0x11111111);
    //while(1);
    for (i = 0; i < 2000; i ++);
    __SEV();
}

//-----------------------------------------------------------------------------------
void HARD_FAULT_Handler(void)
{
  write32(0x200, 0x22222222);
  while(1);
}

//-----------------------------------------------------------------------------------
void SYSTICK_Handler(void)
{
  __SEV();
    systick_cnt++;
    write32(0x200, 0x33333333);
  while(1);
}

//-----------------------------------------------------------------------------------
void EXTERNAL_0_Handler(void)
{
  int i;
  
  __SEV();
  ext_0_cnt++;
  NVIC_ClearPendingIRQ(0);
  
  for (i = 0; i < 200; i ++);
  
  __SEV();
}

//-----------------------------------------------------------------------------------
void EXTERNAL_1_Handler(void)
{
  __SEV();

  NVIC_ClearPendingIRQ(1);
    ext_1_cnt++;
}

//-----------------------------------------------------------------------------------
void EXTERNAL_2_Handler(void) // pp_rx
{
  __SEV();
  
  NVIC_DisableIRQ(2);     // disable PP_RX interrupt until the data processed

  //NVIC_ClearPendingIRQ(2);
  ext_2_cnt++;
}

//-----------------------------------------------------------------------------------
void EXTERNAL_3_Handler(void)
{
  __SEV();

  NVIC_ClearPendingIRQ(3);
    ext_3_cnt++;
}

//-----------------------------------------------------------------------------------
// NoC RX Int
void EXTERNAL_4_Handler(void)
{
  unsigned int data;
  
  __SEV();
  
  NVIC_DisableIRQ(4);     // disable NOC RX interrupt until the data processed

  //NVIC_ClearPendingIRQ(4);
    ext_4_cnt++;
}

//-----------------------------------------------------------------------------------
void EXTERNAL_5_Handler(void)
{
  __SEV();

  NVIC_ClearPendingIRQ(5);
    ext_5_cnt++;
}

//-----------------------------------------------------------------------------------
//TIMER
void EXTERNAL_6_Handler(void)
{
  __SEV();

  static volatile int firstcall = 1; // sync on first INT_ENABLE
  static volatile int testbuff = 0xfff;
  static volatile unsigned int txd_sendctr = 0;
  static volatile unsigned int tempctr = 0;

  if((TM_IF & 0x1) == 0x1){

    if( firstcall==0 && tx_state==TX_ON ){
      
      /*
      // Without pulse shaping
      if(sendPkt[txd_sendctr]==0)
        GPIO_DATAOUT = (GPIO_DATAOUT & 0xfffff000) | 0x800;
      else
        GPIO_DATAOUT = (GPIO_DATAOUT & 0xfffff000) | 0x7ff;
      if(++tempctr==5){
        tempctr = 0;
        if(++txd_sendctr==768)
          txd_sendctr = 0;
      }
      */

      //GPIO_DATAOUT = (GPIO_DATAOUT & 0xfffff000) | (testbuff & 0xfff);
      //testbuff =  ~testbuff; 

      // With pulse shaping
      GPIO_DATAOUT = (GPIO_DATAOUT & 0xfffff000) | (send_buff & 0xfff);
      
      arrctr0++;
      if(arrctr0 == RCOS_FIRLEN){
        arrctr0 = 0;
        symctr0 += 3;
      }

      arrctr1++;
      if(arrctr1 == RCOS_FIRLEN){
        arrctr1 = 0;
        symctr1 +=3;
      }

      arrctr2++;
      if(arrctr2 == RCOS_FIRLEN){
        arrctr2 = 0;
        symctr2 += 3;
      }
      
      // arrctr3++;
      // if(arrctr3 == RCOS_FIRLEN){
      //   arrctr3 = 0;
      //   symctr3 += 3; 
      // }

      // arrctr4++;
      // if(arrctr4 == RCOS_FIRLEN){
      //   arrctr4 = 0;
      //   symctr4 += 3;
      // }
      
      if(symctr0>=RF_PKTLEN && symctr1>=RF_PKTLEN && symctr2>=RF_PKTLEN){ 
        // && symctr3>767 && symctr4>767){

        arrctr0 = 0;
        arrctr1 = -4;
        arrctr2 = -8;
        //arrctr3 = -12;
        //arrctr4 = -16;

        symctr0 = 0;
        symctr1 = 1;
        symctr2 = 2;
        //symctr3 = 3;
        //symctr4 = 4;

        // reset output buffer and TX line
        send_buff = rcos_lut[sendPkt[symctr0]][arrctr0];
        GPIO_DATAOUT = (GPIO_DATAOUT & 0xfffff000);
        //testbuff = 0x400;

        txd_sendctr = 0;
        firstcall = 1;

        tx_state = TX_IDLE;

        NVIC_DisableIRQ(6); // disable this timer interrupt
        TM_CTRL = 0x3; // use PCLK, reset timer, enable timer
      }
      
      send_buff = 0;

      if((arrctr0>=0) && (symctr0<RF_PKTLEN))
        send_buff += rcos_lut[sendPkt[symctr0]][arrctr0];

      if((arrctr1>=0) && (symctr1<RF_PKTLEN))
        send_buff += rcos_lut[sendPkt[symctr1]][arrctr1];

      if((arrctr2>=0) && (symctr2<RF_PKTLEN))
        send_buff += rcos_lut[sendPkt[symctr2]][arrctr2];
      
      // if((arrctr3>=0) && (symctr3<768))
      //   send_buff += rcos_lut[sendPkt[symctr3]][arrctr3];

      // if((arrctr4>=0) && (symctr4<768))
      //   send_buff += rcos_lut[sendPkt[symctr4]][arrctr4];
      
    }
    
    else{
      firstcall = 0;
    }

    TM_IF = 0x0;

  }  

  NVIC_ClearPendingIRQ(6);
    ext_6_cnt++;  

}

//-----------------------------------------------------------------------------------
void EXTERNAL_7_Handler(void)
{
  __SEV();

  NVIC_ClearPendingIRQ(7);
    ext_7_cnt++;
}

//void ioc_recv_uart();

//-----------------------------------------------------------------------------------
void EXTERNAL_8_Handler(void)
{
  __SEV();
  
  NVIC_ClearPendingIRQ(8);
    ext_8_cnt++;
    
  //ioc_recv_uart();
  //GPIO_IF = 0x1;
}

//-----------------------------------------------------------------------------------
void EXTERNAL_9_Handler(void)
{
  __SEV();

  NVIC_ClearPendingIRQ(9);
    ext_9_cnt++;
}

//-----------------------------------------------------------------------------------
void EXTERNAL_10_Handler(void)
{
  __SEV();

  NVIC_ClearPendingIRQ(10);
    ext_10_cnt++;
}

//-----------------------------------------------------------------------------------
void EXTERNAL_11_Handler(void)
{
  __SEV();

  NVIC_ClearPendingIRQ(11);
    ext_11_cnt++;
}

//-----------------------------------------------------------------------------------
void EXTERNAL_12_Handler(void)
{
  __SEV();

  NVIC_ClearPendingIRQ(12);
    ext_12_cnt++;
}

//-----------------------------------------------------------------------------------
void EXTERNAL_13_Handler(void)
{
  __SEV();

  NVIC_ClearPendingIRQ(13);
    ext_13_cnt++;
}

//-----------------------------------------------------------------------------------
void EXTERNAL_14_Handler(void)
{
  __SEV();

  NVIC_ClearPendingIRQ(14);
    ext_14_cnt++;
}

//-----------------------------------------------------------------------------------
void EXTERNAL_15_Handler(void)
{
  __SEV();

  NVIC_ClearPendingIRQ(15);
    ext_15_cnt++;
}
