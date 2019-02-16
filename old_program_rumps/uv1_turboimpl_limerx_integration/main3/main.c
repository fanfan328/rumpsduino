//------------------------------------------------------------------------------

//

// Main Program
// Application    : turboimpl_dspc
// Core           : DSP Core
// Purpose
//  - Turbo code implementation on RUMPS401
//  - > calc d, LLR

// ### Interfacing with LMS6002D, RX part ###

#include "main.h"
#include "turbo_rumps_c3.h"
//#include "libdivide_rumps.h"

#define CODEWORD_LEN 768

#define IO_CHNLCTRL_HDR 0x1
#define IO_BITS_HDR 0x2
#define IO_LLRACK_HDR 0x3
#define IO_NOVAR_HDR 0x4
#define IO_STARTTURBO_HDR 0xa
#define IO_RDY 0xb
#define IO_TX 0xc

#define DSP_LLR_HDR 0x31
#define DSP_BITSACK_HDR 0x32

#define RX_STOP 0xa0
#define RX_DETECT 0xa1
#define RX_TIMING_SYNC 0xa2
#define RX_FREQ_SYNC 0xa3
#define RX_PAYLOAD 0xa4
#define RX_PHASECORR 0xa5
#define RX_DECODE 0xa6
#define RX_SENDUP 0xa7
#define STATE_EMPTY 0xaf

// LED pin number
const unsigned char ledpin = 0;

// Frequency sync known-preamble
const accum freqsync_preamble[2] = { // since I=Q, this saves space
  0.707,   // 1st preamble - 1
  0.707  // 2nd preamble - 1
};

// Mapping constant for sine-cos lookup
// const unsigned long accum sineMappingConst = 0.0012437;
const unsigned long accum sineMappingConst = 0.0024875;
// const unsigned long accum sineMappingConst = 0.0049751;

// Sine look up table
/*
const accum sineTable[256] = {
  0.0000, 0.0245, 0.0491, 0.0736, 0.0980, 0.1224, 0.1467, 0.1710,
  0.1951, 0.2191, 0.2430, 0.2667, 0.2903, 0.3137, 0.3369, 0.3599,
  0.3827, 0.4052, 0.4276, 0.4496, 0.4714, 0.4929, 0.5141, 0.5350,
  0.5556, 0.5758, 0.5957, 0.6152, 0.6344, 0.6532, 0.6716, 0.6895,
  0.7071, 0.7242, 0.7410, 0.7572, 0.7730, 0.7883, 0.8032, 0.8176,
  0.8315, 0.8449, 0.8577, 0.8701, 0.8819, 0.8932, 0.9040, 0.9142,
  0.9239, 0.9330, 0.9415, 0.9495, 0.9569, 0.9638, 0.9700, 0.9757,
  0.9808, 0.9853, 0.9892, 0.9925, 0.9952, 0.9973, 0.9988, 0.9997,
  1.0000, 0.9997, 0.9988, 0.9973, 0.9952, 0.9925, 0.9892, 0.9853,
  0.9808, 0.9757, 0.9700, 0.9638, 0.9569, 0.9495, 0.9415, 0.9330,
  0.9239, 0.9142, 0.9040, 0.8932, 0.8819, 0.8701, 0.8577, 0.8449,
  0.8315, 0.8176, 0.8032, 0.7883, 0.7730, 0.7572, 0.7410, 0.7242,
  0.7071, 0.6895, 0.6716, 0.6532, 0.6344, 0.6152, 0.5957, 0.5758,
  0.5556, 0.5350, 0.5141, 0.4929, 0.4714, 0.4496, 0.4276, 0.4052,
  0.3827, 0.3599, 0.3369, 0.3137, 0.2903, 0.2667, 0.2430, 0.2191,
  0.1951, 0.1710, 0.1467, 0.1224, 0.0980, 0.0736, 0.0491, 0.0245,
  0.0000, -0.0245, -0.0491, -0.0736, -0.0980, -0.1224, -0.1467, -0.1710,
  -0.1951, -0.2191, -0.2430, -0.2667, -0.2903, -0.3137, -0.3369, -0.3599,
  -0.3827, -0.4052, -0.4276, -0.4496, -0.4714, -0.4929, -0.5141, -0.5350,
  -0.5556, -0.5758, -0.5957, -0.6152, -0.6344, -0.6532, -0.6716, -0.6895,
  -0.7071, -0.7242, -0.7410, -0.7572, -0.7730, -0.7883, -0.8032, -0.8176,
  -0.8315, -0.8449, -0.8577, -0.8701, -0.8819, -0.8932, -0.9040, -0.9142,
  -0.9239, -0.9330, -0.9415, -0.9495, -0.9569, -0.9638, -0.9700, -0.9757,
  -0.9808, -0.9853, -0.9892, -0.9925, -0.9952, -0.9973, -0.9988, -0.9997,
  -1.0000, -0.9997, -0.9988, -0.9973, -0.9952, -0.9925, -0.9892, -0.9853,
  -0.9808, -0.9757, -0.9700, -0.9638, -0.9569, -0.9495, -0.9415, -0.9330,
  -0.9239, -0.9142, -0.9040, -0.8932, -0.8819, -0.8701, -0.8577, -0.8449,
  -0.8315, -0.8176, -0.8032, -0.7883, -0.7730, -0.7572, -0.7410, -0.7242,
  -0.7071, -0.6895, -0.6716, -0.6532, -0.6344, -0.6152, -0.5957, -0.5758,
  -0.5556, -0.5350, -0.5141, -0.4929, -0.4714, -0.4496, -0.4276, -0.4052,
  -0.3827, -0.3599, -0.3369, -0.3137, -0.2903, -0.2667, -0.2430, -0.2191,
  -0.1951, -0.1710, -0.1467, -0.1224, -0.0980, -0.0736, -0.0491, -0.0245
};
*/

