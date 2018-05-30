/**
 ****************************************************************************************
 *
 * @file appm_task.c
 *
 * @brief RW APP Task implementation
 *
 * Copyright (C) RivieraWaves 2009-2015
 *
 *
 ****************************************************************************************
 */

/**
 ****************************************************************************************
 * @addtogroup APPTASK
 * @{
 ****************************************************************************************
 */

/*
 * INCLUDE FILES
 ****************************************************************************************
 */

#include "rwip_config.h"          // SW configuration

#if (BLE_APP_PRESENT)

#include "app_task.h"             // Application Manager Task API
#include "app.h"                  // Application Manager Definition
#include "gapc_task.h"            // GAP Controller Task API
#include "gapm_task.h"            // GAP Manager Task API
#include "arch.h"                 // Platform Definitions
#include <string.h>
#include "ke_timer.h"             // Kernel timer

#include "co_utils.h"
#if (BLE_APP_SEC)
#include "app_sec.h"              // Security Module Definition
#endif //(BLE_APP_SEC)

#if (BLE_APP_HT)
#include "app_ht.h"               // Health Thermometer Module Definition
#include "htpt_task.h"
#endif //(BLE_APP_HT)

#if (BLE_APP_DIS)
#include "app_dis.h"              // Device Information Module Definition
#include "diss_task.h"
#endif //(BLE_APP_DIS)

#if (BLE_APP_BATT)
#include "app_batt.h"             // Battery Module Definition
#include "bass_task.h"
#endif //(BLE_APP_BATT)

#if (BLE_APP_HID)
#include "app_hid.h"              // HID Module Definition
#include "hogpd_task.h"
#endif //(BLE_APP_HID)

#if (BLE_APP_HR)
#include "app_hrps.h"
#endif

#if (BLE_APP_DATAPATH_SERVER)
#include "app_datapath_server.h"       // Data Path Server Module Definition
#include "datapathps_task.h"
#endif // (BLE_APP_DATAPATH_SERVER)

#ifdef BLE_APP_AM0
#include "am0_app.h"             // Audio Mode 0 Application
#endif //defined(BLE_APP_AM0)

#if (DISPLAY_SUPPORT)
#include "app_display.h"          // Application Display Definition
#endif //(DISPLAY_SUPPORT)

/*
 * LOCAL FUNCTION DEFINITIONS
 ****************************************************************************************
 */

#define APP_CONN_PARAM_INTERVEL_MIN    (20)

uint8_t ble_stack_ready = 0;

static uint8_t appm_get_handler(const struct ke_state_handler *handler_list,
                                ke_msg_id_t msgid,
                                void *param,
                                ke_task_id_t src_id)
{
    // Counter
    uint8_t counter;

    // Get the message handler function by parsing the message table
    for (counter = handler_list->msg_cnt; 0 < counter; counter--)
    {
        struct ke_msg_handler handler = (struct ke_msg_handler)(*(handler_list->msg_table + counter - 1));

        if ((handler.id == msgid) ||
            (handler.id == KE_MSG_DEFAULT_HANDLER))
        {
            // If handler is NULL, message should not have been received in this state
            ASSERT_ERR(handler.func);

            return (uint8_t)(handler.func(msgid, param, TASK_APP, src_id));
        }
    }

    // If we are here no handler has been found, drop the message
    return (KE_MSG_CONSUMED);
}

/*
 * MESSAGE HANDLERS
 ****************************************************************************************
 */

/**
 ****************************************************************************************
 * @brief
 *
 * @param[in] msgid     Id of the message received.
 * @param[in] param     Pointer to the parameters of the message.
 * @param[in] dest_id   ID of the receiving task instance (TASK_GAP).
 * @param[in] src_id    ID of the sending task instance.
 *
 * @return If the message was consumed or not.
 ****************************************************************************************
 */
static int app_adv_timeout_handler(ke_msg_id_t const msgid,
                                   void const *param,
                                   ke_task_id_t const dest_id,
                                   ke_task_id_t const src_id)
{
    #if (BLE_APP_HID)
    #else
    // Stop advertising
    appm_stop_advertising();
    #endif

    return (KE_MSG_CONSUMED);
}

