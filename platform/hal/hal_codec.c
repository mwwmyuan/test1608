#include "plat_types.h"
#include "reg_codecip.h"
#include "hal_codecip.h"
#include "hal_codec.h"
#include "hal_uart.h"
#include "hal_trace.h"
#include "hal_timer.h"
#include "hal_cmu.h"
#include "hal_aud.h"
#include "hal_chipid.h"
#include "analog.h"
#include "tgt_hardware.h"
#if defined(AUDIO_SW_OUTPUT_GAIN) && !defined(NOSTD)
#include <math.h>
#endif

//#define HAL_CODEC_ACCURATE_SW_GAIN
#ifndef MAX_DIG_DAC_DBVAL
#define MAX_DIG_DAC_DBVAL                   (0)
#endif

#ifndef MIN_DIG_DAC_DBVAL
#define MIN_DIG_DAC_DBVAL                   (-99)
#endif

#ifndef MAX_DIG_DAC_REGVAL
#define MAX_DIG_DAC_REGVAL                  (21)
#endif

#ifndef MIN_DIG_DAC_REGVAL
#define MIN_DIG_DAC_REGVAL                  (0)
#endif

#ifndef ALG_DAC_DBVAL_PER_SHIFT
#define ALG_DAC_DBVAL_PER_SHIFT             (6)
#endif

#ifndef ANALOG_LINEIN_CODEC_SADC_VOL
#define ANALOG_LINEIN_CODEC_SADC_VOL (1)
#endif

#define HAL_CODEC_TX_FIFO_TRIGGER_LEVEL   (CODECIP_FIFO_DEPTH/2)
#define HAL_CODEC_RX_FIFO_TRIGGER_LEVEL   (CODECIP_FIFO_DEPTH/2)

#define HAL_CODEC_YES 1
#define HAL_CODEC_NO 0

static const char * const invalid_id = "Invalid CODEC ID: %d\n";
static const char * const invalid_ch = "Invalid CODEC CH: %d\n";

struct  CODEC_DAC_SAMPLE_RATE_T{
    enum AUD_SAMPRATE_T sample_rate;
    enum HAL_CMU_CODEC_FREQ_T base_freq;        //44.1 or 48    
    uint32_t cmu_div:4;
    uint32_t dac_up:4;      //0x4000a044
    uint32_t sdac_osr:2;    //0x4000a04c
};

const struct  CODEC_DAC_SAMPLE_RATE_T codec_dac_sample_rate[]={
    {AUD_SAMPRATE_8000,   HAL_CMU_CODEC_FREQ_48K,   9, 0x03, 0x02},
    {AUD_SAMPRATE_16000,  HAL_CMU_CODEC_FREQ_48K,   9, 0x01, 0x02},
    {AUD_SAMPRATE_22050,  HAL_CMU_CODEC_FREQ_44_1K, 9, 0x00, 0x02},
    {AUD_SAMPRATE_24000,  HAL_CMU_CODEC_FREQ_48K,   9, 0x00, 0x02},
    {AUD_SAMPRATE_44100,  HAL_CMU_CODEC_FREQ_44_1K, 9, 0x04, 0x02},
    {AUD_SAMPRATE_48000,  HAL_CMU_CODEC_FREQ_48K,   9, 0x04, 0x02},
    {AUD_SAMPRATE_96000,  HAL_CMU_CODEC_FREQ_48K,   9, 0x04, 0x01},
    {AUD_SAMPRATE_192000, HAL_CMU_CODEC_FREQ_48K,   9, 0x04, 0x00},
};

struct  CODEC_ADC_SAMPLE_RATE_T{
    enum AUD_SAMPRATE_T sample_rate;
    enum HAL_CMU_CODEC_FREQ_T base_freq;        //44.1 or 48    
    uint32_t cmu_div:4;
    uint32_t adc_down:2;        //0x4000a044
};

const struct  CODEC_ADC_SAMPLE_RATE_T codec_adc_sample_rate[]={
    {AUD_SAMPRATE_8000,  HAL_CMU_CODEC_FREQ_48K,   9, 0x01},
    {AUD_SAMPRATE_16000, HAL_CMU_CODEC_FREQ_48K,   9, 0x00},
    {AUD_SAMPRATE_44100, HAL_CMU_CODEC_FREQ_44_1K, 9, 0x02},
    {AUD_SAMPRATE_48000, HAL_CMU_CODEC_FREQ_48K,   9, 0x02},
};

