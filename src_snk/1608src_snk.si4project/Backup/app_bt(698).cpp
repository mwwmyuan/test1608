#include "cmsis_os.h"
#include "hal_trace.h"
#include "hal_aud.h"
#include "apps.h"
#include "app_thread.h"
#include "app_status_ind.h"

#include "nvrecord.h"
#include "besbt.h"

extern "C" {
#include "besbt_cfg.h"
#include "eventmgr.h"
#include "me.h"
#include "sec.h"
#include "a2dp.h"
#include "avdtp.h"
#include "avctp.h"
#include "avrcp.h"
#include "hf.h"
#include "sys/mei.h"
}
#include "btalloc.h"
#include "btapp.h"
#include "app_bt.h"
#include "app_bt_func.h"
#include "bt_drv_interface.h"

#if defined(__EARPHONE_STAY_BCR_SLAVE__) && defined(__BT_ONE_BRING_TWO__)
#error can not defined at the same time.
#endif

extern struct BT_DEVICE_T  app_bt_device;
U16 bt_accessory_feature_feature = HF_CUSTOM_FEATURE_SUPPORT;

#ifdef __BT_ONE_BRING_TWO__
BtDeviceRecord record2_copy;
uint8_t record2_avalible;

#endif

BtAccessibleMode g_bt_access_mode = BAM_NOT_ACCESSIBLE;
 
#define APP_BT_PROFILE_BOTH_SCAN_MS (11000)
#define APP_BT_PROFILE_PAGE_SCAN_MS (4000)

osTimerId app_bt_accessmode_timer = NULL;    
BtAccessibleMode app_bt_accessmode_timer_argument = BAM_NOT_ACCESSIBLE;    
static int app_bt_accessmode_timehandler(void const *param);

osTimerDef (APP_BT_ACCESSMODE_TIMER, (void (*)(void const *))app_bt_accessmode_timehandler);                      // define timers

static int app_bt_accessmode_timehandler(void const *param)
{
    const BtAccessModeInfo info = { BT_DEFAULT_INQ_SCAN_INTERVAL,
                                    BT_DEFAULT_INQ_SCAN_WINDOW,
                                    BT_DEFAULT_PAGE_SCAN_INTERVAL,
                                    BT_DEFAULT_PAGE_SCAN_WINDOW };
    BtAccessibleMode accMode = *(BtAccessibleMode*)(param);

    OS_LockStack();
    if (accMode == BAM_CONNECTABLE_ONLY){
        app_bt_accessmode_timer_argument = BAM_GENERAL_ACCESSIBLE;
        osTimerStart(app_bt_accessmode_timer, APP_BT_PROFILE_PAGE_SCAN_MS);
    }else if(accMode == BAM_GENERAL_ACCESSIBLE){
        app_bt_accessmode_timer_argument = BAM_CONNECTABLE_ONLY;
        osTimerStart(app_bt_accessmode_timer, APP_BT_PROFILE_BOTH_SCAN_MS);
    }
    app_bt_ME_SetAccessibleMode(accMode, &info);
    TRACE("app_bt_accessmode_timehandler accMode=%x",accMode); 
    OS_UnlockStack();
    return 0;
}


void app_bt_accessmode_set(BtAccessibleMode mode)
{

   
    const BtAccessModeInfo info = { BT_DEFAULT_INQ_SCAN_INTERVAL,
                                    BT_DEFAULT_INQ_SCAN_WINDOW,
                                    BT_DEFAULT_PAGE_SCAN_INTERVAL,
                                    BT_DEFAULT_PAGE_SCAN_WINDOW };
    OS_LockStack();
    g_bt_access_mode = mode;
    if (g_bt_access_mode == BAM_GENERAL_ACCESSIBLE){
        app_bt_accessmode_timehandler(&g_bt_access_mode);
		app_status_indication_set(APP_STATUS_INDICATION_BOTHSCAN);
    }else{
       
        osTimerStop(app_bt_accessmode_timer);
        app_bt_ME_SetAccessibleMode(g_bt_access_mode, &info);
        TRACE("app_bt_accessmode_set access_mode=%x",g_bt_access_mode);
		
    }
    OS_UnlockStack();
}


void PairingTransferToConnectable(void)
{
    int activeCons;
    OS_LockStack();
    activeCons = MEC(activeCons);
    OS_UnlockStack();
    TRACE("%s",__func__);

    if(activeCons == 0){
        TRACE("!!!PairingTransferToConnectable  BAM_CONNECTABLE_ONLY\n");        
        app_bt_accessmode_set_req(BAM_CONNECTABLE_ONLY);
	if(app_bt_device.src_or_snk==BT_DEVICE_SRC)
		app_status_indication_set(APP_STATUS_INDICATION_SRC_CONNECTABLE);
	else
	    app_status_indication_set(APP_STATUS_INDICATION_CONNECTABLE);
    }
}

int app_bt_state_checker(void)
{
    uint8_t i;
    BtRemoteDevice *remDev = NULL;
    CmgrHandler    *cmgrHandler;

    for (i=0; i<BT_DEVICE_NUM; i++){
        remDev = MeEnumerateRemoteDevices(i);
        cmgrHandler = CMGR_GetAclHandler(remDev);
        TRACE("checker: id:%d state:%d mode:%d role:%d cmghdl:%x sniffInterva:%d/%d IsAudioUp:%d", i, remDev->state, remDev->mode,remDev->role, cmgrHandler,
                                                                 cmgrHandler->sniffInterval, cmgrHandler->sniffInfo.maxInterval, CMGR_IsAudioUp(cmgrHandler));        
        DUMP8("0x%02x ", remDev->bdAddr.addr, BD_ADDR_SIZE);
        TRACE("remDev:%x a2dp State:%d remDev:%x hf_channel Connected:%d remDev:%x ",
                                remDev,
                                app_bt_device.a2dp_connected_stream[i]?A2DP_GetStreamState(app_bt_device.a2dp_connected_stream[i]):A2DP_STREAM_STATE_CLOSED,
                                 app_bt_device.a2dp_connected_stream[i]?app_bt_device.a2dp_connected_stream[i]->device->cmgrHandler.remDev:NULL,
                                HF_IsACLConnected(&app_bt_device.hf_channel[i]),
                                app_bt_device.hf_channel[i].cmgrHandler.remDev);
    }
    return 0;
}

void app_bt_accessible_manager_process(const BtEvent *Event)
{
#ifdef __BT_ONE_BRING_TWO__
    static uint8_t opening_reconnect_cnf_cnt = 0;
    if (app_bt_profile_connect_openreconnecting(NULL)){
        if (Event->eType == BTEVENT_LINK_CONNECT_CNF){
            opening_reconnect_cnf_cnt++;
        }
        if (record2_avalible){
            if (opening_reconnect_cnf_cnt<2){
                return;
            }
        }
    }
#endif    
    switch (Event->eType) {
        case BTEVENT_LINK_CONNECT_CNF:
        case BTEVENT_LINK_CONNECT_IND:          
            TRACE("BTEVENT_LINK_CONNECT_IND/CNF activeCons:%d",MEC(activeCons));
#if defined(__EARPHONE__)   && !defined(FPGA)
            app_stop_10_second_timer(APP_PAIR_TIMER_ID);
#endif
#ifdef __BT_ONE_BRING_TWO__
             if(MEC(activeCons) == 0){
#ifdef __EARPHONE_STAY_BOTH_SCAN__              
                app_bt_accessmode_set_req(BT_DEFAULT_ACCESS_MODE_PAIR);
#else
                app_bt_accessmode_set_req(BAM_CONNECTABLE_ONLY);
#endif
            }else if(MEC(activeCons) == 1){
                app_bt_accessmode_set_req(BAM_CONNECTABLE_ONLY);
            }else if(MEC(activeCons) >= 2){
                app_bt_accessmode_set_req(BAM_NOT_ACCESSIBLE);
            }
#else
            if(MEC(activeCons) == 0){
#ifdef __EARPHONE_STAY_BOTH_SCAN__              
                app_bt_accessmode_set_req(BT_DEFAULT_ACCESS_MODE_PAIR);
#else
                app_bt_accessmode_set_req(BAM_CONNECTABLE_ONLY);
#endif            
            }else if(MEC(activeCons) >= 1){
                app_bt_accessmode_set_req(BAM_NOT_ACCESSIBLE);				
            }
#endif
            break;
        case BTEVENT_LINK_DISCONNECT:           
            TRACE("DISCONNECT activeCons:%d",MEC(activeCons));
#ifdef __EARPHONE_STAY_BOTH_SCAN__
#ifdef __BT_ONE_BRING_TWO__
            if(MEC(activeCons) == 0){
                app_bt_accessmode_set_req(BT_DEFAULT_ACCESS_MODE_PAIR);
            }else if(MEC(activeCons) == 1){
                app_bt_accessmode_set_req(BAM_CONNECTABLE_ONLY);
            }else if(MEC(activeCons) >= 2){
                app_bt_accessmode_set_req(BAM_NOT_ACCESSIBLE);
            }               
#else
            app_bt_accessmode_set_req(BT_DEFAULT_ACCESS_MODE_PAIR);
#endif
#else
            app_bt_accessmode_set_req(BAM_CONNECTABLE_ONLY);
#endif
            break;
#ifdef __BT_ONE_BRING_TWO__
        case BTEVENT_SCO_CONNECT_IND:
        case BTEVENT_SCO_CONNECT_CNF:
            if(MEC(activeCons) == 1){
                app_bt_accessmode_set_req(BAM_NOT_ACCESSIBLE);
            }
            break;
        case BTEVENT_SCO_DISCONNECT:
            if(MEC(activeCons) == 1){
                app_bt_accessmode_set_req(BAM_CONNECTABLE_ONLY);
            }
            break;
#endif            
        default:
            break;
    }
}

