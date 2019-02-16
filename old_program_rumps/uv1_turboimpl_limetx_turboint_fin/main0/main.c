//------------------------------------------------------------------------------

// 

// Main Program
// Application    : turboimpl_ioc
// Core           : IO Core
// Purpose
//  - Turbo code implementation on RUMPS401
//  - > recv input seq, govern operations

// ************** Integrated version - final structure **************

// ### Interfacing with LMS6002D, TX part ###

#include "main.h"
#include "turbo_rumps_c0.h"

// packet structure
#define TURBO_MAX_ITR 1
#define TIMINGPILOT_LEN 16
#define CHUNK_LEN 48
#define CHUNK_NUM 16
#define N_FREQPILOTPAIR 16 // pairs
#define CODEWORD_LEN 768
#define RF_PKTLEN TIMINGPILOT_LEN + (N_FREQPILOTPAIR<<1) + CODEWORD_LEN

// upper layer needs
#define UPPERLAYER_HDR 0xf1
#define FRAME_NBITS 256 // in bits
#define FRAME_NBYTES 32 // in bytes

// shared with isr0.c
#define TX_SETUP 0xa0
#define TX_INITLIME 0xa1
#define TX_IDLE 0xa2
#define TX_ENCODE 0xa3
#define TX_ON 0xa4

volatile uint8_t tx_state = TX_IDLE; 
unsigned char sendPkt[RF_PKTLEN] = {0};

// for pulse shaping
#define RCOS_FIRLEN 12
int rcos_lut[2][RCOS_FIRLEN]; // raised cosine impulse response LUT

int arrctr0, arrctr1, arrctr2, arrctr3, arrctr4; // pulse components counter
unsigned int symctr0, symctr1, symctr2, symctr3, symctr4; // symbol counter
volatile int send_buff = 0;

void init_pulseshaping(); // init raised cosine filter LUT

// LMS6002d register access function
void write_lms6002_reg(uint8_t reg_addr, uint8_t data);
uint8_t read_lms6002_reg(uint8_t reg_addr);

//------------------------------------------------------------------------------

int main(void)

