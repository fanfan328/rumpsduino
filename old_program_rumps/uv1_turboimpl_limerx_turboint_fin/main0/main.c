//------------------------------------------------------------------------------

//

// Main Program
// Application    : turboimpl_ioc
// Core           : IO Core
// Purpose
//  - Turbo code implementation on RUMPS401
//  - > recv input seq, govern operations

// ### Interfacing with LMS6002D, RX part ###

#include "main.h"
#include "turbo_rumps_c0.h"

// packet structure
#define TURBO_MAX_ITR 1
#define TIMINGPILOT_LEN 16
#define CHUNK_LEN 48
#define CHUNK_NUM 16
#define N_FREQPILOTPAIR 16 // pairs
#define CODEWORD_LEN 768
#define RF_PAYLOADLEN (N_FREQPILOTPAIR<<1) + CODEWORD_LEN
#define FRAME_NBITS 256

// turbo decoding packet header
#define IO_CHNLCTRL_HDR 0x1
#define IO_BITS_HDR 0x2
#define IO_LLRACK_HDR 0x3
#define IO_NOVAR_HDR 0x4

#define DSP_LLR_HDR 0x31
#define DSP_BITSACK_HDR 0x32

//shared with isr0.c
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

volatile uint8_t rx_state = RX_STOP; 
unsigned int tempdump[20] = {0};

accum_int_t codeword[RF_PAYLOADLEN]={0}; // I buffer

// LMS6002d register access function
void write_lms6002_reg(uint8_t reg_addr, uint8_t data);
uint8_t read_lms6002_reg(uint8_t reg_addr);

//------------------------------------------------------------------------------

int main(void)

