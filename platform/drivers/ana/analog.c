#include "cmsis.h"
#include "tgt_hardware.h"
#include "hal_trace.h"
#include "hal_timer.h"
#include "hal_codecip.h"
#include "hal_codec.h"
#include "hal_cmu.h"
#include "hal_sysfreq.h"
#include "hal_chipid.h"
#include "analog.h"
#include "pmu.h"

#define CFG_HW_EXT_AUDIOPA_ENABLE HAL_IOMUX_PIN_P2_5


#if !defined(PROGRAMMER)
#include "cmsis_os.h"
#endif

#ifndef ANALOG_GA1B_GAIN
#define ANALOG_GA1B_GAIN ANALOG_GA1A_GAIN
#endif

#ifndef ANALOG_GA2B_GAIN
#define ANALOG_GA2B_GAIN ANALOG_GA2A_GAIN
#endif

#ifndef ANALOG_LINEIN_GA1_GAIN
#define ANALOG_LINEIN_GA1_GAIN (1)
#endif

#ifndef ANALOG_LINEIN_GA2_GAIN
#define ANALOG_LINEIN_GA2_GAIN (1)
#endif

#if !defined(PROGRAMMER)
#define analog_delay(ms) osDelay(ms)
#else
#define analog_delay(ms) hal_sys_timer_delay(MS_TO_TICKS(ms))
#endif

#define ANALOG_TRACE(s,...)
//TRACE(s, ##__VA_ARGS__)

#ifdef _BEST1000_QUAL_DCDC_
//#define __ADC_ACITVE_USE_VCODEC__
#define __DAC_ACITVE_USE_VCODEC__
#define __DAC_ACITVE_USE_LVOLTAGE__
#endif

#define __AUDIO_OUTPUT_VOLT_STAY_HIGH__




#define ANA_EFFUSE_DCCALIB_R_SHIFT (0)
#define ANA_EFFUSE_DCCALIB_R_MASK  (0xf)
#define ANA_EFFUSE_DCCALIB_L_SHIFT (4)
#define ANA_EFFUSE_DCCALIB_L_MASK  (0xf)
#define ANA_EFFUSE_DCCALIB_R_CHECK_SHIFT (8)
#define ANA_EFFUSE_DCCALIB_R_CHECK_MASK  (0xf)
#define ANA_EFFUSE_DCCALIB_L_CHECK_SHIFT (12)
#define ANA_EFFUSE_DCCALIB_L_CHECK_MASK  (0xf)

#define ANA_EFFUSE_GET_FIELD(efuse, mask, shift) ((efuse>>shift)&mask)

#define ANA_DCCALIB_DEFAULT_R   1
#define ANA_DCCALIB_DEFAULT_L   1

#define CODEC_BBPLL1_CLKGEN_DIV_SHIFT   10
#define CODEC_BBPLL1_CLKGEN_DIV_MASK    (0xF << CODEC_BBPLL1_CLKGEN_DIV_SHIFT)
#define CODEC_BBPLL1_CLKGEN_DIV(n)      BITFIELD_VAL(CODEC_BBPLL1_CLKGEN_DIV, n)

#define ANALOG_CODEC_TX_PA_GAIN_SHIFT   6
#define ANALOG_CODEC_TX_PA_GAIN_MASK    (0x1F << ANALOG_CODEC_TX_PA_GAIN_SHIFT)
#define ANALOG_CODEC_TX_PA_GAIN(n)      BITFIELD_VAL(ANALOG_CODEC_TX_PA_GAIN, n)

typedef void (*ANALOG_AUD_IO_CFG_HANDLER_T)(uint8_t);

struct ANALOG_AUD_IO_CFG_T{
    enum AUD_IO_PATH_T io_path;
    uint8_t dev;
    ANALOG_AUD_IO_CFG_HANDLER_T handler;

};

static void analog_aud_mic_cfg(uint8_t dev);
static void analog_aud_linein_cfg(uint8_t dev);

#define ANALOG_AUD_IO_CFG_NUM   5
const struct ANALOG_AUD_IO_CFG_T analog_aud_io_cfg[ANALOG_AUD_IO_CFG_NUM]={
    //input config
    {AUD_INPUT_PATH_MAINMIC, CFG_HW_AUD_INPUT_PATH_MAINMIC_DEV, analog_aud_mic_cfg},
    {AUD_INPUT_PATH_HP_MIC , CFG_HW_AUD_INPUT_PATH_HP_MIC_DEV , analog_aud_linein_cfg},

    //output config
    {AUD_OUTPUT_PATH_SPEAKER, CFG_HW_AUD_OUTPUT_PATH_SPEAKER_DEV, NULL},
};

const uint16_t AUDIO_PLL_CFG_44100[][2] ={
    {0x7D0D, 0xD4F6},  /*44100.00*/
    {0x7C6D, 0xC341},  /*43879.50*/
    {0x7BCD, 0xB18D},  /*43659.00*/
    {0x7B2D, 0x9FD9},  /*43438.50*/
    {0x7A8D, 0x8E24},  /*43218.00*/
    {0x79ED, 0x7C70},  /*42997.50*/
    {0x794D, 0x6ABB},  /*42777.00*/
    {0x78B0, 0x8C95},  /*42560.91*/
    {0x780D, 0x4752},  /*42336.00*/
};

const uint16_t AUDIO_PLL_CFG_48000[][2] ={
    {0x881C, 0xFCB1},  /*48000.00*/
    {0x876E, 0xC31C},  /*47760.00*/
    {0x86C0, 0x8986},  /*47520.00*/
    {0x8612, 0x4FF1},  /*47280.00*/
    {0x8564, 0x165C},  /*47040.00*/
    {0x84B5, 0xDCC6},  /*46800.00*/
    {0x8407, 0xA331},  /*46560.00*/
    {0x835C, 0xE5A4},  /*46324.80*/
    {0x82AB, 0x3006},  /*46080.00*/
};

static bool dac_dc_calib_val_valid = false;