/**
 ****************************************************************************************
 * @brief Handles ready indication from the GAP. - Reset the stack
 *
 * @param[in] msgid     Id of the message received.
 * @param[in] param     Pointer to the parameters of the message.
 * @param[in] dest_id   ID of the receiving task instance (TASK_GAP).
 * @param[in] src_id    ID of the sending task instance.
 *
 * @return If the message was consumed or not.
 ****************************************************************************************
 */
static int gapm_device_ready_ind_handler(ke_msg_id_t const msgid,
                                         void const *param,
                                         ke_task_id_t const dest_id,
                                         ke_task_id_t const src_id)
{
    BLE_APP_FUNC_ENTER();

    // Application has not been initialized
    ASSERT_ERR(ke_state_get(dest_id) == APPM_INIT);

    ble_stack_ready = 1;

    // Reset the stack
    struct gapm_reset_cmd* cmd = KE_MSG_ALLOC(GAPM_RESET_CMD,
                                              TASK_GAPM, TASK_APP,
                                              gapm_reset_cmd);

    cmd->operation = GAPM_RESET;

    ke_msg_send(cmd);

    BLE_APP_FUNC_LEAVE();

    return (KE_MSG_CONSUMED);
}

void set_config(void)
{
    BLE_APP_FUNC_ENTER();

    // Set Device configuration
    struct gapm_set_dev_config_cmd* cmd = KE_MSG_ALLOC(GAPM_SET_DEV_CONFIG_CMD,
                                                       TASK_GAPM, TASK_APP,
                                                       gapm_set_dev_config_cmd);
    // Set the operation
    cmd->operation = GAPM_SET_DEV_CONFIG;
    // Set the device role - Peripheral
    cmd->role	   = GAP_ROLE_PERIPHERAL;
    // Set Data length parameters
    cmd->sugg_max_tx_octets = 400;//BLE_MIN_OCTETS;
    cmd->sugg_max_tx_time	= 400;//BLE_MIN_TIME;
    // Do not support secure connections
    cmd->pairing_mode = GAPM_PAIRING_LEGACY;
#if (BLE_APP_HID)
    // Enable Slave Preferred Connection Parameters present 
    cmd->att_cfg = GAPM_MASK_ATT_SLV_PREF_CON_PAR_EN;
#endif //(BLE_APP_HID)

#ifdef BLE_APP_AM0
    cmd->addr_type	 = GAPM_CFG_ADDR_HOST_PRIVACY;
    cmd->audio_cfg	 = GAPM_MASK_AUDIO_AM0_SUP;
#endif // BLE_APP_AM0
    cmd->api_settings = GAP_ADD_INFO_USE_DEPRECATED_API_BIT;
    // load IRK
    memcpy(cmd->irk.key, app_env.loc_irk, KEY_LEN);

    // Send message
    ke_msg_send(cmd);

    BLE_APP_FUNC_LEAVE();
}
/**
 ****************************************************************************************
 * @brief Handles GAP manager command complete events.
 *
 * @param[in] msgid     Id of the message received.
 * @param[in] param     Pointer to the parameters of the message.
 * @param[in] dest_id   ID of the receiving task instance (TASK_GAP).
 * @param[in] src_id    ID of the sending task instance.
 *
 * @return If the message was consumed or not.
 ****************************************************************************************
 */
