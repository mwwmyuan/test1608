#ifndef __VOICE_OVER_BLE_H__
#define __VOICE_OVER_BLE_H__


/**
 * @brief The role of the voice over BLE
 *
 */
typedef enum
{
    /**< SRC, which collects the voice via the mic, 
        encodes it and sends the encoded audio data to the DST */
    VOB_ROLE_SRC = 0,   
    /**< DST, which receives the encoded audio data, 
        decodes it and plays back it */
    VOB_ROLE_DST        
} VOB_ROLE_E;

/**
 * @brief The message id of key pressed event
 *
 */
typedef enum
{
    /**< Change the role to SRC, which collects the voice via the mic, 
        encodes it and sends the encoded audio data to the DST */
    VOB_START_AS_SRC = 0, 

	/**< connected as SRC -> Start voice stream -> Stop voice stream */
	/**< connected as DST -> Stop voice stream */
    VOB_SWITCH_STATE,    

} VOB_MESSAGE_ID_E;

void notify_vob(VOB_MESSAGE_ID_E message, uint8_t* ptrParam, uint32_t paramLen);
void voice_over_ble_init(void);
int ble_app_init(void);

#endif // #ifndef __VOICE_OVER_BLE_H__

