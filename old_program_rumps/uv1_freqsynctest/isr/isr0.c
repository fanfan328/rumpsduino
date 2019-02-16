//------------------------------------------------------------------------------
//

#include "main.h"

// cannot and should not use printf(), data sharing problem
// message() should be reentrant (by default) 

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

#define RXDETECT_TH 1300

//shared with main0/main.c
#define F_SAMPLERATE 8
#define H_SAMPLERATE 4
#define Q_SAMPLERATE 2
#define TIMING_PILOTPAIR 8 // actually 8 timing pair, 1 freq pair
#define RX_STOP 0xa0
#define RX_DETECT 0xa1
#define RX_TIMING_SYNC 0xa2
#define RX_FREQ_SYNC 0xa3
#define RX_PAYLOAD 0xa4
#define RX_PHASECORR 0xa5
#define RX_DECODE 0xa6
#define RX_SENDUP 0xa7

volatile uint8_t rx_state;
int tempdump[2][3],
    ted_dump[TIMING_PILOTPAIR],
    timingsample_dump[TIMING_PILOTPAIR][6],
    trigdump;

// int codeword[768]; // temporary version
accum_int_t codeword[768]; // final version

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

  static volatile int testbuff = 0xfff;

  // signal variable
  static volatile int in_Idata = 0;
  static volatile int in_Qdata = 0;

  static volatile int Ibuff[3] = {0};
  static volatile int Qbuff[3] = {0};
  static volatile int8_t IQtrig = 0;

  // sampling point tracker
  static volatile int nextStep = 1; // init nextStep count
  static volatile int stepCount = 0; // init to trigger 1st time
  static volatile int nSentSamp = 0;
  static volatile unsigned int univ_ctr = 0; // general reusable counter

  // general buffer
  accum_int_t temp_accumint;

  if((TM_IF & 0x1) == 0x1){

    // Read data - I and Q
    in_Idata = (GPIO_DATAIN>>12) & 0xfff;
    in_Idata = (in_Idata << 20) >> 20; // integer signed extension
    GPIO_DATAOUT = (GPIO_DATAOUT & 0xfffff000) | (testbuff & 0xfff); //debug
    testbuff =  ~testbuff; 

    asm("push {r0}\n"
        "movs  r0, #7\n"      // 1 cycle
        "loop: sub  r0, r0, #1\n" // 1 cycle
        "cmp r0, #0\n"         // 1 cycle
        "bne  loop\n"          // 2 if taken, 1 otherwise
        "pop {r0}\n");

    in_Qdata = (GPIO_DATAIN>>12) & 0xfff;
    in_Qdata = (in_Qdata << 20) >> 20; // integer signed extension
    GPIO_DATAOUT = (GPIO_DATAOUT & 0xfffff000) | (testbuff & 0xfff); //debug
    testbuff =  ~testbuff; //debug

    // Detect TX signal - power level over 3 samples
    if(rx_state==RX_DETECT){
      if(++stepCount==nextStep){
        // init tracker for next sample
        stepCount = 0;
        nextStep = 1;

        // buffer samples
        Ibuff[univ_ctr] = in_Idata;
        Qbuff[univ_ctr] = in_Qdata;

        // check cumulative power level
        if(++univ_ctr==3){ 

          // I value triggers
          if( (abs_f(Ibuff[0])+abs_f(Ibuff[1])+abs_f(Ibuff[2]))>RXDETECT_TH ){
            trigdump = IQtrig = 1;
            univ_ctr = 0;
            nextStep = 1;
            rx_state = RX_TIMING_SYNC;
            GPIO_DATAOUT = (GPIO_DATAOUT & 0xfffff000) | (0xfff); //debug
            GPIO_DATAOUT = (GPIO_DATAOUT & 0xfffff000) | (0x000); //debug7
            // tempdump[0][0] = Ibuff[0];
            // tempdump[0][1] = Ibuff[1];
            // tempdump[0][2] = Ibuff[2];

            // tempdump[1][0] = Qbuff[0];
            // tempdump[1][1] = Qbuff[1];
            // tempdump[1][2] = Qbuff[2];
          }
          // Q value triggers
          else if( (abs_f(Qbuff[0])+abs_f(Qbuff[1])+abs_f(Qbuff[2]))>RXDETECT_TH ){
            trigdump = IQtrig = 2;
            univ_ctr = 0;
            nextStep = 1;
            rx_state = RX_TIMING_SYNC; 
            GPIO_DATAOUT = (GPIO_DATAOUT & 0xfffff000) | (0xfff); //debug
            GPIO_DATAOUT = (GPIO_DATAOUT & 0xfffff000) | (0x000); //debug
            // tempdump[0][0] = Ibuff[0];
            // tempdump[0][1] = Ibuff[1];
            // tempdump[0][2] = Ibuff[2];

            // tempdump[1][0] = Qbuff[0];
            // tempdump[1][1] = Qbuff[1];
            // tempdump[1][2] = Qbuff[2];
          }
          // no trigger - remove oldest sample in buffer
          else{
            univ_ctr = 2;

            Ibuff[0] = Ibuff[1];
            Ibuff[1] = Ibuff[2];

            Qbuff[0] = Qbuff[1];
            Qbuff[1] = Qbuff[2];
          }
        } 
        /*
        // if transmission is detected, next sample starts timing sync
        if(IQtrig>0){
          trigdump = IQtrig;
          univ_ctr = 0;
          nextStep = 1;
          rx_state = RX_TIMING_SYNC;
        }
        */
      }
    }

    // Timing offset correction - use 3 samples
    else if(rx_state==RX_TIMING_SYNC){
      if(++stepCount==nextStep){
        // send I Q to DSP core for synchronization
        while(noc_IC_txbuff_isfull==1)__NOP(); IC_NOC_TX_BUFF3 = rx_state;
        while(noc_IC_txbuff_isfull==1)__NOP(); IC_NOC_TX_BUFF3 = in_Idata;
        while(noc_IC_txbuff_isfull==1)__NOP(); IC_NOC_TX_BUFF3 = in_Qdata;

        // debug dump
        // timingsample_dump[univ_ctr][nSentSamp] = in_Idata;
        // timingsample_dump[univ_ctr][nSentSamp+3] = in_Qdata;

        // update sampling point
        stepCount = 0;
        nextStep = H_SAMPLERATE; // step size for 3 samples 
        
        // update next step per 3 samples - waiting calc from DSP core
        if(++nSentSamp==3){
          nSentSamp = 0;
          
          // receive TED from DSP core
          while(noc_IC_rxbuff3_av!=1)__NOP();
          int ted = IC_NOC_RX_BUFF3;
          nextStep = (F_SAMPLERATE) - ted; // full step 
          ted_dump[univ_ctr] = ted;

          // if timing sync is done
          if(++univ_ctr==TIMING_PILOTPAIR){
            GPIO_DATAOUT = (GPIO_DATAOUT & 0xfffff000) | (0xfff); //debug
            GPIO_DATAOUT = (GPIO_DATAOUT & 0xfffff000) | (0x000); //debug
            univ_ctr = 0;
            rx_state = RX_FREQ_SYNC; // should be RX_FREQ_SYNC later
          }
        }
      }

    }

    // Frequency offset correction
    else if(rx_state==RX_FREQ_SYNC){
      if(++stepCount==nextStep){

        // send I Q to DSP core for frequency offset calculation
        while(noc_IC_txbuff_isfull==1)__NOP(); IC_NOC_TX_BUFF3 = rx_state;
        while(noc_IC_txbuff_isfull==1)__NOP(); IC_NOC_TX_BUFF3 = in_Idata;
        while(noc_IC_txbuff_isfull==1)__NOP(); IC_NOC_TX_BUFF3 = in_Qdata;

        // next step is full bit duration
        nextStep = F_SAMPLERATE;
        stepCount = 0;

        // after 1 pair of freq sync bits, retrieve payload
        if(++univ_ctr==2){
          // reset counter
          univ_ctr = 0;

          // change state
          rx_state = RX_PAYLOAD;
        }
      }
    }

    // Retrieve payload
    else if(rx_state==RX_PAYLOAD){

      // Data retrieval 
      if(++stepCount==nextStep){
        // * temporary version - data
        // if(IQtrig==1)
        //   codeword[univ_ctr].int_cont = in_Idata;
        // else if(IQtrig==2)
        //   codeword[univ_ctr].int_cont = in_Qdata;

        // * final version - data
        // I and Q both needed for I correction, thus
        // I buffered in IOcore, Q in DSPcore
        codeword[univ_ctr].accum_cont = in_Idata; // get I data
        codeword[univ_ctr].int_cont >>= 10; // scale

        temp_accumint.accum_cont = in_Qdata; // get Q data
        temp_accumint.int_cont >>= 10; // scale
        while(noc_IC_txbuff_isfull==1)__NOP();
        IC_NOC_TX_BUFF3 = temp_accumint.int_cont; // send Q for buffering

        // get next bit
        nextStep = F_SAMPLERATE;
        stepCount = 0;

        //back to init state - reset counters and everything
        if(++univ_ctr==768){
          IQtrig = 0;
          nextStep = 1;
          stepCount = 0;
          nSentSamp = 0;
          univ_ctr = 0;

          rx_state = RX_PHASECORR; // phase-correct payload
          
          NVIC_DisableIRQ(6); // disable timer interrupt
          TM_CTRL = 0x3; // use PCLK, reset timer, enable timer
        }

      }
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