static int gapm_cmp_evt_handler(ke_msg_id_t const msgid,
                                struct gapm_cmp_evt const *param,
                                ke_task_id_t const dest_id,
                                ke_task_id_t const src_id)
{
    BLE_APP_FUNC_ENTER();

    BLE_APP_DBG("param->operation: %d status is %d app_env.next_svc is %d", 
		param->operation, param->status, app_env.next_svc);

    switch(param->operation)
    {
        // Reset completed
        case (GAPM_RESET):
        {
            if(param->status == GAP_ERR_NO_ERROR)
            {
                #if (BLE_APP_HID)
                app_hid_start_mouse();
                #endif //(BLE_APP_HID)

                // Set Device configuration
                struct gapm_set_dev_config_cmd* cmd = KE_MSG_ALLOC(GAPM_SET_DEV_CONFIG_CMD,
                                                                   TASK_GAPM, TASK_APP,
                                                                   gapm_set_dev_config_cmd);
                // Set the operation
                cmd->operation = GAPM_SET_DEV_CONFIG;
                // Set the device role - Peripheral
                cmd->role      = GAP_ROLE_ALL;
                // Set Data length parameters
                cmd->sugg_max_tx_octets = 35;//BLE_MIN_OCTETS;
                cmd->sugg_max_tx_time   = 400;//BLE_MIN_TIME;
                // Do not support secure connections
                cmd->pairing_mode = GAPM_PAIRING_LEGACY;
				cmd->max_mtu = 512;
				cmd->att_cfg = 0;
				cmd->att_cfg |= GAPM_MASK_ATT_SVC_CHG_EN;
                #if (BLE_APP_HID)
                // Enable Slave Preferred Connection Parameters present 
                cmd->att_cfg |= GAPM_MASK_ATT_SLV_PREF_CON_PAR_EN;
                #endif //(BLE_APP_HID)

                #ifdef BLE_APP_AM0
                cmd->addr_type   = GAPM_CFG_ADDR_HOST_PRIVACY;
                cmd->audio_cfg   = GAPM_MASK_AUDIO_AM0_SUP;
                #endif // BLE_APP_AM0

                // load IRK
                memcpy(cmd->irk.key, app_env.loc_irk, KEY_LEN);

                // Send message
                ke_msg_send(cmd);
            }
            else
            {
                ASSERT_ERR(0);
            }
        }
        break;
        case (GAPM_PROFILE_TASK_ADD):
        {
           // ASSERT_INFO(param->status == GAP_ERR_NO_ERROR, param->operation, param->status);
            // Add the next requested service
            if (!appm_add_svc())
            {
                // Go to the ready state
                ke_state_set(TASK_APP, APPM_READY);

                // No more service to add, start advertising
                appm_start_advertising();
            }
        }
        break;
        // Device Configuration updated
        case (GAPM_SET_DEV_CONFIG):
        {
            ASSERT_INFO(param->status == GAP_ERR_NO_ERROR, param->operation, param->status);

            // Go to the create db state
            ke_state_set(TASK_APP, APPM_CREATE_DB);

            // Add the first required service in the database
            // and wait for the PROFILE_ADDED_IND
            appm_add_svc();
        }
        break;

        case (GAPM_ADV_NON_CONN):
        case (GAPM_ADV_UNDIRECT):
        #if !(BLE_APP_HID)
        case (GAPM_ADV_DIRECT):
        #endif// !(BLE_APP_HID)
        case (GAPM_ADV_DIRECT_LDC):
        case (GAPM_UPDATE_ADVERTISE_DATA):
        {
            ASSERT_INFO(param->status == GAP_ERR_NO_ERROR, param->operation, param->status);
            
            if (param->status == GAP_ERR_CANCELED)
            {
                if (app_env.adv_mode == APP_FAST_ADV_MODE)
                {
                    app_env.adv_mode = APP_SLOW_ADV_MODE;
                    appm_start_advertising();
                }
                else if (app_env.adv_mode == APP_SLOW_ADV_MODE)
                {
                    app_env.adv_mode = APP_STOP_ADV_MODE;
                }
            }
        } break;

        #if (BLE_APP_HID)
        case (GAPM_ADV_DIRECT):
        {
            if (param->status == GAP_ERR_TIMEOUT)
            {
                ke_state_set(TASK_APP, APPM_READY);
                appm_start_advertising();
            }
        } break;
        #endif //(BLE_APP_HID)

        default:
        {
            // Drop the message
        }
        break;
    }

    BLE_APP_FUNC_LEAVE();

    return (KE_MSG_CONSUMED);
}