void analog_aud_pll_config(enum AUD_SAMPRATE_T samprate, int shift)
{
    unsigned short readVal;
    int shift_Limit;
    switch (samprate) {
        case AUD_SAMPRATE_8000:
            analog_write(0x36,AUDIO_PLL_CFG_48000[0][0]);
            analog_write(0x35,AUDIO_PLL_CFG_48000[0][1]);
            break;
        case AUD_SAMPRATE_44100:
            shift_Limit = sizeof(AUDIO_PLL_CFG_44100)/sizeof(AUDIO_PLL_CFG_44100[0])-1;
            if (shift > shift_Limit)
                shift = shift_Limit;
            analog_write(0x36,AUDIO_PLL_CFG_44100[shift][0]);
            analog_write(0x35,AUDIO_PLL_CFG_44100[shift][1]);
            break;
        case AUD_SAMPRATE_48000:
            shift_Limit = sizeof(AUDIO_PLL_CFG_48000)/sizeof(AUDIO_PLL_CFG_48000[0])-1;
            if (shift > shift_Limit)
                shift = shift_Limit;
            analog_write(0x36,AUDIO_PLL_CFG_48000[shift][0]);
            analog_write(0x35,AUDIO_PLL_CFG_48000[shift][1]);
            break;
        default:
            break;
    }

    analog_read(0x3A,&readVal);
    readVal = (readVal& (~(ANALOG_REGA1_CFG_BBPLL1_CLKGEN_DIVN_MASK<<ANALOG_REGA1_CFG_BBPLL1_CLKGEN_DIVN_SHIFT)))|(((9)&ANALOG_REGA1_CFG_BBPLL1_CLKGEN_DIVN_MASK)<<ANALOG_REGA1_CFG_BBPLL1_CLKGEN_DIVN_SHIFT);
    analog_write(0x3A,readVal);

    analog_read(ANALOG_REG31_OFFSET, &readVal);
    analog_write(ANALOG_REG31_OFFSET,readVal&(~ANALOG_REG31_AUDPLL_FREQ_EN));
    analog_write(ANALOG_REG31_OFFSET,readVal|ANALOG_REG31_AUDPLL_FREQ_EN);
}

void analog_aud_freq_pll_config(uint32_t freq, uint32_t div)
{
    static uint32_t codec_freq = 0;
    static uint32_t codec_div = 0;

    // CODEC_FREQ is likely 24.576M (48K series) or 22.5792M (44.1K series)
    // PLL_nominal = CODEC_FREQ * CODEC_DIV
    // PLL_cfg_val = ((CODEC_FREQ * CODEC_DIV) / 26M) * (1 << 28)

    uint64_t PLL_cfg_val;
    uint16_t high, low;
    unsigned short readVal;
    uint32_t lock;

    if (codec_freq == freq && codec_div == div) {
        return;
    }
    
    lock = int_lock();

    if (codec_freq != freq) {
        codec_freq = freq;

        PLL_cfg_val = ((uint64_t)(1 << 28) * freq * div + 26000000 / 2) / 26000000;
        high = (PLL_cfg_val >> 16) & 0xFFFF;
        low = PLL_cfg_val & 0xFFFF;
        analog_write(0x36, high);
        analog_write(0x35,low);
    }

    if (codec_div != div) {
        codec_div = div;

        analog_read(0x3A, &readVal);
        readVal = SET_BITFIELD(readVal, CODEC_BBPLL1_CLKGEN_DIV, div);
        analog_write(0x3A, readVal);
    }
    analog_read(ANALOG_REG31_OFFSET, &readVal);
    analog_write(ANALOG_REG31_OFFSET,readVal&(~ANALOG_REG31_AUDPLL_FREQ_EN));
    analog_write(ANALOG_REG31_OFFSET,readVal|ANALOG_REG31_AUDPLL_FREQ_EN);
    
    int_unlock(lock);
}

void analog_aud_pll_tune(int sample, uint32_t ms, int adc)
{
    // CODEC_FREQ is likely 24.576M (48K series) or 22.5792M (44.1K series)
    // PLL_nominal = CODEC_FREQ * CODEC_DIV
    // PLL_cfg_val = ((CODEC_FREQ * CODEC_DIV) / 26M) * (1 << 28)
    // Delta = ((SampleDiff / Fs) / TimeDiff) * PLL_cfg_val

    //---------------------------------------------------------
    // DAC
    //---------------------------------------------------------
    // OSR = CODEC_FREQ * CODEC_DIV / CMU_DAC_DIV / (Fs * DAC_UP) = (128, 256, or 512)
    // Delta = (1 << 28) * SampleDiff * CMU_DAC_DIV * DAC_UP * OSR / (MS_TimeDiff / 1000) / 26M

    //---------------------------------------------------------
    // ADC
    //---------------------------------------------------------
    // 512 = CODEC_FREQ * CODEC_DIV / CMU_ADC_DIV / (Fs * ADC_DOWN)
    // Delta = (1 << 28) * SampleDiff * CMU_ADC_DIV * ADC_DOWN * 512 / (MS_TimeDiff / 1000) / 26M

    uint32_t div;
    int64_t delta, new_pll;
    uint16_t high, low;
    uint16_t new_high, new_low;
    uint16_t update;
#ifdef AUD_PLL_DOUBLE
    uint16_t val, bit34_32, new_bit34_32;
#endif

    if (adc) {
        uint32_t adc_down;

        adc_down = hal_codec_get_adc_down(HAL_CODEC_ID_0);
        div = hal_cmu_codec_adc_get_div();

        delta = (int64_t)(1 << 28) * sample * div * adc_down * 512 * 1000 / ms / 26000000;
    } else {
        uint32_t osr, dac_up;

        osr = hal_codec_get_dac_osr(HAL_CODEC_ID_0);
        // DAC_UP is 1 for Fs >= 44.1K
        dac_up = hal_codec_get_dac_up(HAL_CODEC_ID_0);
        div = hal_cmu_codec_dac_get_div();

        delta = (int64_t)(1 << 28) * sample * div * dac_up * osr * 1000 / ms / 26000000;
    }

    TRACE("%s: %s sample=%d ms=%d delta=%d", __FUNCTION__, adc ? "ADC" : "DAC", sample, ms, (int)delta);

    if (delta) {
        analog_read(0x36, &high);
        analog_read(0x35, &low);
#ifdef AUD_PLL_DOUBLE
        analog_read(0x37, &val);
        bit34_32 = GET_BITFIELD(val, ANALOG_AUDPLL_FREQ_34_32);
        new_pll = (int64_t)(((uint64_t)bit34_32 << 32) | ((uint32_t)high << 16) | (uint32_t)low) - delta;
#else
        new_pll = (int64_t)(((uint32_t)high << 16) | (uint32_t)low) - delta;
#endif

        new_high = (new_pll >> 16) & 0xFFFF;
        new_low = new_pll & 0xFFFF;

#ifdef AUD_PLL_DOUBLE
        new_bit34_32 = (new_pll >> 32) & (ANALOG_AUDPLL_FREQ_34_32_MASK >> ANALOG_AUDPLL_FREQ_34_32_SHIFT);
        if (bit34_32 != new_bit34_32) {
            val = SET_BITFIELD(val, ANALOG_AUDPLL_FREQ_34_32, new_bit34_32);
            analog_write(0x37, val);
        }
#endif

        if (new_high != high) {
            analog_write(0x36, new_high);
        }
        if (new_low != low) {
            analog_write(0x35, new_low);
        }

        analog_read(ANALOG_REG31_OFFSET, &update);
        analog_write(ANALOG_REG31_OFFSET,update&(~ANALOG_REG31_AUDPLL_FREQ_EN));
        analog_write(ANALOG_REG31_OFFSET,update|ANALOG_REG31_AUDPLL_FREQ_EN);
    }
}

