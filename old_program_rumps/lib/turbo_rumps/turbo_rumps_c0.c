#include "turbo_rumps_c0.h"
#include "gpio.h"

trellis trel;

const unsigned char intrlv_P[r_M] = {39, 26, 221, 22};

const unsigned char deintrlv_P[r_M] = {214, 199, 194, 181};

// Set encoder trellis
void set_trellis(){
	/*
	 	RSC encoder structure:
	 	- K=4
	 	- G1=13, G2=15, feedback=13
	*/
	int a = trel.numStates;
	pinMode_output(18);
	trel.numStates = 8;
	
	trel.nextStates[0][0] = 0; trel.nextStates[0][1] = 4;
	trel.nextStates[1][0] = 4; trel.nextStates[1][1] = 0;
	trel.nextStates[2][0] = 5; trel.nextStates[2][1] = 1;
	trel.nextStates[3][0] = 1; trel.nextStates[3][1] = 5;
	trel.nextStates[4][0] = 2; trel.nextStates[4][1] = 6;
	trel.nextStates[5][0] = 6; trel.nextStates[5][1] = 2;
	trel.nextStates[6][0] = 7; trel.nextStates[6][1] = 3;
	trel.nextStates[7][0] = 3; trel.nextStates[7][1] = 7;
	
	trel.outputs[0] = 0; trel.outputs[1] = 3;
	trel.outputs[2] = 0; trel.outputs[3] = 3;
	trel.outputs[4] = 1; trel.outputs[5] = 2;
	trel.outputs[6] = 1; trel.outputs[7] = 2;
	trel.outputs[8] = 1; trel.outputs[9] = 2;
	trel.outputs[10] = 1; trel.outputs[11] = 2;
	trel.outputs[12] = 0; trel.outputs[13] = 3;
	trel.outputs[14] = 0; trel.outputs[15] = 3;
	
	trel.prevStates[0][0] = 0; trel.prevStates[0][1] = 1;
	trel.prevStates[1][0] = 3; trel.prevStates[1][1] = 2;
	trel.prevStates[2][0] = 4; trel.prevStates[2][1] = 5;
	trel.prevStates[3][0] = 7; trel.prevStates[3][1] = 6;
	trel.prevStates[4][0] = 1; trel.prevStates[4][1] = 0;
	trel.prevStates[5][0] = 2; trel.prevStates[5][1] = 3;
	trel.prevStates[6][0] = 5; trel.prevStates[6][1] = 4;
	trel.prevStates[7][0] = 6; trel.prevStates[7][1] = 7;
}

//********************************************
// Convolutionally encode data
// return the codeword in parameter, and
// return the final state of encoder
char convenc(unsigned char* seq, size_t dlen, unsigned char* codeword, unsigned char start_state){
	unsigned char curr_state = start_state;
	
	for(int i=0; i<dlen; i++){
		// codeword outputs	
		unsigned char u = (trel.outputs[(curr_state<<1)+seq[i]] & (2))>>1;
		unsigned char v = trel.outputs[(curr_state<<1)+seq[i]] & (1);
		
		codeword[(i<<1)] = u;
		codeword[(i<<1)+1] = v;
		
		curr_state = trel.nextStates[curr_state][seq[i]];
	}
	
	return curr_state;
}

//********************************************
// Turbo encoder
// seq	: as input, is original data sequence (WITHOUT termination bits)
// N		: length of original sequence (WITHOUT termination bits)
// codeword	: resulting codeword, sized N*3
	/* trellis description of encoder
	two identical RSC encoders:
	- K=4
	- G1=13, G2=15, feedback=13
	
	Total code rate of 1/3, no puncturing 
	trellis are not zero-terminated
	DRP interleaver is used
	*/
void r_turbo_encode(unsigned char* seq, size_t dlen, unsigned char* codeword){
	
	// vector for each encoder's resulting codeword
	unsigned char codeword_enc1[(dlen<<1)];
	unsigned char codeword_enc2[(dlen<<1)];
	
	/// First Encoder
	convenc(seq, dlen, codeword_enc1, 0);
	
	// interleave sequence
	unsigned char seq_i[dlen]; 
	drpintrlv_uc(seq, seq_i);
	
	// Second Encoder	
	convenc(seq_i, dlen, codeword_enc2, 0);
	
	/**************************
	* MUX
	**************************/	
	for(int i=0; i<dlen; i++){
		/*// systematic bit
		codeword[i+(i<<1)] = codeword_enc1[(i<<1)];
		
		// parity bit - punctured
    codeword[i+(i<<1)+1] = codeword_enc1[(i<<1)+1];
    codeword[i+(i<<1)+2] = codeword_enc2[(i<<1)+1];*/

		// systematic bit
		codeword[3*i] = codeword_enc1[2*i];
		
		// parity bit - punctured
    codeword[3*i+1] = codeword_enc1[2*i+1];
    codeword[3*i+2] = codeword_enc2[2*i+1];	
	}

	//*****************************
}
/*
//********************************************
// Part of Turbo Decoder
// -deMUX incoming seq to seqs for dec1 and dec2
// -done for every 3 bits of incoming seq
// -i keeps track on index of received seq (0, 1, .. ,K)
void r_turbodec_demux(accum recv_seq[3], accum* seq_dec1, accum* seq_dec2, unsigned char i){
	
	//note that seq_dec2's systematic bits are not interleaved
	//we interleave that after the whole sequence has been received
	//meanwhile we can still go through SW decoding for dec1
	
	seq_dec1[(i<<1)] = seq_dec2[(i<<1)] = recv_seq[0];
	seq_dec1[(i<<1)+1] = recv_seq[1];
	seq_dec2[(i<<1)+1] = recv_seq[2];
}*/