#define APP_BT_SWITCHROLE_LIMIT (1)

#if defined(APP_LINEIN_A2DP_SOURCE)||defined(APP_I2S_A2DP_SOURCE)

void app_bt_role_manager_process(const BtEvent *Event)
{
    static BtRemoteDevice *opRemDev = NULL;
    static uint8_t switchrole_cnt = 0;  
    BtRemoteDevice *remDev = NULL;
	BtStatus status=0;

    //on phone connecting
    switch (Event->eType) {
        case BTEVENT_LINK_CONNECT_IND:
            if(Event->errCode == BEC_NO_ERROR){                
                if (MEC(activeCons) == 1){
                    switch (Event->p.remDev->role) {
                        case BCR_MASTER:
                        case BCR_PMASTER:                            
                            TRACE("CONNECT_IND try to role %x\n", Event->p.remDev);
                            //curr connectrot try to role
                            opRemDev = Event->p.remDev;
                            switchrole_cnt = 0;                        
                            app_bt_Me_SetLinkPolicy(Event->p.remDev, BLP_MASTER_SLAVE_SWITCH|BLP_SNIFF_MODE);
                            break;
                        case BCR_SLAVE:
                        case BCR_PSLAVE:
                        case BCR_ANY:
                        case BCR_UNKNOWN:
                        default:
                            TRACE("CONNECT_IND disable role %x\n", Event->p.remDev);
                            //disable roleswitch when 1 connect
                            app_bt_Me_SetLinkPolicy(Event->p.remDev, BLP_SNIFF_MODE);
                            break;
                    }
                    //set next connector to master
                    app_bt_ME_SetConnectionRole(BCR_MASTER);
                }else if (MEC(activeCons) > 1){
                    switch (Event->p.remDev->role) {
                        case BCR_MASTER:
                        case BCR_PMASTER:
                            TRACE("CONNECT_IND disable role %x\n", Event->p.remDev);
                            //disable roleswitch
                            app_bt_Me_SetLinkPolicy(Event->p.remDev, BLP_SNIFF_MODE);
                            break;
                        case BCR_SLAVE:
                        case BCR_PSLAVE:
                        case BCR_ANY:
                        case BCR_UNKNOWN:
                        default:
                            //disconnect slave
                            TRACE("CONNECT_IND disconnect slave %x\n", Event->p.remDev);
                            app_bt_MeDisconnectLink(Event->p.remDev);
                            break;
                    }
                    //set next connector to master
                    app_bt_ME_SetConnectionRole(BCR_MASTER);
                }
            }
            break;
        case BTEVENT_LINK_CONNECT_CNF:
            if (MEC(activeCons) == 1){
                switch (Event->p.remDev->role) {
                    case BCR_MASTER:
                    case BCR_PMASTER:
						if(app_bt_device.src_or_snk!=BT_DEVICE_SRC)
						{
							//curr connectrot try to role
							opRemDev = Event->p.remDev;
							switchrole_cnt = 0;
							//app_bt_Me_SetLinkPolicy(Event->p.remDev, BLP_MASTER_SLAVE_SWITCH|BLP_SNIFF_MODE);
							status=ME_SwitchRole(Event->p.remDev);
							TRACE("CONNECT_CNF try to role status:%x\n", status);
						}
						break;
                    case BCR_SLAVE:
                    case BCR_PSLAVE:
                     	if(app_bt_device.src_or_snk==BT_DEVICE_SRC)
                     	{
							//curr connectrot try to role
							opRemDev = Event->p.remDev;
							switchrole_cnt = 0;
							status=ME_SwitchRole(Event->p.remDev);
							TRACE("SRC SLAVE CONNECT_CNF try to role status:%x\n", status);
                     	}
                    case BCR_ANY:
                    case BCR_UNKNOWN:
                    default:
                        TRACE("CONNECT_CNF disable role %x\n", Event->p.remDev);
                        //disable roleswitch
                        app_bt_Me_SetLinkPolicy(Event->p.remDev, BLP_SNIFF_MODE);
                        break;
                }
				if(app_bt_device.src_or_snk!=BT_DEVICE_SRC)
				{
					//set next connector to master
					app_bt_ME_SetConnectionRole(BCR_MASTER);
				}
				
            }else if (MEC(activeCons) > 1){
                switch (Event->p.remDev->role) {
                    case BCR_MASTER:
                    case BCR_PMASTER :
                        TRACE("CONNECT_CNF disable role %x\n", Event->p.remDev);
                        //disable roleswitch
                        app_bt_Me_SetLinkPolicy(Event->p.remDev, BLP_SNIFF_MODE);
                        break;
                    case BCR_SLAVE:
                    case BCR_ANY:
                    case BCR_UNKNOWN:
                    default:
                        //disconnect slave
                        TRACE("CONNECT_CNF disconnect slave %x\n", Event->p.remDev);
                        app_bt_MeDisconnectLink(Event->p.remDev);
                        break;
                }
                //set next connector to master
                app_bt_ME_SetConnectionRole(BCR_MASTER);
            }
            break;
        case BTEVENT_LINK_DISCONNECT:
            if (opRemDev == Event->p.remDev){
                opRemDev = NULL;
                switchrole_cnt = 0;
            }
            if (MEC(activeCons) == 0){
                for (uint8_t i=0; i<BT_DEVICE_NUM; i++){
                    if(app_bt_device.a2dp_connected_stream[i])
                        app_bt_A2DP_SetMasterRole(app_bt_device.a2dp_connected_stream[i], FALSE);
                    app_bt_HF_SetMasterRole(&app_bt_device.hf_channel[i], FALSE);
                }
                app_bt_ME_SetConnectionRole(BCR_ANY);
            }else if (MEC(activeCons) == 1){
                //set next connector to master
                app_bt_ME_SetConnectionRole(BCR_MASTER);
            }
            break;
        case BTEVENT_ROLE_CHANGE:
        if (opRemDev == Event->p.remDev){
                switch (Event->p.roleChange.newRole) {
                    case BCR_MASTER:                    
                        if (++switchrole_cnt<=APP_BT_SWITCHROLE_LIMIT){
                            ME_SwitchRole(Event->p.remDev);
                        }else{                      
                            TRACE("ROLE TO SLAVE FAILED remDev%x cnt:%d\n", Event->p.remDev, switchrole_cnt);
                            opRemDev = NULL;
                            switchrole_cnt = 0;
                        }
                        break;
                    case BCR_SLAVE:                     
                        TRACE("ROLE TO SLAVE SUCCESS remDev%x cnt:%d\n", Event->p.remDev, switchrole_cnt);
                        opRemDev = NULL;
                        switchrole_cnt = 0;
                        app_bt_Me_SetLinkPolicy(Event->p.remDev,BLP_SNIFF_MODE);
                        break;
                    case BCR_ANY:
                        break;                    
                    case BCR_UNKNOWN:
                        break;
                    default:
                        break;
                }
            }
            
            if (MEC(activeCons) > 1){
                uint8_t slave_cnt = 0;
                for (uint8_t i=0; i<BT_DEVICE_NUM; i++){
                    remDev = MeEnumerateRemoteDevices(i);
                    if (ME_GetCurrentRole(remDev) == BCR_SLAVE){
                        slave_cnt++;
                    }
                }
                if (slave_cnt>1){
                    TRACE("ROLE_CHANGE disconnect slave %x\n", Event->p.remDev);
                    app_bt_MeDisconnectLink(Event->p.remDev);
                }
            }
            break;
        default:
           break;
        }
}
#else
void app_bt_role_manager_process(const BtEvent *Event)
{
    static BtRemoteDevice *opRemDev = NULL;
    static uint8_t switchrole_cnt = 0;  
    BtRemoteDevice *remDev = NULL;
	BtStatus status=0;

    //on phone connecting
    switch (Event->eType) {
        case BTEVENT_LINK_CONNECT_IND:
            if(Event->errCode == BEC_NO_ERROR){                
                if (MEC(activeCons) == 1){
                    switch (Event->p.remDev->role) {
                        case BCR_MASTER:
                        case BCR_PMASTER:                            
                            TRACE("CONNECT_IND try to role %x\n", Event->p.remDev);
                            //curr connectrot try to role
                            opRemDev = Event->p.remDev;
                            switchrole_cnt = 0;                        
                            app_bt_Me_SetLinkPolicy(Event->p.remDev, BLP_MASTER_SLAVE_SWITCH|BLP_SNIFF_MODE);
                            break;
                        case BCR_SLAVE:
                        case BCR_PSLAVE:
                        case BCR_ANY:
                        case BCR_UNKNOWN:
                        default:
                            TRACE("CONNECT_IND disable role %x\n", Event->p.remDev);
                            //disable roleswitch when 1 connect
                            app_bt_Me_SetLinkPolicy(Event->p.remDev, BLP_SNIFF_MODE);
                            break;
                    }
                    //set next connector to master
                    app_bt_ME_SetConnectionRole(BCR_MASTER);
                }else if (MEC(activeCons) > 1){
                    switch (Event->p.remDev->role) {
                        case BCR_MASTER:
                        case BCR_PMASTER:
                            TRACE("CONNECT_IND disable role %x\n", Event->p.remDev);
                            //disable roleswitch
                            app_bt_Me_SetLinkPolicy(Event->p.remDev, BLP_SNIFF_MODE);
                            break;
                        case BCR_SLAVE:
                        case BCR_PSLAVE:
                        case BCR_ANY:
                        case BCR_UNKNOWN:
                        default:
                            //disconnect slave
                            TRACE("CONNECT_IND disconnect slave %x\n", Event->p.remDev);
                            app_bt_MeDisconnectLink(Event->p.remDev);
                            break;
                    }
                    //set next connector to master
                    app_bt_ME_SetConnectionRole(BCR_MASTER);
                }
            }
            break;
        case BTEVENT_LINK_CONNECT_CNF:
            if (MEC(activeCons) == 1){
                switch (Event->p.remDev->role) {
                    case BCR_MASTER:
                    case BCR_PMASTER:
                        TRACE("CONNECT_CNF try to role %x\n", Event->p.remDev);
                        //curr connectrot try to role
                        opRemDev = Event->p.remDev;
                        switchrole_cnt = 0;
                        app_bt_Me_SetLinkPolicy(Event->p.remDev, BLP_MASTER_SLAVE_SWITCH|BLP_SNIFF_MODE);
                        status=ME_SwitchRole(Event->p.remDev);

						TRACE("CONNECT_CNF try to role status:%x\n", status);
                        break;
                    case BCR_SLAVE:
                    case BCR_PSLAVE:
                    case BCR_ANY:
                    case BCR_UNKNOWN:
                    default:
                        TRACE("CONNECT_CNF disable role %x\n", Event->p.remDev);
                        //disable roleswitch
                        app_bt_Me_SetLinkPolicy(Event->p.remDev, BLP_SNIFF_MODE);
                        break;
                }
                //set next connector to master
                app_bt_ME_SetConnectionRole(BCR_MASTER);
            }else if (MEC(activeCons) > 1){
                switch (Event->p.remDev->role) {
                    case BCR_MASTER:
                    case BCR_PMASTER :
                        TRACE("CONNECT_CNF disable role %x\n", Event->p.remDev);
                        //disable roleswitch
                        app_bt_Me_SetLinkPolicy(Event->p.remDev, BLP_SNIFF_MODE);
                        break;
                    case BCR_SLAVE:
                    case BCR_ANY:
                    case BCR_UNKNOWN:
                    default:
                        //disconnect slave
                        TRACE("CONNECT_CNF disconnect slave %x\n", Event->p.remDev);
                        app_bt_MeDisconnectLink(Event->p.remDev);
                        break;
                }
                //set next connector to master
                app_bt_ME_SetConnectionRole(BCR_MASTER);
            }
            break;
        case BTEVENT_LINK_DISCONNECT:
            if (opRemDev == Event->p.remDev){
                opRemDev = NULL;
                switchrole_cnt = 0;
            }
            if (MEC(activeCons) == 0){
                for (uint8_t i=0; i<BT_DEVICE_NUM; i++){
                    if(app_bt_device.a2dp_connected_stream[i])
                        app_bt_A2DP_SetMasterRole(app_bt_device.a2dp_connected_stream[i], FALSE);
                    app_bt_HF_SetMasterRole(&app_bt_device.hf_channel[i], FALSE);
                }
                app_bt_ME_SetConnectionRole(BCR_ANY);
            }else if (MEC(activeCons) == 1){
                //set next connector to master
                app_bt_ME_SetConnectionRole(BCR_MASTER);
            }
            break;
        case BTEVENT_ROLE_CHANGE:
        if (opRemDev == Event->p.remDev){
                switch (Event->p.roleChange.newRole) {
                    case BCR_MASTER:                    
                        if (++switchrole_cnt<=APP_BT_SWITCHROLE_LIMIT){
                            ME_SwitchRole(Event->p.remDev);
                        }else{                      
                            TRACE("ROLE TO SLAVE FAILED remDev%x cnt:%d\n", Event->p.remDev, switchrole_cnt);
                            opRemDev = NULL;
                            switchrole_cnt = 0;
                        }
                        break;
                    case BCR_SLAVE:                     
                        TRACE("ROLE TO SLAVE SUCCESS remDev%x cnt:%d\n", Event->p.remDev, switchrole_cnt);
                        opRemDev = NULL;
                        switchrole_cnt = 0;
                        app_bt_Me_SetLinkPolicy(Event->p.remDev,BLP_SNIFF_MODE);
                        break;
                    case BCR_ANY:
                        break;                    
                    case BCR_UNKNOWN:
                        break;
                    default:
                        break;
                }
            }
            
            if (MEC(activeCons) > 1){
                uint8_t slave_cnt = 0;
                for (uint8_t i=0; i<BT_DEVICE_NUM; i++){
                    remDev = MeEnumerateRemoteDevices(i);
                    if (ME_GetCurrentRole(remDev) == BCR_SLAVE){
                        slave_cnt++;
                    }
                }
                if (slave_cnt>1){
                    TRACE("ROLE_CHANGE disconnect slave %x\n", Event->p.remDev);
                    app_bt_MeDisconnectLink(Event->p.remDev);
                }
            }
            break;
        default:
           break;
        }
}

