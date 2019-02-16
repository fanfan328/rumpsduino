//------------------------------------------------------------------------------
//
#include "main.h"

static unsigned char inbuf[IN_BUF_SIZE];
static volatile short qin = 0;
static volatile short qout = 0;
 
static volatile char flag_rx_waiting_for_stop_bit;
static volatile char flag_rx_off;
static volatile char rx_mask;
static volatile char flag_rx_ready;
static volatile char flag_tx_ready;
static volatile char flag_tx_start;
static volatile char flag_tx_send_stop_bit;
static volatile char timer_rx_ctr;
static volatile char timer_tx_ctr;
static volatile char bits_left_in_rx;
static volatile char bits_left_in_tx;
static volatile char rx_num_of_bits;
static volatile char tx_num_of_bits;
static volatile char internal_rx_buffer;
static volatile char internal_tx_buffer;
static volatile char user_tx_buffer;

////////////////////////////////
// Interface routines required:
// (Platform dependent)
////////////////////////////////

// 1. get_rx_pin_status()
//    Returns 0 or 1 dependent on whether the receive pin is high or low.
char get_rx_pin_status(void){
	return ((GPIO_DATAIN >> UART_RX)&1 );
}
     
     
// 2. set_tx_pin_high()
//    Sets the transmit pin to the high state.
void set_tx_pin_high(void){
	//GPIO_DATAOUT |= 0x00010000;
	GPIO_BSET = (0x1 << UART_TX);
}


// 3. set_tx_pin_low()
//    Sets the transmit pin to the low state.
void set_tx_pin_low(void){
	//GPIO_DATAOUT &= 0xfffeffff;
	GPIO_BCLR = (0x1 << UART_TX);
}


// 4. idle()
//    Background functions to execute while waiting for input.
void idle(void){
	__NOP();
}


// 5. timer_set( BAUD_RATE )
//    Sets the timer to 3 times the baud rate.
void timer_set(void){
	
	//Timer Settings - hard coded for baud rate 19200, split into 3
	
	TM_PR = 0x115; // prescale counter target value (wanted)  
  //TM_PR = 0x44; // prescale counter - modified /4 from wanted value
	TM_COMR0 = 0x0; //timer counter target value, channel 0
  TM_COMCR = 0x3; // choose reset/stop upon reaching target value, enable interrupt
	
	TM_CTRL = 0x3; // use PCLK, reset timer, enable timer
	TM_CTRL = 0x1; // use PCLK, start timer, enable timer
}


////////////////////////////
// Generic Function
////////////////////////////
 