#define HAL_CODEC_TRACE(str, ...)   TRACE("[%s] " str, __func__, __VA_ARGS__)

static uint8_t algdac_shift;

#ifdef AUDIO_SW_OUTPUT_GAIN
static HAL_CODEC_SW_OUTPUT_COEF_CALLBACK sw_output_coef_callback;
#endif

static void hal_codec_set_dig_dac_gain(uint32_t reg_base, int32_t gain);
int hal_codec_get_dac_volume_tab(struct  CODEC_DAC_VOL_T **vol_ptr);

static inline uint32_t _codec_get_reg_base(enum HAL_CODEC_ID_T id)
{
    ASSERT(id < HAL_CODEC_ID_NUM, invalid_id, id);
    switch(id) {
        case HAL_CODEC_ID_0:
        default:
            return 0x4000A000;
            break;
    }
    return 0;
}

int hal_codec_open(enum HAL_CODEC_ID_T id)
{
    uint32_t reg_base;

    hal_cmu_codec_clock_enable();
    hal_cmu_clock_enable(HAL_CMU_MOD_P_CODEC);
    hal_cmu_reset_clear(HAL_CMU_MOD_P_CODEC);

    reg_base = _codec_get_reg_base(id);

    codecipbus_w_tx_fifo_reset(reg_base, HAL_CODEC_YES);
    codecipbus_w_tx_fifo_reset(reg_base, HAL_CODEC_NO);
    codecipbus_w_rx_fifo_reset(reg_base, HAL_CODEC_YES);
    codecipbus_w_rx_fifo_reset(reg_base, HAL_CODEC_NO);
    codecipbus_w_en(reg_base, HAL_CODEC_YES);           //Reserved
    codecipbus_w_codec_if_en(reg_base, HAL_CODEC_YES);  //DMA enbale

#ifdef AUDIO_SW_OUTPUT_GAIN
    const struct CODEC_DAC_VOL_T *vol_tab_ptr;

    // Init gain settings
    hal_codec_get_dac_volume_tab((struct  CODEC_DAC_VOL_T **)&vol_tab_ptr);
    if (vol_tab_ptr) {
        analog_codec_tx_pa_gain_sel(vol_tab_ptr->tx_pa_gain);
        codecipbus_w_sdm_gain_sel(reg_base, vol_tab_ptr->sdm_gain);
        hal_codec_set_dig_dac_gain(reg_base, MAX_DIG_DAC_DBVAL);
    }
#endif

    return reg_base;
}

int hal_codec_close(enum HAL_CODEC_ID_T id)
{
    uint32_t reg_base;

    reg_base = _codec_get_reg_base(id);

    codecipbus_w_en(reg_base, HAL_CODEC_NO);            //Reserved
    codecipbus_w_codec_if_en(reg_base, HAL_CODEC_NO);   //DMA disable
    codecipbus_w_tx_fifo_reset(reg_base, HAL_CODEC_YES);
    codecipbus_w_tx_fifo_reset(reg_base, HAL_CODEC_NO);
    codecipbus_w_rx_fifo_reset(reg_base, HAL_CODEC_YES);
    codecipbus_w_rx_fifo_reset(reg_base, HAL_CODEC_NO);

    hal_cmu_reset_set(HAL_CMU_MOD_O_CODEC_AD);
    hal_cmu_reset_set(HAL_CMU_MOD_O_CODEC_DA);
    // NEVER reset CODEC registers, for the CODEC driver expects that last configurations
    // still exist in the next stream setup
    //hal_cmu_reset_set(HAL_CMU_MOD_P_CODEC);
    hal_cmu_clock_disable(HAL_CMU_MOD_P_CODEC);
    hal_cmu_codec_clock_disable();

    return 0;
}
int hal_codec_start_stream(enum HAL_CODEC_ID_T id, enum AUD_STREAM_T stream)
{
    uint32_t reg_base;

    reg_base = _codec_get_reg_base(id);

    if (stream == AUD_STREAM_PLAYBACK) {
        codecipbus_w_tx_fifo_reset(reg_base, HAL_CODEC_YES);
        codecipbus_w_tx_fifo_reset(reg_base, HAL_CODEC_NO);
        codecipbus_w_dac_en(reg_base, HAL_CODEC_YES);   //enable dac channel
        codecip_w_dac_en(reg_base, HAL_CODEC_YES);      //enable dac in last step
    }
    else {
        codecipbus_w_rx_fifo_reset(reg_base, HAL_CODEC_YES);
        codecipbus_w_rx_fifo_reset(reg_base, HAL_CODEC_NO);
        codecipbus_w_adc_en(reg_base, HAL_CODEC_YES);
        codecip_w_adc_en(reg_base, HAL_CODEC_YES);
    }

    return reg_base;
}
int hal_codec_stop_stream(enum HAL_CODEC_ID_T id, enum AUD_STREAM_T stream)
{
    uint32_t reg_base;

    reg_base = _codec_get_reg_base(id);

    if (stream == AUD_STREAM_PLAYBACK) {
        codecip_w_dac_en(reg_base, HAL_CODEC_NO);       //disable dac
        codecipbus_w_dac_en(reg_base, HAL_CODEC_NO);    //disable dac channel
        codecipbus_w_tx_fifo_reset(reg_base, HAL_CODEC_YES);
        codecipbus_w_tx_fifo_reset(reg_base, HAL_CODEC_NO);
    }
    else {
        codecip_w_adc_en(reg_base, HAL_CODEC_NO);
        codecipbus_w_adc_en(reg_base, HAL_CODEC_NO);
        codecipbus_w_rx_fifo_reset(reg_base, HAL_CODEC_YES);
        codecipbus_w_rx_fifo_reset(reg_base, HAL_CODEC_NO);
    }

    return reg_base;
}