{ 

  // *** Part1 - Setup ***
  // state indicator
  tx_state = TX_SETUP;
  while(noc_IC_txbuff_isfull==1)__NOP();
  IC_NOC_TX_BUFF1 = IC_NOC_TX_BUFF2 = IC_NOC_TX_BUFF3 = tx_state;

  // Set GPIOs & MUX
  MUXC_SELECT = 0x40; // SPI, GPIO[27:0]
  MUXC_PU &= 0x0f000000;  // SPI, GPIO[23:0] PU disabled

  // Timer Settings - Lime digital interface CLK
  TM_PR = 0x7cf; // prescale target, (7cf)8kbps
  TM_COMR0 = 0x0; //timer target, ch 0
  TM_COMCR = 0x3; // reset on COMR, enable interrupt
  NVIC_SetPriority(6, 0);   // set Ext INT 6 (timer) priority

  TM_CTRL = 0x3; // use PCLK, enable timer, reset timer

  // GPIOs direction
  GPIO_OEN_SET = 0xfff; // output, GPIO[11:0]
  GPIO_OEN_CLR = 0xfff000; //input, GPIO[23:12]
  pinMode_input(26); // tweak - TX_IQSEL
  pinMode_input(27); // tweak - RX_IQSEL

  // define variables
  unsigned char oriFrame[FRAME_NBYTES]; // in bytes
  unsigned char codeword[CODEWORD_LEN] = {0};

  // Initialization
  set_trellis(); // Turbo code trellis
  
  uart_hd_init_uart(1); // UART, 115200 baud
  
         // CPHA, CPOL, BC,   IE,   FSB,  SS,   CR
  spi_init( 0x0,  0x0,  0x1,  0x0,  0x0,  0x1,  0x1); // SPI
  spi_enable();

  init_pulseshaping(); // pulse shaping LUT

  // Initialize chunk packet - preambles are constant
  // * timing pilot - 8 pairs of 2'b10
  for(int i=0; i<TIMINGPILOT_LEN; i++){
    if(i%2==0)
      sendPkt[i] = 1;
    else
      sendPkt[i] = 0;
  }
  // * freq pilot - 2'b11 per chunk-sized bits
  unsigned int sIdx = TIMINGPILOT_LEN;
  for(int i=0; i<N_FREQPILOTPAIR; i++){
    sendPkt[sIdx] = sendPkt[sIdx+1] = 1;
    sIdx += (CHUNK_LEN+2);
  }

  // *** END - part1 ***

  // *** Part2 - Lime's initialization ***
  // state indicator
  tx_state = TX_INITLIME;
  while(noc_IC_txbuff_isfull==1)__NOP();
  IC_NOC_TX_BUFF1 = IC_NOC_TX_BUFF2 = IC_NOC_TX_BUFF3 = tx_state;

  // init lime on user's command
  while(1){
    if(uart_hd_getchar()=='s'){
      // TOP Level
      write_lms6002_reg(0x05, 0x3a); // Soft tx enable
      uart_hd_putchar(read_lms6002_reg(0x05));

      // ### TX Chain ###
      write_lms6002_reg(0x09, 0xc5); // Clock buffers - Tx/Rx DSM SPI
      uart_hd_putchar(read_lms6002_reg(0x09));
      
      // Tx LPF
      write_lms6002_reg(0x34, 0x3e); // select LPF bandwidth (.75MHz)
      uart_hd_putchar(read_lms6002_reg(0x34));
      
      // Tx RF
      write_lms6002_reg(0x41, 0x12); // VGA1 gain (-17dB)
      uart_hd_putchar(read_lms6002_reg(0x41));
      
      write_lms6002_reg(0x45, 0x00); // VGA2 gain (0dB) bit 7-3
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
      
      write_lms6002_reg(0x10, 0x56); // N integer 
      uart_hd_putchar(read_lms6002_reg(0x10));

      write_lms6002_reg(0x11, 0x99); // N fractional over 3 registers
      uart_hd_putchar(read_lms6002_reg(0x11));

      write_lms6002_reg(0x12, 0x99);
      uart_hd_putchar(read_lms6002_reg(0x12));

      write_lms6002_reg(0x13, 0x99);
      uart_hd_putchar(read_lms6002_reg(0x13));

      // debug - turn of TX RF
      //write_lms6002_reg(0x40, 0x0);
      //uart_hd_putchar(read_lms6002_reg(0x40));

      //write_lms6002_reg(0x19, 0x22 + 0x80); // tuned vco cap value
      //uart_hd_putchar(read_lms6002_reg(0x19));
      
      // auto vco cap tuning
      uint8_t last_vtune = 0x2;
      uint8_t curr_vtune;
      uint8_t cmax, cmin;
      for(uint8_t i=0; i<64; i++){
        // change vcocap
        write_lms6002_reg(0x19, i + 0x80);
        for(uint8_t delay=0; delay<100; delay++)__NOP();
        
        // read vtune
        curr_vtune = read_lms6002_reg(0x1a)>>6;
        
        // find cmin cmax
        if(last_vtune==0x2 && curr_vtune==0x0)
          cmin = i;
        else if(last_vtune==0x0 && curr_vtune==0x1){
          cmax = i;
          write_lms6002_reg(0x19, ((cmin+cmax)/2) + 0x80);
          break;
        }
        last_vtune = curr_vtune;
      }
      uart_hd_putchar(read_lms6002_reg(0x19));

      uint8_t topspi_clken;
      uint8_t clbr_result, data_r;
      uint8_t dccal, rccal;

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
      // *unit 1
      // write_lms6002_reg(0x42, 0x6d); // I channel
      // write_lms6002_reg(0x43, 0x94); // Q channel
      // *unit 2
      write_lms6002_reg(0x42, 0x99); // I channel
      write_lms6002_reg(0x43, 0x99);// Q channel

      // ### Turn on DAC after calibrations ###
      temp = (read_lms6002_reg(0x57)) | 0x80; // set 7th bit
      write_lms6002_reg(0x57, temp);
      uart_hd_putchar(temp);
      
      // ### ADC/DAC MISC_CTRL ###
      // TX_sync = 0, IQ
      // RX_sync = 1, IQ
      write_lms6002_reg(0x5a, 0xa0);
      /*
      // ### RF Loopback, mix to LNA1 ###
      write_lms6002_reg(0x08, 0x01);
      uart_hd_putchar(read_lms6002_reg(0x08));

      // ### Set Aux PA gain ###
      temp = ((read_lms6002_reg(0x4c)) & 0xf0) | 0x0;
      write_lms6002_reg(0x4c, temp);
      uart_hd_putchar(temp);
      */

      break;
    }
  }

  // reset TX output line
  GPIO_DATAOUT = (GPIO_DATAOUT & 0xfffff000);

  // *** END - part2 ***

  while(1){ // Main loop

    // *** Part3 - Waiting for frame to be sent, from upper layer ***
    // state indicator
    tx_state = TX_IDLE;
    while(noc_IC_txbuff_isfull==1)__NOP();
    IC_NOC_TX_BUFF1 = IC_NOC_TX_BUFF2 = IC_NOC_TX_BUFF3 = tx_state;

    uart_hd_putchar(tx_state);

    // ~~ Temporary ~~
    /*
    volatile char ch = uart_hd_getchar();
    volatile char vga1reg = read_lms6002_reg(0x41);
    uart_hd_putchar(vga1reg);
    if(ch==']'){
      vga1reg++;
      write_lms6002_reg(0x41, vga1reg); // VGA1 gain
    }
    else if(ch=='['){
      vga1reg--;
      write_lms6002_reg(0x41, vga1reg); // VGA1 gain
    }
    uart_hd_putchar(vga1reg);
    */
    // ~~ Temporary ~~
    
    // wait for frame header - data / vga1 control
    volatile char ch;
    volatile char vga1reg;

    while(1){
      vga1reg = read_lms6002_reg(0x41);
      ch = uart_hd_getchar();

      if(ch==UPPERLAYER_HDR)
        break;

      else if(ch==']') // increase vga1
        vga1reg++;
      
      else if(ch=='[') // decrease vga1
        vga1reg--;

      write_lms6002_reg(0x41, vga1reg); // VGA1 gain
      uart_hd_putchar(read_lms6002_reg(0x41)); // TX VGA1 info
    }
    
    // retrieves frames - 256 bits
    for(int i=0; i<FRAME_NBYTES; i++){
      oriFrame[i] = uart_hd_getchar(); // get frame in bytes

      __NOP();__NOP();__NOP();__NOP();
      __NOP();__NOP();__NOP();__NOP();

      uart_hd_putchar(oriFrame[i]);
    }

    // unpack to "non-efficient" structure,
    // due to encoder/pulse shaper argument structure
    unsigned char unpackedFrame[FRAME_NBITS] = {0};
    for(int i=0; i<FRAME_NBITS; i++){
      // packed idx i/8, bit i%8
      unpackedFrame[i] = ((oriFrame[i>>3]) >> (i&7)) & 0x1;
    }
    // *** END - part3 ***



    // *** Part4 - Turbo Coding & Packet Construction***
    // state indicator
    tx_state = TX_ENCODE;
    while(noc_IC_txbuff_isfull==1)__NOP();
    IC_NOC_TX_BUFF1 = IC_NOC_TX_BUFF2 = IC_NOC_TX_BUFF3 = tx_state;

    uart_hd_putchar(tx_state);

    // Turbo encoding
    /*
    // ~~ Temporary
    for(int i=0; i<CODEWORD_LEN; i++){
      if(i%3==0)
        codeword[i] = 0;
      else
        codeword[i] = 1;
    }
    codeword[0] = 1;
    codeword[1] = codeword[2] = 0;    
    */
    r_turbo_encode(unpackedFrame, FRAME_NBITS, codeword);

    // Packet construction
    unsigned int pktIdx = TIMINGPILOT_LEN+2;
    unsigned int tracker = 0; // to avoid MOD(24)
    for(int i=0; i<CODEWORD_LEN; i++){
      sendPkt[pktIdx] = codeword[i];
      if(++tracker==CHUNK_LEN){
        tracker = 0;
        pktIdx += 3;
      }
      else
        pktIdx++;
    }    

    // *** END - part4 ***

    

    // *** Part5 - Chunk construction & Lime TX ***
    // state indicator
    tx_state = TX_ON;
    while(noc_IC_txbuff_isfull==1)__NOP();
    IC_NOC_TX_BUFF1 = IC_NOC_TX_BUFF2 = IC_NOC_TX_BUFF3 = tx_state;

    uart_hd_putchar(tx_state);
    
    // start Lime TX
    tx_state = TX_ON;
    NVIC_EnableIRQ(6); // enable Ext INT 6 (timer)

    while(digitalRead(26)!=0)__NOP(); // monitor TX_IQSEL
    while(digitalRead(26)!=1)__NOP();

    // slight clock timing adjustment
    asm("push {r0}\n"
        "movs  r0, #3\n"      // 1 cycle
        "loop: sub  r0, r0, #1\n" // 1 cycle
        "cmp r0, #0\n"         // 1 cycle
        "bne  loop\n"          // 2 if taken, 1 otherwise
        "pop {r0}\n");

    TM_CTRL = 0x1; // use PCLK, enable timer, start timer
    
    while(tx_state!=TX_IDLE)__NOP(); // wait for a chunk TX

    // slight delay before updating next tx_state to PC
    // due to crappy UART module
    asm("push {r0, r1}\n"
          "movs r1, #1\n"
          "outloop: movs r0, #255\n"      // 1 cycle
          "inloop: sub r0, r0, #1\n" // 1 cycle
          "cmp r0, #0\n"         // 1 cycle
          "bne inloop\n"          // 2 if taken, 1 otherwise
          "sub r1, r1, #1\n" // 1 cycle
          "cmp r1, #0\n"         // 1 cycle
          "bne outloop\n"          // 2 if taken, 1 otherwise
          "pop {r0, r1}\n");

    // *** END - part5 ***



  } // END - Main loop

  return 0;

}

void init_pulseshaping(){
  // set LUT
  // ** bit 0
  rcos_lut[0][0] = rcos_lut[0][8] = 0; //0
  rcos_lut[0][1] = rcos_lut[0][7] = -186; // -269
  rcos_lut[0][2] = rcos_lut[0][6] = -425; // -615
  rcos_lut[0][3] = rcos_lut[0][5] = -628; // -909
  rcos_lut[0][4] = -707; // -1024

  // ** bit 1
  rcos_lut[1][0] = rcos_lut[1][8] = 0; //0
  rcos_lut[1][1] = rcos_lut[1][7] = 186; // 269
  rcos_lut[1][2] = rcos_lut[1][6] = 425; // 615
  rcos_lut[1][3] = rcos_lut[1][5] = 628; // 909
  rcos_lut[1][4] = 707; // 1024

  // dummy guard band
  rcos_lut[0][9] = rcos_lut[0][10] = rcos_lut[0][11] = 0;
  rcos_lut[1][9] = rcos_lut[1][10] = rcos_lut[1][11] = 0;

  // init counters
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

  send_buff = rcos_lut[sendPkt[symctr0]][arrctr0];
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