static int gapc_get_dev_info_req_ind_handler(ke_msg_id_t const msgid,
        struct gapc_get_dev_info_req_ind const *param,
        ke_task_id_t const dest_id,
        ke_task_id_t const src_id)
{
    switch(param->req)
    {
        case GAPC_DEV_NAME:
        {
            struct gapc_get_dev_info_cfm * cfm = KE_MSG_ALLOC_DYN(GAPC_GET_DEV_INFO_CFM,
                                                    src_id, dest_id,
                                                    gapc_get_dev_info_cfm, APP_DEVICE_NAME_MAX_LEN);
            cfm->req = param->req;
            cfm->info.name.length = appm_get_dev_name(cfm->info.name.value);

            // Send message
            ke_msg_send(cfm);
        } break;

        case GAPC_DEV_APPEARANCE:
        {
            // Allocate message
            struct gapc_get_dev_info_cfm *cfm = KE_MSG_ALLOC(GAPC_GET_DEV_INFO_CFM,
                                                             src_id, dest_id,
                                                             gapc_get_dev_info_cfm);
            cfm->req = param->req;
            // Set the device appearance
            #if (BLE_APP_HT)
            // Generic Thermometer - TODO: Use a flag
            cfm->info.appearance = 728;
            #elif (BLE_APP_HID)
            // HID Mouse
            cfm->info.appearance = 962;
            #else
            // No appearance
            cfm->info.appearance = 0;
            #endif

            // Send message
            ke_msg_send(cfm);
        } break;

        case GAPC_DEV_SLV_PREF_PARAMS:
        {
            // Allocate message
            struct gapc_get_dev_info_cfm *cfm = KE_MSG_ALLOC(GAPC_GET_DEV_INFO_CFM,
                    src_id, dest_id,
                                                            gapc_get_dev_info_cfm);
            cfm->req = param->req;
            // Slave preferred Connection interval Min
            cfm->info.slv_params.con_intv_min = 8;
            // Slave preferred Connection interval Max
            cfm->info.slv_params.con_intv_max = 10;
            // Slave preferred Connection latency
            cfm->info.slv_params.slave_latency  = 0;
            // Slave preferred Link supervision timeout
            cfm->info.slv_params.conn_timeout    = 200;  // 2s (500*10ms)

            // Send message
            ke_msg_send(cfm);
        } break;

        default: /* Do Nothing */ break;
    }

    return (KE_MSG_CONSUMED);
}
/**
 ****************************************************************************************
 * @brief Handles GAPC_SET_DEV_INFO_REQ_IND message.
 *
 * @param[in] msgid     Id of the message received.
 * @param[in] param     Pointer to the parameters of the message.
 * @param[in] dest_id   ID of the receiving task instance (TASK_GAP).
 * @param[in] src_id    ID of the sending task instance.
 *
 * @return If the message was consumed or not.
 ****************************************************************************************
 */
static int gapc_set_dev_info_req_ind_handler(ke_msg_id_t const msgid,
        struct gapc_set_dev_info_req_ind const *param,
        ke_task_id_t const dest_id,
        ke_task_id_t const src_id)
{
    // Set Device configuration
    struct gapc_set_dev_info_cfm* cfm = KE_MSG_ALLOC(GAPC_SET_DEV_INFO_CFM, src_id, dest_id,
                                                     gapc_set_dev_info_cfm);
    // Reject to change parameters
    cfm->status = GAP_ERR_REJECTED;
    cfm->req = param->req;
    // Send message
    ke_msg_send(cfm);

    return (KE_MSG_CONSUMED);
}

/**
 ****************************************************************************************
 * @brief Handles connection complete event from the GAP. Enable all required profiles
 *
 * @param[in] msgid     Id of the message received.
 * @param[in] param     Pointer to the parameters of the message.
 * @param[in] dest_id   ID of the receiving task instance (TASK_GAP).
 * @param[in] src_id    ID of the sending task instance.
 *
 * @return If the message was consumed or not.
 ****************************************************************************************
 */
