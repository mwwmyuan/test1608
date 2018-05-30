export DEBUG                                            ?= 1
# enable:1
# disable:0

WATCHER_DOG                                            ?= 0
# enable:1
# disable:0

export MBED                                             ?= 0
# enable:1
# disable:0

export RTOS                                             ?= 1
# enable:1
# disable:0

export PC_CMD_UART		?= 0
# enable:1
# disable:0
 
export APP_TEST_AUDIO             ?= 0
# enable:1
# disable:0

export APP_TEST_SDMMC             ?= 0
# enable:1
# disable:0

export AUDIO_CODEC_ASYNC_CLOSE             ?= 1
# enable:1
# disable:0

export HW_FIR_EQ_PROCESS                         ?= 0
# enable:1
# disable:0

export SW_IIR_EQ_PROCESS                         ?= 0
# enable:1
# disable:0

export AUDIO_RESAMPLE                         ?= 0
# enable:1
# disable:0

export SPEECH_ECHO_CANCEL                      ?= 1
# enable:1
# disable:0

export SPEECH_NOISE_SUPPRESS                  ?= 1
# enable:1
# disable:0

export SPEAKER_NOISE_SUPPRESS 				  ?= 0
# enable:1
# disable:0

export SPEECH_PACKET_LOSS_CONCEALMENT                  ?= 1
# enable:1
# disable:0

export SPEECH_WEIGHTING_FILTER_SUPPRESS 				  ?= 1
# enable:1
# disable:0

export SPEAKER_WEIGHTING_FILTER_SUPPRESS 				  ?= 0
# enable:1
# disable:0

export HFP_1_6_ENABLE                           ?= 0
# enable:1
# disable:0

export MSBC_PLC_ENABLE                           ?= 0
# enable:1
# disable:0

export MSBC_16K_SAMPLE_RATE                      ?= 0
# enable:1
# disable:0

#if chip version over G should open this macro
export SBC_FUNC_IN_ROM                           ?= 1
# enable:1
# disable:0

export VOICE_DETECT                                 ?= 0
# enable:1
# disable:0

export VOICE_PROMPT                                 ?= 1
# enable:1
# disable:0

export VOICE_RECOGNITION                                 ?= 1
# enable:1
# disable:0

export LED_STATUS_INDICATION                   ?= 0
# enable:1
# disable:0

export BLE                                                 ?= 0
# enable:1
# disable:0

export BTADDR_GEN                                    ?= 1
# enable:1
# disable:0

export FACTORY_MODE                                 ?= 1
# enable:1
# disable:0

export ENGINEER_MODE                                 ?= 1
# enable:1
# disable:0

export AUDIO_SCO_BTPCM_CHANNEL               ?= 1
# enable:1
# disable:0

export A2DP_AAC_ON                           ?= 0
# enable:1
# disable:0

export APP_LINEIN_A2DP_SOURCE                           ?= 1
# enable:1
# disable:0

export APP_I2S_A2DP_SOURCE                           ?= 0
# enable:1
# disable:0

LDS_FILE    := best1000.lds

include $(KBUILD_ROOT)/config/$(T)/customize.mk
include $(KBUILD_ROOT)/config/$(T)/hardware.mk
include $(KBUILD_ROOT)/config/convert.mk