int hal_codec_get_dac_volume_tab(struct  CODEC_DAC_VOL_T **vol_ptr)
{
    //if can get dac tab from xxx, get it.
    *vol_ptr = (struct  CODEC_DAC_VOL_T *)codec_dac_vol;

    //else get local dac tab.

    return 0;
}

int hal_codec_setup_stream_smaple_rate_trim(enum HAL_CODEC_ID_T id, enum AUD_STREAM_T stream, struct HAL_CODEC_CONFIG_T *cfg, float trim)
{
    uint8_t i;
    //uint32_t codec_freq;
    for(i = 0; i < ARRAY_SIZE(codec_dac_sample_rate); i++)
    {
        if(codec_dac_sample_rate[i].sample_rate == cfg->sample_rate)
        {
            break;
        }
    }
    ASSERT(i < ARRAY_SIZE(codec_dac_sample_rate), "%s: Invalid playback sample rate: %d", __func__, cfg->sample_rate);
    analog_aud_freq_pll_config(codec_dac_sample_rate[i].base_freq+(codec_dac_sample_rate[i].base_freq*trim), codec_dac_sample_rate[i].cmu_div);
    return 0;
}

uint32_t hal_codec_get_alg_dac_shift(void)
{
    return algdac_shift;
}

static void hal_codec_set_dig_dac_gain(uint32_t reg_base, int32_t gain)
{
    uint32_t val;
    int32_t remains;

    if (gain >= MAX_DIG_DAC_DBVAL) {
        val = MAX_DIG_DAC_REGVAL;
        algdac_shift = 0;
    } else if (gain <= MIN_DIG_DAC_DBVAL) {
        val = MIN_DIG_DAC_REGVAL;
        // Keep the original algdac_shift
    } else {
        remains = MAX_DIG_DAC_REGVAL - (MAX_DIG_DAC_DBVAL - gain);
        if (remains > 0) {
            val = remains;
            algdac_shift = 0;
        } else {
            val = -remains;
            algdac_shift = val / ALG_DAC_DBVAL_PER_SHIFT + 1;
            val = val % ALG_DAC_DBVAL_PER_SHIFT;
            val = ALG_DAC_DBVAL_PER_SHIFT - val;
        }
    }

    //TRACE("hal_codec: set digdac gain=%d shift=%u val=%u", gain, algdac_shift, val);

    codecipbus_w_sdac_lvolume_sel(reg_base, val);
    if(hal_get_chip_metal_id() >= HAL_CHIP_METAL_ID_2) {
        codecipbus_w_sdac_rvolume_sel(reg_base, val);
    }
}