void analog_aud_pll_open(void)
{
#ifdef __AUDIO_RESAMPLE__
    if (hal_get_chip_metal_id() >= HAL_CHIP_METAL_ID_2 && hal_cmu_get_audio_resample_status()) {
        unsigned short val;
        uint32_t lock;
        
        lock = int_lock();

        hal_analogif_accessmode_set(HAL_ANALOGIF_MODE_FULLACCESS);

        analog_read(ANALOG_REGB4_OFFSET, &val);
        val |= (1 << 5);
        analog_write(ANALOG_REGB4_OFFSET, val);
        
        hal_analogif_accessmode_set(HAL_ANALOGIF_MODE_PROTECTACCESS);
        
        int_unlock(lock);
    } else
#endif
    {
        hal_cmu_pll_enable(HAL_CMU_PLL_AUD, HAL_CMU_PLL_USER_AUD);
    }
}

void analog_aud_pll_close(void)
{
#ifdef __AUDIO_RESAMPLE__
    if (hal_get_chip_metal_id() >= HAL_CHIP_METAL_ID_2 && hal_cmu_get_audio_resample_status()) {
        unsigned short val;
        uint32_t lock;
        
        lock = int_lock();
        hal_analogif_accessmode_set(HAL_ANALOGIF_MODE_FULLACCESS);

        analog_read(ANALOG_REGB4_OFFSET, &val);
        val &= ~(1 << 5);
        analog_write(ANALOG_REGB4_OFFSET, val);

        hal_analogif_accessmode_set(HAL_ANALOGIF_MODE_PROTECTACCESS);
        
        int_unlock(lock);
    } else
#endif
    {
        hal_cmu_pll_disable(HAL_CMU_PLL_AUD, HAL_CMU_PLL_USER_AUD);
    }
}

int analog_usbdevice_init(void)
{
    unsigned short readVal;

    analog_read(ANALOG_REG31_OFFSET, &readVal);
    analog_write(ANALOG_REG31_OFFSET,readVal|ANALOG_REG31_USBPLL_FREQ_EN);

    // Only for metal 0
    //analog_write(0x45, 0x645A);

    //analog_write(0x47, 0x1A61); // default usb tx io drive (2)
    //analog_write(0x47, 0x1661); // reduce usb tx io drive (1)
    //analog_write(0x47, 0x1E61); // increase usb tx io drive (3)

    analog_write(0x46, 0x01EF); // decrease pull-up resistor
    //analog_write(0x4D, 0x29C9); // increase ldo voltage for USB host

    return 0;
}

int analog_usbhost_init(void)
{
    unsigned short readVal;

    analog_read(ANALOG_REG31_OFFSET, &readVal);
    analog_write(ANALOG_REG31_OFFSET,readVal|ANALOG_REG31_USBPLL_FREQ_EN);

    // Only for metal 0
    //analog_write(0x45, 0x645A);

    //analog_write(0x47, 0x1A61); // default usb tx io drive (2)
    //analog_write(0x47, 0x1661); // reduce usb tx io drive (1)
    //analog_write(0x47, 0x1E61); // increase usb tx io drive (3)

    analog_write(0x46, 0xC1EF); // decrease pull-up resistor
    //analog_write(0x4D, 0x29C9); // increase ldo voltage for USB host

    return 0;
}

//Note: Can be used to open many dacs, also can be used to close all dacs
static void analog_aud_enable_dac(uint8_t dac)
{
    unsigned short readVal;

    ANALOG_TRACE("[%s] dac = 0x%x", __func__, dac);
    analog_read(ANALOG_REG31_OFFSET, &readVal);

    //clear all dac bit
    readVal &= ~ANALOG_REG31_ENABLE_ALL_DAC;

    if(dac & ANALOG_AUD_LDAC)
    {
        readVal |= ANALOG_REG31_CODEC_TXEN_LDAC;
    }

    if(dac & ANALOG_AUD_RDAC)
    {
        readVal |= ANALOG_REG31_CODEC_TXEN_RDAC;
    }

    analog_write(ANALOG_REG31_OFFSET, readVal);
#if 0
    analog_read(ANALOG_REG3A_OFFSET, &readVal);

    //clear all dac bit
    readVal &= ~ANALOG_REG3A_ENABLE_ALL_DAC_PA;

    if(dac & ANALOG_AUD_LDAC)
    {
        readVal |= ANALOG_REG3A_CFG_TXEN_LPA;
    }

    if(dac & ANALOG_AUD_RDAC)
    {
        readVal |= ANALOG_REG3A_CFG_TXEN_RPA;
    }

    analog_write(ANALOG_REG3A_OFFSET, readVal);
#endif
}

