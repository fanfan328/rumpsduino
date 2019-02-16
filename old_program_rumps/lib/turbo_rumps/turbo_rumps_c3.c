#include "turbo_rumps_c3.h"
#include "mac.h"
#include "libdivide_rumps.h"
#include "gpio.h"
//#include "libdivide.h"

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
	pinMode_output(0);
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
// -calc branch metric delta for one timestamp 
void r_turbodec_dcalc(accum recv_sys, accum recv_par, accum delta[16], accum ln_apriori1, accum ln_apriori0, accum noise_var){
  
	accum_int_t sbit_corr, pbit_corr, noisevar_temp;
  accum_int_t ut, vt;
  
	//noisevar_temp.accum_cont = 1/noise_var;
	noisevar_temp.accum_cont = noise_var;
	//struct libdivide_s64_t tdenom = libdivide_s64_gen(noisevar_temp.int_cont);
  
  // iterates through possible state transitions
  for(char i=0; i<(trel.numStates<<1); i++){
		// get trellis output prototype, convert to bipolar
		signed char u = (trel.outputs[i] & (2))>>1;
		signed char v = trel.outputs[i] & (1);
		u = u+u-1;
		v = v+v-1;
		
	  sbit_corr.accum_cont = recv_sys;
	  pbit_corr.accum_cont = recv_par;
		
		// save sys and par bit in accum_int_t because we want to use MAC
		ut.accum_cont = u;
		vt.accum_cont = v;
		
		// use MAC to do MUL
		int64_t temp_64b = mac_smul_32((int32_t)(sbit_corr.int_cont), (int32_t)(ut.int_cont));
		sbit_corr.int_cont = ( temp_64b >> 15);
		temp_64b = mac_smul_32((int32_t)(pbit_corr.int_cont), (int32_t)(vt.int_cont));
		pbit_corr.int_cont = ( temp_64b >> 15);
		//sbit_corr.accum_cont *= ut.accum_cont;
		//pbit_corr.accum_cont *= vt.accum_cont;
		
		// use libdivide library for DIV
		sbit_corr.accum_cont += pbit_corr.accum_cont;
		//tdenom = libdivide_s64_gen(noisevar_temp.int_cont);
  	//sbit_corr.int_cont = libdivide_s64_do((int64_t)(sbit_corr.int_cont)<<15, &tdenom);
		//sbit_corr.accum_cont /= noise_var;
		//sbit_corr.accum_cont *= noisevar_temp.accum_cont;
		temp_64b = mac_smul_32((int32_t)sbit_corr.int_cont, (int32_t)noisevar_temp.int_cont);
		sbit_corr.int_cont = (temp_64b >> 15);
		
		// recall apriori is P(x=1)
		if((i&1)==0)
			delta[i] = ln_apriori0 + sbit_corr.accum_cont;
		else
			delta[i] = ln_apriori1 + sbit_corr.accum_cont;
		
		//delta[i] = ln_approx(1-apriori) + (recv_sys*u + recv_par*v)/noise_var;
		// (sbit_corr.accum_cont + pbit_corr.accum_cont)
  }  
}