void timer_isr(void){
	char flag_in;
	
 	//GPIO_DATAOUT ^= 1;
 	
	// Transmitter Section
	if ( flag_tx_start == TRUE){
		if ( flag_tx_ready == FALSE ){ //send start bit
			if ( --timer_tx_ctr<=0 ){
				set_tx_pin_low();
				flag_tx_ready = TRUE;
				timer_tx_ctr = BAUD_SPLIT;
				//GPIO_DATAOUT |= 0x0000000f;
				//GPIO_DATAOUT &= 0xfffffffe; //CHECKPOINT
			}
		}
		else{ 
  		if ( --timer_tx_ctr<=0 ){
    		if ( flag_tx_send_stop_bit == FALSE){ //send data bits
					
      		if( (internal_tx_buffer >> (tx_num_of_bits - bits_left_in_tx))&1 )
      			set_tx_pin_high();
      		else
      			set_tx_pin_low();
      	
      		timer_tx_ctr = BAUD_SPLIT;
      
      		if ( --bits_left_in_tx<=0 ){ //last bit of data
      			flag_tx_send_stop_bit = TRUE;
						//GPIO_DATAOUT |= 0x0000000f;
						//GPIO_DATAOUT &= 0xfffffffd; //CHECKPOINT
      		}
      	}
      	else{ //send stop bit
					set_tx_pin_high();
					timer_tx_ctr = BAUD_SPLIT;
					flag_tx_send_stop_bit = FALSE;
					flag_tx_ready = FALSE;
					flag_tx_start = FALSE;
					//GPIO_DATAOUT |= 0x0000000f;
					//GPIO_DATAOUT &= 0xfffffffb; //CHECKPOINT
      	}
    	}
		}
		
	} // end of Transmitter Section
	
	// Receiver Section
	if ( flag_rx_off==FALSE ){
  	if ( flag_rx_waiting_for_stop_bit == TRUE){ //wait for stop bit
    	if ( --timer_rx_ctr<=0 ){
      	flag_rx_waiting_for_stop_bit = FALSE;
        flag_rx_ready = FALSE;
       	inbuf[qin] = internal_rx_buffer & 0xFF;
        if ( ++qin>=IN_BUF_SIZE )
        	qin = 0;
        //if(qin>qout){	
					//GPIO_DATAOUT |= 0x0000000f;
					//GPIO_DATAOUT &= 0xfffffffb; //CHECKPOINT
				//}
      }
		}
    else{
    	if ( flag_rx_ready==FALSE ){ //check for start bit
				// Test for Start Bit
        if ( get_rx_pin_status()==0 ){
        	flag_rx_ready = TRUE;
          internal_rx_buffer = 0;
          timer_rx_ctr = BAUD_SPLIT+1;
          bits_left_in_rx = rx_num_of_bits;
					//GPIO_DATAOUT |= 0x0000000f;
					//GPIO_DATAOUT &= 0xfffffffe; //CHECKPOINT
        }
      }
      else{ //receive data bits   
      	if ( --timer_rx_ctr<=0 ){ 
        	timer_rx_ctr = BAUD_SPLIT;
          flag_in = get_rx_pin_status();
					internal_rx_buffer |= (flag_in << (rx_num_of_bits - bits_left_in_rx));
          if ( --bits_left_in_rx<=0 ){
          	flag_rx_waiting_for_stop_bit = TRUE;
						//GPIO_DATAOUT |= 0x0000000f;
						//GPIO_DATAOUT &= 0xfffffffd; //CHECKPOINT
          }
        }
      }
    } // end of rx_test_busy
	} // end of Receiver Section
	
} //ENDFUNCTION: timer_isr
	
 
void init_uart( void ){
	
	//RUMPS specific lines
	GPIO_OEN_SET = (0x1 << UART_TX); //(tx)
	GPIO_OEN_CLR = (0x1 << UART_RX); //(rx)
	
	NVIC_SetPriority(6, 0);		// set Ext INT 6 (timer) priority
  
	NVIC_EnableIRQ(6);			  // enable Ext INT 6 (timer) 
	//
	
	flag_tx_ready = FALSE;
	flag_tx_start = FALSE;
	flag_tx_send_stop_bit = FALSE;
	flag_rx_ready = FALSE;
  flag_rx_waiting_for_stop_bit = FALSE;
  flag_rx_off = FALSE;
  rx_num_of_bits = 8;
  tx_num_of_bits = 8;
  
  set_tx_pin_high();
 
  timer_set();
  //set_timer_interrupt( timer_isr );   // Enable timer interrupt
} //ENDFUNCTION: init_uart

//the original blocking _getchar 

/*char _getchar( void ){
	char ch;
	
 	while ( qout==qin );
    //idle();
    
	//GPIO_DATAOUT |= 0x0000000f;
	//GPIO_DATAOUT &= 0xfffffff7; //CHECKPOINT
	
  ch = inbuf[qout] & 0xFF;
  if ( ++qout>=IN_BUF_SIZE )
  	qout = 0;
    	
  return ch;
}*/


//the modified non-blocking _getchar
char _getchar( char* ch){

 	if ( qout==qin )
 	  return 0;
	
  *ch = inbuf[qout] & 0xFF;
  if ( ++qout>=IN_BUF_SIZE )
  	qout = 0;
    	
  return 1;
}

void _putchar( char ch ){
	while ( flag_tx_start == TRUE);
 
	// invoke_UART_transmit
  timer_tx_ctr = BAUD_SPLIT;
  bits_left_in_tx = tx_num_of_bits;
  internal_tx_buffer = ch;
  flag_tx_start = TRUE;
}
 
void flush_input_buffer( void ) {
	qin = 0;
  qout = 0;
}
 
char kbhit( void ){
	return( qin!=qout );
}
 
void turn_rx_on( void ){
	flag_rx_off = FALSE;
}
 
void turn_rx_off( void ){
	flag_rx_off = TRUE;
}
