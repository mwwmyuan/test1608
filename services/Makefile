ifeq ($(MBED),1)
obj-y := audioflinger/ bt_profiles/ fs/ bt_app/ overlay/ resources/ ../utils/crc32/
else
obj-y := audioflinger/ bt_profiles/ bt_app/ overlay/ resources/ ../utils/crc32/
endif
ifeq ($(BLE),1)
obj-y += ble_app/ bridge/ ble_stack/ ../utils/retention_ram/ ble_profiles/
endif

ifneq ($(FPGA),1)
obj-y += nvrecord/
endif

ifneq ($(filter 1, $(HW_FIR_EQ_PROCESS) $(SW_IIR_EQ_PROCESS) $(AUDIO_DRC)),)
obj-y += audio_eq/
endif

ifeq ($(VOICE_RECOGNITION),1)
obj-y += voice_recognition/
endif

ifeq ($(PCTOOLS_SUPPT),1)
obj-y += rt_rwburn/
endif