#ifdef AUDIO_SW_OUTPUT_GAIN
static void hal_codec_set_sw_gain(int32_t gain)
{
    float coef;

    if (sw_output_coef_callback == NULL) {
        return;
    }

    if (gain >= MAX_DIG_DAC_DBVAL) {
        coef = 1;
    } else if (gain <= MIN_DIG_DAC_DBVAL) {
        coef = 0;
    } else {
#if !defined(NOSTD) && defined(HAL_CODEC_ACCURATE_SW_GAIN)
        // The math lib will consume 4K+ bytes of space
        coef = (float)pow(10, (float)gain / 20);
#else
        // Resolution: 3dB
        const float factor_m3db = 0.70794578;
        uint32_t cnt;
        int i;

        coef = 1;

        if (gain < 0) {
            cnt = (-gain + 1) / 3;
            for (i = 0; i < cnt; i++) {
                coef *= factor_m3db;
            }
        } else if (gain > 0) {
            cnt = (gain + 1) / 3;
            for (i = 0; i < cnt; i++) {
                coef /= factor_m3db;
            }
        }
#endif
    }

    sw_output_coef_callback(coef);
}

void hal_codec_set_sw_output_coef_callback(HAL_CODEC_SW_OUTPUT_COEF_CALLBACK callback)
{
    sw_output_coef_callback = callback;
}
#endif

