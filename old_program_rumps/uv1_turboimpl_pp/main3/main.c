//------------------------------------------------------------------------------

//

// Main Program
// Application		: turboimpl_dspc_pp
// Core						: DSP Core
// Purpose
//	- Turbo code implementation on RUMPS401
//  - > calc d, LLR

#include "main.h"
#include "turbo_rumps_c3.h"
//#include "libdivide_rumps.h"

#define IO_CHNLCTRL_HDR 0x1
#define IO_BITS_HDR 0x2
#define IO_LLRACK_HDR 0x3

#define DSP_LLR_HDR 0x31
#define DSP_BITSACK_HDR 0x32


const unsigned char ledpin = 0;


void main_scheduler (void);


//------------------------------------------------------------------------------

int main(void)

{
  set_trellis();
  
          // CPHA, CPOL, BC, IE,  FSB, SS,  CR
  spi_init( 0x0, 0x0, 0x3, 0x0, 0x0, 0x1, 0x1);
  spi_enable();
  // mux
  MUXC_SELECT = 0x00000010;	// SPI selected
  MUXC_PU &= 0x0fffffff;	// SPI PU disabled

  // Define variables
  short iorecv_i = 0; //count bits received from IO core
	short retllr_i = 0; //count returned LLR calculation
	short delta_i = 0; //count sent delta
	char alpha_i = 0; //count received alpha (increment)
	char beta_i = 31; //count received beta (decrement)
	
	accum iorecv_pbit[(r_win<<1)]; //store parity bit, for Le (2*windows)
	accum local_deltabuff[(r_win<<1)][16]; //local delta buffer for 2*windows
	accum local_alphabuff[r_win][8];
	accum local_betabuff[r_win][8];
	
	accum temp_sysbit;
	accum temp_ln_ap1;
	accum temp_ln_ap0;
	accum noise_var;
	
	unsigned char calcLLR;
	unsigned char iorecv_hold = 0; //flag for receiving data from IO core
	unsigned char deltasend_hold = 0; //flag for sending delta over to NCs
	unsigned char llrsend_hold = 0; //flag for sending LLR to IO core
	
	unsigned char flag_pkt_type = 0;
	
  accum_int_t temp_accumint; //temp variable for manipulating bits between accum and int type
  accum_int_t temp_ai[4];
  
  //init beta here since the last index is constant for all window
  for(char i=0; i<8; i++)
	  local_betabuff[r_win-1][i] = -2.0794;
  

  while(1){
  	
    // ## DTRMN_PKT_TYPE
    // Examine packet header
    if( ((NC_NOC_CSR0 & 0x1)==0x1) && (flag_pkt_type==0)  )
      flag_pkt_type = NC_NOC_RX_BUFF0;
  	  	
  	// ## RECV_CTRL_IO - receive calcLLR flag and noise_var from IO core
  	//only for 1st bit (checking method is chosen so we dont need additional flag)
  	if( flag_pkt_type==IO_CHNLCTRL_HDR && ((NC_NOC_CSR0 & 0x1)==0x1) && (((NC_NOC_CSR2&0xf0000)>>16)>=0x2)  ){  
		  flag_pkt_type = 0; //clear packet type's flag
		  
  	  calcLLR = NC_NOC_RX_BUFF0;
  	  
  	  temp_accumint.int_cont = NC_NOC_RX_BUFF0;
  	  noise_var = temp_accumint.accum_cont;
  	  
  	  //send back ACK - just send back calcLLR
  	  while((NC_NOC_CSR1 & 0x2)==0x2)__NOP(); NC_NOC_TX_BUFF0 = calcLLR;
  	  
  	  // change NoC buffer control (Later on interrupt version!!)
  	  // **left the CSR even though it is not used, for checking purpose
  	  // from IOC - flag per 3 flits
  	  // from NCs - flag per 8 flit
  	  //NC_NOC_CSR2 &= 0xffff0000;
  	  //NC_NOC_CSR2 |= 0x1884;
  	  //NC_NOC_CSR1 = 0x10000000; //set NOC_RIM
  	  
  	}
  	
  	// ## RECV_BITS_IO
  	// Receive data info from IO core, calc delta
  	//if( (iorecv_hold==0) && ((NC_NOC_CSR0 & 0x1)==0x1) && (((NC_NOC_CSR2&0xf0000)>>16)==0x3)){
    if( (flag_pkt_type==IO_BITS_HDR) && ((NC_NOC_CSR0 & 0x1)==0x1) && (((NC_NOC_CSR2&0xf0000)>>16)>=0x4) ){    	 
  		
  		//receive sysbit, parbit, apriori from NoC
  		temp_accumint.int_cont = NC_NOC_RX_BUFF0; 
  		temp_sysbit = temp_accumint.accum_cont;
  		
  		temp_accumint.int_cont = NC_NOC_RX_BUFF0; 
  		iorecv_pbit[iorecv_i & ((r_win<<1)-1)] = temp_accumint.accum_cont;
  		
  		temp_accumint.int_cont = NC_NOC_RX_BUFF0; 
  		temp_ln_ap1 = temp_accumint.accum_cont;
  	  
  	  temp_accumint.int_cont = NC_NOC_RX_BUFF0;
  	  temp_ln_ap0 = temp_accumint.accum_cont;
  	    		
  		//calculate, store delta
  		r_turbodec_dcalc(temp_sysbit, iorecv_pbit[iorecv_i & ((r_win<<1)-1)], local_deltabuff[iorecv_i & ((r_win<<1)-1)], temp_ln_ap1, temp_ln_ap0, noise_var);
      
  		iorecv_i++;
      
      spi_send(0x31000000+iorecv_i);
  		
  		//send back ACK
  	  while((NC_NOC_CSR1 & 0x2)==0x2)__NOP(); NC_NOC_TX_BUFF0 = DSP_BITSACK_HDR; // Header - bits ack
  	  while((NC_NOC_CSR1 & 0x2)==0x2)__NOP(); NC_NOC_TX_BUFF0 = iorecv_i;
  		
		  flag_pkt_type = 0; //clear packet type's flag
  		
  		//blink LED
      //if( (iorecv_i & (r_win-1))==0 )
  	    //GPIO_BTGL = 0x1 << ledpin;
  	  
  		//stop receiving if it received two windows and none decoded completely
  		if( iorecv_i==256 || ((iorecv_i&(r_win-1))== 0 && ((iorecv_i>>r_win_mul)-(retllr_i>>r_win_mul))>=2) )
  			iorecv_hold = 1;
  	}
  	
  	// ## SEND_DELTA_NC
  	// Send delta to NC0
  	if( (iorecv_i>delta_i) && (deltasend_hold==0) && ((iorecv_i & (r_win-1))==0) ){
  	  for(char i=0; i<r_win; i++){
    	  for(char j=0; j<16; j++){
    		  temp_accumint.accum_cont = local_deltabuff[delta_i & ((r_win<<1)-1)][j]; 
    		  
    		  while((NC_NOC_CSR1 & 0x2)==0x2)__NOP(); NC_NOC_TX_BUFF1 = temp_accumint.int_cont;
        }
        delta_i++;
        while((NC_NOC_CSR0 & 0x4)!=0x4)__NOP(); int tempack = NC_NOC_RX_BUFF1;
      }
      //if( ((delta_i & (r_win-1))==0) && (((delta_i>>r_win_mul)-(retllr_i>>r_win_mul))>=1))
      deltasend_hold = 1;
      spi_send(0x30000001); //~~~DEBUG, done sending d for a window
  	
  	  //blink led
  	  //if( (delta_i & (r_win-1))==0 )
	      //GPIO_BTGL = 0x1 << ledpin;
  	}
  	
  	// ## WAIT_ALPHA_BETA_NC
  	// Wait for alpha and beta from NC0
  	if( ((NC_NOC_CSR0 & 0x4)==0x4) && (alpha_i<32) && (beta_i>0) && (((NC_NOC_CSR2&0xf00000)>>20)>=0x8)){
  	  // receive alpha
      for(char j=0; j<r_win; j++){
    	  for(char i=0; i<8; i++){
    	    while((NC_NOC_CSR0 & 0x4)!=0x4)__NOP();
      	  temp_accumint.int_cont = NC_NOC_RX_BUFF1;
      	  local_alphabuff[alpha_i][i] = temp_accumint.accum_cont;
    	  }
        alpha_i++;
        while((NC_NOC_CSR1 & 0x2)==0x2)__NOP(); NC_NOC_TX_BUFF1 = alpha_i;
	    }
      // receive beta
      for(char j=0; j<r_win-1; j++){
    	  for(char i=0; i<8; i++){
    	    while((NC_NOC_CSR0 & 0x4)!=0x4)__NOP();
      	  temp_accumint.int_cont = NC_NOC_RX_BUFF1;
      	  local_betabuff[beta_i-1][i] = temp_accumint.accum_cont;
    	  }
        beta_i--;
        while((NC_NOC_CSR1 & 0x2)==0x2)__NOP(); NC_NOC_TX_BUFF1 = beta_i;
  	  }
      spi_send(0x30000002); //~~~DEBUG, done recv a/b for a window
  	}
  	
  	//if( ((NC_NOC_CSR0 & 0x10)==0x10) && (alpha_i<32) && (((NC_NOC_CSR2&0xf000000)>>24)>=0x8) ){
  	//}
  	  	
  	// ## CALC_LLR_NC 
  	// Calculate and send it to NC for conversion of LLR->Le
  	
		// check for ACK per x flits sent
		if( (NC_NOC_CSR0 & 0x10)==0x10 ){
		  int temp_ack = NC_NOC_RX_BUFF2;
		  llrsend_hold = 0;
		}
  	
  	// Sending part
  	if( (alpha_i==32) && (beta_i==0) && (llrsend_hold==0) ){
  	  //send and wait ACK per x flits
  	  temp_accumint.accum_cont = r_turbodec_llrcalc(local_deltabuff[retllr_i & ((r_win<<1)-1)], local_alphabuff[retllr_i & (r_win-1)],
  	                                 local_betabuff[retllr_i & (r_win-1)], iorecv_pbit[retllr_i & ((r_win<<1)-1)], calcLLR, noise_var);
  	    
      if(retllr_i==0){ //send calcLLR on beginning of a half-iteration
        while((NC_NOC_CSR1 & 0x2)==0x2)__NOP();
        NC_NOC_TX_BUFF2 = calcLLR;
        while((NC_NOC_CSR0 & 0x10)!=0x10)__NOP(); //wait for ACK
        int tempack = NC_NOC_RX_BUFF2;
      }
      
      while((NC_NOC_CSR1 & 0x2)==0x2)__NOP();
      NC_NOC_TX_BUFF2 = temp_accumint.int_cont;
      
      if( (retllr_i & 31)==0 )
        spi_send(0x300000a3); //~~DEBUG, 
      
      retllr_i++;
      
      if( (retllr_i & 7)==0 )
        llrsend_hold = 1;
      
      //blink LED
      //if((retllr_i & (r_win-1))==0)
  	    //GPIO_BTGL = 0x1 << ledpin;
  	  
  	  //allow sending next window's delta
  	  if( ((delta_i & (r_win-1))==0) && (((delta_i>>r_win_mul)-(retllr_i>>r_win_mul))<2))
        deltasend_hold = 0;
      
  		//after it decoded a full window, clear flags and reset counters
  		if((retllr_i & (r_win-1))==0){
  			alpha_i = 0;
  			beta_i = 31;
        spi_send(0x300000b3); //~~~DEBUG, done calculation of LLR for a window
  		}
  		
  		if( (iorecv_i<256) && ((retllr_i&(r_win-1))== 0) && (((iorecv_i>>r_win_mul)-(retllr_i>>r_win_mul))<2) )
  			iorecv_hold = 0;
  		
  		if(retllr_i==256){ 
  			iorecv_i = iorecv_hold = delta_i = retllr_i = 0; //reset recv and send counter
  	    NC_NOC_CSR1 = 0x20000000; //clear NOC_RIM, revert to flag per 1 flit (to recv calcLLR & noise_var)
  		}
  	}  
  	
  }	

	return 0;

}

  