{

  // *** Part1 - Setup ***

  // Set GPIOs & MUX
  MUXC_SELECT = 0x40; // SPI, GPIO[27:0]
  MUXC_PU &= 0x0f000000;  // SPI, GPIO[23:0] PU disabled

  // Timer Settings - Lime digital interface CLK
  TM_PR = 0x3e7; // prescale target, 16kbps
  TM_COMR0 = 0x0; //timer target, ch 0
  TM_COMCR = 0x3; // reset on COMR, enable interrupt
  NVIC_SetPriority(6, 0);   // set Ext INT 6 (timer) priority

  TM_CTRL = 0x3; // use PCLK, reset timer, enable timer

  // GPIOs direction
  GPIO_OEN_SET = 0xfff; // output, GPIO[11:0]
  GPIO_OEN_CLR = 0xfff000; //input, GPIO[23:12]
  pinMode_input(26); // tweak - TX_IQSEL
  pinMode_input(27); // tweak - RX_IQSEL

  // Initialization
  set_trellis(); // Turbo code trellis
  
  uart_hd_init_uart(1); // UART, 115200 baud
  
  // SPI -  CPHA, CPOL, BC,   IE,   FSB,  SS,   CR
  spi_init( 0x0,  0x0,  0x1,  0x0,  0x0,  0x1,  0x1);
  spi_enable();

  // RX variables
  volatile uint8_t init_rxclk = 0; // flag to init RX clock on every frame
  volatile unsigned int nSent = 0; // raw payload idx
  volatile unsigned int nRecv = 0; // corrected payload idx 

  // Turbo decoding variables
  short dspsent_i = 0;  // keeps track of bits sent to DSP core
  short dec_i = 0;    // keeps track of decoded bits
  short recv_i = 0;   // keeps track of received channel bits 
  
  accum LLR_Le_arr[256]; // stores LLR / Le per half iteration
  accum noise_var;
  volatile accum noisevar_mean;
    
  unsigned char calcLLR = 0;  
  
  unsigned char dec_stat = 0; // flag if dec1 (0) / dec2 (1) is active now
  unsigned char recv_done = 0; // flag of a data frame is fully received
  unsigned char halfitr_ctr = 0; // track number of iteration(per-half itr)
  unsigned char dspsend_hold = 0; // flag to hold sending data to DSP core
  
  unsigned char I_last_send = intrlv_Im1; // latest permutated index for DRP interleaver
  unsigned char I_last_recv = intrlv_Im1; // latest permutated index for DRP interleaver
  
  unsigned char flag_pkt_type = 0;
  
  accum_int_t temp_accumint; // temp variable for accum & int bit manipulation

  // *** END - part1 ***


  // *** Part2 - Lime's initialization ***
  while(1){
    if(uart_hd_getchar()=='s'){
      // ### ADC/DAC MISC_CTRL ###
      // TX_sync = 0, IQ
      // RX_sync = 1, IQ
      write_lms6002_reg(0x5a, 0xa0);

      // TOP Level
      write_lms6002_reg(0x05, 0x36); // Soft rx enable
      uart_hd_putchar(read_lms6002_reg(0x05));

      // ### RX Chain ###

      // RXout/ADCin switch "closed"
      write_lms6002_reg(0x09, 0xc4);
      uart_hd_putchar(read_lms6002_reg(0x09));

      // Rx LPF
      write_lms6002_reg(0x54, 0x3e); // select LPF bandwidth (.75MHz)
      uart_hd_putchar(read_lms6002_reg(0x54));
      
      // Rx VGA2
      write_lms6002_reg(0x65, 0x01); // VGA2 gain (3dB) 3dB mult
      uart_hd_putchar(read_lms6002_reg(0x65));
      
      // Rx FE
      write_lms6002_reg(0x75, 0xd0); // active LNA=LNA1, LNA gain mode=max gain
      uart_hd_putchar(read_lms6002_reg(0x75));
      
      write_lms6002_reg(0x76, 0x66); // VGA1 control feedback resistor (19dB)
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
      
      write_lms6002_reg(0x20, 0x56); // N integer 
      uart_hd_putchar(read_lms6002_reg(0x20));

      write_lms6002_reg(0x21, 0x99); // N fractional over 3 registers
      uart_hd_putchar(read_lms6002_reg(0x21));

      write_lms6002_reg(0x22, 0x99);
      uart_hd_putchar(read_lms6002_reg(0x22));

      write_lms6002_reg(0x23, 0x99);
      uart_hd_putchar(read_lms6002_reg(0x23));

      //write_lms6002_reg(0x29, 0x26 + 0x80); // tuned vco cap value
      //uart_hd_putchar(read_lms6002_reg(0x29));
      
      // auto vco cap tuning
      uint8_t last_vtune = 0x2;
      uint8_t curr_vtune;
      uint8_t cmax, cmin;
      for(uint8_t i=0; i<64; i++){
        // change vcocap
        write_lms6002_reg(0x29, i + 0x80);
        for(uint8_t delay=0; delay<100; delay++)__NOP();
        
        // read vtune
        curr_vtune = read_lms6002_reg(0x2a)>>6;
        
        // find cmin cmax
        if(last_vtune==0x2 && curr_vtune==0x0)
          cmin = i;
        else if(last_vtune==0x0 && curr_vtune==0x1){
          cmax = i;
          write_lms6002_reg(0x29, ((cmin+cmax)/2) + 0x80);
          break;
        }
        last_vtune = curr_vtune;
      }
      uart_hd_putchar(read_lms6002_reg(0x29));
      
      uint8_t topspi_clken;
      uint8_t clbr_result, data_r;
      uint8_t dccal, rccal;

      //GPIO_DATAOUT = (GPIO_DATAOUT & 0xfffff000) | 0xfff;

      // ### Turn off DAC before calibrations ###
      uint8_t temp = (read_lms6002_reg(0x57)) & 0x7f; // clear 7th bit
      write_lms6002_reg(0x57, temp);
      uart_hd_putchar(temp);

      // ### Calibrations ###
      //****
      // (1) DC offset calibration of LPF Tuning Module
      topspi_clken = read_lms6002_reg(0x09); // save TOP::CLK_EN
      write_lms6002_reg(0x09, topspi_clken|0x20); // TOP::CLK_EN[5]=1

      //-----------
      // DC Calibration, TOP module
      clbr_result = 0; // 0 'false', 1 'true'

      write_lms6002_reg(0x03, 0x08); // DC_ADDR = 0
      write_lms6002_reg(0x03, 0x28); // DC_START_CLBR = 1 
      write_lms6002_reg(0x03, 0x08); // DC_START_CLBR = 0
      
      if(read_lms6002_reg(0x00)==31){ // read DC_REGVAL
        
        write_lms6002_reg(0x00, 0x0); // DC_REG_VAL = 0;
        write_lms6002_reg(0x03, 0x28); // DC_START_CLBR = 1 
        write_lms6002_reg(0x03, 0x08); // DC_START_CLBR = 0
        
        if(read_lms6002_reg(0x00)!=0) // read DC_REGVAL
          clbr_result = 1;
      }
      else
        clbr_result = 1;

      uart_hd_putchar(clbr_result);

      // End of DC calibration, TOP module
      //-----------

      // check TOP DC calibration result
      if(clbr_result==0){ // PANIC: algo doesnt converge
        uart_hd_putchar(0x1f);
      }
      else if(clbr_result==1){ // SUCCESS: algo converge
        uart_hd_putchar(0x11);
        
        dccal = read_lms6002_reg(0x00); // DCCAL = TOP:DC_REGVAL
        write_lms6002_reg(0x55, dccal); // RXLPF::DCO_DACCAL = DCCAL
        write_lms6002_reg(0x35, dccal); // TXLPF::DCO_DACCAL = DCCAL
        write_lms6002_reg(0x09, topspi_clken); // restore TOP:CLK_EN

        uart_hd_putchar(dccal);
      }

      //****
      // (2) LPF Bandwidth Tuning procedure
      write_lms6002_reg(0x07, 0x0f); // TOP::BWC_LPFCAL, .75MHz
      write_lms6002_reg(0x07, 0x8f); // TOP::EN_CAL_LPFCAL = 1 (enable)

      write_lms6002_reg(0x06, 0x0d); // TOP::RST_CAL_LPFCAL = 1 (rst_active)
      __NOP(); __NOP();
      write_lms6002_reg(0x06, 0x0c); // TOP::RST_CAL_LPFCAL = 0 (rst_inactive)
      __NOP(); __NOP();

      rccal = read_lms6002_reg(0x01); // RCCAL = TOP::RCCAL_LPFCAL
      rccal = (rccal>>5)& 0x7;
      
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

      write_lms6002_reg(0x07, 0x0f); // TOP::EN_CAL_LPFCAL = 0 (disable)

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
      write_lms6002_reg(0x42, 0x6d); // I channel
      write_lms6002_reg(0x43, 0x94); // Q channel
      // *unit 2
      //write_lms6002_reg(0x42, 0x99); // I channel
      //write_lms6002_reg(0x43, 0x99);// Q channel

      // ### Turn on DAC after calibrations ###
      temp = (read_lms6002_reg(0x57)) | 0x80; // set 7th bit
      write_lms6002_reg(0x57, temp);
      uart_hd_putchar(temp);
      
      //GPIO_DATAOUT = (GPIO_DATAOUT & 0xfffff000) | 0x000;

      // ### TEMPORARY - Signal other cores that Lime has been initialized ###
      while(noc_IC_txbuff_isfull==1)__NOP();
      IC_NOC_TX_BUFF1 = IC_NOC_TX_BUFF2 = IC_NOC_TX_BUFF3 = 0x11; 
      
      int tempack;
      while(noc_IC_rxbuff1_av!=1)__NOP(); tempack = IC_NOC_RX_BUFF1;
      while(noc_IC_rxbuff2_av!=1)__NOP(); tempack = IC_NOC_RX_BUFF2;
      while(noc_IC_rxbuff3_av!=1)__NOP(); tempack = IC_NOC_RX_BUFF3;

      break;
    }
  }
  // *** END - part2 ***

  // some delay to allow Lime's settling down
  asm("push {r0, r1}\n"
          "movs r1, #200\n"
          "outloop: movs r0, #255\n"      // 1 cycle
          "inloop: sub r0, r0, #1\n" // 1 cycle
          "cmp r0, #0\n"         // 1 cycle
          "bne inloop\n"          // 2 if taken, 1 otherwise
          "sub r1, r1, #1\n" // 1 cycle
          "cmp r1, #0\n"         // 1 cycle
          "bne outloop\n"          // 2 if taken, 1 otherwise
          "pop {r0, r1}\n");

  // start software timer for RX
  rx_state=RX_DETECT;
  init_rxclk = 1; // init RX software clock

  // ** MAIN LOOP
  while(1){

    // *** Part3 - Lime RX (ISR) ***
    // Initialize software clock, start RX detection
    // receive and phase correct chunks separately
    if(rx_state==RX_DETECT && init_rxclk==1){
      init_rxclk = 0; // don't init RX software clock

      NVIC_EnableIRQ(6); // enable timer interrupt

      while(digitalRead(27)!=1)__NOP(); // monitor RX_IQSEL
      while(digitalRead(27)!=0)__NOP();

      TM_CTRL = 0x1; // use PCLK, start timer, enable timer
    }

    // Phase correction on received payload - with DSP core
    // estimate noise variance of payload at the same time
    else if(rx_state==RX_PHASECORR){

      // wait till DSP core is ready for phase correction
      // while(noc_IC_rxbuff3_av!=1)__NOP();
      // int tempBuff = IC_NOC_RX_BUFF3;

      // init variables for noise var estimate 
      noisevar_mean = 0;
      unsigned int sbit_tracker = 2; // sbit[i%3], pbit1[i%3+1], pbit2[i%3+2]

      // phase correction, with new freq pilot per chunk
      while(noc_IC_txbuff_isfull==1)__NOP();
      IC_NOC_TX_BUFF3 = rx_state;
      nSent = nRecv = 0;

      for(int i=0; i<CHUNK_NUM; i++){
        // * send freq pilot pair
        while(noc_IC_txbuff_isfull==1)__NOP();
        IC_NOC_TX_BUFF3 = codeword[nSent++].int_cont;
        IC_NOC_TX_BUFF3 = codeword[nSent++].int_cont;

        // * send and received corrected payload
        for(int j=0; j<CHUNK_LEN; j++){
          while(noc_IC_txbuff_isfull==1)__NOP(); // send
          IC_NOC_TX_BUFF3 = codeword[nSent++].int_cont;

          while(noc_IC_rxbuff3_av!=1)__NOP(); // receive
          codeword[nRecv].int_cont = IC_NOC_RX_BUFF3;

          // sum of sys_bit, for noise variance estimation
          if(sbit_tracker++==2){ // avoiding MOD(3)
            sbit_tracker = 0;
            noisevar_mean += codeword[nRecv].accum_cont;
          }

          nRecv++;
        }
      }

      // receive debug variable after full frame correction
      for(int i=0; i<12; i++){ 
        while(noc_IC_rxbuff3_av!=1)__NOP();
        tempdump[i] = IC_NOC_RX_BUFF3;  
        while(noc_IC_txbuff_isfull==1)__NOP();
        IC_NOC_TX_BUFF3 = tempdump[i];
      }

      // rx_state = RX_SENDUP;

      // decode frame - inform other cores
      rx_state = RX_DECODE;
      while(noc_IC_txbuff_isfull==1)__NOP();
      IC_NOC_TX_BUFF3 = RX_DECODE;
    }
    // *** END - part3 ***

    
    // *** Part4 - Turbo Decoding ***
    else if(rx_state==RX_DECODE){
      // Finish up the noise variance calculation started
      // alongside phase correction
      // E{x^2} part
      while(noc_IC_rxbuff3_av!=1)__NOP();
      temp_accumint.int_cont = IC_NOC_RX_BUFF3;
      noise_var = temp_accumint.accum_cont;
      
      // E{|x|}^2 part
      noisevar_mean >>= 8; // divide by 256
      while(noc_IC_txbuff_isfull==1)__NOP();
      temp_accumint.accum_cont = noisevar_mean;
      IC_NOC_TX_BUFF3 = temp_accumint.int_cont; // send to DSP for squaring

      while(noc_IC_rxbuff3_av!=1)__NOP();
      temp_accumint.int_cont = IC_NOC_RX_BUFF3;
      noisevar_mean = temp_accumint.accum_cont; // receive squared value back
      
      // final estimated noise variance (inverse for faster calculation)
      noise_var -= noisevar_mean;
      noise_var = 1/noise_var;

      // main decoding loop
      while(1){
        // ## Examine packet header ##
        if( (noc_IC_rxbuff3_av==1) && (flag_pkt_type==0) )
          flag_pkt_type = IC_NOC_RX_BUFF3;
        
        // ## Receive LLR/Le from NC1, decoding loop control ##
        if( noc_IC_rxbuff2_av==1 ){ 
          //adjust index accordingly, depends on dec1 / dec2
          unsigned char llridx;
          if(dec_stat==0)
            llridx = dec_i;
          else
            llridx = I_last_recv = drp_idxcalc(r_K, r_M, I_last_recv,
                                              (unsigned char*)intrlv_P, dec_i);
          
          //store LLR / Le
          temp_accumint.int_cont = IC_NOC_RX_BUFF2;
          LLR_Le_arr[llridx] = temp_accumint.accum_cont;
          
          dec_i++;
          
          while(noc_IC_txbuff_isfull==1)__NOP();
          IC_NOC_TX_BUFF2 = dec_i;
          
          //when on hold, 1 window returned allows 1 more window to be sent
          if( dspsent_i<256 && dspsend_hold==2 && (dec_i&(r_win-1))== 0 &&
              ((dspsent_i>>r_win_mul)-(dec_i>>r_win_mul))<2 )
            dspsend_hold = 0;
            
          // Switch between decoders per half iteration
          if(dec_i==256){
            dec_i = dspsent_i = 0; //reset counter
            dspsend_hold = 0; //allow sending to DSP
            
            dec_stat = 1 - dec_stat; //toggle active decoder
            
            if(dec_stat==0) //reset interleaver recursive count
              I_last_send = I_last_recv = intrlv_Im1;
            
            halfitr_ctr++;
            
            if(halfitr_ctr == ((TURBO_MAX_ITR<<1)-1)) //signal DSP core to calc LLR
              calcLLR = 1; 
          
            //to do on the final iteration - send decoded data to upper layer
            if(halfitr_ctr == (TURBO_MAX_ITR<<1)){
              recv_i = recv_done = 0; //allow receiving channel data, reset counter
              halfitr_ctr = 0; //reset iteration counter
              calcLLR = 0; //signal DSP core to calc Le
              
              // decoding done
              break;
            }
          } 
        }
        
        // ## Send bits to DSP core ##
        // * check for ACK per 1 bits packet sent
        if( (flag_pkt_type==DSP_BITSACK_HDR) && (noc_IC_rxbuff3_av==1) ){
          flag_pkt_type = 0; //clear packet type's flag
          int temp_ack = IC_NOC_RX_BUFF3;
          
          dspsend_hold = 0;
          
          //dspsend_hold criteria #2- dspsent_i is a multiple of r_win
          //hold sending if it sent out two windows, and none of them returned completely
          //or if it has sent all bits of a frame
          if( dspsent_i==256 ||
              ((dspsent_i&(r_win-1))== 0 &&
               ((dspsent_i>>r_win_mul)-(dec_i>>r_win_mul))>=2
              )
            )
            dspsend_hold = 2;
        }

        // * sending part
        if( (dspsent_i<256) && (dspsend_hold==0) ){
          // ** Send control bits, if this is first bit of an iteration
          if(dspsent_i==0){
            //send calcLLR and noise_var to DSP core
            while(noc_IC_txbuff_isfull==1)__NOP();
            IC_NOC_TX_BUFF3 = IO_CHNLCTRL_HDR; // Header - chnl_control
            
            while(noc_IC_txbuff_isfull==1)__NOP();
            IC_NOC_TX_BUFF3 = calcLLR;

            while(noc_IC_txbuff_isfull==1)__NOP();
            temp_accumint.accum_cont = noise_var;
            IC_NOC_TX_BUFF3 = temp_accumint.int_cont;
            
            //wait for ACK -  since it is blocking, no need to differentiate this one
            while(noc_IC_rxbuff3_av!=1)__NOP();
            int temp_ack = IC_NOC_RX_BUFF3;
            
          }
          
          // ** Send data bits
          while(noc_IC_txbuff_isfull==1)__NOP();
          IC_NOC_TX_BUFF3 = IO_BITS_HDR; // Header - bits
                
          //if dec1 is active
          if(dec_stat==0){          
            // temp_accumint.accum_cont = recv_sbit[dspsent_i];
            while(noc_IC_txbuff_isfull==1)__NOP(); // systematic bit
            IC_NOC_TX_BUFF3 = codeword[(dspsent_i<<1)+dspsent_i].int_cont;

            // temp_accumint.accum_cont = recv_pbit1[dspsent_i];
            while(noc_IC_txbuff_isfull==1)__NOP(); // parity bit-1
            IC_NOC_TX_BUFF3 = codeword[(dspsent_i<<1)+dspsent_i+1].int_cont;
            
            //apriori prob - ln(apriori) and ln(1-apriori)
            if(halfitr_ctr==0){ 
              while(noc_IC_txbuff_isfull==1)__NOP();
              temp_accumint.accum_cont = -0.6931;
              IC_NOC_TX_BUFF3 = temp_accumint.int_cont;

              while(noc_IC_txbuff_isfull==1)__NOP();
              IC_NOC_TX_BUFF3 = temp_accumint.int_cont;
            }
            else{
              while(noc_IC_txbuff_isfull==1)__NOP();
              temp_accumint.accum_cont = ln_approx(LLR_Le_arr[dspsent_i]);
              IC_NOC_TX_BUFF3 = temp_accumint.int_cont;

              while(noc_IC_txbuff_isfull==1)__NOP();
              temp_accumint.accum_cont = ln_approx(1-LLR_Le_arr[dspsent_i]);
              IC_NOC_TX_BUFF3 = temp_accumint.int_cont;
            }
          }
          
          //if dec2 is active
          else{ 
            I_last_send = drp_idxcalc(r_K, r_M, I_last_send,
                                      (unsigned char*)intrlv_P, dspsent_i);
            
            // temp_accumint.accum_cont = recv_sbit[I_last_send];
            while(noc_IC_txbuff_isfull==1)__NOP(); // systematic bit
            IC_NOC_TX_BUFF3 = codeword[(I_last_send<<1)+I_last_send].int_cont;
            
            // temp_accumint.accum_cont = recv_pbit2[dspsent_i];
            while(noc_IC_txbuff_isfull==1)__NOP(); // parity bit-2
            IC_NOC_TX_BUFF3 = codeword[(dspsent_i<<1)+dspsent_i+2].int_cont;
            
            //apriori prob - ln(apriori) and ln(1-apriori)
            if(halfitr_ctr==0){ // !!! Might not need this !!!
              while(noc_IC_txbuff_isfull==1)__NOP();
              temp_accumint.accum_cont = -0.6931;
              IC_NOC_TX_BUFF3 = temp_accumint.int_cont;

              while(noc_IC_txbuff_isfull==1)__NOP();
              IC_NOC_TX_BUFF3 = temp_accumint.int_cont;
            }
            else{
              while(noc_IC_txbuff_isfull==1)__NOP();
              temp_accumint.accum_cont = ln_approx(LLR_Le_arr[I_last_send]); 
              IC_NOC_TX_BUFF3 = temp_accumint.int_cont;

              while(noc_IC_txbuff_isfull==1)__NOP();
              temp_accumint.accum_cont = ln_approx(1-LLR_Le_arr[I_last_send]); 
              IC_NOC_TX_BUFF3 = temp_accumint.int_cont;
            }
          }
                      
          //increment sent bit counter
          dspsent_i++;
          
          //hold sending to dsp until ACK received for each packet
          dspsend_hold = 1;
        }
      }
      
      // interface to upper layer
      rx_state = RX_SENDUP;
    }
    // *** END - part4 ***
    

    // *** Part5 - Interfacing to upper layer ***
    // Done processing received frame, send to PC
    else if(rx_state==RX_SENDUP){

      // debug - dump freq sync calc
      uart_hd_putchar(0xf1); __NOP();

      for(int i=0; i<14; i++){ // 12(DSP)+2(IO)
        for(int j=3; j>=0; j--) 
          uart_hd_putchar(tempdump[i] >> (j<<3));
      }

      uart_hd_putchar(0xf1); __NOP();

      
      // debug - dump phase corrected codeword (s16.15)
      for(int i=0; i<CODEWORD_LEN; i+=3){
        // uart_hd_putchar(codeword[i].int_cont>>4);
        if(codeword[i].accum_cont>=0)
          uart_hd_putchar(0x1);
        else
          uart_hd_putchar(0x0);
        __NOP(); __NOP();
      }
      
      /*
      // final - send decoded frame
      for(int i=0; i<FRAME_NBITS; i++){
        if(LLR_Le_arr[i]>0)
          uart_hd_putchar(0x1);
        else
          uart_hd_putchar(0x0);
        __NOP(); __NOP();
      }
      */
      // back to detection
      rx_state = RX_DETECT;
      init_rxclk = 1; // init RX software clock
    }
    // *** END - part5 ***

  } // ** END - MAIN LOOP


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