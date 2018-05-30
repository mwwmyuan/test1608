#ifndef __CFG_HARDWARE__
#define __CFG_HARDWARE__
#include "hal_iomux.h"
#include "hal_gpio.h"
#include "hal_key.h"
#include "hal_aud.h"

#ifdef __cplusplus
extern "C" {
#endif

//pwl
#define CFG_HW_PLW_NUM (3)
extern const struct HAL_IOMUX_PIN_FUNCTION_MAP cfg_hw_pinmux_pwl[CFG_HW_PLW_NUM];

//adckey define
#define CFG_HW_ADCKEY_NUMBER 9
#define CFG_HW_ADCKEY_BASE 0
#define CFG_HW_ADCKEY_ADC_MAXVOLT 1000
#define CFG_HW_ADCKEY_ADC_MINVOLT 0
#define CFG_HW_ADCKEY_ADC_KEYVOLT_BASE 130
extern const uint16_t CFG_HW_ADCKEY_MAP_TABLE[CFG_HW_ADCKEY_NUMBER];

#define BTA_AV_CO_SBC_MAX_BITPOOL  53

//gpiokey define
#define CFG_HW_GPIOKEY_NUM (2)
#define CFG_HW_GPIOKEY_DOWN_LEVEL          (0)
#define CFG_HW_GPIOKEY_UP_LEVEL            (1)
extern const struct HAL_KEY_GPIOKEY_CFG_T cfg_hw_gpio_key_cfg[CFG_HW_GPIOKEY_NUM];

// audio codec
#define CFG_HW_AUD_INPUT_PATH_MAINMIC_DEV (ANALOG_AUD_ADC_A)
#define CFG_HW_AUD_INPUT_PATH_HP_MIC_DEV (ANALOG_AUD_ADC_A | ANALOG_AUD_ADC_B)
#define CFG_HW_AUD_OUTPUT_PATH_SPEAKER_DEV (ANALOG_AUD_LDAC | ANALOG_AUD_RDAC)

//bt config
extern const char *BT_LOCAL_NAME;
extern uint8_t ble_addr[6];
extern uint8_t bt_addr[6];

//mic
//step 6db  rate: 0~0x7
#define ANALOG_GA1A_GAIN (1)
//step 0.75db rate: 0~0x7
#define ANALOG_GA2A_GAIN (1)
//step 2db 0db = 7 rate: 0~0xf
#define CODEC_SADC_VOL (3)


//speaker
/* MAX GAIN TX_PA_GAIN*0.75+03*0+MAX_DIG_DAC_REGVAL*1=0db */
/* 16*0.75+03*0+21*1=0db  16ohm Vpp>=800mV*/
//#define TX_PA_GAIN 16
//#define MAX_DIG_DAC_REGVAL (21)
#define TX_PA_GAIN 15
#define MAX_DIG_DAC_REGVAL (21)

 
#define CODEC_DAC_VOL_LEVEL_NUM (TGT_VOLUME_LEVEL_QTY)
extern const struct CODEC_DAC_VOL_T codec_dac_vol[CODEC_DAC_VOL_LEVEL_NUM];

//range -12~+12
#define CFG_HW_AUD_EQ_NUM_BANDS (8)
extern const int8_t cfg_aud_eq_sbc_band_settings[CFG_HW_AUD_EQ_NUM_BANDS];
#define CFG_AUD_EQ_IIR_NUM_BANDS (4)

//battery info
#define APP_BATTERY_MIN_MV (3200)
#define APP_BATTERY_PD_MV   (3100)

#define APP_BATTERY_MAX_MV (4200)

extern const struct HAL_IOMUX_PIN_FUNCTION_MAP app_battery_ext_charger_detecter_cfg;
extern const struct HAL_IOMUX_PIN_FUNCTION_MAP app_battery_ext_charger_indicator_cfg;

#ifdef __cplusplus
}
#endif

#endif