static void analog_aud_enable_dac_pa(uint8_t dac)
{
    unsigned short readVal;


    analog_read(ANALOG_REG3A_OFFSET, &readVal);

    //clear all dac bit
    readVal &= ~ANALOG_REG3A_ENABLE_ALL_DAC_PA;
    readVal |= ANALOG_REG3A_CFG_BBPLL1_CLKGEN_EN;
    if(dac & ANALOG_AUD_LDAC)
    {
        readVal |= ANALOG_REG3A_CFG_TXEN_LPA;
    }

    if(dac & ANALOG_AUD_RDAC)
    {
        readVal |= ANALOG_REG3A_CFG_TXEN_RPA;
    }

    analog_write(ANALOG_REG3A_OFFSET, readVal);


}



//Note: Can be used to open many adcs, also can be used to close all adcs
static void analog_aud_enable_adc(uint8_t adc)
{
    unsigned short readVal;

    ANALOG_TRACE("[%s] adc = 0x%x", __func__, adc);
    analog_read(ANALOG_REG31_OFFSET, &readVal);

    //clear all adc bit
    readVal &= ~ANALOG_REG31_ENABLE_ALL_ADC;

    //force enable mic a , we need clock!!!
    if (adc != ANALOG_AUD_IO_NONE){
        readVal |= ANALOG_REG31_ENABLE_MIC_A;
    }

    if(adc & ANALOG_AUD_ADC_B)
    {
        readVal |= ANALOG_REG31_ENABLE_MIC_B;
    }

    if(adc & ANALOG_AUD_ADC_C)
    {
        readVal |= ANALOG_REG31_ENABLE_MIC_C;
    }

    if(adc & ANALOG_AUD_ADC_D)
    {
        readVal |= ANALOG_REG31_ENABLE_MIC_D;
    }

    analog_write(ANALOG_REG31_OFFSET, readVal);
}

static void analog_aud_mic_cfg(uint8_t dev)
{
    unsigned short reg_val_3f = 0;
    unsigned short reg_val_32 = 0;

    analog_read(0x3F,& reg_val_3f);
    if (dev & ANALOG_AUD_ADC_A){        
        reg_val_3f |= (1<<13);        
        reg_val_32 |= ((ANALOG_GA1A_GAIN & ANALOG_REG32_CFG_PGA1A_GAIN_MASK)<<ANALOG_REG32_CFG_PGA1A_GAIN_SHIFT) |
                      ((ANALOG_GA2A_GAIN & ANALOG_REG32_CFG_PGA2A_GAIN_MASK)<<ANALOG_REG32_CFG_PGA2A_GAIN_SHIFT);
    }
    if (dev & ANALOG_AUD_ADC_B){
        reg_val_3f |= (1<<14);        
        reg_val_32 |= ((ANALOG_GA1B_GAIN & ANALOG_REG32_CFG_PGA1B_GAIN_MASK)<<ANALOG_REG32_CFG_PGA1B_GAIN_SHIFT) |
                      ((ANALOG_GA2B_GAIN & ANALOG_REG32_CFG_PGA2B_GAIN_MASK)<<ANALOG_REG32_CFG_PGA2B_GAIN_SHIFT);
    }
    analog_write(0x3F,reg_val_3f);
    analog_write(ANALOG_REG32_OFFSET, reg_val_32);
}

static void analog_aud_linein_cfg(uint8_t dev)
{
    unsigned short reg_val_3f = 0;
    unsigned short reg_val_32 = 0;
    analog_read(0x3F,& reg_val_3f);


#if 0
    if (dev & ANALOG_AUD_ADC_A){
        reg_val_32 |= ((ANALOG_LINEIN_GA1_GAIN & ANALOG_REG32_CFG_PGA1A_GAIN_MASK)<<ANALOG_REG32_CFG_PGA1A_GAIN_SHIFT) |
                      ((ANALOG_LINEIN_GA2_GAIN & ANALOG_REG32_CFG_PGA2A_GAIN_MASK)<<ANALOG_REG32_CFG_PGA2A_GAIN_SHIFT);
    }
    if (dev & ANALOG_AUD_ADC_B){
        reg_val_32 |= ((ANALOG_LINEIN_GA1_GAIN & ANALOG_REG32_CFG_PGA1B_GAIN_MASK)<<ANALOG_REG32_CFG_PGA1B_GAIN_SHIFT) |
                      ((ANALOG_LINEIN_GA2_GAIN & ANALOG_REG32_CFG_PGA2B_GAIN_MASK)<<ANALOG_REG32_CFG_PGA2B_GAIN_SHIFT);
    }
#else
//adjust gain
if (dev & ANALOG_AUD_ADC_A){
	reg_val_32 |= ((ANALOG_GA1A_GAIN & ANALOG_REG32_CFG_PGA1A_GAIN_MASK)<<ANALOG_REG32_CFG_PGA1A_GAIN_SHIFT) |
				  ((ANALOG_GA1A_GAIN & ANALOG_REG32_CFG_PGA2A_GAIN_MASK)<<ANALOG_REG32_CFG_PGA2A_GAIN_SHIFT);
}
if (dev & ANALOG_AUD_ADC_B){
	reg_val_32 |= ((ANALOG_GA1A_GAIN & ANALOG_REG32_CFG_PGA1B_GAIN_MASK)<<ANALOG_REG32_CFG_PGA1B_GAIN_SHIFT) |
				  ((ANALOG_GA1A_GAIN & ANALOG_REG32_CFG_PGA2B_GAIN_MASK)<<ANALOG_REG32_CFG_PGA2B_GAIN_SHIFT);
}


#endif

    reg_val_3f &= ~((1<<13)|(1<<14));        
    analog_write(0x3F,reg_val_3f);
    analog_write(ANALOG_REG32_OFFSET, reg_val_32);
}

