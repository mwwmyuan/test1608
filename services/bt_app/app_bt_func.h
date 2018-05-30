#ifndef __APP_BT_FUNC_H__
#define __APP_BT_FUNC_H__

#include "cmsis_os.h"
#include "hal_trace.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "me.h"
#include "a2dp.h"
#include "hf.h"
#include "sys/mei.h"

typedef enum _bt_fn_req {
    Me_switch_sco_req                           = 0,
    ME_SetConnectionRole_req                    = 1, 
    MeDisconnectLink_req                        = 2, 
    ME_StopSniff_req                            = 3, 
    ME_SetAccessibleMode_req                    = 4, 
    Me_SetLinkPolicy_req                        = 5, 
    CMGR_SetSniffTimer_req                      = 6, 
    CMGR_SetSniffInofToAllHandlerByRemDev_req   = 7, 
    A2DP_OpenStream_req                         = 8, 
    A2DP_CloseStream_req                        = 9, 
    A2DP_SetMasterRole_req                      = 10,
    HF_CreateServiceLink_req                    = 11,
    HF_DisconnectServiceLink_req                = 12,
    HF_CreateAudioLink_req                      = 13,
    HF_DisconnectAudioLink_req                  = 14,
    HF_EnableSniffMode_req                      = 15,
    HF_SetMasterRole_req                        = 16,
}bt_fn_req;

typedef union _bt_fn_param {
    //BtStatus Me_switch_sco(uint16_t  scohandle)
    struct {
        uint16_t  scohandle;
    } Me_switch_sco_param;

    //BtConnectionRole ME_SetConnectionRole(BtConnectionRole role)
    struct {
        BtConnectionRole role;
    } BtConnectionRole_param;

    // void MeDisconnectLink(BtRemoteDevice* remDev)
    struct {
        BtRemoteDevice* remDev;
    } MeDisconnectLink_param;
    
    //BtStatus ME_StopSniff(BtRemoteDevice *remDev)
    struct {
        BtRemoteDevice* remDev;
    } ME_StopSniff_param;

    //BtStatus ME_SetAccessibleMode(BtAccessibleMode mode, const BtAccessModeInfo *info)
    struct {
        BtAccessibleMode mode;
        BtAccessModeInfo info;
    } ME_SetAccessibleMode_param;

    //BtStatus Me_SetLinkPolicy(BtRemoteDevice *remDev, BtLinkPolicy policy)
    struct {
        BtRemoteDevice *remDev;
        BtLinkPolicy policy;
    } Me_SetLinkPolicy_param;

    /*BtStatus CMGR_SetSniffTimer(CmgrHandler *Handler, 
                                BtSniffInfo* SniffInfo, 
                                TimeT Time)
       */
    struct {
        CmgrHandler *Handler;
        BtSniffInfo SniffInfo;
        TimeT Time;
    } CMGR_SetSniffTimer_param;

    /*BtStatus CMGR_SetSniffInofToAllHandlerByRemDev(BtSniffInfo* SniffInfo, 
                                                                BtRemoteDevice *RemDev)
       */
    struct {
        BtSniffInfo SniffInfo;
        BtRemoteDevice *RemDev;
    } CMGR_SetSniffInofToAllHandlerByRemDev_param;

    //BtStatus A2DP_OpenStream(A2dpStream *Stream, BT_BD_ADDR *Addr)
    struct {
        A2dpStream *Stream;
        BT_BD_ADDR *Addr;
    } A2DP_OpenStream_param;

    //BtStatus A2DP_CloseStream(A2dpStream *Stream);
    struct {
        A2dpStream *Stream;
    } A2DP_CloseStream_param;

    //BtStatus A2DP_SetMasterRole(A2dpStream *Stream, BOOL Flag);    
    struct {
        A2dpStream *Stream;
        BOOL Flag;
    } A2DP_SetMasterRole_param;

    //BtStatus HF_CreateServiceLink(HfChannel *Chan, BT_BD_ADDR *Addr)
    struct {
        HfChannel *Chan;
        BT_BD_ADDR *Addr;
    } HF_CreateServiceLink_param;

    //BtStatus HF_DisconnectServiceLink(HfChannel *Chan)
    struct {
        HfChannel *Chan;
    } HF_DisconnectServiceLink_param;

    //BtStatus HF_CreateAudioLink(HfChannel *Chan)
    struct {
        HfChannel *Chan;
    } HF_CreateAudioLink_param;

    //BtStatus HF_DisconnectAudioLink(HfChannel *Chan)
    struct {
        HfChannel *Chan;
    } HF_DisconnectAudioLink_param;

    //BtStatus HF_EnableSniffMode(HfChannel *Chan, BOOL Enable)
    struct {
        HfChannel *Chan;
        BOOL Enable;
    } HF_EnableSniffMode_param;

    //BtStatus HF_SetMasterRole(HfChannel *Chan, BOOL Flag);
    struct {
        HfChannel *Chan;
        BOOL Flag;
    } HF_SetMasterRole_param;
} bt_fn_param;

typedef struct {
    uint32_t src_thread;
    uint32_t request_id;
    bt_fn_param param;
} APP_BT_MAIL;

int app_bt_mail_init(void);

int app_bt_Me_switch_sco(uint16_t  scohandle);

int app_bt_ME_SetConnectionRole(BtConnectionRole role);

int app_bt_MeDisconnectLink(BtRemoteDevice* remDev);

int app_bt_ME_StopSniff(BtRemoteDevice *remDev);

int app_bt_ME_SetAccessibleMode(BtAccessibleMode mode, const BtAccessModeInfo *info);

int app_bt_Me_SetLinkPolicy(BtRemoteDevice *remDev, BtLinkPolicy policy);

int app_bt_CMGR_SetSniffTimer(CmgrHandler *Handler, 
                                        BtSniffInfo* SniffInfo, 
                                        TimeT Time);

int app_bt_CMGR_SetSniffInofToAllHandlerByRemDev(BtSniffInfo* SniffInfo, 
                            								     BtRemoteDevice *RemDev);

int app_bt_A2DP_OpenStream(A2dpStream *Stream, BT_BD_ADDR *Addr);

int app_bt_A2DP_CloseStream(A2dpStream *Stream);

int app_bt_A2DP_SetMasterRole(A2dpStream *Stream, BOOL Flag);

int app_bt_HF_CreateServiceLink(HfChannel *Chan, BT_BD_ADDR *Addr);

int app_bt_HF_DisconnectServiceLink(HfChannel *Chan);

int app_bt_HF_CreateAudioLink(HfChannel *Chan);

int app_bt_HF_DisconnectAudioLink(HfChannel *Chan);

int app_bt_HF_EnableSniffMode(HfChannel *Chan, BOOL Enable);

int app_bt_HF_SetMasterRole(HfChannel *Chan, BOOL Flag);

void app_bt_accessible_manager_process(const BtEvent *Event);
void app_bt_role_manager_process(const BtEvent *Event);
void app_bt_sniff_manager_process(const BtEvent *Event);
void app_bt_golbal_handle_hook(const BtEvent *Event);


#ifdef __cplusplus
}
#endif
#endif /* __APP_BT_FUNC_H__ */

