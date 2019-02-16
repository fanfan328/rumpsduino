//------------------------------------------------------------------------------
//
// Main Program
// Application		: turboimpl_nc1
// Core						: Normal Core 1
// Purpose
//	- Turbo code implementation on RUMPS401
//  - > convert LLR(ratio) -> Le(probability) format

#include "main.h"
#include "turbo_rumps_c2.h"

const unsigned char ledpin = 0;

void main_scheduler (void);

//------------------------------------------------------------------------------
int main(void)
{
  set_trellis();
  pinMode_output(ledpin);
  

  //Timer Settings - Lime interfacing
  TM_PR = 0x1; // prescale target, 4MHz 
  TM_COMR0 = 0x0; //timer target, ch 0
  TM_COMCR = 0x3; // reset on COMR, enable interrupt
  TM_OCR = 0x6; // toggle TM_COM0/COM1 on match

  TM_CTRL = 0x3; // use PCLK, reset timer, enable timer
  TM_CTRL = 0x1; // use PCLK, start timer, enable timer
  
  // MUX - select TM_COM0 & TM_COM1
  MUXC_SELECT = 0x4;

  accum_int_t temp_accumint; //temp variable for manipulating bits between accum and int type
  accum ratio;
  signed char calcLLR = -1; // -1 means yet to be informed by DSP core
  
  short sent_i = 0;
  
  while(1){    
    // receive calcLLR control flag
    if( (calcLLR==-1) && ((NC_NOC_CSR0 & 0x40)==0x40) ){
      calcLLR = NC_NOC_RX_BUFF3;
      NC_NOC_TX_BUFF3 = calcLLR;
    }
    // && (((NC_NOC_CSR2&0xf0000000)>>28)>=0x2)
    if( (calcLLR!=-1) && ((NC_NOC_CSR0 & 0x40)==0x40) ){
      
      // ## RECV_RATIO_DSP
      // Receive LLR format (ratio) from DSP
      temp_accumint.int_cont = NC_NOC_RX_BUFF3;
      ratio = temp_accumint.accum_cont;
      
      //change ratio -> prob for Le
      if(!calcLLR){
        ratio = exp_approx(ratio);
        ratio /= (1+ratio);
        if(ratio > 0.99990)
          ratio = 0.99990;
        else if(ratio < 0.0001)
          ratio = 0.0001;
      }
      
      // send back ACK to DSP core
      if( ((sent_i+1) & 7)==0 ){
        while((NC_NOC_CSR1 & 0x2)==0x2)__NOP();
        NC_NOC_TX_BUFF3 = sent_i+1;
      }
      
      // ## SEND_CONVERTED_IO
      // Send converted/non-converted LLR/Le to IO core
      while((NC_NOC_CSR1 & 0x2)==0x2)__NOP();
      temp_accumint.accum_cont = ratio;
      NC_NOC_TX_BUFF0 = temp_accumint.int_cont;
            
      // wait for ACK from IO core
      while((NC_NOC_CSR0 & 0x1)!=0x1)__NOP();
      int temp_ack = NC_NOC_RX_BUFF0;
      
      //blink LED
      sent_i++;
      
      // reset flag & counter
      if(sent_i==256){
        calcLLR = -1;
        sent_i = 0;
      }
      
      if( (sent_i & 31) == 0 )
  	    GPIO_BTGL = 0x1 << ledpin;
    }
	  
  }
   
	return 0;
}
  