#endif
static int app_bt_sniff_manager_init(void)
{
    BtSniffInfo sniffInfo;
    BtRemoteDevice *remDev = NULL;
    
    for (uint8_t i=0; i<BT_DEVICE_NUM; i++){
        remDev = MeEnumerateRemoteDevices(i);
        sniffInfo.maxInterval = CMGR_SNIFF_MAX_INTERVAL;
        sniffInfo.minInterval = CMGR_SNIFF_MIN_INTERVAL;
        sniffInfo.attempt = CMGR_SNIFF_ATTEMPT;
        sniffInfo.timeout = CMGR_SNIFF_TIMEOUT;
        app_bt_CMGR_SetSniffInofToAllHandlerByRemDev(&sniffInfo, remDev);
        app_bt_HF_EnableSniffMode(&app_bt_device.hf_channel[i], FALSE);
    }
    
    return 0;
}

void app_bt_sniff_manager_process(const BtEvent *Event)
{
    static BtRemoteDevice *opRemDev = NULL;
    BtRemoteDevice *remDev = NULL;
    CmgrHandler    *currCmgrHandler = NULL;
    CmgrHandler    *otherCmgrHandler = NULL;
    bool need_reconfig = false;
    BtSniffInfo sniffInfo;

    if (!besbt_cfg.sniff)
        return;

    switch (Event->eType) {
        case BTEVENT_LINK_CONNECT_IND:
            if(Event->errCode == BEC_NO_ERROR){
                TRACE("CONNECT_IND sniff info %x\n", Event->p.remDev);
                sniffInfo.maxInterval = CMGR_SNIFF_MAX_INTERVAL;
                sniffInfo.minInterval = CMGR_SNIFF_MIN_INTERVAL;
                sniffInfo.attempt = CMGR_SNIFF_ATTEMPT;
                sniffInfo.timeout = CMGR_SNIFF_TIMEOUT;
                app_bt_CMGR_SetSniffInofToAllHandlerByRemDev(&sniffInfo, Event->p.remDev);
                if (MEC(activeCons) > 1){
                    currCmgrHandler = CMGR_GetConnIndHandler(Event->p.remDev);
                    for (uint8_t i=0; i<BT_DEVICE_NUM; i++){
                        remDev = MeEnumerateRemoteDevices(i);
                        if (Event->p.remDev != remDev){
                            otherCmgrHandler = CMGR_GetAclHandler(remDev);
                            if (otherCmgrHandler && currCmgrHandler){
                                if (otherCmgrHandler->sniffInfo.maxInterval == currCmgrHandler->sniffInfo.maxInterval){
                                    sniffInfo.maxInterval = otherCmgrHandler->sniffInfo.maxInterval -20;
                                    sniffInfo.minInterval = otherCmgrHandler->sniffInfo.minInterval - 20;
                                    sniffInfo.attempt = CMGR_SNIFF_ATTEMPT;
                                    sniffInfo.timeout = CMGR_SNIFF_TIMEOUT;
                                    app_bt_CMGR_SetSniffInofToAllHandlerByRemDev(&sniffInfo, Event->p.remDev);
                                    TRACE("CONNECT_IND reinit sniff info\n");
                                }
                            }
                            break;
                        }
                    }
                }
            }
            break;        
        case BTEVENT_LINK_CONNECT_CNF:
            if(Event->errCode == BEC_NO_ERROR){
                TRACE("CONNECT_CNF init %x\n", Event->p.remDev);
                sniffInfo.maxInterval = CMGR_SNIFF_MAX_INTERVAL;
                sniffInfo.minInterval = CMGR_SNIFF_MIN_INTERVAL;
                sniffInfo.attempt = CMGR_SNIFF_ATTEMPT;
                sniffInfo.timeout = CMGR_SNIFF_TIMEOUT;
                app_bt_CMGR_SetSniffInofToAllHandlerByRemDev(&sniffInfo, Event->p.remDev);
                if (MEC(activeCons) > 1){
                    currCmgrHandler = CMGR_GetAclHandler(Event->p.remDev);
                    for (uint8_t i=0; i<BT_DEVICE_NUM; i++){
                        remDev = MeEnumerateRemoteDevices(i);
                        if (Event->p.remDev != remDev){
                            otherCmgrHandler = CMGR_GetAclHandler(remDev);
                            if (otherCmgrHandler && currCmgrHandler){
                                if (otherCmgrHandler->sniffInfo.maxInterval == currCmgrHandler->sniffInfo.maxInterval){
                                    sniffInfo.maxInterval = otherCmgrHandler->sniffInfo.maxInterval -20;
                                    sniffInfo.minInterval = otherCmgrHandler->sniffInfo.minInterval - 20;
                                    sniffInfo.attempt = CMGR_SNIFF_ATTEMPT;
                                    sniffInfo.timeout = CMGR_SNIFF_TIMEOUT;
                                    app_bt_CMGR_SetSniffInofToAllHandlerByRemDev(&sniffInfo, Event->p.remDev);
                                    TRACE("CONNECT_CNF reinit sniff info\n");
                                }
                            }
                            break;
                        }
                    }
                }
            }
            break;
        case BTEVENT_LINK_DISCONNECT:
             if (opRemDev == Event->p.remDev){
                opRemDev = NULL;
            }
            sniffInfo.maxInterval = CMGR_SNIFF_MAX_INTERVAL;
            sniffInfo.minInterval = CMGR_SNIFF_MIN_INTERVAL;
            sniffInfo.attempt = CMGR_SNIFF_ATTEMPT;
            sniffInfo.timeout = CMGR_SNIFF_TIMEOUT;
            app_bt_CMGR_SetSniffInofToAllHandlerByRemDev(&sniffInfo, Event->p.remDev);
            break;
        case BTEVENT_MODE_CHANGE:
            if(Event->p.modeChange.curMode == BLM_SNIFF_MODE){
                currCmgrHandler = CMGR_GetAclHandler(Event->p.remDev);
                if (Event->p.modeChange.interval > CMGR_SNIFF_MAX_INTERVAL){
                        if (!opRemDev){
                            opRemDev = currCmgrHandler->remDev;
                        }
                        currCmgrHandler->sniffInfo.maxInterval = CMGR_SNIFF_MAX_INTERVAL;
                        currCmgrHandler->sniffInfo.minInterval = CMGR_SNIFF_MIN_INTERVAL;
                        currCmgrHandler->sniffInfo.attempt = CMGR_SNIFF_ATTEMPT;
                        currCmgrHandler->sniffInfo.timeout = CMGR_SNIFF_TIMEOUT;
                        app_bt_CMGR_SetSniffInofToAllHandlerByRemDev(&currCmgrHandler->sniffInfo, Event->p.remDev);
                        app_bt_ME_StopSniff(currCmgrHandler->remDev);
                }else{
                    if (currCmgrHandler){
                        currCmgrHandler->sniffInfo.maxInterval = Event->p.modeChange.interval;
                        currCmgrHandler->sniffInfo.minInterval = Event->p.modeChange.interval;
                        currCmgrHandler->sniffInfo.attempt = CMGR_SNIFF_ATTEMPT;
                        currCmgrHandler->sniffInfo.timeout = CMGR_SNIFF_TIMEOUT;
                        app_bt_CMGR_SetSniffInofToAllHandlerByRemDev(&currCmgrHandler->sniffInfo, Event->p.remDev);
                    }
                    if (MEC(activeCons) > 1){
                        for (uint8_t i=0; i<BT_DEVICE_NUM; i++){
                            remDev = MeEnumerateRemoteDevices(i);
                            if (Event->p.remDev != remDev){
                                otherCmgrHandler = CMGR_GetAclHandler(remDev);
                                if (otherCmgrHandler){
                                    if (otherCmgrHandler->sniffInfo.maxInterval == currCmgrHandler->sniffInfo.maxInterval){
                                        if (ME_GetCurrentMode(remDev) == BLM_ACTIVE_MODE){
                                            otherCmgrHandler->sniffInfo.maxInterval -= 20;
                                            otherCmgrHandler->sniffInfo.minInterval -= 20;
                                            otherCmgrHandler->sniffInfo.attempt = CMGR_SNIFF_ATTEMPT;
                                            otherCmgrHandler->sniffInfo.timeout = CMGR_SNIFF_TIMEOUT;
                                            app_bt_CMGR_SetSniffInofToAllHandlerByRemDev(&otherCmgrHandler->sniffInfo, remDev);
                                            TRACE("reconfig sniff other RemDev:%x\n", remDev);
                                        }else if (ME_GetCurrentMode(remDev) == BLM_SNIFF_MODE){
                                            need_reconfig = true;
                                        }
                                    }
                                }
                                break;
                            }
                        }
                    }
                    if (need_reconfig){
                        opRemDev = remDev;
                        if (currCmgrHandler){
                            currCmgrHandler->sniffInfo.maxInterval -= 20;
                            currCmgrHandler->sniffInfo.minInterval -= 20;
                            currCmgrHandler->sniffInfo.attempt = CMGR_SNIFF_ATTEMPT;
                            currCmgrHandler->sniffInfo.timeout = CMGR_SNIFF_TIMEOUT;
                            app_bt_CMGR_SetSniffInofToAllHandlerByRemDev(&currCmgrHandler->sniffInfo, currCmgrHandler->remDev);
                        }
                        app_bt_ME_StopSniff(currCmgrHandler->remDev);
                        TRACE("reconfig sniff setup op opRemDev:%x\n", opRemDev);
                    }
                }
            }
            if (Event->p.modeChange.curMode == BLM_ACTIVE_MODE){
                if (opRemDev == Event->p.remDev){
                    TRACE("reconfig sniff op opRemDev:%x\n", opRemDev);
                    opRemDev = NULL;
                    currCmgrHandler = CMGR_GetAclHandler(Event->p.remDev);
                    if (currCmgrHandler){
                        app_bt_CMGR_SetSniffTimer(currCmgrHandler, NULL, 1000);
                    }
                }
            }
            break;
        case BTEVENT_ACL_DATA_ACTIVE:
            CmgrHandler    *cmgrHandler;
            /* Start the sniff timer */
            cmgrHandler = CMGR_GetAclHandler(Event->p.remDev);
            if (cmgrHandler)
                app_bt_CMGR_SetSniffTimer(cmgrHandler, NULL, CMGR_SNIFF_TIMER);
            break;
        case BTEVENT_SCO_CONNECT_IND:
        case BTEVENT_SCO_CONNECT_CNF:
            app_bt_Me_SetLinkPolicy(Event->p.scoConnect.remDev, BLP_DISABLE_ALL);
            if (MEC(activeCons) > 1){
                for (uint8_t i=0; i<BT_DEVICE_NUM; i++){
                    remDev = MeEnumerateRemoteDevices(i);
                    if (Event->p.remDev == remDev){
                        continue;
                    }
                    otherCmgrHandler = CMGR_GetConnIndHandler(remDev);
                    if (otherCmgrHandler){
                        if (CMGR_IsLinkUp(otherCmgrHandler)){
                            if (ME_GetCurrentMode(remDev) == BLM_ACTIVE_MODE){
                                TRACE(" ohter dev disable sniff");
                                app_bt_Me_SetLinkPolicy(remDev, BLP_DISABLE_ALL);
                            }else if (ME_GetCurrentMode(remDev) == BLM_SNIFF_MODE){
                                TRACE(" ohter dev exit & disable sniff");
                                app_bt_ME_StopSniff(remDev);
                                app_bt_Me_SetLinkPolicy(remDev, BLP_DISABLE_ALL);
                            } 
                        }
                    }
                }
            }           
            break;
        case BTEVENT_SCO_DISCONNECT:
            if (MEC(activeCons) == 1){
                app_bt_Me_SetLinkPolicy(Event->p.scoConnect.remDev, BLP_SNIFF_MODE);
            }else{
                for (uint8_t i=0; i<BT_DEVICE_NUM; i++){
                    remDev = MeEnumerateRemoteDevices(i);
                    if (Event->p.remDev == remDev){
                        continue;
                    }
                    otherCmgrHandler = CMGR_GetConnIndHandler(remDev);
                }
                TRACE("SCO_DISCONNECT:%d%/%d %x/%x\n", CMGR_IsAudioUp(currCmgrHandler), CMGR_IsAudioUp(otherCmgrHandler),
                                                 CMGR_GetRemDev(currCmgrHandler), Event->p.remDev);
                if (otherCmgrHandler){
                    if (!CMGR_IsAudioUp(otherCmgrHandler)){                        
                        TRACE("enable sniff to all\n");
                        app_bt_Me_SetLinkPolicy(Event->p.scoConnect.remDev, BLP_SNIFF_MODE);
                        app_bt_Me_SetLinkPolicy(CMGR_GetRemDev(otherCmgrHandler), BLP_SNIFF_MODE);
                    }
                }else{
                    app_bt_Me_SetLinkPolicy(Event->p.scoConnect.remDev, BLP_SNIFF_MODE);
                }
            }
            break;
        default:
            break;
    }
}

