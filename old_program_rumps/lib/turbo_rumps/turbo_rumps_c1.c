#include "turbo_rumps_c1.h"
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
	trel.numStates = 8;
	
	trel.nextStates[0][0] = 0; trel.nextStates[0][1] = 4;
	trel.nextStates[1][0] = 4; trel.nextStates[1][1] = 0;
	trel.nextStates[2][0] = 5; trel.nextStates[2][1] = 1;
	trel.nextStates[3][0] = 1; trel.nextStates[3][1] = 5;
	trel.nextStates[4][0] = 2; trel.nextStates[4][1] = 6;
	trel.nextStates[5][0] = 6; trel.nextStates[5][1] = 2;
	trel.nextStates[6][0] = 7; trel.nextStates[6][1] = 3;
	trel.nextStates[7][0] = 3; trel.nextStates[7][1] = 7;
	pinMode_output(0);
	
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
// Part of Turbo Decoder
// LUT Log-MAP's max* function 
accum max_f(accum a, accum b){
	//find max(a,b) and |a-b|
	accum ret_value = a;
	accum diff = a-b;
	if(diff<0){
		ret_value = b;
		//diff *= -1;
	}
	
	return ret_value;
}

//********************************************
// Part of Turbo Decoder
// calc forward metric alpha for one timestamp
void r_turbodec_acalc(accum curr_delta[16], accum curr_alpha[8], accum nxt_alpha[8]){
	accum alpha_sum = 0;
	for(char i=0; i<trel.numStates; i++){
		nxt_alpha[i] = max_f(curr_delta[(trel.prevStates[i][0]<<1)] + curr_alpha[trel.prevStates[i][0]],
												 curr_delta[(trel.prevStates[i][1]<<1)+1] + curr_alpha[trel.prevStates[i][1]]);
		if(i==0)
			alpha_sum = nxt_alpha[i];
		else
			alpha_sum = max_f(alpha_sum, nxt_alpha[i]);
	}
	//normalization
	for(char i=0; i<trel.numStates; i++) nxt_alpha[i]-=alpha_sum;
}

//********************************************
// Part of Turbo Decoder
// calc forward metric beta for one timestamp
void r_turbodec_bcalc(accum curr_delta[16], accum curr_beta[8], accum nxt_beta[8]){
	accum beta_sum = 0;
	for(char i=0; i<trel.numStates; i++){
		nxt_beta[i] = max_f(curr_delta[(i<<1)] + curr_beta[trel.nextStates[i][0]],
												 curr_delta[(i<<1)+1] + curr_beta[trel.nextStates[i][1]]);
		if(i==0)
			beta_sum = nxt_beta[i];
		else
			beta_sum = max_f(beta_sum, nxt_beta[i]);
	}
	//normalization
	for(char i=0; i<trel.numStates; i++) nxt_beta[i]-=beta_sum;
}