int hal_codec_setup_stream(enum HAL_CODEC_ID_T id, enum AUD_STREAM_T stream, struct HAL_CODEC_CONFIG_T *cfg)
{
    uint32_t reg_base;

    reg_base = _codec_get_reg_base(id);

    if (stream == AUD_STREAM_PLAYBACK)
    {
        //set playback channel num
        //Note: best1000 channel num is 2
        if(HAL_CODEC_CONFIG_CHANNEL_NUM & cfg->set_flag)
        {
            if (cfg->channel_num == AUD_CHANNEL_NUM_2)
            {
                codecipbus_w_dual_dac_en(reg_base, HAL_CODEC_YES);  //select dual channel dac
                codecip_w_dual_dac_en(reg_base, HAL_CODEC_YES);     //enable dual channel dac
            }
            else
            {
                codecipbus_w_dual_dac_en(reg_base, HAL_CODEC_NO);   //select signal channel dac
                codecip_w_dual_dac_en(reg_base, HAL_CODEC_NO);      //disable dual channel dac
            }
        }

        //set playback bits
        //Note: best1000 just use AUD_BITS_16
        if(HAL_CODEC_CONFIG_BITS & cfg->set_flag)
        {
            if (cfg->bits == AUD_BITS_16)
            {
                codecipbus_w_dac_16bit_en(reg_base, HAL_CODEC_YES);
            }
            else
            {
                codecipbus_w_dac_16bit_en(reg_base, HAL_CODEC_NO);
            }
        }

        //set playback sample rate
        if(HAL_CODEC_CONFIG_SMAPLE_RATE & cfg->set_flag)
        {
            uint8_t i;
            for(i=0;i<ARRAY_SIZE(codec_dac_sample_rate);i++)
            {
                if(codec_dac_sample_rate[i].sample_rate == cfg->sample_rate)
                {
                    break;
                }
            }
            ASSERT(i < ARRAY_SIZE(codec_dac_sample_rate), "[%s] Invalid playback sample rate: %d", __func__, cfg->sample_rate);
            
            HAL_CODEC_TRACE("sample_rate = %d div:%d osr:%d dac_up:%d", cfg->sample_rate,
                                                                        codec_dac_sample_rate[i].cmu_div,
                                                                        codec_dac_sample_rate[i].sdac_osr,
                                                                        codec_dac_sample_rate[i].dac_up);
            hal_cmu_codec_dac_set_div(codec_dac_sample_rate[i].cmu_div);
            analog_aud_freq_pll_config(codec_dac_sample_rate[i].base_freq, codec_dac_sample_rate[i].cmu_div);
            codecipbus_w_sdac_osr_sel(reg_base, codec_dac_sample_rate[i].sdac_osr);
            codecipbus_w_dac_up_sel(reg_base, codec_dac_sample_rate[i].dac_up);

            hal_cmu_anc_enable(HAL_CMU_ANC_CLK_USER_ANC);
            hal_cmu_codec_clock_enable();
            hal_cmu_reset_pulse(HAL_CMU_MOD_O_CODEC_DA);
        }

        //set playback volume
        if(HAL_CODEC_CONFIG_VOL & cfg->set_flag)
        {
            if(cfg->vol < CODEC_DAC_VOL_LEVEL_NUM)
            {
                struct  CODEC_DAC_VOL_T *vol_tab_ptr = NULL;
                hal_codec_get_dac_volume_tab(&vol_tab_ptr);
                vol_tab_ptr += cfg->vol;
#ifdef AUDIO_SW_OUTPUT_GAIN
                hal_codec_set_sw_gain(vol_tab_ptr->sdac_volume);
#else
                analog_codec_tx_pa_gain_sel(vol_tab_ptr->tx_pa_gain);
                codecipbus_w_sdm_gain_sel(reg_base, vol_tab_ptr->sdm_gain);
                hal_codec_set_dig_dac_gain(reg_base, vol_tab_ptr->sdac_volume);
#endif
            }
        }

        //enable or disable playback dma
        if(HAL_CODEC_CONFIG_USE_DMA & cfg->set_flag)
        {
            codecipbus_w_tx_fifo_threshold(reg_base, 3);    /* fifo level to trigger empty interrupt or dma req */
            codecipbus_w_enable_tx_dma(reg_base, HAL_CODEC_YES);    /* dma config */
        }
    }
    else {

        //set capture channel num
        if(HAL_CODEC_CONFIG_CHANNEL_NUM & cfg->set_flag)
        {
            if (cfg->channel_num == AUD_CHANNEL_NUM_2)
            {
                codecipbus_w_dual_mic_en(reg_base, HAL_CODEC_YES);  //select dual channel adc
                codecip_w_dual_mic_en(reg_base, HAL_CODEC_YES);     //enable dual channel adc
            }
            else
            {
                codecipbus_w_dual_mic_en(reg_base, HAL_CODEC_NO);
                codecip_w_dual_mic_en(reg_base, HAL_CODEC_NO);
            }
        }

        //select capture input path
        if(HAL_CODEC_CONFIG_IO_PATH & cfg->set_flag)
        {
            if (cfg->io_path == AUD_INPUT_PATH_DIGMIC){
                codecipbus_w_pdm_en(reg_base, HAL_CODEC_YES);
                codecipbus_w_adc_ch0_sel_pdm_path(reg_base, HAL_CODEC_YES);
                codecipbus_w_adc_ch1_sel_pdm_path(reg_base, HAL_CODEC_YES);
            }else{
                int dev;
                dev = analog_aud_input_cfg(cfg->io_path);

                codecipbus_w_pdm_en(reg_base, HAL_CODEC_NO);
                codecipbus_w_adc_ch0_sel_pdm_path(reg_base, HAL_CODEC_NO);
                codecipbus_w_adc_ch1_sel_pdm_path(reg_base, HAL_CODEC_NO);
                if (dev & ANALOG_AUD_ADC_B){
                    codecip_w_adc_order_swap(reg_base, HAL_CODEC_YES);
                }else{
                    codecip_w_adc_order_swap(reg_base, HAL_CODEC_NO);
                }
            }
        }
        
        //set capture sample rate
        if(HAL_CODEC_CONFIG_SMAPLE_RATE & cfg->set_flag)
        {
            uint8_t i;
            for(i=0;i<ARRAY_SIZE(codec_adc_sample_rate);i++)
            {
                if(codec_adc_sample_rate[i].sample_rate == cfg->sample_rate)
                {
                    break;
                }
            }
            ASSERT(i < ARRAY_SIZE(codec_adc_sample_rate), "[%s] Invalid capture sample rate: %d", __func__, cfg->sample_rate);
       
            HAL_CODEC_TRACE("sample_rate = %d div:%d adc_down:%d", cfg->sample_rate,
                                                                   codec_adc_sample_rate[i].cmu_div,
                                                                   codec_adc_sample_rate[i].adc_down);
            hal_cmu_codec_adc_set_div(codec_adc_sample_rate[i].cmu_div);
            analog_aud_freq_pll_config(codec_adc_sample_rate[i].base_freq, codec_adc_sample_rate[i].cmu_div);
            codecip_w_adc_down_sel(reg_base, codec_adc_sample_rate[i].adc_down);

            hal_cmu_anc_enable(HAL_CMU_ANC_CLK_USER_ANC);
            hal_cmu_codec_clock_enable();

            hal_cmu_reset_pulse(HAL_CMU_MOD_O_CODEC_AD);
        }

        //set capture volume
        if(HAL_CODEC_CONFIG_VOL & cfg->set_flag)
        {
            if(cfg->io_path == AUD_INPUT_PATH_LINEIN){
                codecipbus_w_sadc_vol_ch0_sel(reg_base, ANALOG_LINEIN_CODEC_SADC_VOL);
                codecipbus_w_sadc_vol_ch1_sel(reg_base, ANALOG_LINEIN_CODEC_SADC_VOL);
            }else{
                codecipbus_w_sadc_vol_ch0_sel(reg_base, CODEC_SADC_VOL);
                codecipbus_w_sadc_vol_ch1_sel(reg_base, CODEC_SADC_VOL);
            } 
        }

        //enable or disable capture dma
        if(HAL_CODEC_CONFIG_USE_DMA & cfg->set_flag)
        {
            codecipbus_w_rx_fifo_threshold(reg_base, HAL_CODEC_RX_FIFO_TRIGGER_LEVEL);
            codecipbus_w_enable_rx_dma(reg_base, HAL_CODEC_YES);
        }
    }

    return reg_base;
}