uint8_t analog_aud_get_io_cfg_dev(enum AUD_IO_PATH_T input_path)
{
    ANALOG_TRACE("[%s] input_path = 0x%x", __func__, input_path);
    for(uint8_t i=0; i<ANALOG_AUD_IO_CFG_NUM;i++)
    {
        if(input_path == analog_aud_io_cfg[i].io_path)
        {
            return analog_aud_io_cfg[i].dev;
        }
    }

    //security
    ASSERT(0, "[%s] input_path(0x%x) has not defined device", __func__, input_path);

    //or this is reasonable
    return ANALOG_AUD_IO_NONE;
}

static ANALOG_AUD_IO_CFG_HANDLER_T analog_aud_get_io_cfg_handler(enum AUD_IO_PATH_T input_path)
{
    for(uint8_t i=0; i<ANALOG_AUD_IO_CFG_NUM;i++)
    {
        if(input_path == analog_aud_io_cfg[i].io_path)
        {
            return analog_aud_io_cfg[i].handler;
        }
    }

    //security
    ASSERT(0, "[%s] input_path(0x%x) has not defined handler", __func__, input_path);

    //or this is reasonable
    return NULL;
}

//Note: This function is called after analog_aud_coedc_open() has opened corresponding adc,
//      This function deals with parameter setting.
int analog_aud_input_cfg(enum AUD_IO_PATH_T input_path)
{
    uint8_t dev;
    ANALOG_AUD_IO_CFG_HANDLER_T handler = NULL;

    dev = analog_aud_get_io_cfg_dev(input_path);
    handler = analog_aud_get_io_cfg_handler(input_path);
    if(handler)
    {
        handler(dev);
    }
    else
    {
        ASSERT(0, "[%s] input_path(0x%x) has a NULL handler", __func__, input_path);
    }
    return dev;
}

void analog_codec_tx_pa_gain_sel(uint32_t v)
{
    unsigned short val = 0;
    analog_read(0x33, &val);
    val &= ~ANALOG_CODEC_TX_PA_GAIN_MASK;
    if (v)
        val |= (ANALOG_CODEC_TX_PA_GAIN_MASK & (v << ANALOG_CODEC_TX_PA_GAIN_SHIFT));
    analog_write(0x33, val);
}

uint32_t analog_codec_tx_pa_gain_get()
{
    unsigned short val = 0;
    analog_read(0x33, &val);
    val = (val & ANALOG_CODEC_TX_PA_GAIN_MASK)>>ANALOG_CODEC_TX_PA_GAIN_SHIFT;
    return val;
}  

void analog_codec_rx_gain_sel(uint32_t chnl,uint32_t gain1, uint32_t gain2)
{
    unsigned short readVal = 0;

    if (chnl)
        return;

    if (chnl & 0x01){
        readVal |= ((gain1 & ANALOG_REG32_CFG_PGA1A_GAIN_MASK)<<ANALOG_REG32_CFG_PGA1A_GAIN_SHIFT) |
                   ((gain2 & ANALOG_REG32_CFG_PGA2A_GAIN_MASK)<<ANALOG_REG32_CFG_PGA2A_GAIN_SHIFT);
    }

    if (chnl & 0x02){
        readVal |= ((gain1 & ANALOG_REG32_CFG_PGA1B_GAIN_MASK)<<ANALOG_REG32_CFG_PGA1B_GAIN_SHIFT) |
                   ((gain2 & ANALOG_REG32_CFG_PGA2B_GAIN_MASK)<<ANALOG_REG32_CFG_PGA2B_GAIN_SHIFT);
    }

    analog_write(ANALOG_REG32_OFFSET,readVal);
}