APP_BT_GOLBAL_HANDLE_HOOK_HANDLER app_bt_golbal_handle_hook_handler[APP_BT_GOLBAL_HANDLE_HOOK_USER_QTY] = {0};
void app_bt_golbal_handle_hook(const BtEvent *Event)
{
    uint8_t i;
    for (i=0; i<APP_BT_GOLBAL_HANDLE_HOOK_USER_QTY; i++){
        if (app_bt_golbal_handle_hook_handler[i])
            app_bt_golbal_handle_hook_handler[i](Event);
    }
}

int app_bt_golbal_handle_hook_set(enum APP_BT_GOLBAL_HANDLE_HOOK_USER_T user, APP_BT_GOLBAL_HANDLE_HOOK_HANDLER handler)
{
    app_bt_golbal_handle_hook_handler[user] = handler;
    return 0;
}

APP_BT_GOLBAL_HANDLE_HOOK_HANDLER app_bt_golbal_handle_hook_get(enum APP_BT_GOLBAL_HANDLE_HOOK_USER_T user)
{
    return app_bt_golbal_handle_hook_handler[user];
}

/////There is a device connected, so stop PAIR_TIMER and POWEROFF_TIMER of earphone.
BtHandler  app_bt_handler;
static void app_bt_golbal_handle(const BtEvent *Event)
{
    switch (Event->eType) {
        case BTEVENT_HCI_COMMAND_SENT:
		case BTEVENT_ACL_DATA_NOT_ACTIVE:
            return;
        case BTEVENT_ACL_DATA_ACTIVE:
            CmgrHandler    *cmgrHandler;
            /* Start the sniff timer */
            cmgrHandler = CMGR_GetAclHandler(Event->p.remDev);
            if (cmgrHandler)
                app_bt_CMGR_SetSniffTimer(cmgrHandler, NULL, CMGR_SNIFF_TIMER);
            return;
    }
    
    TRACE("app_bt_golbal_handle evt = %d",Event->eType);

    switch (Event->eType) {
        case BTEVENT_LINK_CONNECT_IND: 
        case BTEVENT_LINK_CONNECT_CNF:            
            TRACE("CONNECT_IND/CNF evt:%d errCode:0x%0x newRole:%d activeCons:%d",Event->eType, Event->errCode, Event->p.remDev->role, MEC(activeCons));            
#if defined(__EARPHONE__) && defined(__AUTOPOWEROFF__)  && !defined(FPGA)
            if (MEC(activeCons) == 0){
                app_start_10_second_timer(APP_POWEROFF_TIMER_ID);
            }else{
                app_stop_10_second_timer(APP_POWEROFF_TIMER_ID);
            }
#endif
#ifdef __BT_ONE_BRING_TWO__
            if (MEC(activeCons) > 2){
                app_bt_MeDisconnectLink(Event->p.remDev);
            }
#else
            if (MEC(activeCons) > 1){
                app_bt_MeDisconnectLink(Event->p.remDev);
            }
#endif
            break;
        case BTEVENT_LINK_DISCONNECT:
            TRACE("DISCONNECT evt = %d",Event->eType);
#if defined(__EARPHONE__) && defined(__AUTOPOWEROFF__) && !defined(FPGA)
            if (MEC(activeCons) == 0){
                app_start_10_second_timer(APP_POWEROFF_TIMER_ID);
            }
#endif
            break;
        case BTEVENT_ROLE_CHANGE:
            TRACE("ROLE_CHANGE eType:0x%x errCode:0x%x newRole:%d activeCons:%d", Event->eType, Event->errCode, Event->p.roleChange.newRole, MEC(activeCons));
            break;
        case BTEVENT_MODE_CHANGE:
            TRACE("MODE_CHANGE evt:%d errCode:0x%0x curMode=0x%0x, interval=%d ",Event->eType, Event->errCode,Event->p.modeChange.curMode,Event->p.modeChange.interval);
            break;
        case BTEVENT_ACCESSIBLE_CHANGE:
            TRACE("ACCESSIBLE_CHANGE evt:%d errCode:0x%0x aMode=0x%0x",Event->eType, Event->errCode,Event->p.aMode); 
            break;
        default:
            break;
    }
    app_bt_role_manager_process(Event);
    app_bt_accessible_manager_process(Event);
    app_bt_sniff_manager_process(Event);
    app_bt_golbal_handle_hook(Event);
}

