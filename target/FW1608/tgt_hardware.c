#include "cmsis_os.h"
#include "tgt_hardware.h"
#include "hal_iomux.h"
#include "hal_gpio.h"
#include "hal_key.h"

const struct HAL_IOMUX_PIN_FUNCTION_MAP cfg_hw_pinmux_pwl[CFG_HW_PLW_NUM] = {
    {HAL_IOMUX_PIN_P1_6, HAL_IOMUX_FUNC_AS_GPIO, HAL_IOMUX_PIN_VOLTAGE_VIO, HAL_IOMUX_PIN_PULLUP_ENALBE},//white
    {HAL_IOMUX_PIN_P1_7, HAL_IOMUX_FUNC_AS_GPIO, HAL_IOMUX_PIN_VOLTAGE_VIO, HAL_IOMUX_PIN_PULLUP_ENALBE},//blue
    {HAL_IOMUX_PIN_P1_5, HAL_IOMUX_FUNC_AS_GPIO, HAL_IOMUX_PIN_VOLTAGE_VIO, HAL_IOMUX_PIN_PULLUP_ENALBE},//green
};

//adckey define
const uint16_t CFG_HW_ADCKEY_MAP_TABLE[CFG_HW_ADCKEY_NUMBER] = {
    HAL_KEY_CODE_FN9,HAL_KEY_CODE_FN8,HAL_KEY_CODE_FN7,
    HAL_KEY_CODE_FN6,HAL_KEY_CODE_FN5,HAL_KEY_CODE_FN4,
    HAL_KEY_CODE_FN3,HAL_KEY_CODE_FN2,HAL_KEY_CODE_FN1
};

//gpiokey define
#define CFG_HW_GPIOKEY_DOWN_LEVEL          (0)
#define CFG_HW_GPIOKEY_UP_LEVEL            (1)
const struct HAL_KEY_GPIOKEY_CFG_T cfg_hw_gpio_key_cfg[CFG_HW_GPIOKEY_NUM] = {
    {HAL_KEY_CODE_FN1,{HAL_IOMUX_PIN_P3_4, HAL_IOMUX_FUNC_AS_GPIO, HAL_IOMUX_PIN_VOLTAGE_VIO, HAL_IOMUX_PIN_PULLUP_ENALBE}},
    {HAL_KEY_CODE_FN2,{HAL_IOMUX_PIN_P3_5, HAL_IOMUX_FUNC_AS_GPIO, HAL_IOMUX_PIN_VOLTAGE_VIO, HAL_IOMUX_PIN_PULLUP_ENALBE}},
    
};






//bt config
const char *BT_LOCAL_NAME = "WT_BOX_EP\0";
uint8_t ble_addr[6] = {0xBE,0x99,0x34,0x45,0x56,0x67};
uint8_t bt_addr[6] = {0x1e,0x57,0x34,0x45,0x56,0x67};

//audio config
//freq bands range {[0k:2.5K], [2.5k:5K], [5k:7.5K], [7.5K:10K], [10K:12.5K], [12.5K:15K], [15K:17.5K], [17.5K:20K]}
//gain range -12~+12
const int8_t cfg_aud_eq_sbc_band_settings[CFG_HW_AUD_EQ_NUM_BANDS] = {0, 0, 0, 0, 0, 0, 0, 0};

/* MAX GAIN 16*0.75+03*0+21*1=0db */
/* 16ohm Vpp>=800mV */
const struct  CODEC_DAC_VOL_T codec_dac_vol[CODEC_DAC_VOL_LEVEL_NUM]={
    {TX_PA_GAIN, 3,-21}, /* -21db  VOL_WARNINGTONE */
    {TX_PA_GAIN, 3,-100}, /* -25db  VOL_LEVEL_MUTE  */
    {TX_PA_GAIN, 3,-45}, /* -25db  VOL_LEVEL_0  */
    {TX_PA_GAIN, 3,-42}, /* -42db  VOL_LEVEL_1  */
    {TX_PA_GAIN, 3,-39}, /* -39db  VOL_LEVEL_2  */
    {TX_PA_GAIN, 3,-36}, /* -36db  VOL_LEVEL_3  */
    {TX_PA_GAIN, 3,-33}, /* -33db  VOL_LEVEL_4  */
    {TX_PA_GAIN, 3,-30}, /* -30db  VOL_LEVEL_5  */
    {TX_PA_GAIN, 3,-27}, /* -27db  VOL_LEVEL_6  */
    {TX_PA_GAIN, 3,-24}, /* -24db  VOL_LEVEL_7  */   
    {TX_PA_GAIN, 3,-21}, /* -21db  VOL_LEVEL_8  */
    {TX_PA_GAIN, 3,-18}, /* -18db  VOL_LEVEL_9  */
    {TX_PA_GAIN, 3,-15}, /* -15db  VOL_LEVEL_10*/
    {TX_PA_GAIN, 3,-12}, /* -12db    VOL_LEVEL_11*/
    {TX_PA_GAIN, 3, -9}, /* -9db    VOL_LEVEL_12*/
    {TX_PA_GAIN, 3, -6}, /* -6db    VOL_LEVEL_13 */
    {TX_PA_GAIN, 3, -3}, /* -3db    VOL_LEVEL_14 */
    {TX_PA_GAIN, 3,  0}, /*  0db     VOL_LEVEL_15 */
};

const struct HAL_IOMUX_PIN_FUNCTION_MAP app_battery_ext_charger_detecter_cfg = {
    HAL_IOMUX_PIN_NUM, HAL_IOMUX_FUNC_AS_GPIO, HAL_IOMUX_PIN_VOLTAGE_VIO, HAL_IOMUX_PIN_PULLUP_ENALBE
};

const struct HAL_IOMUX_PIN_FUNCTION_MAP app_battery_ext_charger_indicator_cfg = {
    HAL_IOMUX_PIN_NUM, HAL_IOMUX_FUNC_AS_GPIO, HAL_IOMUX_PIN_VOLTAGE_VIO, HAL_IOMUX_PIN_PULLUP_ENALBE
};
	
const struct HAL_IOMUX_PIN_FUNCTION_MAP app_line_in_detect_cfg = {
    HAL_IOMUX_PIN_P2_3, HAL_IOMUX_FUNC_AS_GPIO, HAL_IOMUX_PIN_VOLTAGE_VIO, HAL_IOMUX_PIN_PULLUP_ENALBE
};	

const struct HAL_IOMUX_PIN_FUNCTION_MAP app_line_out_detect_cfg = {
    HAL_IOMUX_PIN_P2_2, HAL_IOMUX_FUNC_AS_GPIO, HAL_IOMUX_PIN_VOLTAGE_VIO, HAL_IOMUX_PIN_PULLUP_ENALBE
};