void anlog_open(void)
{
    unsigned short readVal;

    hal_analogif_accessmode_set(HAL_ANALOGIF_MODE_FULLACCESS);

    // Init 0xA2 / 0xA3
    analog_read(0xA2, &readVal);
    readVal |= (1 << 15);
    analog_write(0xA2, readVal);
    analog_write(0xA3, readVal);

    // Init ANALOG_REGAE_RCHAN_OFFSET_CORRECT_CODE to 0
    if (hal_get_chip_metal_id() >= HAL_CHIP_METAL_ID_4) {
        analog_write(ANALOG_REGAE_OFFSET, 0x48C3);
    }

    // Init 0xa0
	// set micbias to vcode 	
    analog_read(0xa0, &readVal);
    readVal=(readVal&(~(0xf<<12)))|(0xf<<12);
    readVal=(readVal&(~(0xf<<7)))|(0xf<<7);
    analog_write(0xa0, readVal);

    //DAC INIT
    analog_write(0x33,0xFC00);
    if(hal_get_chip_metal_id() >= HAL_CHIP_METAL_ID_3)
    {
        analog_write(0x3C,0x6859);
    }
    //dac rise ramp
#ifdef AUDIO_OUTPUT_DIFF
    analog_write(ANALOG_REG3B_OFFSET, ANALOG_REG3B_CODEC_TX_PA_DIFFMODE);
    analog_write(0xA4,0x0000);
    analog_write(0xA5,0x0000);
#else
    analog_write(ANALOG_REG3B_OFFSET, ANALOG_REG3B_CODEC_TX_PA_SOFTSTART(63));
    analog_write(0xA4,0x1000);
    analog_write(0xA5,0x1000);
#endif
    if(hal_get_chip_metal_id() <  HAL_CHIP_METAL_ID_4)
    {
        analog_write(0xB0,0x4108);
    }
    else if(hal_get_chip_metal_id() == HAL_CHIP_METAL_ID_4)
    {
        analog_write(0xB0,0x0108);
    }
#ifdef __AUDIO_OUTPUT_VOLT_STAY_HIGH__
    if(hal_get_chip_metal_id() >= HAL_CHIP_METAL_ID_2)
    {
        //STAY_HIGH need TX_PA_SOFTSTART != 0
        analog_read(ANALOG_REG3B_OFFSET, &readVal);
        readVal |= ANALOG_REG3B_CODEC_TX_PA_SOFTSTART(1);
        analog_write(ANALOG_REG3B_OFFSET,readVal);
    }
#endif
    if(hal_get_chip_metal_id() >= HAL_CHIP_METAL_ID_4){
        readVal = 0;
        readVal = (readVal & ~0xFF) | ((1 << 4) | (0x3 << 2));
#ifdef __AUDIO_OUTPUT_VOLT_STAY_HIGH__
        readVal |= (0x3 << 6);      // CODEC OUTPUT DON'T DOWN
#endif		
    }else{
        readVal = (1 << 9);
        if(hal_get_chip_metal_id() >= HAL_CHIP_METAL_ID_2){
            readVal = (readVal & ~0xFF) | ((1 << 4) | (0x3 << 2));
#ifdef __AUDIO_OUTPUT_VOLT_STAY_HIGH__
            readVal |= (0x3 << 6);      // CODEC OUTPUT DON'T DOWN
#endif
        }
#ifdef _BEST1000_QUAL_DCDC_
        readVal = (readVal & ~(0x3 << 13));
#else
        readVal = (readVal & ~(0x3 << 13)) | (0x1 << 13);
#endif
    }
    analog_write(ANALOG_REGB4_OFFSET, readVal);

// defualt set adc(a, b, c, d) mic mode
    analog_write(0x3F,0x0AAA);
//Note: capmode using largegain mode can get better noise performance
//Note: enable largegain and (pga1x = 0x06) have same gain
#ifdef _MIC_CAPMODE_
    analog_read(ANALOG_REG32_OFFSET, &readVal);
#ifdef _MIC_LARGEGAIN_
    readVal |= ANALOG_REG32_CFG_PGA1A_LARGEGAIN;
    analog_write(ANALOG_REG32_OFFSET, readVal);  // enable largegain
#else
    readVal |= 0;
    analog_write(ANALOG_REG32_OFFSET,readVal);  // enable largegaindd
#endif
    analog_write(0x33,0xFC00);  // set pga(c and d) gain, set codec_tx_pa_gain
    analog_write(0xA0,0x8c6d);  // enable pga(a and b) capmode
    analog_write(0xA1,0x7A25);  // enable pga(c and d) capmode
#else
    analog_read(0xA0, &readVal);
    readVal &= ~((1 << 0)|(1 << 3));
    readVal |= (1<<6)|(1<<11);
    analog_write(0xA0, readVal);

    analog_read(ANALOG_REGA1_OFFSET, &readVal);
    analog_write(ANALOG_REGA1_OFFSET, readVal&(~(ANALOG_REGA1_PGAC_CAPMODE|ANALOG_REGA1_PGAD_CAPMODE)));
    
#endif

    //close all adc, dac, pll and so on.
    analog_write(ANALOG_REG31_OFFSET, 0);

    analog_aud_freq_pll_config(HAL_CMU_CODEC_FREQ_48K,9);

    analog_read(ANALOG_REGA1_OFFSET, &readVal);
    analog_write(ANALOG_REGA1_OFFSET, readVal&(~ANALOG_REGA1_EN_VCMLPF));
    //analog_write(ANALOG_REGA1_OFFSET, readVal|ANALOG_REGA1_EN_VCMLPF); //improve vmic PSRR.
    hal_analogif_accessmode_set(HAL_ANALOGIF_MODE_PROTECTACCESS);
}

void analog_aud_codec_mic_enable(enum AUD_IO_PATH_T input_path, bool en)
{
	if(en)
	{
#ifdef _AUTO_SWITCH_POWER_MODE__
#ifdef _MIC_CAPMODE_
        unsigned short readVal_a0;
#endif
        unsigned short readVal_34;
        //force disable ADC
        analog_read(ANALOG_REG34_OFFSET,&readVal_34);
        readVal_34 |= ANALOG_REG34_CFG_ADC_A|ANALOG_REG34_CFG_ADC_A_DR|ANALOG_REG34_CFG_ADC_B|ANALOG_REG34_CFG_ADC_B_DR;
        analog_write(ANALOG_REG34_OFFSET,readVal_34);
#ifdef __ADC_ACITVE_USE_VCODEC__
        //Vcodec ramp up to 2.7v
        pmu_module_set_volt(PMU_CODEC, PMU_CODEC_2_7V, PMU_CODEC_2_7V);
        pmu_module_config(PMU_CODEC,PMU_MANUAL_MODE,PMU_LDO_ON,PMU_LP_MODE_ON,PMU_DSLEEP_MODE_OFF); //enable VCODEC
#else

#endif
        analog_delay(1);
#ifdef _MIC_CAPMODE_
        // disable pga(a and b) capmode
        analog_read(0xA0, &readVal_a0);
        readVal_a0 &= ~(1<<0);
        analog_write(0xA0,readVal_a0);
        analog_delay(2);
#endif
#endif
		analog_aud_enable_adc(analog_aud_get_io_cfg_dev(input_path));
#ifdef _AUTO_SWITCH_POWER_MODE__
#ifdef __ADC_ACITVE_USE_VCODEC__
        analog_delay(2);
        //Vcodec ramp down to 2.5v
        pmu_module_set_volt(PMU_CODEC, PMU_CODEC_2_5V, PMU_CODEC_2_5V);
        analog_delay(2);
        //Vcodec ramp down to 2.3v
        pmu_module_set_volt(PMU_CODEC, PMU_CODEC_2_3V, PMU_CODEC_2_3V);
        analog_delay(2);
        //Vcodec ramp down to 2.2v
        pmu_module_set_volt(PMU_CODEC, PMU_CODEC_2_2V, PMU_CODEC_2_2V);
        analog_delay(2);
        //Vcodec close
        pmu_module_config(PMU_CODEC,PMU_MANUAL_MODE,PMU_LDO_OFF,PMU_LP_MODE_ON,PMU_DSLEEP_MODE_OFF); //disable VCODEC
#else

#endif
#ifdef _MIC_CAPMODE_
        // enable pga(a and b) capmode
        readVal_a0 |= (1<<0);
        analog_write(0xA0,readVal_a0);
#endif
        analog_delay(5);
        //resume ADC status
        readVal_34 &= ~(ANALOG_REG34_CFG_ADC_A|ANALOG_REG34_CFG_ADC_A_DR|ANALOG_REG34_CFG_ADC_B|ANALOG_REG34_CFG_ADC_B_DR);
        analog_write(ANALOG_REG34_OFFSET,readVal_34);
#endif
	}
	else
	{
		analog_aud_enable_adc(ANALOG_AUD_IO_NONE);
	}

}