const accum sineTable[512] = {
  0.0000,0.0123,0.0245,0.0368,0.0491,0.0613,0.0736,0.0858,
  0.0980,0.1102,0.1224,0.1346,0.1467,0.1589,0.1710,0.1830,
  0.1951,0.2071,0.2191,0.2311,0.2430,0.2549,0.2667,0.2785,
  0.2903,0.3020,0.3137,0.3253,0.3369,0.3484,0.3599,0.3713,
  0.3827,0.3940,0.4052,0.4164,0.4276,0.4386,0.4496,0.4605,
  0.4714,0.4822,0.4929,0.5035,0.5141,0.5246,0.5350,0.5453,
  0.5556,0.5657,0.5758,0.5858,0.5957,0.6055,0.6152,0.6249,
  0.6344,0.6438,0.6532,0.6624,0.6716,0.6806,0.6895,0.6984,
  0.7071,0.7157,0.7242,0.7327,0.7410,0.7491,0.7572,0.7652,
  0.7730,0.7807,0.7883,0.7958,0.8032,0.8105,0.8176,0.8246,
  0.8315,0.8382,0.8449,0.8514,0.8577,0.8640,0.8701,0.8761,
  0.8819,0.8876,0.8932,0.8987,0.9040,0.9092,0.9142,0.9191,
  0.9239,0.9285,0.9330,0.9373,0.9415,0.9456,0.9495,0.9533,
  0.9569,0.9604,0.9638,0.9670,0.9700,0.9729,0.9757,0.9783,
  0.9808,0.9831,0.9853,0.9873,0.9892,0.9909,0.9925,0.9939,
  0.9952,0.9963,0.9973,0.9981,0.9988,0.9993,0.9997,0.9999,
  1.0000,0.9999,0.9997,0.9993,0.9988,0.9981,0.9973,0.9963,
  0.9952,0.9939,0.9925,0.9909,0.9892,0.9873,0.9853,0.9831,
  0.9808,0.9783,0.9757,0.9729,0.9700,0.9670,0.9638,0.9604,
  0.9569,0.9533,0.9495,0.9456,0.9415,0.9373,0.9330,0.9285,
  0.9239,0.9191,0.9142,0.9092,0.9040,0.8987,0.8932,0.8876,
  0.8819,0.8761,0.8701,0.8640,0.8577,0.8514,0.8449,0.8382,
  0.8315,0.8246,0.8176,0.8105,0.8032,0.7958,0.7883,0.7807,
  0.7730,0.7652,0.7572,0.7491,0.7410,0.7327,0.7242,0.7157,
  0.7071,0.6984,0.6895,0.6806,0.6716,0.6624,0.6532,0.6438,
  0.6344,0.6249,0.6152,0.6055,0.5957,0.5858,0.5758,0.5657,
  0.5556,0.5453,0.5350,0.5246,0.5141,0.5035,0.4929,0.4822,
  0.4714,0.4605,0.4496,0.4386,0.4276,0.4164,0.4052,0.3940,
  0.3827,0.3713,0.3599,0.3484,0.3369,0.3253,0.3137,0.3020,
  0.2903,0.2785,0.2667,0.2549,0.2430,0.2311,0.2191,0.2071,
  0.1951,0.1830,0.1710,0.1589,0.1467,0.1346,0.1224,0.1102,
  0.0980,0.0858,0.0736,0.0613,0.0491,0.0368,0.0245,0.0123,
  0.0000,-0.0123,-0.0245,-0.0368,-0.0491,-0.0613,-0.0736,-0.0858,
  -0.0980,-0.1102,-0.1224,-0.1346,-0.1467,-0.1589,-0.1710,-0.1830,
  -0.1951,-0.2071,-0.2191,-0.2311,-0.2430,-0.2549,-0.2667,-0.2785,
  -0.2903,-0.3020,-0.3137,-0.3253,-0.3369,-0.3484,-0.3599,-0.3713,
  -0.3827,-0.3940,-0.4052,-0.4164,-0.4276,-0.4386,-0.4496,-0.4605,
  -0.4714,-0.4822,-0.4929,-0.5035,-0.5141,-0.5246,-0.5350,-0.5453,
  -0.5556,-0.5657,-0.5758,-0.5858,-0.5957,-0.6055,-0.6152,-0.6249,
  -0.6344,-0.6438,-0.6532,-0.6624,-0.6716,-0.6806,-0.6895,-0.6984,
  -0.7071,-0.7157,-0.7242,-0.7327,-0.7410,-0.7491,-0.7572,-0.7652,
  -0.7730,-0.7807,-0.7883,-0.7958,-0.8032,-0.8105,-0.8176,-0.8246,
  -0.8315,-0.8382,-0.8449,-0.8514,-0.8577,-0.8640,-0.8701,-0.8761,
  -0.8819,-0.8876,-0.8932,-0.8987,-0.9040,-0.9092,-0.9142,-0.9191,
  -0.9239,-0.9285,-0.9330,-0.9373,-0.9415,-0.9456,-0.9495,-0.9533,
  -0.9569,-0.9604,-0.9638,-0.9670,-0.9700,-0.9729,-0.9757,-0.9783,
  -0.9808,-0.9831,-0.9853,-0.9873,-0.9892,-0.9909,-0.9925,-0.9939,
  -0.9952,-0.9963,-0.9973,-0.9981,-0.9988,-0.9993,-0.9997,-0.9999,
  -1.0000,-0.9999,-0.9997,-0.9993,-0.9988,-0.9981,-0.9973,-0.9963,
  -0.9952,-0.9939,-0.9925,-0.9909,-0.9892,-0.9873,-0.9853,-0.9831,
  -0.9808,-0.9783,-0.9757,-0.9729,-0.9700,-0.9670,-0.9638,-0.9604,
  -0.9569,-0.9533,-0.9495,-0.9456,-0.9415,-0.9373,-0.9330,-0.9285,
  -0.9239,-0.9191,-0.9142,-0.9092,-0.9040,-0.8987,-0.8932,-0.8876,
  -0.8819,-0.8761,-0.8701,-0.8640,-0.8577,-0.8514,-0.8449,-0.8382,
  -0.8315,-0.8246,-0.8176,-0.8105,-0.8032,-0.7958,-0.7883,-0.7807,
  -0.7730,-0.7652,-0.7572,-0.7491,-0.7410,-0.7327,-0.7242,-0.7157,
  -0.7071,-0.6984,-0.6895,-0.6806,-0.6716,-0.6624,-0.6532,-0.6438,
  -0.6344,-0.6249,-0.6152,-0.6055,-0.5957,-0.5858,-0.5758,-0.5657,
  -0.5556,-0.5453,-0.5350,-0.5246,-0.5141,-0.5035,-0.4929,-0.4822,
  -0.4714,-0.4605,-0.4496,-0.4386,-0.4276,-0.4164,-0.4052,-0.3940,
  -0.3827,-0.3713,-0.3599,-0.3484,-0.3369,-0.3253,-0.3137,-0.3020,
  -0.2903,-0.2785,-0.2667,-0.2549,-0.2430,-0.2311,-0.2191,-0.2071,
  -0.1951,-0.1830,-0.1710,-0.1589,-0.1467,-0.1346,-0.1224,-0.1102,
  -0.0980,-0.0858,-0.0736,-0.0613,-0.0491,-0.0368,-0.0245,-0.0123,
};