int hal_codec_sdm_gain_set(enum HAL_CODEC_ID_T id, uint32_t gain)
{
    uint32_t reg_base = 0;
    reg_base = _codec_get_reg_base(id);
    codecipbus_w_sdm_gain_sel(reg_base, gain);
    return reg_base;
}

int hal_codec_send(enum HAL_CODEC_ID_T id, uint8_t *value, uint32_t value_len)
{
    uint32_t reg_base;

    reg_base = _codec_get_reg_base(id);

    return reg_base;
}
uint8_t hal_codec_recv(enum HAL_CODEC_ID_T id, uint8_t *value, uint32_t value_len)
{
    uint32_t reg_base;

    reg_base = _codec_get_reg_base(id);

    return reg_base;
}

uint32_t hal_codec_get_dac_osr(enum HAL_CODEC_ID_T id)
{
    uint32_t sel;
    uint32_t reg_base;
    reg_base = _codec_get_reg_base(id);

    sel = codecipbus_r_sdac_osr_sel(reg_base);
    if (sel == 0) {
        return 128;
    } else if (sel == 1) {
        return 256;
    } else {
        return 512;
    }
}

uint32_t hal_codec_get_dac_up(enum HAL_CODEC_ID_T id)
{
    uint32_t sel;
    uint32_t reg_base;
    reg_base = _codec_get_reg_base(id);

    sel = codecipbus_r_dac_up_sel(reg_base);
    if (sel == 0) {
        return 2;
    } else if (sel == 1) {
        return 3;
    } else if (sel == 2) {
        return 4;
    } else if (sel == 3) {
        return 6;
    } else {
        return 1;
    }
}

uint32_t hal_codec_get_adc_down(enum HAL_CODEC_ID_T id)
{
    uint32_t sel;
    uint32_t reg_base;
    reg_base = _codec_get_reg_base(id);

    sel = codecip_r_adc_down_sel(reg_base);
    if (sel == 0) {
        return 3;
    } else if (sel == 1) {
        return 6;
    } else {
        return 1;
    }
}

int hal_codec_da_chain_set(enum HAL_CODEC_ID_T id, uint8_t sdm_gain, uint8_t sdac_val)
{
    uint32_t reg_base;
    reg_base = _codec_get_reg_base(id);
    
    codecipbus_w_sdm_gain_sel(reg_base, sdm_gain);
    codecipbus_w_sdac_lvolume_sel(reg_base, sdac_val);
    if(hal_get_chip_metal_id() >= HAL_CHIP_METAL_ID_2) {
        codecipbus_w_sdac_rvolume_sel(reg_base, sdac_val);
    }

    return 0;
}

int hal_codec_ad_chain_set(enum HAL_CODEC_ID_T id, uint8_t sadc_vol)
{
    uint32_t reg_base;
    reg_base = _codec_get_reg_base(id);

    codecipbus_w_sadc_vol_ch0_sel(reg_base, sadc_vol);
    codecipbus_w_sadc_vol_ch1_sel(reg_base, sadc_vol);
    
    return 0;
}

