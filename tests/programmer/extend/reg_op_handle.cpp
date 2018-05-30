#include "stdint.h"
#include "stdbool.h"
#include "plat_types.h"
#include "string.h"
#include "nv_factory_section.h"
#include "bt_drv_interface.h"
#include "hal_trace.h"
#include "hal_cache.h"
#include "hal_analogif.h"
#include "extend_cmd_handle.h"
#include "reg_op_handle.h"


enum ERR_CODE reg_op_handle_cmd(enum REG_OP_TYPE cmd, unsigned char *param, unsigned int len)
{
    uint32_t reg_reg;   
    uint16_t reg_val_16;
    uint32_t reg_val_32;
    uint8_t cret[5];
    cret[0] = ERR_NONE;

    switch (cmd) {
        case REG_OP_WRITE: {
            EXTEND_CMD_BUFF_TO_U32(param, reg_reg);            
            EXTEND_CMD_BUFF_TO_U32(param+4, reg_val_32);
            TRACE("REG_OP_WRITE %x=%x",reg_reg,reg_val_32);
            if (reg_reg>0xffff){                
                *(volatile unsigned int *)(reg_reg) = reg_val_32;
            }else{              
                hal_analogif_reg_write(reg_reg, reg_val_32);
            }
            extend_cmd_send_reply(cret,1);
            break;
        }
        case REG_OP_READ: {         
            EXTEND_CMD_BUFF_TO_U32(param, reg_reg);
            if (reg_reg>0xffff){                
                reg_val_32 = *(volatile unsigned int *)(reg_reg);

            }else{
                hal_analogif_reg_read(reg_reg, &reg_val_16);
                reg_val_32 = reg_val_16;
            }
            EXTEND_CMD_U32_TO_BUFF(&cret[1],reg_val_32);   
            TRACE("REG_OP_READ %x=%x",reg_reg,reg_val_32);
            extend_cmd_send_reply(cret, 5);
            break;
        }
        default: {
            TRACE("Invalid command: 0x%x", cmd);
            return ERR_INTERNAL;
        }
    }   
    return ERR_NONE;
}