/*
const accum sineTable[1024] = {
  0.0000,0.0061,0.0123,0.0184,0.0245,0.0307,0.0368,0.0429,
  0.0491,0.0552,0.0613,0.0674,0.0736,0.0797,0.0858,0.0919,
  0.0980,0.1041,0.1102,0.1163,0.1224,0.1285,0.1346,0.1407,
  0.1467,0.1528,0.1589,0.1649,0.1710,0.1770,0.1830,0.1891,
  0.1951,0.2011,0.2071,0.2131,0.2191,0.2251,0.2311,0.2370,
  0.2430,0.2489,0.2549,0.2608,0.2667,0.2726,0.2785,0.2844,
  0.2903,0.2962,0.3020,0.3078,0.3137,0.3195,0.3253,0.3311,
  0.3369,0.3427,0.3484,0.3542,0.3599,0.3656,0.3713,0.3770,
  0.3827,0.3883,0.3940,0.3996,0.4052,0.4108,0.4164,0.4220,
  0.4276,0.4331,0.4386,0.4441,0.4496,0.4551,0.4605,0.4660,
  0.4714,0.4768,0.4822,0.4876,0.4929,0.4982,0.5035,0.5088,
  0.5141,0.5194,0.5246,0.5298,0.5350,0.5402,0.5453,0.5505,
  0.5556,0.5607,0.5657,0.5708,0.5758,0.5808,0.5858,0.5908,
  0.5957,0.6006,0.6055,0.6104,0.6152,0.6201,0.6249,0.6296,
  0.6344,0.6391,0.6438,0.6485,0.6532,0.6578,0.6624,0.6670,
  0.6716,0.6761,0.6806,0.6851,0.6895,0.6940,0.6984,0.7028,
  0.7071,0.7114,0.7157,0.7200,0.7242,0.7285,0.7327,0.7368,
  0.7410,0.7451,0.7491,0.7532,0.7572,0.7612,0.7652,0.7691,
  0.7730,0.7769,0.7807,0.7846,0.7883,0.7921,0.7958,0.7995,
  0.8032,0.8068,0.8105,0.8140,0.8176,0.8211,0.8246,0.8280,
  0.8315,0.8349,0.8382,0.8416,0.8449,0.8481,0.8514,0.8546,
  0.8577,0.8609,0.8640,0.8670,0.8701,0.8731,0.8761,0.8790,
  0.8819,0.8848,0.8876,0.8904,0.8932,0.8960,0.8987,0.9013,
  0.9040,0.9066,0.9092,0.9117,0.9142,0.9167,0.9191,0.9215,
  0.9239,0.9262,0.9285,0.9308,0.9330,0.9352,0.9373,0.9395,
  0.9415,0.9436,0.9456,0.9476,0.9495,0.9514,0.9533,0.9551,
  0.9569,0.9587,0.9604,0.9621,0.9638,0.9654,0.9670,0.9685,
  0.9700,0.9715,0.9729,0.9743,0.9757,0.9770,0.9783,0.9796,
  0.9808,0.9820,0.9831,0.9842,0.9853,0.9863,0.9873,0.9883,
  0.9892,0.9901,0.9909,0.9917,0.9925,0.9932,0.9939,0.9946,
  0.9952,0.9958,0.9963,0.9968,0.9973,0.9977,0.9981,0.9985,
  0.9988,0.9991,0.9993,0.9995,0.9997,0.9998,0.9999,1.0000,
  1.0000,1.0000,0.9999,0.9998,0.9997,0.9995,0.9993,0.9991,
  0.9988,0.9985,0.9981,0.9977,0.9973,0.9968,0.9963,0.9958,
  0.9952,0.9946,0.9939,0.9932,0.9925,0.9917,0.9909,0.9901,
  0.9892,0.9883,0.9873,0.9863,0.9853,0.9842,0.9831,0.9820,
  0.9808,0.9796,0.9783,0.9770,0.9757,0.9743,0.9729,0.9715,
  0.9700,0.9685,0.9670,0.9654,0.9638,0.9621,0.9604,0.9587,
  0.9569,0.9551,0.9533,0.9514,0.9495,0.9476,0.9456,0.9436,
  0.9415,0.9395,0.9373,0.9352,0.9330,0.9308,0.9285,0.9262,
  0.9239,0.9215,0.9191,0.9167,0.9142,0.9117,0.9092,0.9066,
  0.9040,0.9013,0.8987,0.8960,0.8932,0.8904,0.8876,0.8848,
  0.8819,0.8790,0.8761,0.8731,0.8701,0.8670,0.8640,0.8609,
  0.8577,0.8546,0.8514,0.8481,0.8449,0.8416,0.8382,0.8349,
  0.8315,0.8280,0.8246,0.8211,0.8176,0.8140,0.8105,0.8068,
  0.8032,0.7995,0.7958,0.7921,0.7883,0.7846,0.7807,0.7769,
  0.7730,0.7691,0.7652,0.7612,0.7572,0.7532,0.7491,0.7451,
  0.7410,0.7368,0.7327,0.7285,0.7242,0.7200,0.7157,0.7114,
  0.7071,0.7028,0.6984,0.6940,0.6895,0.6851,0.6806,0.6761,
  0.6716,0.6670,0.6624,0.6578,0.6532,0.6485,0.6438,0.6391,
  0.6344,0.6296,0.6249,0.6201,0.6152,0.6104,0.6055,0.6006,
  0.5957,0.5908,0.5858,0.5808,0.5758,0.5708,0.5657,0.5607,
  0.5556,0.5505,0.5453,0.5402,0.5350,0.5298,0.5246,0.5194,
  0.5141,0.5088,0.5035,0.4982,0.4929,0.4876,0.4822,0.4768,
  0.4714,0.4660,0.4605,0.4551,0.4496,0.4441,0.4386,0.4331,
  0.4276,0.4220,0.4164,0.4108,0.4052,0.3996,0.3940,0.3883,
  0.3827,0.3770,0.3713,0.3656,0.3599,0.3542,0.3484,0.3427,
  0.3369,0.3311,0.3253,0.3195,0.3137,0.3078,0.3020,0.2962,
  0.2903,0.2844,0.2785,0.2726,0.2667,0.2608,0.2549,0.2489,
  0.2430,0.2370,0.2311,0.2251,0.2191,0.2131,0.2071,0.2011,
  0.1951,0.1891,0.1830,0.1770,0.1710,0.1649,0.1589,0.1528,
  0.1467,0.1407,0.1346,0.1285,0.1224,0.1163,0.1102,0.1041,
  0.0980,0.0919,0.0858,0.0797,0.0736,0.0674,0.0613,0.0552,
  0.0491,0.0429,0.0368,0.0307,0.0245,0.0184,0.0123,0.0061,
  0.0000,-0.0061,-0.0123,-0.0184,-0.0245,-0.0307,-0.0368,-0.0429,
  -0.0491,-0.0552,-0.0613,-0.0674,-0.0736,-0.0797,-0.0858,-0.0919,
  -0.0980,-0.1041,-0.1102,-0.1163,-0.1224,-0.1285,-0.1346,-0.1407,
  -0.1467,-0.1528,-0.1589,-0.1649,-0.1710,-0.1770,-0.1830,-0.1891,
  -0.1951,-0.2011,-0.2071,-0.2131,-0.2191,-0.2251,-0.2311,-0.2370,
  -0.2430,-0.2489,-0.2549,-0.2608,-0.2667,-0.2726,-0.2785,-0.2844,
  -0.2903,-0.2962,-0.3020,-0.3078,-0.3137,-0.3195,-0.3253,-0.3311,
  -0.3369,-0.3427,-0.3484,-0.3542,-0.3599,-0.3656,-0.3713,-0.3770,
  -0.3827,-0.3883,-0.3940,-0.3996,-0.4052,-0.4108,-0.4164,-0.4220,
  -0.4276,-0.4331,-0.4386,-0.4441,-0.4496,-0.4551,-0.4605,-0.4660,
  -0.4714,-0.4768,-0.4822,-0.4876,-0.4929,-0.4982,-0.5035,-0.5088,
  -0.5141,-0.5194,-0.5246,-0.5298,-0.5350,-0.5402,-0.5453,-0.5505,
  -0.5556,-0.5607,-0.5657,-0.5708,-0.5758,-0.5808,-0.5858,-0.5908,
  -0.5957,-0.6006,-0.6055,-0.6104,-0.6152,-0.6201,-0.6249,-0.6296,
  -0.6344,-0.6391,-0.6438,-0.6485,-0.6532,-0.6578,-0.6624,-0.6670,
  -0.6716,-0.6761,-0.6806,-0.6851,-0.6895,-0.6940,-0.6984,-0.7028,
  -0.7071,-0.7114,-0.7157,-0.7200,-0.7242,-0.7285,-0.7327,-0.7368,
  -0.7410,-0.7451,-0.7491,-0.7532,-0.7572,-0.7612,-0.7652,-0.7691,
  -0.7730,-0.7769,-0.7807,-0.7846,-0.7883,-0.7921,-0.7958,-0.7995,
  -0.8032,-0.8068,-0.8105,-0.8140,-0.8176,-0.8211,-0.8246,-0.8280,
  -0.8315,-0.8349,-0.8382,-0.8416,-0.8449,-0.8481,-0.8514,-0.8546,
  -0.8577,-0.8609,-0.8640,-0.8670,-0.8701,-0.8731,-0.8761,-0.8790,
  -0.8819,-0.8848,-0.8876,-0.8904,-0.8932,-0.8960,-0.8987,-0.9013,
  -0.9040,-0.9066,-0.9092,-0.9117,-0.9142,-0.9167,-0.9191,-0.9215,
  -0.9239,-0.9262,-0.9285,-0.9308,-0.9330,-0.9352,-0.9373,-0.9395,
  -0.9415,-0.9436,-0.9456,-0.9476,-0.9495,-0.9514,-0.9533,-0.9551,
  -0.9569,-0.9587,-0.9604,-0.9621,-0.9638,-0.9654,-0.9670,-0.9685,
  -0.9700,-0.9715,-0.9729,-0.9743,-0.9757,-0.9770,-0.9783,-0.9796,
  -0.9808,-0.9820,-0.9831,-0.9842,-0.9853,-0.9863,-0.9873,-0.9883,
  -0.9892,-0.9901,-0.9909,-0.9917,-0.9925,-0.9932,-0.9939,-0.9946,
  -0.9952,-0.9958,-0.9963,-0.9968,-0.9973,-0.9977,-0.9981,-0.9985,
  -0.9988,-0.9991,-0.9993,-0.9995,-0.9997,-0.9998,-0.9999,-1.0000,
  -1.0000,-1.0000,-0.9999,-0.9998,-0.9997,-0.9995,-0.9993,-0.9991,
  -0.9988,-0.9985,-0.9981,-0.9977,-0.9973,-0.9968,-0.9963,-0.9958,
  -0.9952,-0.9946,-0.9939,-0.9932,-0.9925,-0.9917,-0.9909,-0.9901,
  -0.9892,-0.9883,-0.9873,-0.9863,-0.9853,-0.9842,-0.9831,-0.9820,
  -0.9808,-0.9796,-0.9783,-0.9770,-0.9757,-0.9743,-0.9729,-0.9715,
  -0.9700,-0.9685,-0.9670,-0.9654,-0.9638,-0.9621,-0.9604,-0.9587,
  -0.9569,-0.9551,-0.9533,-0.9514,-0.9495,-0.9476,-0.9456,-0.9436,
  -0.9415,-0.9395,-0.9373,-0.9352,-0.9330,-0.9308,-0.9285,-0.9262,
  -0.9239,-0.9215,-0.9191,-0.9167,-0.9142,-0.9117,-0.9092,-0.9066,
  -0.9040,-0.9013,-0.8987,-0.8960,-0.8932,-0.8904,-0.8876,-0.8848,
  -0.8819,-0.8790,-0.8761,-0.8731,-0.8701,-0.8670,-0.8640,-0.8609,
  -0.8577,-0.8546,-0.8514,-0.8481,-0.8449,-0.8416,-0.8382,-0.8349,
  -0.8315,-0.8280,-0.8246,-0.8211,-0.8176,-0.8140,-0.8105,-0.8068,
  -0.8032,-0.7995,-0.7958,-0.7921,-0.7883,-0.7846,-0.7807,-0.7769,
  -0.7730,-0.7691,-0.7652,-0.7612,-0.7572,-0.7532,-0.7491,-0.7451,
  -0.7410,-0.7368,-0.7327,-0.7285,-0.7242,-0.7200,-0.7157,-0.7114,
  -0.7071,-0.7028,-0.6984,-0.6940,-0.6895,-0.6851,-0.6806,-0.6761,
  -0.6716,-0.6670,-0.6624,-0.6578,-0.6532,-0.6485,-0.6438,-0.6391,
  -0.6344,-0.6296,-0.6249,-0.6201,-0.6152,-0.6104,-0.6055,-0.6006,
  -0.5957,-0.5908,-0.5858,-0.5808,-0.5758,-0.5708,-0.5657,-0.5607,
  -0.5556,-0.5505,-0.5453,-0.5402,-0.5350,-0.5298,-0.5246,-0.5194,
  -0.5141,-0.5088,-0.5035,-0.4982,-0.4929,-0.4876,-0.4822,-0.4768,
  -0.4714,-0.4660,-0.4605,-0.4551,-0.4496,-0.4441,-0.4386,-0.4331,
  -0.4276,-0.4220,-0.4164,-0.4108,-0.4052,-0.3996,-0.3940,-0.3883,
  -0.3827,-0.3770,-0.3713,-0.3656,-0.3599,-0.3542,-0.3484,-0.3427,
  -0.3369,-0.3311,-0.3253,-0.3195,-0.3137,-0.3078,-0.3020,-0.2962,
  -0.2903,-0.2844,-0.2785,-0.2726,-0.2667,-0.2608,-0.2549,-0.2489,
  -0.2430,-0.2370,-0.2311,-0.2251,-0.2191,-0.2131,-0.2071,-0.2011,
  -0.1951,-0.1891,-0.1830,-0.1770,-0.1710,-0.1649,-0.1589,-0.1528,
  -0.1467,-0.1407,-0.1346,-0.1285,-0.1224,-0.1163,-0.1102,-0.1041,
  -0.0980,-0.0919,-0.0858,-0.0797,-0.0736,-0.0674,-0.0613,-0.0552,
  -0.0491,-0.0429,-0.0368,-0.0307,-0.0245,-0.0184,-0.0123,-0.0061,
};
*/