#if 0
void analog_aud_codec_speaker_enable(bool en)
{
	if(en)
	{
#ifdef _AUTO_SWITCH_POWER_MODE__
        pmu_module_config(PMU_CODEC,PMU_MANUAL_MODE,PMU_LDO_ON,PMU_LP_MODE_ON,PMU_DSLEEP_MODE_OFF); //enable VCODEC
        analog_delay(1);
#endif
		analog_aud_enable_dac(analog_aud_get_io_cfg_dev(AUD_OUTPUT_PATH_SPEAKER));
#ifdef _AUTO_SWITCH_POWER_MODE__
        analog_delay(1);
        pmu_module_config(PMU_CODEC,PMU_MANUAL_MODE,PMU_LDO_OFF,PMU_LP_MODE_ON,PMU_DSLEEP_MODE_OFF); //enable VCODEC

#endif

	}
	else
	{
		analog_aud_enable_dac(ANALOG_AUD_IO_NONE);
	}

}
#endif

void analog_aud_coedc_open(enum AUD_IO_PATH_T input_path)
{
    unsigned short readVal_34;

#ifdef __DAC_ACITVE_USE_LVOLTAGE__
#else
#ifdef _AUTO_SWITCH_POWER_MODE__
    pmu_mode_change(BEST1000_DIG_DCDC_MODE);
#ifdef __DAC_ACITVE_USE_VCODEC__
    pmu_module_set_volt(PMU_CODEC, PMU_CODEC_2_7V, PMU_CODEC_2_7V);
    pmu_module_config(PMU_CODEC,PMU_MANUAL_MODE,PMU_LDO_ON,PMU_LP_MODE_ON,PMU_DSLEEP_MODE_OFF); //enable VCODEC
#else

#endif
#endif

#ifndef _BEST1000_QUAL_DCDC_
    pmu_module_config(PMU_CODEC,PMU_MANUAL_MODE,PMU_LDO_ON,PMU_LP_MODE_ON,PMU_DSLEEP_MODE_OFF); //enable VCODEC
#endif
    analog_delay(1);
#endif

//   analog_read(0x3A,&readVal);
//   analog_write(0x3A,readVal |(1<<ANALOG_REG3A_CFG_BBPLL1_CLKGEN_EN));
//   analog_read(0x3A,&readVal);
//   ANALOG_TRACE("open 0x3A = %x",readVal);

    //force enable vcapcom bais
    analog_read(ANALOG_REG34_OFFSET,&readVal_34);
    readVal_34 |= ANALOG_REG34_CFG_RX|ANALOG_REG34_CFG_RX_DR;
    analog_write(ANALOG_REG34_OFFSET,readVal_34);
//    analog_delay(120);
//   adc/adc operation must in NORMAL VOLTAGE
    analog_aud_enable_dac(analog_aud_get_io_cfg_dev(AUD_OUTPUT_PATH_SPEAKER));
//   analog_aud_enable_adc(analog_aud_get_io_cfg_dev(input_path));
//    analog_delay(10);

#ifdef __DAC_ACITVE_USE_LVOLTAGE__
#ifdef _AUTO_SWITCH_POWER_MODE__
    pmu_mode_change(BEST1000_DIG_DCDC_MODE);
#endif
#else
#ifdef _AUTO_SWITCH_POWER_MODE__
#ifdef __DAC_ACITVE_USE_VCODEC__
    //Vcodec ramp down to 2.5v
    pmu_module_set_volt(PMU_CODEC, PMU_CODEC_2_5V, PMU_CODEC_2_5V);
    analog_delay(10);
    //Vcodec ramp down to 2.3v
    pmu_module_set_volt(PMU_CODEC, PMU_CODEC_2_3V, PMU_CODEC_2_3V);
    analog_delay(10);
    //Vcodec ramp down to 2.2v
    pmu_module_set_volt(PMU_CODEC, PMU_CODEC_2_2V, PMU_CODEC_2_2V);
    analog_delay(5);
    //Vcodec close
    pmu_module_config(PMU_CODEC,PMU_MANUAL_MODE,PMU_LDO_OFF,PMU_LP_MODE_ON,PMU_DSLEEP_MODE_OFF); //enable VCODEC
#else

#endif
#endif
#endif
    // wait for power stable
//    analog_delay(10);
	analog_delay(30);
	
    //resume enable vcapcom bais
    readVal_34 &= ~(ANALOG_REG34_CFG_RX|ANALOG_REG34_CFG_RX_DR);
    analog_write(ANALOG_REG34_OFFSET,readVal_34);

    if (!dac_dc_calib_val_valid){
        analog_delay(50);
        analog_aud_enable_dac_pa(analog_aud_get_io_cfg_dev(AUD_OUTPUT_PATH_SPEAKER));
    }

#ifdef CFG_HW_EXT_AUDIOPA_ENABLE
	static const struct HAL_IOMUX_PIN_FUNCTION_MAP pinmux_output_switch =
	{CFG_HW_EXT_AUDIOPA_ENABLE, HAL_IOMUX_FUNC_AS_GPIO, HAL_IOMUX_PIN_VOLTAGE_VIO, HAL_IOMUX_PIN_NOPULL};
    
	hal_iomux_init(&pinmux_output_switch, 1);
    
    hal_gpio_pin_set_dir(CFG_HW_EXT_AUDIOPA_ENABLE, HAL_GPIO_DIR_OUT, 0);
#endif
}

void analog_aud_codec_speaker_enable(bool en)
{
    if (!dac_dc_calib_val_valid){
#ifdef CFG_HW_EXT_AUDIOPA_ENABLE
        if (en) {
			
			analog_delay(800);
			
            hal_gpio_pin_set(CFG_HW_EXT_AUDIOPA_ENABLE);
        } else {
            
            hal_gpio_pin_clr(CFG_HW_EXT_AUDIOPA_ENABLE);
        }
#endif        
    }else{
        if (en) {
			
            analog_aud_enable_dac_pa(CFG_HW_AUD_OUTPUT_PATH_SPEAKER_DEV);
#ifdef CFG_HW_EXT_AUDIOPA_ENABLE
			

            hal_sys_timer_delay_us(5);
			
			analog_delay(800);
			
            hal_gpio_pin_set(CFG_HW_EXT_AUDIOPA_ENABLE);
#endif
        } else {
            analog_aud_enable_dac_pa(ANALOG_AUD_IO_NONE);
#ifdef CFG_HW_EXT_AUDIOPA_ENABLE
            hal_gpio_pin_clr(CFG_HW_EXT_AUDIOPA_ENABLE);
#endif
        }
    }
}