void app_bt_golbal_handle_init(void)
{
    BtEventMask mask = BEM_NO_EVENTS;
    ME_InitHandler(&app_bt_handler);
    app_bt_handler.callback = app_bt_golbal_handle;
    ME_RegisterGlobalHandler(&app_bt_handler);
    mask |= BEM_ROLE_CHANGE | BEM_SCO_CONNECT_CNF | BEM_SCO_DISCONNECT | BEM_SCO_CONNECT_IND;

    mask |= BEM_LINK_CONNECT_IND;
    mask |= BEM_LINK_DISCONNECT;
    mask |= BEM_LINK_CONNECT_CNF;
    mask |= BEM_ACCESSIBLE_CHANGE;
#ifdef __BT_ONE_BRING_TWO__     
    mask |= BEM_MODE_CHANGE;
#endif  
    app_bt_ME_SetConnectionRole(BCR_ANY);
    for (uint8_t i=0; i<BT_DEVICE_NUM; i++){
        app_bt_A2DP_SetMasterRole(&app_bt_device.a2dp_stream[i], FALSE);
#if defined(A2DP_AAC_ON)
        app_bt_A2DP_SetMasterRole(&app_bt_device.a2dp_aac_stream[i], FALSE);
#endif
        app_bt_HF_SetMasterRole(&app_bt_device.hf_channel[i], FALSE);
    }
    ME_SetEventMask(&app_bt_handler, mask);
    app_bt_sniff_manager_init();
    app_bt_accessmode_timer = osTimerCreate (osTimer(APP_BT_ACCESSMODE_TIMER), osTimerOnce, &app_bt_accessmode_timer_argument);
}

void app_bt_send_request(uint32_t message_id, uint32_t param0, uint32_t param1, uint32_t ptr)
{
    APP_MESSAGE_BLOCK msg;
    
    TRACE("app_bt_send_request: %d\n", message_id);
    msg.mod_id = APP_MODUAL_BT;
    msg.msg_body.message_id = message_id;
    msg.msg_body.message_Param0 = param0;
    msg.msg_body.message_Param1 = param1;
    msg.msg_body.message_ptr = ptr;
    app_mailbox_put(&msg);
}


static int app_bt_handle_process(APP_MESSAGE_BODY *msg_body)
{
    BtAccessibleMode old_access_mode;
    TRACE("app_bt_handle_process: %d\n", msg_body->message_id);

    switch (msg_body->message_id) {
        case APP_BT_REQ_ACCESS_MODE_SET:
            old_access_mode = g_bt_access_mode;
            app_bt_accessmode_set(msg_body->message_Param0);
            if (msg_body->message_Param0 == BAM_GENERAL_ACCESSIBLE &&         
                old_access_mode != BAM_GENERAL_ACCESSIBLE){
#if FPGA == 0
                app_status_indication_set(APP_STATUS_INDICATION_BOTHSCAN);
                app_voice_report(APP_STATUS_INDICATION_BOTHSCAN, 0);
                app_start_10_second_timer(APP_PAIR_TIMER_ID);
#endif
            }else{
                //app_status_indication_set(APP_STATUS_INDICATION_PAGESCAN);
            }
            break;
        default:
            break;
    }

    return 0;
}

void *app_bt_profile_active_store_ptr_get(uint8_t *bdAddr)
{
    static btdevice_profile device_profile = {false, false, false,0};
    btdevice_profile *ptr;
    nvrec_btdevicerecord *record = NULL;

#ifndef FPGA
    if (!nv_record_btdevicerecord_find((BT_BD_ADDR *)bdAddr,&record)){
        ptr = &(record->device_plf);
        DUMP8("0x%02x ", bdAddr, BD_ADDR_SIZE);
        TRACE("%s hfp_act:%d hsp_act:%d a2dp_act:0x%x codec_type=%x", __func__, ptr->hfp_act, ptr->hsp_act, ptr->a2dp_act,ptr->a2dp_codectype);
    }else
#endif
    {
        ptr = &device_profile;
        TRACE("%s default", __func__);
    }
    return (void *)ptr;
}

enum bt_profile_reconnect_mode{
    bt_profile_reconnect_null,
    bt_profile_reconnect_openreconnecting,
    bt_profile_reconnect_reconnecting,
};

enum bt_profile_connect_status{
    bt_profile_connect_status_unknow,
    bt_profile_connect_status_success,
    bt_profile_connect_status_failure,
};

struct app_bt_profile_manager{
    bool has_connected;
    enum bt_profile_connect_status hfp_connect;
    enum bt_profile_connect_status hsp_connect;
    enum bt_profile_connect_status a2dp_connect;
    BT_BD_ADDR rmt_addr;
    bt_profile_reconnect_mode reconnect_mode;
    A2dpStream *stream;
    HfChannel *chan;
    uint16_t reconnect_cnt;    
    osTimerId connect_timer;    
    void (* connect_timer_cb)(void const *);
};

//reconnect = (INTERVAL+PAGETO)*CNT = (3000ms+5000ms)*15 = 120s
#define APP_BT_PROFILE_RECONNECT_RETRY_INTERVAL_MS (3000)
#define APP_BT_PROFILE_RECONNECT_RETRY_LIMIT_CNT (15)
#define APP_BT_PROFILE_CONNECT_RETRY_MS (10000)

static struct app_bt_profile_manager bt_profile_manager[BT_DEVICE_NUM];
static void app_bt_profile_reconnect_timehandler(void const *param);
 
osTimerDef (BT_PROFILE_CONNECT_TIMER0, app_bt_profile_reconnect_timehandler);                      // define timers
#ifdef __BT_ONE_BRING_TWO__
osTimerDef (BT_PROFILE_CONNECT_TIMER1, app_bt_profile_reconnect_timehandler);
#endif

static void app_bt_profile_connect_hf_retry_timehandler(void const *param)
{
    struct app_bt_profile_manager *bt_profile_manager_p = (struct app_bt_profile_manager *)param;    
    app_bt_HF_CreateServiceLink(bt_profile_manager_p->chan, &bt_profile_manager_p->rmt_addr);
}

static void app_bt_profile_connect_a2dp_retry_timehandler(void const *param)
{
    struct app_bt_profile_manager *bt_profile_manager_p = (struct app_bt_profile_manager *)param;    
    app_bt_A2DP_OpenStream(bt_profile_manager_p->stream, &bt_profile_manager_p->rmt_addr);
}