// sine lookup table, for details look at note
// RUMPS and n1169 extension specific
inline accum sin_lookup(int inAngle){
  unsigned int idx = 0;
  int angleSign = sign_f(inAngle);
  
  // Filter input angle
  if(angleSign<0) // sin(-x) = -sin(x)
    inAngle = (~inAngle)+1;

  // COMPILER_ERROR - assuming input won't exceed 2pi
  // let's discard this operation for now
  while(inAngle>205887) // sin(x) = sin(x+2pi)
    inAngle-=205887;
  
  // Index mapping - inAngle to LUT index
  // simply want integer part of inAngle * mappingConst
  // implementation is a bit complicated due to 64bit mul
  ulaccum_int_t mappingInput,
                mappingConst;
  mappingInput.ulaccum_cont = inAngle;          // A
  mappingConst.ulaccum_cont = sineMappingConst; // B

  //  split a and b into 32 bits halves
  uint32_t a_lo = (uint32_t) mappingInput.uint_cont;
  uint32_t a_hi = mappingInput.uint_cont >> 32;
  uint32_t b_lo = (uint32_t) mappingConst.uint_cont;
  uint32_t b_hi = mappingConst.uint_cont >> 32;

  // * splitting calculations
  uint64_t p0 = mac_umul_32(a_lo, b_lo);
  uint64_t p1 = mac_umul_32(a_lo, b_hi);
  uint64_t p2 = mac_umul_32(a_hi, b_lo);
  uint64_t p3 = mac_umul_32(a_hi, b_hi);

  // * carry from lower half MUL
  uint32_t cy = (uint32_t)(((p0 >> 32) + (uint32_t)p1 + (uint32_t)p2) >> 32);

  // final MUL result - we only take hi (S31.32)
  // uint32_t lo = p0 + (p1 << 32) + (p2 << 32);
  uint32_t hi = p3 + (p1 >> 32) + (p2 >> 32) + cy;
  // if(lo>0x80000000) // >0.5
  //   hi++;
  idx = hi & 511;

  // Return lookup value
  // COMPILER_ERROR - if both modification and return happen
  // accum_int_t returnVal;
  // returnVal.accum_cont = sineTable[idx];  
  // if(angleSign<0) // sin(-x) = -sin(x)
  //   returnVal.int_cont = (~returnVal.int_cont) + 1;
  // return returnVal.accum_cont; 
  
  return angleSign * sineTable[idx];
}