static int gapc_connection_req_ind_handler(ke_msg_id_t const msgid,
                                           struct gapc_connection_req_ind const *param,
                                           ke_task_id_t const dest_id,
                                           ke_task_id_t const src_id)
{
    app_env.conidx = KE_IDX_GET(src_id);

    BLE_APP_FUNC_ENTER();

    // Check if the received Connection Handle was valid
    if (app_env.conidx != GAP_INVALID_CONIDX)
    {
        // Retrieve the connection info from the parameters
        app_env.conhdl = param->conhdl;

        // Clear the advertising timeout timer
        if (ke_timer_active(APP_ADV_TIMEOUT_TIMER, TASK_APP))
        {
            ke_timer_clear(APP_ADV_TIMEOUT_TIMER, TASK_APP);
        }

        // Send connection confirmation
        struct gapc_connection_cfm *cfm = KE_MSG_ALLOC(GAPC_CONNECTION_CFM,
                KE_BUILD_ID(TASK_GAPC, app_env.conidx), TASK_APP,
                gapc_connection_cfm);

        #if(BLE_APP_SEC)
        cfm->auth      = app_sec_get_bond_status() ? GAP_AUTH_REQ_NO_MITM_BOND : GAP_AUTH_REQ_NO_MITM_NO_BOND; // TODO [FBE] restore valid data
        #else // !(BLE_APP_SEC)
        cfm->auth      = GAP_AUTH_REQ_NO_MITM_NO_BOND;
        #endif // (BLE_APP_SEC)
        // Send the message
        ke_msg_send(cfm);

        #if DISPLAY_SUPPORT
        // Update displayed information
        app_display_set_adv(false);
        app_display_set_con(true);
        #endif //(DISPLAY_SUPPORT)

        /*--------------------------------------------------------------
         * ENABLE REQUIRED PROFILES
         *--------------------------------------------------------------*/

        #if (BLE_APP_BATT)
        // Enable Battery Service
        app_batt_enable_prf(app_env.conhdl);
        #endif //(BLE_APP_BATT)

        #if (BLE_APP_HID)
        // Enable HID Service
        app_hid_enable_prf(app_env.conhdl);
        #endif //(BLE_APP_HID)

        #if (BLE_APP_HR)
        app_hrps_connected_evt_handler(app_env.conhdl);
        #endif

		#if (BLE_DATAPATH_SERVER)
		app_datapath_server_connected_evt_handler(app_env.conhdl);
		#endif

        // We are now in connected State
        ke_state_set(dest_id, APPM_CONNECTED);

        #if (BLE_APP_SEC && !defined(BLE_APP_AM0))
        if (app_sec_get_bond_status())
        {
            // Ask for the peer device to either start encryption
            app_sec_send_security_req(app_env.conidx);
        }
        #endif // (BLE_APP_SEC && !defined(BLE_APP_AM0))

		app_connected_evt_handler();
    }
    else
    {
        // No connection has been establish, restart advertising
        appm_start_advertising();
    }
    
    BLE_APP_FUNC_LEAVE();

    return (KE_MSG_CONSUMED);
}

/**
 ****************************************************************************************
 * @brief Handles GAP controller command complete events.
 *
 * @param[in] msgid     Id of the message received.
 * @param[in] param     Pointer to the parameters of the message.
 * @param[in] dest_id   ID of the receiving task instance (TASK_GAP).
 * @param[in] src_id    ID of the sending task instance.
 *
 * @return If the message was consumed or not.
 ****************************************************************************************
 */
static int gapc_cmp_evt_handler(ke_msg_id_t const msgid,
                                struct gapc_cmp_evt const *param,
                                ke_task_id_t const dest_id,
                                ke_task_id_t const src_id)
{
    switch(param->operation)
    {
        case (GAPC_UPDATE_PARAMS):
        {
            if (param->status != GAP_ERR_NO_ERROR)
            {
//                appm_disconnect();
            }
        } break;

        default:
        {
        } break;
    }

    return (KE_MSG_CONSUMED);
}

/**
 ****************************************************************************************
 * @brief Handles disconnection complete event from the GAP.
 *
 * @param[in] msgid     Id of the message received.
 * @param[in] param     Pointer to the parameters of the message.
 * @param[in] dest_id   ID of the receiving task instance (TASK_GAP).
 * @param[in] src_id    ID of the sending task instance.
 *
 * @return If the message was consumed or not.
 ****************************************************************************************
 */
static int gapc_disconnect_ind_handler(ke_msg_id_t const msgid,
                                      struct gapc_disconnect_ind const *param,
                                      ke_task_id_t const dest_id,
                                      ke_task_id_t const src_id)
{
    // Go to the ready state
    ke_state_set(TASK_APP, APPM_READY);

    #if (BLE_APP_HT)
    // Stop interval timer
    app_stop_timer();
    #endif //(BLE_APP_HT)

    #if (DISPLAY_SUPPORT)
    // Update Connection State screen
    app_display_set_con(false);
    #endif //(DISPLAY_SUPPORT)

    #if (BLE_APP_HR)
    app_hrps_disconnected_evt_handler();
    #endif

	#if (BLE_DATAPATH_SERVER)
	app_datapath_server_disconnected_evt_handler();
	#endif

	app_disconnected_evt_handler();

    // Restart Advertising
    appm_start_advertising();

    return (KE_MSG_CONSUMED);
}