static void app_bt_profile_reconnect_timehandler(void const *param)
{
    struct app_bt_profile_manager *bt_profile_manager_p = (struct app_bt_profile_manager *)param;
    btdevice_profile *btdevice_plf_p = (btdevice_profile *)app_bt_profile_active_store_ptr_get(bt_profile_manager_p->rmt_addr.addr);
    if (bt_profile_manager_p->connect_timer_cb){
        bt_profile_manager_p->connect_timer_cb(param);
        bt_profile_manager_p->connect_timer_cb = NULL;
    }else{
        if (btdevice_plf_p->hfp_act){
            TRACE("try connect hf");
            app_bt_HF_CreateServiceLink(bt_profile_manager_p->chan, &bt_profile_manager_p->rmt_addr);
        }else if(btdevice_plf_p->a2dp_act){
            TRACE("try connect a2dp");
            app_bt_A2DP_OpenStream(bt_profile_manager_p->stream, &bt_profile_manager_p->rmt_addr);
        }
    }
}

void app_bt_profile_connect_manager_open(void)
{
    uint8_t i=0;
    for (i=0;i<BT_DEVICE_NUM;i++){
        bt_profile_manager[i].has_connected = false;
        bt_profile_manager[i].hfp_connect = bt_profile_connect_status_unknow;
        bt_profile_manager[i].hsp_connect = bt_profile_connect_status_unknow;
        bt_profile_manager[i].a2dp_connect = bt_profile_connect_status_unknow;
        memset(bt_profile_manager[i].rmt_addr.addr,0 ,0);
        bt_profile_manager[i].reconnect_mode = bt_profile_reconnect_null;
        bt_profile_manager[i].stream = NULL;
        bt_profile_manager[i].chan = NULL;
        bt_profile_manager[i].reconnect_cnt = 0;
        bt_profile_manager[i].connect_timer_cb = NULL;
    }
    
    bt_profile_manager[BT_DEVICE_ID_1].connect_timer = osTimerCreate (osTimer(BT_PROFILE_CONNECT_TIMER0), osTimerOnce, &bt_profile_manager[BT_DEVICE_ID_1]);
#ifdef __BT_ONE_BRING_TWO__        
    bt_profile_manager[BT_DEVICE_ID_2].connect_timer = osTimerCreate (osTimer(BT_PROFILE_CONNECT_TIMER1), osTimerOnce, &bt_profile_manager[BT_DEVICE_ID_2]);
#endif
}

BOOL app_bt_profile_connect_openreconnecting(void *ptr)
{
    bool nRet = false;
    uint8_t i;

    for (i=0;i<BT_DEVICE_NUM;i++){
        nRet |= bt_profile_manager[i].reconnect_mode == bt_profile_reconnect_openreconnecting ? true : false;
    }

    return nRet;
}

void app_bt_profile_connect_manager_opening_reconnect(void)
{
    int ret;
    BtDeviceRecord record1;
    BtDeviceRecord record2;
    btdevice_profile *btdevice_plf_p;
    int find_invalid_record_cnt;
#if defined(APP_LINEIN_A2DP_SOURCE)||defined(APP_I2S_A2DP_SOURCE)
	if(app_bt_device.src_or_snk==BT_DEVICE_SRC)
	{
		return ;
	}
#endif
    OS_LockStack();
    do{
        find_invalid_record_cnt = 0;
        ret = nv_record_enum_latest_two_paired_dev(&record1,&record2);
        if(ret == 1){
            btdevice_plf_p = (btdevice_profile *)app_bt_profile_active_store_ptr_get(record1.bdAddr.addr);
            if (!(btdevice_plf_p->hfp_act)&&!(btdevice_plf_p->a2dp_act)){
                nv_record_ddbrec_delete(&record1.bdAddr);
                find_invalid_record_cnt++;
            }
        }else if(ret == 2){
            btdevice_plf_p = (btdevice_profile *)app_bt_profile_active_store_ptr_get(record1.bdAddr.addr);
            if (!(btdevice_plf_p->hfp_act)&&!(btdevice_plf_p->a2dp_act)){
                nv_record_ddbrec_delete(&record1.bdAddr);
                find_invalid_record_cnt++;
            }
            btdevice_plf_p = (btdevice_profile *)app_bt_profile_active_store_ptr_get(record2.bdAddr.addr);
            if (!(btdevice_plf_p->hfp_act)&&!(btdevice_plf_p->a2dp_act)){
                nv_record_ddbrec_delete(&record2.bdAddr);
                find_invalid_record_cnt++;
            }
        }
    }while(find_invalid_record_cnt);
    
    TRACE("!!!app_bt_opening_reconnect:\n");
    DUMP8("%02x ", &record1.bdAddr, 6);
    DUMP8("%02x ", &record2.bdAddr, 6);


    if(ret > 0){
        TRACE("!!!start reconnect first device\n");

        if(MEC(pendCons) == 0){
            bt_profile_manager[BT_DEVICE_ID_1].reconnect_mode = bt_profile_reconnect_openreconnecting;
            memcpy(bt_profile_manager[BT_DEVICE_ID_1].rmt_addr.addr, record1.bdAddr.addr, BD_ADDR_SIZE);
            btdevice_plf_p = (btdevice_profile *)app_bt_profile_active_store_ptr_get(bt_profile_manager[BT_DEVICE_ID_1].rmt_addr.addr);
#if defined(A2DP_AAC_ON)
            if(btdevice_plf_p->a2dp_codectype == AVDTP_CODEC_TYPE_MPEG2_4_AAC)
                bt_profile_manager[BT_DEVICE_ID_1].stream = &app_bt_device.a2dp_aac_stream[BT_DEVICE_ID_1];
            else
                bt_profile_manager[BT_DEVICE_ID_1].stream = &app_bt_device.a2dp_stream[BT_DEVICE_ID_1];
                
#else
            bt_profile_manager[BT_DEVICE_ID_1].stream = &app_bt_device.a2dp_stream[BT_DEVICE_ID_1];
#endif

            
            bt_profile_manager[BT_DEVICE_ID_1].chan = &app_bt_device.hf_channel[BT_DEVICE_ID_1];

            if (btdevice_plf_p->hfp_act){
                TRACE("try connect hf");
                app_bt_HF_CreateServiceLink(bt_profile_manager[BT_DEVICE_ID_1].chan, &bt_profile_manager[BT_DEVICE_ID_1].rmt_addr);
            }else if(btdevice_plf_p->a2dp_act) {            
                TRACE("try connect a2dp");
                app_bt_A2DP_OpenStream(bt_profile_manager[BT_DEVICE_ID_1].stream, &bt_profile_manager[BT_DEVICE_ID_1].rmt_addr);
            }
        }
#ifdef __BT_ONE_BRING_TWO__        
        if(ret > 1){            
            TRACE("!!!need reconnect second device\n");
            bt_profile_manager[BT_DEVICE_ID_2].reconnect_mode = bt_profile_reconnect_openreconnecting;
            memcpy(bt_profile_manager[BT_DEVICE_ID_2].rmt_addr.addr, record2.bdAddr.addr, BD_ADDR_SIZE);
            btdevice_plf_p = (btdevice_profile *)app_bt_profile_active_store_ptr_get(bt_profile_manager[BT_DEVICE_ID_2].rmt_addr.addr);
#if defined(A2DP_AAC_ON)
            if(btdevice_plf_p->a2dp_codectype == AVDTP_CODEC_TYPE_MPEG2_4_AAC)
                bt_profile_manager[BT_DEVICE_ID_2].stream = &app_bt_device.a2dp_aac_stream[BT_DEVICE_ID_2];
            else
                bt_profile_manager[BT_DEVICE_ID_2].stream = &app_bt_device.a2dp_stream[BT_DEVICE_ID_2];
                
#else
            bt_profile_manager[BT_DEVICE_ID_2].stream = &app_bt_device.a2dp_stream[BT_DEVICE_ID_2];
#endif 
            bt_profile_manager[BT_DEVICE_ID_2].chan = &app_bt_device.hf_channel[BT_DEVICE_ID_2];
        }
#endif        
    }

    else
    {
        TRACE("!!!go to pairing\n");
#ifdef __EARPHONE_STAY_BOTH_SCAN__
        app_bt_accessmode_set_req(BT_DEFAULT_ACCESS_MODE_PAIR);
#else
        app_bt_accessmode_set_req(BAM_CONNECTABLE_ONLY);
#endif

    }
    OS_UnlockStack();
}

