//------------------------------------------------------------------------------

//

// Main Program
// Application		: turboimpl_ioc
// Core						: IO Core
// Purpose
//	- Turbo code implementation on RUMPS401
//  - > recv input seq, govern operations

// ### Interfacing with LMS6002D, RX part ###

#include "main.h"
#include "turbo_rumps_c0.h"

#define TURBO_MAX_ITR 1

#define IO_CHNLCTRL_HDR 0x1
#define IO_BITS_HDR 0x2
#define IO_LLRACK_HDR 0x3
#define IO_NOVAR_HDR 0x4
#define IO_STARTTURBO_HDR 0xa

#define DSP_LLR_HDR 0x31
#define DSP_BITSACK_HDR 0x32

void main_scheduler (void);

// LMS6002d register access function
void write_lms6002_reg(uint8_t reg_addr, uint8_t data);
uint8_t read_lms6002_reg(uint8_t reg_addr);

//------------------------------------------------------------------------------

int main(void)

{
  // Set GPIOs & MUX
  MUXC_SELECT = 0x40; // SPI, GPIO[27:0]
  MUXC_PU &= 0x0fffffff;  // SPI PU disabled

  GPIO_OEN_SET = 0xfff; // output, GPIO[11:0]
  GPIO_OEN_CLR = 0xfff000; //input, GPIO[23:12]
  pinMode_input(26); // tweak - TX_IQSEL
  pinMode_input(27); // tweak - RX_IQSEL

  // Initialization: Trellis, SPI, UART
  set_trellis();
  uart_hd_init_uart(1); // set pinMode 24 & 25
         // CPHA, CPOL, BC,   IE,   FSB,  SS,   CR
  spi_init( 0x0,  0x0,  0x1,  0x0,  0x0,  0x1,  0x1);
  spi_enable();

  // Define variables
	short dspsent_i = 0;	//keeps track of bits sent to DSP core
	short dec_i = 0; 		//keeps track of decoded bits
  short recv_i = 0;		//keeps track of received channel bits 
  char temp_recv_i = 0;
	
	accum LLR_Le_arr[256]; //stores LLR / Le per half iteration
	accum noise_var;
	
  // Channel data's receive buffer
  accum recv_sbit[256]; 	//received systematic bit
  accum recv_pbit1[256]; //received parity bit dec1
  accum recv_pbit2[256];	//received parity bit dec2
		
	unsigned char calcLLR = 0;	
	
	unsigned char dec_stat = 0; //flag if dec1 (0) / dec2 (1) is active now
	unsigned char recv_done = 0; //flag of a data frame is fully received
	unsigned char halfitr_ctr = 0; //track number of iteration(per-half itr)
	unsigned char dspsend_hold = 0; //flag to hold sending data to DSP core
	
	unsigned char I_last_send = intrlv_Im1; //latest permutated index for DRP interleaver
	unsigned char I_last_recv = intrlv_Im1; //latest permutated index for DRP interleaver
  
  unsigned char flag_pkt_type = 0;
  
  accum_int_t temp_accumint; //temp variable for manipulating bits between accum and int type
  accum noisevar_mean;
  

  // *** Init Loop - Lime's initialization part ***
  while(1){
    char temp_uart;

    temp_uart = uart_hd_getchar(); // wait for command
    if(temp_uart=='s'){
      // ### TX Chain ###
      // TOP Level
      write_lms6002_reg(0x05, 0x3e); // Soft tx/rx enable
      uart_hd_putchar(read_lms6002_reg(0x05));

      write_lms6002_reg(0x09, 0xc5); // Clock buffers - Tx/Rx DSM SPI
      uart_hd_putchar(read_lms6002_reg(0x09));
      
      // Tx LPF
      write_lms6002_reg(0x34, 0x3e); // select LPF bandwidth (.75MHz)
      uart_hd_putchar(read_lms6002_reg(0x34));
      
      // Tx RF
      write_lms6002_reg(0x41, 0x19); // VGA1 gain (-10dB)
      uart_hd_putchar(read_lms6002_reg(0x41));
      
      write_lms6002_reg(0x45, 0x78); // VGA2 gain (15dB)
      uart_hd_putchar(read_lms6002_reg(0x45));
      
      write_lms6002_reg(0x44, 0x0b); // Select PA1
      uart_hd_putchar(read_lms6002_reg(0x44));
      
      // Tx PLL + DSM
      write_lms6002_reg(0x15, 0xfd); // Output frequency
      uart_hd_putchar(read_lms6002_reg(0x15));
      
      write_lms6002_reg(0x16, 0x8c); // CP current (1200uA)
      uart_hd_putchar(read_lms6002_reg(0x16));
      
      write_lms6002_reg(0x17, 0xe3); // CP UP offset current (30uA)
      uart_hd_putchar(read_lms6002_reg(0x17));
      
      write_lms6002_reg(0x10, 0x54); // N integer 
      uart_hd_putchar(read_lms6002_reg(0x10));

      write_lms6002_reg(0x11, 0x66); // N fractional over 3 registers
      uart_hd_putchar(read_lms6002_reg(0x11));

      write_lms6002_reg(0x12, 0x66);
      uart_hd_putchar(read_lms6002_reg(0x12));

      write_lms6002_reg(0x13, 0x66);
      uart_hd_putchar(read_lms6002_reg(0x13));

      write_lms6002_reg(0x19, 0x1a + 0x80); // tuned vco cap value
      uart_hd_putchar(read_lms6002_reg(0x19));
      
      // ### RX Chain ###
      // Rx LPF
      write_lms6002_reg(0x54, 0x3e); // select LPF bandwidth (.75MHz)
      uart_hd_putchar(read_lms6002_reg(0x54));
      
      // Rx VGA2
      write_lms6002_reg(0x65, 0x01); // VGA2 gain (3dB)
      uart_hd_putchar(read_lms6002_reg(0x65));
      
      // Rx FE
      write_lms6002_reg(0x75, 0xd0); // active LNA=LNA1, LNA gain mode=max gain
      uart_hd_putchar(read_lms6002_reg(0x75));
      
      write_lms6002_reg(0x76, 0x78); // VGA1 control feedback resistor (120)
      uart_hd_putchar(read_lms6002_reg(0x76));
      
      write_lms6002_reg(0x79, 0x37); // LNA load resistor - internal load (55)
      uart_hd_putchar(read_lms6002_reg(0x79));
      
      // Rx PLL + DSM
      write_lms6002_reg(0x25, 0xfd); // Output frequency
      uart_hd_putchar(read_lms6002_reg(0x25));
      
      write_lms6002_reg(0x26, 0x8c); // CP current (1200uA)
      uart_hd_putchar(read_lms6002_reg(0x26));
      
      write_lms6002_reg(0x27, 0xe3); // CP UP offset current (30uA)
      uart_hd_putchar(read_lms6002_reg(0x27));
      
      write_lms6002_reg(0x20, 0x58); // N integer 
      uart_hd_putchar(read_lms6002_reg(0x20));

      write_lms6002_reg(0x21, 0x66); // N fractional over 3 registers
      uart_hd_putchar(read_lms6002_reg(0x21));

      write_lms6002_reg(0x22, 0x66);
      uart_hd_putchar(read_lms6002_reg(0x22));

      write_lms6002_reg(0x23, 0x66);
      uart_hd_putchar(read_lms6002_reg(0x23));

      write_lms6002_reg(0x29, 0x27 + 0x80); // tuned vco cap value
      uart_hd_putchar(read_lms6002_reg(0x29));
      
      uint8_t topspi_clken;
      uint8_t clbr_result;

      // ### Turn off DAC before calibrations ###
      uint8_t temp = (read_lms6002_reg(0x57)) & 0x7f; // clear 7th bit
      write_lms6002_reg(0x57, temp);
      uart_hd_putchar(temp);

      // ### Calibrations ###
      //****
      // (1) DC offset calibration of LPF Tuning Module
      uint8_t dccal = 0x0f;
      write_lms6002_reg(0x55, dccal); // RXLPF::DCO_DACCAL = DCCAL
      write_lms6002_reg(0x35, dccal); // TXLPF::DCO_DACCAL = DCCAL
      uart_hd_putchar(dccal);

      //****
      // (2) LPF Bandwidth Tuning procedure
      uint8_t rccal = 0x07;
      uint8_t data_r;

      // RXLPF::RCCAL_LPF = RCCAL
      data_r = read_lms6002_reg(0x56);
      data_r &= 0x8f; // clear out prev RCCAL
      data_r |= (rccal << 4); // assign calibrated RCCAL 
      write_lms6002_reg(0x56, data_r);
      
      // TXLPF::RCCAL_LPF = RCCAL
      data_r = read_lms6002_reg(0x36);
      data_r &= 0x8f; // clear out prev RCCAL
      data_r |= (rccal << 4); // assign calibrated RCCAL 
      write_lms6002_reg(0x36, data_r);

      uart_hd_putchar(rccal);

      //****
      // (3) TXLPF DC Offset Calibration
      topspi_clken = read_lms6002_reg(0x09); // save TOP::CLK_EN
      write_lms6002_reg(0x09, topspi_clken|0x2); // TOP::CLK_EN[1] = 1

      //-----------
      // DC Calibration, TXLPF module, channel I (ADDR=0)
      clbr_result = 0; // 0 'false', 1 'true'

      write_lms6002_reg(0x33, 0x08); // DC_ADDR = 0
      write_lms6002_reg(0x33, 0x28); // DC_START_CLBR = 1 
      write_lms6002_reg(0x33, 0x08); // DC_START_CLBR = 0
      
      if(read_lms6002_reg(0x30)==31){ // read DC_REGVAL
        
        write_lms6002_reg(0x30, 0x0); // DC_REG_VAL = 0;
        write_lms6002_reg(0x33, 0x28); // DC_START_CLBR = 1 
        write_lms6002_reg(0x33, 0x08); // DC_START_CLBR = 0
        
        if(read_lms6002_reg(0x30)!=0) // read DC_REGVAL
          clbr_result = 1;
      }
      else
        clbr_result = 1;

      uart_hd_putchar(clbr_result);
      
      // End of DC calibration, TXLPF module, channel I
      //-----------

      if(clbr_result==1){
        //-----------
        // DC Calibration, TXLPF module, channel Q (ADDR=1)
        clbr_result = 0; // 0 'false', 1 'true'

        write_lms6002_reg(0x33, 0x09); // DC_ADDR = 1
        write_lms6002_reg(0x33, 0x29); // DC_START_CLBR = 1 
        write_lms6002_reg(0x33, 0x09); // DC_START_CLBR = 0
        
        if(read_lms6002_reg(0x30)==31){ // read DC_REGVAL
          
          write_lms6002_reg(0x30, 0x0); // DC_REG_VAL = 0;
          write_lms6002_reg(0x33, 0x29); // DC_START_CLBR = 1 
          write_lms6002_reg(0x33, 0x09); // DC_START_CLBR = 0
          
          if(read_lms6002_reg(0x30)!=0) // read DC_REGVAL
            clbr_result = 1;
        }
        else
          clbr_result = 1;

        uart_hd_putchar(clbr_result);

        // End of DC calibration, TXLPF module, channel Q
        //-----------
      }

      // check TXLPF DC calibration result
      if(clbr_result==0){ // PANIC: algo doesnt converge
        uart_hd_putchar(0x2f);
      }
      else if(clbr_result==1){ // SUCCESS: algo converge
        uart_hd_putchar(0x21);
        write_lms6002_reg(0x09, topspi_clken); // restore TOP:CLK_EN
      }

      //****
      // (4) RXLPF DC Offset Calibration
      topspi_clken = read_lms6002_reg(0x09); // save TOP::CLK_EN
      write_lms6002_reg(0x09, topspi_clken|0x8); // TOP::CLK_EN[3] = 1

      //-----------
      // DC Calibration, RXLPF module, channel I (ADDR=0)
      clbr_result = 0; // 0 'false', 1 'true'

      write_lms6002_reg(0x53, 0x08); // DC_ADDR = 0
      write_lms6002_reg(0x53, 0x28); // DC_START_CLBR = 1 
      write_lms6002_reg(0x53, 0x08); // DC_START_CLBR = 0
      
      if(read_lms6002_reg(0x50)==31){ // read DC_REGVAL
        
        write_lms6002_reg(0x50, 0x0); // DC_REG_VAL = 0;
        write_lms6002_reg(0x53, 0x28); // DC_START_CLBR = 1 
        write_lms6002_reg(0x53, 0x08); // DC_START_CLBR = 0
        
        if(read_lms6002_reg(0x50)!=0) // read DC_REGVAL
          clbr_result = 1;
      }
      else
        clbr_result = 1;

      uart_hd_putchar(clbr_result);
      
      // End of DC calibration, RXLPF module, channel I
      //-----------

      if(clbr_result==1){
        //-----------
        // DC Calibration, RTXLPF module, channel Q (ADDR=1)
        clbr_result = 0; // 0 'false', 1 'true'

        write_lms6002_reg(0x53, 0x09); // DC_ADDR = 1
        write_lms6002_reg(0x53, 0x29); // DC_START_CLBR = 1 
        write_lms6002_reg(0x53, 0x09); // DC_START_CLBR = 0
        
        if(read_lms6002_reg(0x50)==31){ // read DC_REGVAL
          
          write_lms6002_reg(0x50, 0x0); // DC_REG_VAL = 0;
          write_lms6002_reg(0x53, 0x29); // DC_START_CLBR = 1 
          write_lms6002_reg(0x53, 0x09); // DC_START_CLBR = 0
          
          if(read_lms6002_reg(0x50)!=0) // read DC_REGVAL
            clbr_result = 1;
        }
        else
          clbr_result = 1;

        uart_hd_putchar(clbr_result);

        // End of DC calibration, RXLPF module, channel Q
        //-----------
      }

      // check RXLPF DC calibration result
      if(clbr_result==0){ // PANIC: algo doesnt converge
        uart_hd_putchar(0x3f);
      }
      else if(clbr_result==1){ // SUCCESS: algo converge
        uart_hd_putchar(0x31);
        write_lms6002_reg(0x09, topspi_clken); // restore TOP:CLK_EN
      }

      //****
      // (5) RXVGA2 DC Offset Calibration
      topspi_clken = read_lms6002_reg(0x09); // save TOP::CLK_EN
      write_lms6002_reg(0x09, topspi_clken|0x10); // TOP::CLK_EN[4] = 1

      //-----------
      // DC Calibration, DC ref channel (ADDR=0)
      clbr_result = 0; // 0 'false', 1 'true'

      write_lms6002_reg(0x63, 0x08); // DC_ADDR = 0
      write_lms6002_reg(0x63, 0x28); // DC_START_CLBR = 1 
      write_lms6002_reg(0x63, 0x08); // DC_START_CLBR = 0
      
      if(read_lms6002_reg(0x60)==31){ // read DC_REGVAL
        
        write_lms6002_reg(0x60, 0x0); // DC_REG_VAL = 0;
        write_lms6002_reg(0x63, 0x28); // DC_START_CLBR = 1 
        write_lms6002_reg(0x63, 0x08); // DC_START_CLBR = 0
        
        if(read_lms6002_reg(0x60)!=0) // read DC_REGVAL
          clbr_result = 1;
      }
      else
        clbr_result = 1;

      uart_hd_putchar(clbr_result);

      if(clbr_result==1){
        //-----------
        // DC Calibration, VGA2A, I channel (ADDR=1)
        clbr_result = 0; // 0 'false', 1 'true'

        write_lms6002_reg(0x63, 0x09); // DC_ADDR = 1
        write_lms6002_reg(0x63, 0x29); // DC_START_CLBR = 1 
        write_lms6002_reg(0x63, 0x09); // DC_START_CLBR = 0
        
        if(read_lms6002_reg(0x60)==31){ // read DC_REGVAL
          
          write_lms6002_reg(0x60, 0x0); // DC_REG_VAL = 0;
          write_lms6002_reg(0x63, 0x29); // DC_START_CLBR = 1 
          write_lms6002_reg(0x63, 0x09); // DC_START_CLBR = 0
          
          if(read_lms6002_reg(0x60)!=0) // read DC_REGVAL
            clbr_result = 1;
        }
        else
          clbr_result = 1;

        uart_hd_putchar(clbr_result);

        if(clbr_result==1){
          //-----------
          // DC Calibration, VGA2A, Q channel (ADDR=2)
          clbr_result = 0; // 0 'false', 1 'true'

          write_lms6002_reg(0x63, 0x0a); // DC_ADDR = 2
          write_lms6002_reg(0x63, 0x2a); // DC_START_CLBR = 1 
          write_lms6002_reg(0x63, 0x0a); // DC_START_CLBR = 0
          
          if(read_lms6002_reg(0x60)==31){ // read DC_REGVAL
            
            write_lms6002_reg(0x60, 0x0); // DC_REG_VAL = 0;
            write_lms6002_reg(0x63, 0x2a); // DC_START_CLBR = 1 
            write_lms6002_reg(0x63, 0x0a); // DC_START_CLBR = 0
            
            if(read_lms6002_reg(0x60)!=0) // read DC_REGVAL
              clbr_result = 1;
          }
          else
            clbr_result = 1;

          uart_hd_putchar(clbr_result);

          if(clbr_result==1){
            //-----------
            // DC Calibration, VGA2B, I channel (ADDR=3)
            clbr_result = 0; // 0 'false', 1 'true'

            write_lms6002_reg(0x63, 0x0b); // DC_ADDR = 3
            write_lms6002_reg(0x63, 0x2b); // DC_START_CLBR = 1 
            write_lms6002_reg(0x63, 0x0b); // DC_START_CLBR = 0
            
            if(read_lms6002_reg(0x60)==31){ // read DC_REGVAL
              
              write_lms6002_reg(0x60, 0x0); // DC_REG_VAL = 0;
              write_lms6002_reg(0x63, 0x2b); // DC_START_CLBR = 1 
              write_lms6002_reg(0x63, 0x0b); // DC_START_CLBR = 0
              
              if(read_lms6002_reg(0x60)!=0) // read DC_REGVAL
                clbr_result = 1;
            }
            else
              clbr_result = 1;

            uart_hd_putchar(clbr_result);

            if(clbr_result==1){
              //-----------
              // DC Calibration, VGA2B, Q channel (ADDR=4)
              clbr_result = 0; // 0 'false', 1 'true'

              write_lms6002_reg(0x63, 0x0c); // DC_ADDR = 4
              write_lms6002_reg(0x63, 0x2c); // DC_START_CLBR = 1 
              write_lms6002_reg(0x63, 0x0c); // DC_START_CLBR = 0
              
              if(read_lms6002_reg(0x60)==31){ // read DC_REGVAL
                
                write_lms6002_reg(0x60, 0x0); // DC_REG_VAL = 0;
                write_lms6002_reg(0x63, 0x2c); // DC_START_CLBR = 1 
                write_lms6002_reg(0x63, 0x0c); // DC_START_CLBR = 0
                
                if(read_lms6002_reg(0x60)!=0) // read DC_REGVAL
                  clbr_result = 1;
              }
              else
                clbr_result = 1;

              uart_hd_putchar(clbr_result);
            }
          }
        }
      }
      // End of DC calibration, RXVGA2 module
      //-----------

      // check RXVGA2 DC calibration result
      if(clbr_result==0){ // PANIC: algo doesnt converge
        uart_hd_putchar(0x4f);
      }
      else if(clbr_result==1){ // SUCCESS: algo converge
        uart_hd_putchar(0x41);
        write_lms6002_reg(0x09, topspi_clken); // restore TOP:CLK_EN
      }

      // ### Lo Leakage Manual cancellation
      write_lms6002_reg(0x42, 0x86); // I channel
      write_lms6002_reg(0x43, 0x91); // Q channel

      // ### Turn on DAC after calibrations ###
      temp = (read_lms6002_reg(0x57)) | 0x80; // clear 7th bit
      write_lms6002_reg(0x57, temp);
      uart_hd_putchar(temp);
      
      // ### Signal other cores to enter decoding state ###
      while(noc_IC_txbuff_isfull==1)__NOP();
      IC_NOC_TX_BUFF1 = IC_NOC_TX_BUFF2 = IC_NOC_TX_BUFF3 = IO_STARTTURBO_HDR; 
      
      int tempack;
      while(noc_IC_rxbuff1_av!=1)__NOP(); tempack = IC_NOC_RX_BUFF1;
      while(noc_IC_rxbuff2_av!=1)__NOP(); tempack = IC_NOC_RX_BUFF2;
      while(noc_IC_rxbuff3_av!=1)__NOP(); tempack = IC_NOC_RX_BUFF3;

      break;
    }
  }
  // *** END - Init Loop ***


  // *** Main Loop - Turbo coding part ***
  while(1){
    
    // ## GET_CHNL_DATA
    // Get data from channel - currently from uart (without noise!!)
		while(recv_done==0){
		  //recv and send back ACK
		  char temp_uart;
  		//accum_int_t xtemp;
  		
			temp_uart = uart_hd_getchar();
			if(temp_recv_i==0){
			  recv_sbit[recv_i] = temp_uart; 
			  recv_sbit[recv_i] = (recv_sbit[recv_i]<<1) - 1;
			  //temp_accumint.accum_cont = recv_sbit[recv_i];
			  temp_recv_i++;
			}
			else if(temp_recv_i==1){
			  recv_pbit1[recv_i] = temp_uart;
			  recv_pbit1[recv_i] = (recv_pbit1[recv_i]<<1) - 1;
			  //temp_accumint.accum_cont = recv_pbit1[recv_i];
			  temp_recv_i++;
			}
			else if(temp_recv_i==2){
			  recv_pbit2[recv_i] = temp_uart;
			  recv_pbit2[recv_i] = (recv_pbit2[recv_i]<<1) - 1;
			  //temp_accumint.accum_cont = recv_pbit2[recv_i];
			  temp_recv_i = 0;
			  recv_i++;

        // send sbit to dsp for aiding in noise estimation, process is sequential
        //kickstart with a header, wait for ACK
        if(recv_i==1){
          while(noc_IC_txbuff_isfull==1)__NOP(); IC_NOC_TX_BUFF3 = IO_NOVAR_HDR;
          while(noc_IC_rxbuff3_av!=1)__NOP(); int temp_ack = IC_NOC_RX_BUFF3;
          noisevar_mean = 0;
        }
        //send sbit
        while(noc_IC_txbuff_isfull==1)__NOP();
        temp_accumint.accum_cont = recv_sbit[recv_i-1];
        IC_NOC_TX_BUFF3 = temp_accumint.int_cont;

        //sum of sbit, for noise var estimation
        noisevar_mean += recv_sbit[recv_i-1];
			}
  		
      uart_hd_putchar(temp_uart);

			//increment received bit count, mark if a complete frame is received
			if(recv_i==256){
        // set flag
				recv_done = 1;

        //calculate noise variance estimate
        // -- E{x^2} part
        while(noc_IC_rxbuff3_av!=1)__NOP();
        temp_accumint.int_cont = IC_NOC_RX_BUFF3;
        noise_var = temp_accumint.accum_cont >> 8;

        // -- E{|x|}^2 part
        noisevar_mean = noisevar_mean >> 8; //divide by K
        while(noc_IC_txbuff_isfull==1)__NOP();
        temp_accumint.accum_cont = noisevar_mean;
        IC_NOC_TX_BUFF3 = temp_accumint.int_cont; //send to DSP for squaring
        while(noc_IC_rxbuff3_av!=1)__NOP();
        temp_accumint.int_cont = IC_NOC_RX_BUFF3;
        noisevar_mean = temp_accumint.accum_cont;

        noise_var -= noisevar_mean;
        noise_var = 1/noise_var;
			}
		}
    
    // ## DTRMN_PKT_TYPE
    // Examine packet header
    if( ((IC_NOC_CSR0 & 0x40)==0x40) && (flag_pkt_type==0) )
      flag_pkt_type = IC_NOC_RX_BUFF3;
    
    /*
    // ~~DEBUG
    if( (IC_NOC_CSR0 & 0x4)==0x4 ){
      char tempbuff = IC_NOC_RX_BUFF1;
      uart_hd_putchar(tempbuff);
      while( !_getchar(&tempbuff) )__NOP(); //wait for ACK
      while((IC_NOC_CSR1 & 0x2)==0x2)__NOP(); IC_NOC_TX_BUFF1 = tempbuff;
    }
    */
    
    // ## RECV_LLR_NC
    // Receive LLR / Le from NC1 core accordingly
		//if( ((IC_NOC_CSR0 & 0x10)==0x10) && (((IC_NOC_CSR2&0xf000000)>>24)>=0x8) ){
		if( (IC_NOC_CSR0 & 0x10)==0x10 ){	
		  //adjust index accordingly, depends on dec1 / dec2
			//for(char i=0; i<8; i++){
  			unsigned char llridx;
  			if(dec_stat==0)
  				llridx = dec_i;
  			else
  				llridx = I_last_recv = drp_idxcalc(r_K, r_M, I_last_recv, (unsigned char*)intrlv_P, dec_i);
  			
  			//store LLR / Le
  			temp_accumint.int_cont = IC_NOC_RX_BUFF2;
  			LLR_Le_arr[llridx] = temp_accumint.accum_cont;
  			
  			dec_i++;
			//}
			
		  //send back ACK per x flits
		  //while((IC_NOC_CSR1 & 0x2)==0x2)__NOP(); IC_NOC_TX_BUFF2 = IO_LLRACK_HDR; // Header - LLR ack
		  while((IC_NOC_CSR1 & 0x2)==0x2)__NOP(); IC_NOC_TX_BUFF2 = dec_i;
      
			//blink led
			//if((dec_i & (r_win-1))==0)
			  //GPIO_BTGL = 0x1 << ledpin;
			
			//when on hold, 1 window returned allows 1 more window to be sent
			if( dspsent_i<256 && dspsend_hold==2 && (dec_i&(r_win-1))== 0 && ((dspsent_i>>r_win_mul)-(dec_i>>r_win_mul))<2 )
				dspsend_hold = 0;
				
  		// Switch between decoders per half iteration
  		if(dec_i==256){
  			dec_i = dspsent_i = 0; //reset counter
  			dspsend_hold = 0; //allow sending to DSP
  			
  			dec_stat = 1 - dec_stat; //toggle active decoder
  			
  			if(dec_stat==0) //reset interleaver recursive count
  				I_last_send = I_last_recv = intrlv_Im1;
  			
  			if(++halfitr_ctr == ((TURBO_MAX_ITR<<1)-1)) //signal DSP core to calc LLR
  				calcLLR = 1; 
      
  			//to do on the final iteration - move on to new frame
  			if(halfitr_ctr == (TURBO_MAX_ITR<<1)){
  				recv_i = recv_done = 0; //allow receiving channel data, reset counter
  				halfitr_ctr = 0; //reset iteration counter
  				calcLLR = 0; //signal DSP core to calc Le
  				
  				char uart_buff;
  				//output decision based on LLR
  				for(short i=0; i<256; i++){
  					if(LLR_Le_arr[i]>0)
  						uart_buff = 1;	
  					else
  						uart_buff = 0;
  				
  				  uart_hd_putchar(uart_buff); //send through UART
  				  uart_hd_getchar();
  				}
  			}
  		}
  		
		}
		
    // ## SEND_BITS_DSP
		// Send bits to DSP core
		
		// check for ACK per 1 bits packet sent
		if( (flag_pkt_type==DSP_BITSACK_HDR) && ((IC_NOC_CSR0 & 0x40)==0x40) ){
		  flag_pkt_type = 0; //clear packet type's flag
		  int temp_ack = IC_NOC_RX_BUFF3;
		  
		  dspsend_hold = 0;
		  
			//dspsend_hold criteria #2- dspsent_i is a multiple of r_win
			//hold sending if it sent out two windows, and none of them returned completely
			//or if it has sent all bits of a frame
		  if( dspsent_i==256 || ((dspsent_i&(r_win-1))== 0 && ((dspsent_i>>r_win_mul)-(dec_i>>r_win_mul))>=2) )
				dspsend_hold = 2;
		}
		
		// Sending part
		if( (recv_i>dspsent_i) && (dspsend_hold==0) ){
			// ** Send control bits, if this is first bit of an iteration
			if(dspsent_i==0){
			  //send calcLLR and noise_var to DSP core
			  while((IC_NOC_CSR1 & 0x2)==0x2)__NOP(); IC_NOC_TX_BUFF3 = IO_CHNLCTRL_HDR; // Header - chnl_control
			  while((IC_NOC_CSR1 & 0x2)==0x2)__NOP(); IC_NOC_TX_BUFF3 = calcLLR;
			  while((IC_NOC_CSR1 & 0x2)==0x2)__NOP(); temp_accumint.accum_cont = noise_var; IC_NOC_TX_BUFF3 = temp_accumint.int_cont;
			  
        //send calcLLR to NC1
			  while((IC_NOC_CSR1 & 0x2)==0x2)__NOP(); IC_NOC_TX_BUFF2 = calcLLR;
        
			  //wait for ACK -  since it is blocking, no need to differentiate this one
			  while((IC_NOC_CSR0 & 0x40)!=0x40)__NOP();
			  int temp_ack = IC_NOC_RX_BUFF3;
			  
			}
			
			// ** Send data bits
			while((IC_NOC_CSR1 & 0x2)==0x2)__NOP(); IC_NOC_TX_BUFF3 = IO_BITS_HDR; // Header - bits
						
			//if dec1 is active
			if(dec_stat==0){
				while((IC_NOC_CSR1 & 0x2)==0x2)__NOP(); temp_accumint.accum_cont = recv_sbit[dspsent_i]; IC_NOC_TX_BUFF3 = temp_accumint.int_cont; //systematic bit
				while((IC_NOC_CSR1 & 0x2)==0x2)__NOP(); temp_accumint.accum_cont = recv_pbit1[dspsent_i]; IC_NOC_TX_BUFF3 = temp_accumint.int_cont; //parity bit
				
				//apriori prob - ln(apriori) and ln(1-apriori)
				if(halfitr_ctr==0){ 
					while((IC_NOC_CSR1 & 0x2)==0x2)__NOP();
					temp_accumint.accum_cont = -0.6931;
					IC_NOC_TX_BUFF3 = temp_accumint.int_cont;
					while((IC_NOC_CSR1 & 0x2)==0x2)__NOP();
					IC_NOC_TX_BUFF3 = temp_accumint.int_cont;
				}
				else{
					while((IC_NOC_CSR1 & 0x2)==0x2)__NOP();
					temp_accumint.accum_cont = ln_approx(LLR_Le_arr[dspsent_i]);
					IC_NOC_TX_BUFF3 = temp_accumint.int_cont;
					while((IC_NOC_CSR1 & 0x2)==0x2)__NOP();
					temp_accumint.accum_cont = ln_approx(1-LLR_Le_arr[dspsent_i]);
					IC_NOC_TX_BUFF3 = temp_accumint.int_cont;
				}
			}
			
			//if dec2 is active
			else{ 
				I_last_send = drp_idxcalc(r_K, r_M, I_last_send, (unsigned char*)intrlv_P, dspsent_i);
				while((IC_NOC_CSR1 & 0x2)==0x2)__NOP(); temp_accumint.accum_cont = recv_sbit[I_last_send]; IC_NOC_TX_BUFF3 = temp_accumint.int_cont; //systematic bit
				while((IC_NOC_CSR1 & 0x2)==0x2)__NOP(); temp_accumint.accum_cont = recv_pbit2[dspsent_i]; IC_NOC_TX_BUFF3 = temp_accumint.int_cont; //parity bit
				
				//apriori prob - ln(apriori) and ln(1-apriori)
				if(halfitr_ctr==0){
					while((IC_NOC_CSR1 & 0x2)==0x2)__NOP();
					temp_accumint.accum_cont = -0.6931;
					IC_NOC_TX_BUFF3 = temp_accumint.int_cont;
					while((IC_NOC_CSR1 & 0x2)==0x2)__NOP();
					IC_NOC_TX_BUFF3 = temp_accumint.int_cont;
				}
				else{
					while((IC_NOC_CSR1 & 0x2)==0x2)__NOP();
					temp_accumint.accum_cont = ln_approx(LLR_Le_arr[I_last_send]); 
					IC_NOC_TX_BUFF3 = temp_accumint.int_cont;
					while((IC_NOC_CSR1 & 0x2)==0x2)__NOP();
					temp_accumint.accum_cont = ln_approx(1-LLR_Le_arr[I_last_send]); 
					IC_NOC_TX_BUFF3 = temp_accumint.int_cont;
			  }
			}
					  			
			//increment sent bit counter
			dspsent_i++;
			
			//hold sending to dsp until ACK received for each packet
			dspsend_hold = 1;
			
			//blink led
			//if((dspsent_i & (r_win-1))==0)
			  //GPIO_BTGL = 0x1 << ledpin;
			
		} 
    
  }
  // *** END - Main Loop ***

	return 0;

}

  
void write_lms6002_reg(uint8_t reg_addr, uint8_t data){
  //takes 7 bits, add CMD=1 for write on MSB
  reg_addr = (reg_addr & 0x7f) | 0x80;
    
  spi_send( (reg_addr << 8) | (data & 0xff) );
}

uint8_t read_lms6002_reg(uint8_t reg_addr){
  //takes 7 bits, add CMD=0 for read on MSB
  reg_addr = (reg_addr & 0x7f);

  //do transfer
  spi_send((reg_addr << 8));
  
  while ((SPI_STATE & 0x1) == 0x0) // wait SP_IF high
    __NOP();
  
  SPI_STATE |= 0x1; // write 1 to clear
  
  return (SPI_DATA & 0xff);
}
