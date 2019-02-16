//------------------------------------------------------------------------------
// Library
// Name				: turbo_rumps.h
// Purpose
//	- turbo encoding & decoding, fixed configuration
//	- two identical RSC encoders:
//	 	K=4
//	 	G1=13, G2=15, feedback=13
//	- Both encoders are not trellis terminated
//	- Log-MAP algorithm
//	- functions are made as general as possible

#ifndef __TURBO_RUMPS_H_
#define __TURBO_RUMPS_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdfix.h>
#include "fixpoint_math.h"
#include "drp_intrlv.h"

#define r_win 32
#define r_win_mul 5
#define intrlv_Im1 229
#define deintrlv_Im1 238

// create trellis structure
typedef struct {
	
	unsigned char numStates;
	unsigned char nextStates [8][2];
	unsigned char outputs [16];
	unsigned char prevStates [8][2];
}trellis;

// Set trellis based on defined spec
void set_trellis();

// Convolutionally encode data
char convenc(unsigned char* seq, size_t dlen, unsigned char* codeword, unsigned char start_state);

// Encode data with turbo encoder structure
void r_turbo_encode(unsigned char* seq, size_t dlen, unsigned char* codeword);

// Decoder - deMUX incoming seq for dec1 and dec2
void r_turbodec_demux(accum recv_seq[3], accum* seq_dec1, accum* seq_dec2, unsigned char i);

// Decoder - Log-MAP's max* function 
accum max_f(accum a, accum b);

// Decoder - calc branch metric delta
void r_turbodec_dcalc(accum recv_sys, accum recv_par, accum delta[16], accum apriori, accum noise_var);

// Decoder - calc LLR / Le
accum r_turbodec_llrcalc(accum curr_delta[16], accum curr_alpha[8], accum nxt_beta[8], accum recv_parity_bit, char isLLR, accum noise_var);

// Decoder - calc forward metric alpha
void r_turbodec_acalc(accum curr_delta[16], accum curr_alpha[8], accum nxt_alpha[8]);

// Decoder - calc forward metric beta
void r_turbodec_bcalc(accum curr_delta[16], accum curr_beta[8], accum nxt_beta[8]);

// Convert unipolar to bipolar 
void uni2bi(char* uni_seq, int N);

// Convert ratio to probability (for apriori bit probability)
accum ratio2prob(accum ratio);

//------------------------------------------------------------------------------
// Global variables
trellis trel;

extern const unsigned char intrlv_P[r_M];

extern const unsigned char deintrlv_P[r_M];

#endif /* __TURBO_RUMPS_H_ */
