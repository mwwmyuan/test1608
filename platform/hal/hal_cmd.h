#ifndef __HAL_CMD_H__
#define __HAL_CMD_H__

#include "stdint.h"

// typedef enum {
//         HAL_CMD_ERR_NONE = 0x00,
//         HAL_CMD_ERR_LEN = 0x01,
//         HAL_CMD_ERR_CHECKSUM = 0x02,
//         HAL_CMD_ERR_NOT_SYNC = 0x03,

//         HAL_CMD_ERR_CMD = 0x06,

//         HAL_CMD_ERR_BURN_OK = 0x60,
//         HAL_CMD_ERR_SECTOR_SIZE = 0x61,

//         HAL_CMD_ERR_BURN_INFO_MISSING = 0x63,
//         HAL_CMD_ERR_SECTOR_DATA_LEN = 0x64,
//         HAL_CMD_ERR_SECTOR_DATA_CRC = 0x65,
//         HAL_CMD_ERR_SECTOR_SEQ = 0x66,
//         HAL_CMD_ERR_ERASE_FLSH = 0x67,
//         HAL_CMD_ERR_BURN_FLSH = 0x68,
// } HAL_CMD_ERR_T;

// typedef enum {
//         HAL_CMD_GET_PARA = 0x00,
//         HAL_CMD_SET_PARA,
//         HAL_CMD_SAVE_PARA,
//         HAL_CMD_PUSH_PARA,

//         HAL_CMD_PLAYBACK_SWITCH = 0x08,
//         HAL_CMD_CAPTURE_SWITCH,

//         HAL_CMD_BURN_HANDSHAKE = 0x10,
//         HAL_CMD_BURN_START,
//         HAL_CMD_BURN_DATA,

//         HAL_CMD_HANDSHAKE = 0x18,
//         HAL_CMD_SHUTDOWN,
//         HAL_CMD_REBOOT,
//         HAL_CMD_NOTIFICATION,

//         HAL_CMD_INVALID = 0xff	
// } EL_CMD_E;

// typedef enum {
//         HAL_CMD_PARA_SOUND_VOLUME,
//         HAL_CMD_PARA_LC_SWITCH,
//         HAL_CMD_PARA_EQ_GAIN,
//         HAL_CMD_PARA_ANC_SWITCH,
//         HAL_CMD_PARA_ANC_GAIN,

//         HAL_CMD_PARA_INVALID
// } EL_PARA_E;

typedef int (*hal_cmd_callback_t)(uint8_t *buf, uint32_t  len);

int hal_cmd_init (void);
int hal_cmd_open (void);
int hal_cmd_close (void);
int hal_cmd_run (void);

int hal_cmd_register(char *name, hal_cmd_callback_t callback);

// hal_cmd_t *hal_cmd_get_ptr(void);

#endif