static int gapm_profile_added_ind_handler(ke_msg_id_t const msgid,
                                          struct gapm_profile_added_ind *param,
                                          ke_task_id_t const dest_id,
                                          ke_task_id_t const src_id)
{
    // Current State
    uint8_t state            = ke_state_get(dest_id);

    if (state == APPM_CREATE_DB)
    {
        switch (param->prf_task_id)
        {
            #if defined (BLE_APP_AM0)
            case (TASK_ID_AM0_HAS):
            {
                am0_app_set_prf_task(param->prf_task_nb);
            } break;
            #endif // defined (BLE_APP_AM0)

            default: /* Nothing to do */ break;
        }
    }
    else
    {
        ASSERT_INFO(0, state, src_id);
    }

    return KE_MSG_CONSUMED;
}

static int gapm_conn_param_update_handler(ke_msg_id_t const msgid,
                                          struct gapc_param_update_req_ind *param,
                                          ke_task_id_t const dest_id,
                                          ke_task_id_t const src_id)
{
    struct gapc_param_update_cfm* cfm = KE_MSG_ALLOC(GAPC_PARAM_UPDATE_CFM, src_id, dest_id,
                                            gapc_param_update_cfm);

    TRACE("%s intv:%d ", __func__, param->intv_max);

    if (param->intv_min <= APP_CONN_PARAM_INTERVEL_MIN) {
        cfm->accept = false;
    } else {
        cfm->accept = true;
        cfm->ce_len_min = param->intv_min;
        cfm->ce_len_max = param->intv_max;
    }

    ke_msg_send(cfm);

    return (KE_MSG_CONSUMED);
}

/**
 ****************************************************************************************
 * @brief Handles reception of all messages sent from the lower layers to the application
 * @param[in] msgid     Id of the message received.
 * @param[in] param     Pointer to the parameters of the message.
 * @param[in] dest_id   ID of the receiving task instance
 * @param[in] src_id    ID of the sending task instance.
 *
 * @return If the message was consumed or not.
 ****************************************************************************************
 */
static int appm_msg_handler(ke_msg_id_t const msgid,
                            void *param,
                            ke_task_id_t const dest_id,
                            ke_task_id_t const src_id)
{
    // Retrieve identifier of the task from received message
    ke_task_id_t src_task_id = MSG_T(msgid);
    // Message policy
    uint8_t msg_pol          = KE_MSG_CONSUMED;


    switch (src_task_id)
    {
        case (TASK_ID_GAPC):
        {
            #if (BLE_APP_SEC)
            if ((msgid >= GAPC_BOND_CMD) &&
                (msgid <= GAPC_SECURITY_IND))
            {
                // Call the Security Module
                msg_pol = appm_get_handler(&app_sec_table_handler, msgid, param, src_id);
            }
            #endif //(BLE_APP_SEC)
            // else drop the message
        } break;

        case (TASK_ID_GATTC):
        {
            // Service Changed - Drop
        } break;

        #if (BLE_APP_HT)
        case (TASK_ID_HTPT):
        {
            // Call the Health Thermometer Module
            msg_pol = appm_get_handler(&app_ht_table_handler, msgid, param, src_id);
        } break;
        #endif //(BLE_APP_HT)

        #if (BLE_APP_DIS)
        case (TASK_ID_DISS):
        {
            // Call the Device Information Module
            msg_pol = appm_get_handler(&app_dis_table_handler, msgid, param, src_id);
        } break;
        #endif //(BLE_APP_DIS)

        #if (BLE_APP_HID)
        case (TASK_ID_HOGPD):
        {
            // Call the HID Module
            msg_pol = appm_get_handler(&app_hid_table_handler, msgid, param, src_id);
        } break;
        #endif //(BLE_APP_HID)

        #if (BLE_APP_BATT)
        case (TASK_ID_BASS):
        {
            // Call the Battery Module
            msg_pol = appm_get_handler(&app_batt_table_handler, msgid, param, src_id);
        } break;
        #endif //(BLE_APP_BATT)

        #if defined(BLE_APP_AM0)
        case (TASK_ID_AM0):
        {
            // Call the Audio Mode 0 Module
            msg_pol = appm_get_handler(&am0_app_table_handler, msgid, param, src_id);
        } break;
        case (TASK_ID_AM0_HAS):
        {
            // Call the Audio Mode 0 Module
            msg_pol = appm_get_handler(&am0_app_has_table_handler, msgid, param, src_id);
        } break;
        #endif // defined(BLE_APP_AM0)

        #if (BLE_APP_HR)
        case (TASK_ID_HRPS):
        {
            // Call the HRPS Module
            msg_pol = appm_get_handler(&app_hrps_table_handler, msgid, param, src_id);
        } break;
        #endif
		#if (BLE_APP_DATAPATH_SERVER)
        case (TASK_ID_DATAPATHPS):
        {
            // Call the Battery Module
            msg_pol = appm_get_handler(&app_datapath_server_table_handler, msgid, param, src_id);
        } break;
		#endif //(BLE_APP_DATAPATH_SERVER)

        default:
        {
            #if (BLE_APP_HT)
            if (msgid == APP_HT_MEAS_INTV_TIMER)
            {
                msg_pol = appm_get_handler(&app_ht_table_handler, msgid, param, src_id);
            }
            #endif //(BLE_APP_HT)

            #if (BLE_APP_HID)
            if (msgid == APP_HID_MOUSE_TIMEOUT_TIMER)
            {
                msg_pol = appm_get_handler(&app_hid_table_handler, msgid, param, src_id);
            }
            #endif //(BLE_APP_HID)
        } break;
    }

    return (msg_pol);
}