//********************************************
// Part of Turbo Decoder
// -calc LLR / Le for one timestamp
// -isLLR=1 means output LLR instead of Le
accum r_turbodec_llrcalc(accum curr_delta[16], accum curr_alpha[8], accum nxt_beta[8], accum recv_parity_bit, char isLLR, accum noise_var){
	accum LLR_Le_num = 0;
	accum LLR_Le_denom = 0;
	accum ratio = 0;
	signed char nextState, v;
	
	accum_int_t pbit_corr, noisevar_temp;
	accum_int_t vt;
	
	//noisevar_temp.accum_cont = 1/noise_var;
	noisevar_temp.accum_cont = noise_var;
	//struct libdivide_s64_t tdenom = libdivide_s64_gen(noisevar_temp.int_cont);
	
	for(char i=0; i<trel.numStates; i++){
		//numerator part - bit 1
		nextState = trel.nextStates[i][1];
		v = trel.outputs[(i<<1) + 1] & (1);
		v = v+v-1;
    
	  pbit_corr.accum_cont = recv_parity_bit;
	
    // use MAC for MUL
		vt.accum_cont = v;
		int64_t temp_64b = mac_smul_32((int32_t)(pbit_corr.int_cont), (int32_t)(vt.int_cont));
		pbit_corr.int_cont = (temp_64b>>15);
		//pbit_corr.accum_cont *= vt.accum_cont;
		
		// use libdivide for DIV
		//tdenom = libdivide_s64_gen(noisevar_temp.int_cont);
  	//pbit_corr.int_cont = libdivide_s64_do((int64_t)(pbit_corr.int_cont)<<15, &tdenom);
		//pbit_corr.accum_cont /= noise_var;
		//pbit_corr.accum_cont *= noisevar_temp.accum_cont;
		temp_64b = mac_smul_32((int32_t)pbit_corr.int_cont, (int32_t)noisevar_temp.int_cont);
		pbit_corr.int_cont = (temp_64b >> 15);	
				
    if(i==0){
			if(isLLR==1)
				LLR_Le_num = curr_alpha[i] + nxt_beta[nextState] + curr_delta[(i<<1)+1];
      else 
				LLR_Le_num = curr_alpha[i] + nxt_beta[nextState] + pbit_corr.accum_cont;
    }
    else{
      if(isLLR==1)
				LLR_Le_num = max_f( LLR_Le_num, curr_alpha[i] + nxt_beta[nextState] + curr_delta[(i<<1)+1] );
      else
				LLR_Le_num = max_f( LLR_Le_num, curr_alpha[i] + nxt_beta[nextState] + pbit_corr.accum_cont );
    }
		
		//denominator part - bit 0
		nextState = trel.nextStates[i][0];
		v = trel.outputs[(i<<1)] & (1);
		v = v+v-1;
    
	  pbit_corr.accum_cont = recv_parity_bit;
	  
    // use MAC for MUL
		vt.accum_cont = v;
		temp_64b = mac_smul_32((int32_t)(pbit_corr.int_cont), (int32_t)(vt.int_cont));
		pbit_corr.int_cont = (temp_64b>>15);
		//pbit_corr.accum_cont *= vt.accum_cont;
		
		// use libdivide for DIV
		//tdenom = libdivide_s64_gen(noisevar_temp.int_cont);
  	//pbit_corr.int_cont = libdivide_s64_do((int64_t)(pbit_corr.int_cont)<<15, &tdenom);
		//pbit_corr.accum_cont /= noise_var;
		//pbit_corr.accum_cont *= noisevar_temp.accum_cont;
		temp_64b = mac_smul_32((int32_t)pbit_corr.int_cont, (int32_t)noisevar_temp.int_cont);
		pbit_corr.int_cont = (temp_64b >> 15);	
	  
    if(i==0){
			if(isLLR==1)
				LLR_Le_denom = curr_alpha[i] + nxt_beta[nextState] + curr_delta[(i<<1)];
      else 
				LLR_Le_denom = curr_alpha[i] + nxt_beta[nextState] + pbit_corr.accum_cont;
    }
    else{
      if(isLLR==1)
				LLR_Le_denom = max_f( LLR_Le_denom, curr_alpha[i] + nxt_beta[nextState] + curr_delta[(i<<1)] );
    	else
				LLR_Le_denom = max_f( LLR_Le_denom, curr_alpha[i] + nxt_beta[nextState] + pbit_corr.accum_cont );
    }	
	}
	
	//clipping of value to avoid overflow
	ratio = LLR_Le_num - LLR_Le_denom;
	if(ratio>10)
		ratio = 10;
	else if(ratio<-10)
		ratio = -10;
	/*
	//change ratio -> prob for Le
	if(!isLLR){
		ratio = ratio2prob(exp_approx(ratio));
		if(ratio > 0.99990)
			ratio = 0.99990;
		else if(ratio < 0.0001)
			ratio = 0.0001;
	}
  */
	return ratio;
}

// Convert ratio to probability (for apriori bit probability)
//Formula is simply ratio/(1+ratio)
accum ratio2prob(accum ratio){
	/*accum_int_t temp;
	temp.accum_cont = ratio;
	
	// use libdivide for DIV
	struct libdivide_s64_t tdenom = libdivide_s64_gen(temp.int_cont+0x8000); // ratio+1 (fixed point)
  temp.int_cont = libdivide_s64_do((int64_t)(temp.int_cont)<<15, &tdenom);
	
	return temp.accum_cont;
	*/
	return ratio/(1+ratio);
}