void app_bt_profile_connect_manager_hf(enum BT_DEVICE_ID_T id, HfChannel *Chan, HfCallbackParms *Info)
{
    btdevice_profile *btdevice_plf_p = (btdevice_profile *)app_bt_profile_active_store_ptr_get((uint8_t *)Info->p.remDev->bdAddr.addr);
    
    osTimerStop(bt_profile_manager[id].connect_timer);
    bt_profile_manager[id].connect_timer_cb = NULL;
    if (Chan&&Info){
        switch(Info->event)
        {
            case HF_EVENT_SERVICE_CONNECTED:                
                TRACE("%s HF_EVENT_SERVICE_CONNECTED",__func__);
				if(app_bt_device.src_or_snk==BT_DEVICE_SNK)
		 app_status_indication_set(APP_STATUS_INDICATION_CONNECTED);
		else
			//app_status_indication_set(APP_STATUS_INDICATION_SRC_CONNECTED);
                btdevice_plf_p->hfp_act = true;
#ifndef FPGA
                nv_record_touch_cause_flush();
#endif
                bt_profile_manager[id].hfp_connect = bt_profile_connect_status_success;                 
                bt_profile_manager[id].reconnect_cnt = 0;
                bt_profile_manager[id].chan = &app_bt_device.hf_channel[id];
                memcpy(bt_profile_manager[id].rmt_addr.addr, Info->p.remDev->bdAddr.addr, BD_ADDR_SIZE);
                if (bt_profile_manager[id].reconnect_mode == bt_profile_reconnect_openreconnecting){
                    //do nothing
                }else if (bt_profile_manager[id].reconnect_mode == bt_profile_reconnect_reconnecting){
                    if (btdevice_plf_p->a2dp_act && bt_profile_manager[id].a2dp_connect != bt_profile_connect_status_success){                
                        TRACE("!!!continue connect a2dp\n");
                         app_bt_A2DP_OpenStream(bt_profile_manager[id].stream, &bt_profile_manager[id].rmt_addr);
                    }
                }
#ifdef __AUTO_CONNECT_OHTER_PROFILE__
                else{
                    bt_profile_manager[id].connect_timer_cb = app_bt_profile_connect_a2dp_retry_timehandler;
                    osTimerStart(bt_profile_manager[id].connect_timer, APP_BT_PROFILE_CONNECT_RETRY_MS);
                }
#endif
                break;            
            case HF_EVENT_SERVICE_DISCONNECTED:                
                TRACE("%s HF_EVENT_SERVICE_DISCONNECTED discReason:%d",__func__, Info->p.remDev->discReason);
				app_status_indication_set(APP_STATUS_INDICATION_CONNECTABLE);
                bt_profile_manager[id].hfp_connect = bt_profile_connect_status_failure;                
                if (bt_profile_manager[id].reconnect_mode == bt_profile_reconnect_openreconnecting){
                    //do nothing
                }else if (bt_profile_manager[id].reconnect_mode == bt_profile_reconnect_reconnecting){
                    if (++bt_profile_manager[id].reconnect_cnt < APP_BT_PROFILE_RECONNECT_RETRY_LIMIT_CNT){                        
                        osTimerStart(bt_profile_manager[id].connect_timer, APP_BT_PROFILE_RECONNECT_RETRY_INTERVAL_MS);
                    }else{
                        bt_profile_manager[id].reconnect_mode = bt_profile_reconnect_null;                    
                    }                    
                    TRACE("%s try to reconnect cnt:%d",__func__, bt_profile_manager[id].reconnect_cnt);
                }else if(Info->p.remDev->discReason == 0x8){
                    bt_profile_manager[id].reconnect_mode = bt_profile_reconnect_reconnecting;                    
                    TRACE("%s try to reconnect",__func__);
                    osTimerStart(bt_profile_manager[id].connect_timer, APP_BT_PROFILE_RECONNECT_RETRY_INTERVAL_MS);
                }else{
                    bt_profile_manager[id].hfp_connect = bt_profile_connect_status_unknow;
                }
                break;
            default:
                break;                
        }
    }
    
    if (bt_profile_manager[id].reconnect_mode == bt_profile_reconnect_reconnecting){        
        bool reconnect_hfp_proc_final = true;
        bool reconnect_a2dp_proc_final = true;
        if (bt_profile_manager[id].hfp_connect == bt_profile_connect_status_failure){
            reconnect_hfp_proc_final = false;
        }
        if (bt_profile_manager[id].a2dp_connect == bt_profile_connect_status_failure){
            reconnect_a2dp_proc_final = false;
        }
        if (reconnect_hfp_proc_final && reconnect_a2dp_proc_final){
            TRACE("!!!reconnect success %d/%d/%d\n", bt_profile_manager[id].hfp_connect, bt_profile_manager[id].hsp_connect, bt_profile_manager[id].a2dp_connect);
            bt_profile_manager[id].reconnect_mode = bt_profile_reconnect_null;            
        }            
    }else if (bt_profile_manager[id].reconnect_mode == bt_profile_reconnect_openreconnecting){
        bool opening_hfp_proc_final = false;
        bool opening_a2dp_proc_final = false;

        if (btdevice_plf_p->hfp_act && bt_profile_manager[id].hfp_connect == bt_profile_connect_status_unknow){
            opening_hfp_proc_final = false;
        }else{
            opening_hfp_proc_final = true;
        }

        if (btdevice_plf_p->a2dp_act && bt_profile_manager[id].a2dp_connect == bt_profile_connect_status_unknow){
            opening_a2dp_proc_final = false;
        }else{
            opening_a2dp_proc_final = true;
        }

        if ((opening_hfp_proc_final && opening_a2dp_proc_final) ||
            (bt_profile_manager[id].hfp_connect == bt_profile_connect_status_failure)){        
            TRACE("!!!reconnect success %d/%d/%d\n", bt_profile_manager[id].hfp_connect, bt_profile_manager[id].hsp_connect, bt_profile_manager[id].a2dp_connect);
            bt_profile_manager[id].reconnect_mode = bt_profile_reconnect_null;            
        }

        if (btdevice_plf_p->hfp_act && bt_profile_manager[id].hfp_connect == bt_profile_connect_status_success){
            if (btdevice_plf_p->a2dp_act && !opening_a2dp_proc_final){                
                TRACE("!!!continue connect a2dp\n");
                 app_bt_A2DP_OpenStream(bt_profile_manager[id].stream, &bt_profile_manager[id].rmt_addr);
            }
        }

        if (bt_profile_manager[id].reconnect_mode == bt_profile_reconnect_null){
            for (uint8_t i=0; i<BT_DEVICE_NUM; i++){
                if (bt_profile_manager[i].reconnect_mode == bt_profile_reconnect_openreconnecting){
                    TRACE("!!!hf->start reconnect second device\n");
                    if (btdevice_plf_p->hfp_act){
                        TRACE("try connect hf");
                        app_bt_HF_CreateServiceLink(bt_profile_manager[i].chan, &bt_profile_manager[i].rmt_addr);
                    }else if(btdevice_plf_p->a2dp_act) {            
                        TRACE("try connect a2dp");
                        app_bt_A2DP_OpenStream(bt_profile_manager[i].stream, &bt_profile_manager[i].rmt_addr);
                    }
                    break;
                }
            }
        }
    }

    if (!bt_profile_manager[id].has_connected &&
        (bt_profile_manager[id].hfp_connect == bt_profile_connect_status_success ||
         bt_profile_manager[id].hsp_connect == bt_profile_connect_status_success||
         bt_profile_manager[id].a2dp_connect == bt_profile_connect_status_success)){

        bt_profile_manager[id].has_connected = true;
#ifdef MEDIA_PLAYER_SUPPORT
        app_voice_report(APP_STATUS_INDICATION_CONNECTED, id);
#endif
    }

    if (bt_profile_manager[id].has_connected &&
        (bt_profile_manager[id].hfp_connect != bt_profile_connect_status_success &&
         bt_profile_manager[id].hsp_connect != bt_profile_connect_status_success &&
         bt_profile_manager[id].a2dp_connect != bt_profile_connect_status_success)){

        bt_profile_manager[id].has_connected = false;
		if(app_bt_device.src_or_snk==BT_DEVICE_SNK)
		 app_status_indication_set(APP_STATUS_INDICATION_CONNECTABLE);
		else
			app_status_indication_set(APP_STATUS_INDICATION_SRC_CONNECTABLE);
#ifdef MEDIA_PLAYER_SUPPORT
        app_voice_report(APP_STATUS_INDICATION_DISCONNECTED, id);
#endif
    }
}