void analog_aud_coedc_close(void)
{
    unsigned short readVal;

    //adc/adc operation must in NORMAL VOLTAGE
    analog_aud_enable_dac_pa(ANALOG_AUD_IO_NONE);
    analog_delay(1);
    //close all dac, modify
    analog_aud_enable_dac(ANALOG_AUD_IO_NONE);
    //close all adc, modify
    analog_aud_enable_adc(ANALOG_AUD_IO_NONE);
#ifndef __CODEC_ASYNC_CLOSE__
    analog_delay(100);
#endif

    //disable vcapcom bais
    analog_read(ANALOG_REG34_OFFSET,&readVal);
    readVal &= ~(ANALOG_REG34_CFG_RX|ANALOG_REG34_CFG_RX_DR);
    analog_write(ANALOG_REG34_OFFSET,readVal);

#ifdef _AUTO_SWITCH_POWER_MODE__
    pmu_mode_change(BEST1000_ANA_DCDC_MODE);
#endif

#ifndef _BEST1000_QUAL_DCDC_
    pmu_module_config(PMU_CODEC,PMU_MANUAL_MODE,PMU_LDO_OFF,PMU_LP_MODE_ON,PMU_DSLEEP_MODE_OFF); //disable VCODEC
#endif
}


int analog_aud_coedc_mute(void)
{
    unsigned short readVal;
    uint32_t g_tx_pa_gain_old;

    if (dac_dc_calib_val_valid){
        return 0;
    }

    g_tx_pa_gain_old = analog_codec_tx_pa_gain_get();
    analog_codec_tx_pa_gain_sel(0);
    analog_read(ANALOG_REG31_OFFSET, &readVal);
    analog_write(ANALOG_REG31_OFFSET,readVal |
                                     ANALOG_REG31_CODEC_CH0_MUTE|
                                     ANALOG_REG31_CODEC_CH1_MUTE);
    return g_tx_pa_gain_old;
}

void analog_aud_coedc_nomute(void)
{
    unsigned short readVal;

    if (dac_dc_calib_val_valid){
        return;
    }

    analog_read(ANALOG_REG31_OFFSET, &readVal);
    analog_write(ANALOG_REG31_OFFSET,readVal &
                                     ~(ANALOG_REG31_CODEC_CH0_MUTE|
                                     ANALOG_REG31_CODEC_CH1_MUTE));
    analog_codec_tx_pa_gain_sel(TX_PA_GAIN);
}

static int POSSIBLY_UNUSED dc_calib_checksum_valid(uint16_t efuse)
{
    int i;
    uint32_t cnt = 0;

    for (i = 0; i < 12; i++) {
        if (efuse & (1 << i)) {
            cnt++;
        }
    }

    return (((~cnt) & 0xF) == ((efuse >> 12) & 0xF));
}


void analog_aud_get_dc_calib_value(int16_t *dc_l, int16_t *dc_r)
{
	uint16_t val;
	uint16_t efuse;
    uint8_t valid_cnt = 0;

	pmu_get_efuse(PMU_EFUSE_PAGE_DCCALIB_L, &efuse);
#ifdef ANA_OLD_DC_CALIB
	if (efuse == 0) {
		goto _old_calib;
	}
#endif
	val = efuse & 0x7FF;
	if (efuse & 0x800) {
		*dc_l = val;
	} else {
		*dc_l = -val;
	}
	if (dc_calib_checksum_valid(efuse)) {
        valid_cnt++;
		TRACE("Dc calib L OK: 0x%04x", efuse);
	} else {
		TRACE("Warning: Bad dc calib efuse L: 0x%04x", efuse);
		*dc_l = 0;
	}

	pmu_get_efuse(PMU_EFUSE_PAGE_DCCALIB_R, &efuse);
#ifdef ANA_OLD_DC_CALIB
	if (efuse == 0) {
		goto _old_calib;
	}
#endif
	val = efuse & 0x7FF;
	if (efuse & 0x800) {
		*dc_r = val;
	} else {
		*dc_r = -val;
	}
	if (dc_calib_checksum_valid(efuse)) {
        valid_cnt++;
		TRACE("Dc calib R OK: 0x%04x", efuse);
	} else {
		TRACE("Warning: Bad dc calib efuse L: 0x%04x", efuse);
		*dc_r = 0;
	}

_got_values:
	TRACE("ANA: DC CALIB L=%d R=%d", *dc_l, *dc_r);

#if defined(ANA_DC_CALIB_L) || defined(ANA_DC_CALIB_R)
#ifdef ANA_DC_CALIB_L
	*dc_l = ANA_DC_CALIB_L;
#endif
#ifdef ANA_DC_CALIB_R
	*dc_r = ANA_DC_CALIB_R;
#endif
	TRACE("ANA: OVERRIDE DC CALIB L=%d R=%d", *dc_l, *dc_r);
#endif

    if (valid_cnt == 2){
        dac_dc_calib_val_valid = true;
    }

	return;

#ifdef ANA_OLD_DC_CALIB
_old_calib:
	pmu_get_efuse(PMU_EFUSE_PAGE_RESERVED_9, &efuse);
	val = efuse & 0xFF;
	if (efuse & 0x100) {
		*dc_l = val;
	} else {
		*dc_l = -val;
	}

	pmu_get_efuse(PMU_EFUSE_PAGE_RESERVED_10, &efuse);
	val = efuse & 0xFF;
	if (efuse & 0x100) {
		*dc_r = val;
	} else {
		*dc_r = -val;
	}
#endif

	goto _got_values;
}