// cosine lookup table, for details look at note
// RUMPS and n1169 extension specific
inline accum cos_lookup(int inAngle){
  // Filter input angle
  if(sign_f(inAngle)<0) // cos(-x) = cos(x)
    inAngle = (~inAngle)+1;

  // Return lookup value
  // cos(x) = sin(x+pi/2)
  inAngle += 51472; // 51472 is pi/2 in accum format
  return sin_lookup(inAngle);
}

// Shared with isr3.c
volatile uint8_t payloadBufferDone = 0;
volatile uint8_t freqsyncBufferDone = 0;
volatile int freqsyncBuff[3] = {0}; // buffer for freq sync's 2nd preamble
volatile int qCodewordBuff[768]; // buffer whole frame's Q value

//------------------------------------------------------------------------------

int main(void)

{
  set_trellis();
  

  // *** Part1 - Setup ***
  // MUX - select TM_COM0 & TM_COM1
  MUXC_SELECT = 0x4;

  pinMode_output(ledpin);
  digitalWrite_high(ledpin);
  
  // Define variables
  int Idata, Qdata;
  volatile uint8_t rx_state = STATE_EMPTY;
  
  // Buffer for TED-sign version
  int Isamp[3]; // 0-prev, 1-mid, 2-curr
  int Qsamp[3];
  
  // Counters and flags
  unsigned int nSamples = 0;
  accum ted_total;
  int tempcalc = 0;
  int ted = 0;
  int stepCorrection = 0;

  // General calculation buffer
  accum_int_t temp_accumint;
  int32_t mulOpA_s32, mulOpB_s32;
  int64_t mulResult_s64;
  int tempdump[20];
  
  // Buffer for Freq sync
  accum_int_t iBuff, qBuff;
  accum_int_t sigNorm;
  accum_int_t sinBuff, cosBuff;
  accum_int_t iCorrected, qCorrected;
  accum_int_t phaseOffset;
  accum_int_t planeCorr; // plane and random starting phase offset
  accum_int_t relativeCorr; // relative phase offset
  accum_int_t tempCorr;

  // Interrupts
  NVIC_SetPriority(2, 0);   

  // *** END - part1 ***

  // *** Part2 - Lime's initialization ***
  while(1){
    // wait for Lime's initialization to be finished
    while(noc_NC_rxbuff0_av!=1)__NOP();
    int tempack = NC_NOC_RX_BUFF0;
    
    while(noc_NC_txbuff_isfull==1)__NOP();
    NC_NOC_TX_BUFF0 = tempack;

    break;
  }
  
  // *** Part3 - Lime RX ***
  while(1){

    //determine sync state - header from IO Core
    if(rx_state==STATE_EMPTY){
      while(noc_NC_rxbuff0_av!=1)__NOP();  
      rx_state = NC_NOC_RX_BUFF0;
    }

    // DETECT - calc power level I^2 + Q2
    if(rx_state==RX_DETECT){
      //receive I and Q
      while(noc_NC_rxbuff0_av!=1)__NOP(); Idata = NC_NOC_RX_BUFF0;
      while(noc_NC_rxbuff0_av!=1)__NOP(); Qdata = NC_NOC_RX_BUFF0;

      unsigned int sigPower = 0;
      
      mulResult_s64 = mac_smul_32((int32_t)Idata, (int32_t)Idata);
      sigPower += (unsigned int)mulResult_s64;

      mulResult_s64 = mac_smul_32((int32_t)Qdata, (int32_t)Qdata);
      sigPower += (unsigned int)mulResult_s64;

      //send back power calc result
      while(noc_NC_txbuff_isfull==1)__NOP();
      NC_NOC_TX_BUFF0 = sigPower;

      // reset rx_state
      rx_state = STATE_EMPTY;
    }

    // TIMING_SYNC - TED algorithm
    if(rx_state==RX_TIMING_SYNC){
      
      //receive I and Q
      while(noc_NC_rxbuff0_av!=1)__NOP(); Idata = NC_NOC_RX_BUFF0;
      while(noc_NC_rxbuff0_av!=1)__NOP(); Qdata = NC_NOC_RX_BUFF0;

      Isamp[nSamples] = sign_f(Idata);
      Qsamp[nSamples] = sign_f(Qdata);
    
      if(++nSamples==3){
        nSamples = 0; // reset counter

        // TED calc - Sign version 
        ted = 0;

        // I-part
        tempcalc = Isamp[2]- Isamp[0];
        tempcalc = mac_smul_32((int32_t)(Isamp[1]),
                               (int32_t)(tempcalc));
        ted += tempcalc;

        // Q-part
        tempcalc = Qsamp[2]- Qsamp[0];
        tempcalc = mac_smul_32((int32_t)(Qsamp[1]),
                               (int32_t)(tempcalc));
        ted += tempcalc;

        // determine step correction
        stepCorrection = sign_f(ted);

        //send back synchronization result
        while(noc_NC_txbuff_isfull==1)__NOP();
        NC_NOC_TX_BUFF0 = stepCorrection;
      }

      // reset rx_state
      rx_state = STATE_EMPTY;
    }

    // FREQ_SYNC - calc phase offset 
    if(rx_state==RX_FREQ_SYNC){

      // Receiving data from IO core
      // For 1st preamble, it is done in main program
      // For 2nd preamble, it is done in interrupt subroutine
      // freq sync calculation is slow, thus interrupt is
      // necessary so DSP core won't miss 2nd preamble and
      // payloads to be buffered

      nSamples++;

      // 1st preamble sync - pre sync preparation
      if(nSamples==1){
        // receive I and Q
        while(noc_NC_rxbuff0_av!=1)__NOP(); Idata = NC_NOC_RX_BUFF0;
        while(noc_NC_rxbuff0_av!=1)__NOP(); Qdata = NC_NOC_RX_BUFF0;

        // Turn on interrupt -  for 2nd preamble and payloads
        NVIC_EnableIRQ(2); 
        NC_NOC_CSR1 = 0x4000000; // NOC_RIE_SET

        // determine which cartesius plane the received symbol is at
        // and move symbol to plane 1 (preamble '1')
        // so that cross product phase detector works properly
        if(Idata>=0){
          if(Qdata>=0) // plane 1, corr 0
            planeCorr.accum_cont = 0;
          else                  // plane 4, corr 270 degree
            planeCorr.accum_cont = 4.7124; //154415
        }
        else{
          if(Qdata>=0) // plane 2, corr 90 degree
            planeCorr.accum_cont = 1.5708; // 51471 
          else                  // plane 3, corr 180 degree
            planeCorr.accum_cont = 3.1416; // 102943 
        }

      }
      else if(nSamples==2){
        // receive I and Q
        while(freqsyncBufferDone!=1)__NOP();
        Idata = freqsyncBuff[1]; // index 0 is header
        Qdata = freqsyncBuff[2];
      }

      // scale back to 1 and -1 value (1 = +1024)
      iBuff.accum_cont = Idata; iBuff.int_cont >>= 10;
      qBuff.accum_cont = Qdata; qBuff.int_cont >>= 10;

      // normalization - N = sqrt(i^2 + q^2)
      if(nSamples==1){
        sigNorm.accum_cont = 0;
        mulResult_s64 = mac_smul_32((int32_t)(iBuff.int_cont),
                             (int32_t)(iBuff.int_cont));
        temp_accumint.int_cont = (mulResult_s64>>15);
        sigNorm.accum_cont += temp_accumint.accum_cont; //i^2
        
        mulResult_s64 = mac_smul_32((int32_t)(qBuff.int_cont),
                             (int32_t)(qBuff.int_cont));
        temp_accumint.int_cont = (mulResult_s64>>15);
        sigNorm.accum_cont += temp_accumint.accum_cont; //q^2
        
        unsigned int t = (sigNorm.int_cont << 1);
        t = S15_16_sqrt(t); // sqrt
        t >>= 1;
        sigNorm.int_cont = t;
      }

      iBuff.accum_cont /= sigNorm.accum_cont;
      qBuff.accum_cont /= sigNorm.accum_cont;

      // ~~DEBUG, dump normalized I & Q
      if(nSamples==1){
        tempdump[0] = iBuff.int_cont; // freqbit_1 I
        tempdump[1] = qBuff.int_cont; // freqbit_1 Q
        sinBuff.accum_cont = sin_lookup(planeCorr.int_cont);
        cosBuff.accum_cont = cos_lookup(planeCorr.int_cont);
      }
      else if(nSamples==2){
        tempdump[5] = iBuff.int_cont; // freqbit_2 I
        tempdump[6] = qBuff.int_cont; // freqbit_2 Q
        sinBuff.accum_cont = 0;
        cosBuff.accum_cont = 1;
      }

      // Set up 1st plane / random offset correction for the
      // corresponding freqsync bit 1 / 2
      // ^ Above
      // sinBuff.accum_cont = sin_lookup(planeCorr.int_cont);
      // cosBuff.accum_cont = cos_lookup(planeCorr.int_cont);
      
      ////
      // Improved flow, loop of phase correction and detection
      // (contains loop of detect and correct by fixed angle pi/32)
      // (to approach the accuracy of arcsin in detection)
      tempCorr.int_cont = 0;
      while(1){
        // Phase offset correction part -> v * exp(-j.theta)
        // ** I part
        iCorrected.accum_cont = 0;

        mulResult_s64 = mac_smul_32((int32_t)(iBuff.int_cont),
                             (int32_t)(cosBuff.int_cont));
        temp_accumint.int_cont = (mulResult_s64>>15);
        iCorrected.accum_cont += temp_accumint.accum_cont;

        mulResult_s64 = mac_smul_32((int32_t)(qBuff.int_cont),
                             (int32_t)(sinBuff.int_cont));
        temp_accumint.int_cont = (mulResult_s64>>15);
        iCorrected.accum_cont += temp_accumint.accum_cont;
        
        // ** Q part
        qCorrected.accum_cont = 0;

        mulResult_s64 = mac_smul_32((int32_t)(qBuff.int_cont),
                             (int32_t)(cosBuff.int_cont));
        temp_accumint.int_cont = (mulResult_s64>>15);
        qCorrected.accum_cont += temp_accumint.accum_cont;

        mulResult_s64 = mac_smul_32((int32_t)(iBuff.int_cont),
                             (int32_t)(sinBuff.int_cont));
        temp_accumint.int_cont = (mulResult_s64>>15);
        qCorrected.accum_cont -= temp_accumint.accum_cont;

        // Phase offset detection part
        temp_accumint.accum_cont = freqsync_preamble[nSamples-1];

        if(nSamples==2)
          temp_accumint.int_cont = tempdump[0]; // normalized freqbit1-I
        mulResult_s64 = mac_smul_32((int32_t)(temp_accumint.int_cont),
                             (int32_t)(qCorrected.int_cont));
        phaseOffset.int_cont = (mulResult_s64>>15);

        if(nSamples==2)
          temp_accumint.int_cont = tempdump[1]; // normalized freqbit1-Q
        mulResult_s64 = mac_smul_32((int32_t)(temp_accumint.int_cont),
                             (int32_t)(iCorrected.int_cont));
        temp_accumint.int_cont = (mulResult_s64>>15);
        phaseOffset.accum_cont -= temp_accumint.accum_cont;

        // Accuracy loop part
        if(abs_f(phaseOffset.int_cont)<=3217){
          tempCorr.accum_cont += phaseOffset.accum_cont;
          break;
        }

        // ** correction angle = pi/32
        // LUT[256], sin(pi/32) = [4], cos(pi/32) = [68]
        // LUT[512], sin(pi/32) = [8], cos(pi/32) = [136]
        if(sign_f(phaseOffset.int_cont)>0){
          sinBuff.accum_cont = sineTable[8];
          cosBuff.accum_cont = sineTable[136];
          tempCorr.int_cont += 3217; // + pi/32
        }
        else{
          sinBuff.accum_cont = -sineTable[8];
          cosBuff.accum_cont = sineTable[136];
          tempCorr.int_cont -= 3217; // + pi/32
        }

        // ** recursive for next loop
        iBuff.accum_cont = iCorrected.accum_cont;
        qBuff.accum_cont = qCorrected.accum_cont;

      }
      phaseOffset.accum_cont = tempCorr.accum_cont;

      // 1st preamble sync - random starting phase offset
      if(nSamples==1){
        // special case when received point lays below (.707,.707)
        // it is on plane 1 but before the ref point
        // then it has been rotated by 2pi - angle
        // if((planeCorr.accum_cont==0) && (phaseOffset.accum_cont<0))
        //   planeCorr.int_cont += 205887;

        // phase diff adds to plane correction value
        // as random starting offset correction
        // tempdump[0] = iBuff.int_cont; // freqbit_1 I
        // tempdump[1] = qBuff.int_cont; // freqbit_1 Q
        tempdump[2] = iCorrected.int_cont; // freqbit_1 Icorr
        tempdump[3] = qCorrected.int_cont; // freqbit_1 Qcorr
        tempdump[10] = sigNorm.int_cont; // normalization factor

        planeCorr.accum_cont += phaseOffset.accum_cont;

        tempdump[4] = planeCorr.int_cont; // total RandOffset
      }

      // 2nd preamble sync - symbols' relative phase offset
      else if(nSamples==2){
        // relative phase difference
        relativeCorr.accum_cont = phaseOffset.accum_cont;

        // tempdump[5] = iBuff.int_cont; // freqbit_2 I
        // tempdump[6] = qBuff.int_cont; // freqbit_2 Q
        tempdump[7] = iCorrected.int_cont; // freqbit_2 Icorr
        tempdump[8] = qCorrected.int_cont; // freqbit_2 Qcorr
        tempdump[9] = relativeCorr.int_cont; // relative offset
        tempdump[11] = sigNorm.int_cont; // normalization factor

        nSamples = 0;

        // since FREQ_SNYC is concurrently running with PAYLOAD
        // and PAYLOAD is (supposed) to finish later
        // wait until payload buffering (by isr3.c) finishes
        while(payloadBufferDone!=1)__NOP();
        freqsyncBufferDone = payloadBufferDone = 0; // reset flag
        digitalWrite_high(0);

        // reset rx_state
        rx_state = STATE_EMPTY;

        // prepare initial angle value for RX_PHASECORR
        // random angle correction - from 1st preamble, and
        // relative angle correction - from 2nd preamble
        // remember that 2nd preamble is already offset by
        // this angle, thus we need to recover 1st bit payload
        // to total offset affecting 2nd preamble, that is
        // random + relative
        phaseOffset.accum_cont = planeCorr.accum_cont +
                                 relativeCorr.accum_cont;
        // phaseOffset.int_cont += 20017; // 30 degree

        // signal IO core that DSP core is ready for phase correction
        while(noc_NC_txbuff_isfull==1)__NOP();
        NC_NOC_TX_BUFF0 = RX_PHASECORR;        
      }

    }

    // PHASECORR - correct payload's phase offset
    if(rx_state==RX_PHASECORR){

      // receive I
      while(noc_IC_rxbuff0_av!=1)__NOP();
      int temp = NC_NOC_RX_BUFF0;
      iBuff.accum_cont = temp;
      qBuff.accum_cont = qCodewordBuff[nSamples];

      // scaling
      iBuff.int_cont>>=10; 
      qBuff.int_cont>>=10;

      // relative angle correction - from 2nd preamble
      // each bit after 2nd preamble is affected by this
      // cumulatively
      // ** cumulative angle offset
      phaseOffset.accum_cont += relativeCorr.accum_cont;
      if(phaseOffset.int_cont>205887)
        phaseOffset.int_cont -= 205887;

      // ** cosine sine LUT
      sinBuff.accum_cont = sin_lookup(phaseOffset.int_cont);
      cosBuff.accum_cont = cos_lookup(phaseOffset.int_cont);

      // ** I part
      iCorrected.accum_cont = 0;

      mulResult_s64 = mac_smul_32((int32_t)(iBuff.int_cont),
                           (int32_t)(cosBuff.int_cont));
      temp_accumint.int_cont = (mulResult_s64>>15);
      iCorrected.accum_cont += temp_accumint.accum_cont;

      mulResult_s64 = mac_smul_32((int32_t)(qBuff.int_cont),
                           (int32_t)(sinBuff.int_cont));
      temp_accumint.int_cont = (mulResult_s64>>15);
      iCorrected.accum_cont += temp_accumint.accum_cont;

      // send back phase-corrected I
      while(noc_NC_txbuff_isfull==1)__NOP();
      NC_NOC_TX_BUFF0 = iCorrected.int_cont;
      // NC_NOC_TX_BUFF0 = 0x8000;

      if(++nSamples==768){
        nSamples = 0;

        for(int i=0; i<12; i++){
          while(noc_NC_txbuff_isfull==1)__NOP();
          NC_NOC_TX_BUFF0 = tempdump[i];
        }
      }

      // reset rx_state;
      rx_state = STATE_EMPTY;

    }

    // I dont know why I must have this 'break' case
    // so that COMPILER ERROR wont happen
    if(rx_state==RX_STOP)
      break;

  } 
  // *** END - part3 ***

  // *** Part4 - Turbo Decoding ***
  // *** END - part4 ***

  return 0;

}