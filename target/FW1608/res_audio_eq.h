#ifndef __RES_AUDIO_EQ_H__
#define __RES_AUDIO_EQ_H__
	
#ifdef __SW_IIR_EQ_PROCESS__
const IIR_CFG_T audio_eq_iir_cfg = {
    .gain0 = 0,
    .gain1 = 0,
    .num = 5,
    .param = {
        {0.0,   50.0,   0.7},
        {0.0,   250.0,  0.7},
        {0.0,   1000.0, 0.7},
        {0.0,   4000.0, 0.7},
        {0.0,   8000.0, 0.7}    
    }
};
#endif

#ifdef __HW_FIR_EQ_PROCESS__
const FIR_CFG_T audio_eq_fir_cfg = {
    .gain0 = 6,
    .gain1 = 6,
    .len = 128,
    .coef = {
        #include "res/eq/EQ_COEF.txt"
    }
};
#endif


#ifdef __cplusplus
	extern "C" {
#endif
		
#ifdef __cplusplus
	}
#endif
#endif
	