void app_bt_profile_connect_manager_a2dp(enum BT_DEVICE_ID_T id, A2dpStream *Stream, const A2dpCallbackParms *Info)
{
    btdevice_profile *btdevice_plf_p = (btdevice_profile *)app_bt_profile_active_store_ptr_get((uint8_t *)Stream->stream.conn.remDev->bdAddr.addr);

    osTimerStop(bt_profile_manager[id].connect_timer);
    bt_profile_manager[id].connect_timer_cb = NULL;
    if (Stream&&Info){
        switch(Info->event)
        {
            case A2DP_EVENT_STREAM_OPEN:                
                TRACE("%s A2DP_EVENT_STREAM_OPEN,codec type=%x",__func__,Info->p.configReq->codec.codecType);
                btdevice_plf_p->a2dp_act = true;         
                btdevice_plf_p->a2dp_codectype = Info->p.configReq->codec.codecType; 
#ifndef FPGA
                nv_record_touch_cause_flush();
#endif
                bt_profile_manager[id].a2dp_connect = bt_profile_connect_status_success;
                bt_profile_manager[id].reconnect_cnt = 0;
                bt_profile_manager[id].stream = app_bt_device.a2dp_connected_stream[id];
                memcpy(bt_profile_manager[id].rmt_addr.addr, Stream->stream.conn.remDev->bdAddr.addr, BD_ADDR_SIZE);
                if (bt_profile_manager[id].reconnect_mode == bt_profile_reconnect_openreconnecting){
                    //do nothing
                }else if (bt_profile_manager[id].reconnect_mode == bt_profile_reconnect_reconnecting){
                    if (btdevice_plf_p->hfp_act&& bt_profile_manager[id].hfp_connect != bt_profile_connect_status_success){                
                        TRACE("!!!continue connect hfp\n");
                        app_bt_HF_CreateServiceLink(bt_profile_manager[id].chan, &bt_profile_manager[id].rmt_addr);
                    }
                }
#ifdef __AUTO_CONNECT_OHTER_PROFILE__
                else{
                    bt_profile_manager[id].connect_timer_cb = app_bt_profile_connect_hf_retry_timehandler;
                    osTimerStart(bt_profile_manager[id].connect_timer, APP_BT_PROFILE_CONNECT_RETRY_MS);
                }
#endif
                break;            
            case A2DP_EVENT_STREAM_CLOSED:                
                TRACE("%s A2DP_EVENT_STREAM_CLOSED %d",__func__, Info->discReason);
                bt_profile_manager[id].a2dp_connect = bt_profile_connect_status_failure;
                
                if (bt_profile_manager[id].reconnect_mode == bt_profile_reconnect_openreconnecting){
                   //do nothing
                }else if (bt_profile_manager[id].reconnect_mode == bt_profile_reconnect_reconnecting){
                   if (++bt_profile_manager[id].reconnect_cnt < APP_BT_PROFILE_RECONNECT_RETRY_LIMIT_CNT){
                       osTimerStart(bt_profile_manager[id].connect_timer, APP_BT_PROFILE_RECONNECT_RETRY_INTERVAL_MS);
                   }else{
                       bt_profile_manager[id].reconnect_mode = bt_profile_reconnect_null;                    
                   }
                   TRACE("%s try to reconnect cnt:%d",__func__, bt_profile_manager[id].reconnect_cnt);
                }else if((Info->discReason == 0x8) && 
                        (btdevice_plf_p->a2dp_act)&&
                        (!btdevice_plf_p->hfp_act) &&
                        (!btdevice_plf_p->hsp_act)){
                    bt_profile_manager[id].reconnect_mode = bt_profile_reconnect_reconnecting;                    
                    TRACE("%s try to reconnect cnt:%d",__func__, bt_profile_manager[id].reconnect_cnt);
                    osTimerStart(bt_profile_manager[id].connect_timer, APP_BT_PROFILE_RECONNECT_RETRY_INTERVAL_MS);
               }else{
                    bt_profile_manager[id].a2dp_connect = bt_profile_connect_status_unknow;
               }
               break;
            default:
                break;                
        }
    }

    if (bt_profile_manager[id].reconnect_mode == bt_profile_reconnect_reconnecting){        
        bool reconnect_hfp_proc_final = true;
        bool reconnect_a2dp_proc_final = true;
        if (bt_profile_manager[id].hfp_connect == bt_profile_connect_status_failure){
            reconnect_hfp_proc_final = false;
        }
        if (bt_profile_manager[id].a2dp_connect == bt_profile_connect_status_failure){
            reconnect_a2dp_proc_final = false;
        }
        if (reconnect_hfp_proc_final && reconnect_a2dp_proc_final){
            TRACE("!!!reconnect success %d/%d/%d\n", bt_profile_manager[id].hfp_connect, bt_profile_manager[id].hsp_connect, bt_profile_manager[id].a2dp_connect);
            bt_profile_manager[id].reconnect_mode = bt_profile_reconnect_null;            
        }            
    }else if (bt_profile_manager[id].reconnect_mode == bt_profile_reconnect_openreconnecting){
        bool opening_hfp_proc_final = false;
        bool opening_a2dp_proc_final = false;

        if (btdevice_plf_p->hfp_act && bt_profile_manager[id].hfp_connect == bt_profile_connect_status_unknow){
            opening_hfp_proc_final = false;
        }else{
            opening_hfp_proc_final = true;
        }

        if (btdevice_plf_p->a2dp_act && bt_profile_manager[id].a2dp_connect == bt_profile_connect_status_unknow){
            opening_a2dp_proc_final = false;
        }else{
            opening_a2dp_proc_final = true;
        }

        if ((opening_hfp_proc_final && opening_a2dp_proc_final) ||
            (bt_profile_manager[id].a2dp_connect == bt_profile_connect_status_failure)){        
            TRACE("!!!reconnect success %d/%d/%d\n", bt_profile_manager[id].hfp_connect, bt_profile_manager[id].hsp_connect, bt_profile_manager[id].a2dp_connect);
            bt_profile_manager[id].reconnect_mode = bt_profile_reconnect_null;
        }

        if (btdevice_plf_p->a2dp_act && bt_profile_manager[id].a2dp_connect== bt_profile_connect_status_success){
            if (btdevice_plf_p->hfp_act && !opening_hfp_proc_final){                
                TRACE("!!!continue connect hf\n");
                app_bt_HF_CreateServiceLink(bt_profile_manager[id].chan, &bt_profile_manager[id].rmt_addr);
            }
        }

        if (bt_profile_manager[id].reconnect_mode == bt_profile_reconnect_null){
            for (uint8_t i=0; i<BT_DEVICE_NUM; i++){
                if (bt_profile_manager[i].reconnect_mode == bt_profile_reconnect_openreconnecting){
                    TRACE("!!!a2dp->start reconnect second device\n");
                    if (btdevice_plf_p->hfp_act){
                        TRACE("try connect hf");
                        app_bt_HF_CreateServiceLink(bt_profile_manager[i].chan, &bt_profile_manager[i].rmt_addr);
                    }else if(btdevice_plf_p->a2dp_act) {            
                        TRACE("try connect a2dp");
                         app_bt_A2DP_OpenStream(bt_profile_manager[i].stream, &bt_profile_manager[i].rmt_addr);
                     }
                    break;
                }
            }
        }
    }
    
    if (!bt_profile_manager[id].has_connected &&
        (bt_profile_manager[id].hfp_connect == bt_profile_connect_status_success ||
         bt_profile_manager[id].hsp_connect == bt_profile_connect_status_success||
         bt_profile_manager[id].a2dp_connect == bt_profile_connect_status_success)){

        bt_profile_manager[id].has_connected = true;
		if(app_bt_device.src_or_snk==BT_DEVICE_SNK)
		 app_status_indication_set(APP_STATUS_INDICATION_CONNECTED);
		else
			//app_status_indication_set(APP_STATUS_INDICATION_SRC_CONNECTED);
#ifdef MEDIA_PLAYER_SUPPORT
        app_voice_report(APP_STATUS_INDICATION_CONNECTED, id);
#endif
    }

    if (bt_profile_manager[id].has_connected &&
        
         (bt_profile_manager[id].a2dp_connect != bt_profile_connect_status_success)){

        bt_profile_manager[id].has_connected = false;
		
		if(app_bt_device.src_or_snk==BT_DEVICE_SNK)
		app_status_indication_set(APP_STATUS_INDICATION_CONNECTABLE);
		else
			app_status_indication_set(APP_STATUS_INDICATION_SRC_CONNECTABLE);
#ifdef MEDIA_PLAYER_SUPPORT
        app_voice_report(APP_STATUS_INDICATION_DISCONNECTED, id);
#endif
    }

}

BtStatus LinkDisconnectDirectly(void)
{
    OS_LockStack();
	int conid;
	
	if(bt_profile_manager[0].a2dp_connect == bt_profile_connect_status_success){
	   BtRemoteDevice *remDev = NULL;
       remDev = A2DP_GetRemoteDevice(app_bt_device.a2dp_connected_stream[0]);
	   app_bt_MeDisconnectLink(remDev);
	 }
	
    TRACE("%s id1 hf:%d a2dp:%d",__func__, app_bt_device.hf_channel[BT_DEVICE_ID_1].state, app_bt_device.a2dp_stream[BT_DEVICE_ID_1].stream.state);
    //if(app_bt_device.hf_channel[BT_DEVICE_ID_1].state == HF_STATE_OPEN){        
       // app_bt_HF_DisconnectServiceLink(&app_bt_device.hf_channel[BT_DEVICE_ID_1]);
    //}
    if(app_bt_device.a2dp_stream[BT_DEVICE_ID_1].stream.state == AVDTP_STRM_STATE_STREAMING ||
        app_bt_device.a2dp_stream[BT_DEVICE_ID_1].stream.state == AVDTP_STRM_STATE_OPEN){
        app_bt_A2DP_CloseStream(&app_bt_device.a2dp_stream[BT_DEVICE_ID_1]);
    }
#if defined(A2DP_AAC_ON)    
    if(app_bt_device.a2dp_aac_stream[BT_DEVICE_ID_1].stream.state == AVDTP_STRM_STATE_STREAMING ||
        app_bt_device.a2dp_aac_stream[BT_DEVICE_ID_1].stream.state == AVDTP_STRM_STATE_OPEN){
        app_bt_A2DP_CloseStream(&app_bt_device.a2dp_aac_stream[BT_DEVICE_ID_1]);
    }
#endif

#ifdef __BT_ONE_BRING_TWO__
    TRACE("%s id2 hf:%d a2dp:%d",__func__, app_bt_device.hf_channel[BT_DEVICE_ID_2].state, app_bt_device.a2dp_stream[BT_DEVICE_ID_2].stream.state);
    if(app_bt_device.hf_channel[BT_DEVICE_ID_2].state == HF_STATE_OPEN){
        app_bt_HF_DisconnectServiceLink(&app_bt_device.hf_channel[BT_DEVICE_ID_2]);
    }
    if(app_bt_device.a2dp_stream[BT_DEVICE_ID_2].stream.state == AVDTP_STRM_STATE_STREAMING ||
        app_bt_device.a2dp_stream[BT_DEVICE_ID_2].stream.state == AVDTP_STRM_STATE_OPEN){
        app_bt_A2DP_CloseStream(&app_bt_device.a2dp_stream[BT_DEVICE_ID_2]);
    }
#if defined(A2DP_AAC_ON)    
    if(app_bt_device.a2dp_aac_stream[BT_DEVICE_ID_2].stream.state == AVDTP_STRM_STATE_STREAMING ||
        app_bt_device.a2dp_aac_stream[BT_DEVICE_ID_2].stream.state == AVDTP_STRM_STATE_OPEN){
        app_bt_A2DP_CloseStream(&app_bt_device.a2dp_aac_stream[BT_DEVICE_ID_2]);
    }
#endif
#endif
    OS_UnlockStack();
    return BT_STATUS_SUCCESS;
}

void app_bt_init(void)
{
    app_bt_mail_init();
    app_set_threadhandle(APP_MODUAL_BT, app_bt_handle_process);
//    SecSetIoCapRspRejectExt(app_bt_profile_connect_openreconnecting);
}