/*
 * GLOBAL VARIABLES DEFINITION
 ****************************************************************************************
 */
/* Default State handlers definition. */
KE_MSG_HANDLER_TAB(appm)
{
    // Note: first message is latest message checked by kernel so default is put on top.
    {KE_MSG_DEFAULT_HANDLER,    (ke_msg_func_t)appm_msg_handler},

    {APP_ADV_TIMEOUT_TIMER,     (ke_msg_func_t)app_adv_timeout_handler},

    {GAPM_DEVICE_READY_IND,     (ke_msg_func_t)gapm_device_ready_ind_handler},
    {GAPM_CMP_EVT,              (ke_msg_func_t)gapm_cmp_evt_handler},
    {GAPC_GET_DEV_INFO_REQ_IND, (ke_msg_func_t)gapc_get_dev_info_req_ind_handler},
    {GAPC_SET_DEV_INFO_REQ_IND, (ke_msg_func_t)gapc_set_dev_info_req_ind_handler},
    {GAPC_CONNECTION_REQ_IND,   (ke_msg_func_t)gapc_connection_req_ind_handler},
    {GAPC_CMP_EVT,              (ke_msg_func_t)gapc_cmp_evt_handler},
    {GAPC_DISCONNECT_IND,       (ke_msg_func_t)gapc_disconnect_ind_handler},
    {GAPM_PROFILE_ADDED_IND,    (ke_msg_func_t)gapm_profile_added_ind_handler},
    {GAPC_PARAM_UPDATE_REQ_IND, (ke_msg_func_t)gapm_conn_param_update_handler}
};


#if 0
void appm_task_init(struct ke_task_desc *task_desc)
{
    // Get the address of the environment
    struct appm_env_tag *appm_env = PRF_ENV_GET(appm, appm);

    task_desc->msg_handler_tab = appm_msg_handler_tab;
    task_desc->msg_cnt         = ARRAY_LEN(appm_msg_handler_tab);
    task_desc->state           = appm_env->state;
    task_desc->idx_max         = appm_IDX_MAX;
}
#endif

/* Defines the place holder for the states of all the task instances. */
ke_state_t appm_state[APP_IDX_MAX];

const struct ke_task_desc TASK_DESC_APP = {appm_msg_handler_tab, appm_state, APP_IDX_MAX, ARRAY_LEN(appm_msg_handler_tab)};

#endif //(BLE_APP_PRESENT)

/// @} APPTASK
