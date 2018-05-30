/* mbed Microcontroller Library
 * CMSIS-style functionality to support dynamic vectors
 *******************************************************************************
 * Copyright (c) 2011 ARM Limited. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of ARM Limited nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *******************************************************************************
 */
#include "cmsis_nvic.h"
#include "plat_types.h"
#include "hal_trace.h"

// The section must be aligned to 64-word boundary -- aligned(0x100)
#define VECTOR_LOC __attribute__((section(".vector_table")))

#define NVIC_ROM_VECTOR_ADDRESS (0x0)       // Initial vector position in ROM

static uint32_t VECTOR_LOC vector_table[NVIC_NUM_VECTORS];

void NVIC_SetVector(IRQn_Type IRQn, uint32_t vector)
{
    uint32_t *vectors = (uint32_t*)SCB->VTOR;
    uint32_t i;

    // Copy and switch to dynamic vectors if the first time called
    if (vectors != vector_table) {
        uint32_t *old_vectors = vectors;
        vectors = vector_table;
        for (i = 0; i < NVIC_NUM_VECTORS; i++) {
            vectors[i] = old_vectors[i];
        }
        SCB->VTOR = (uint32_t)vector_table;
    }
    vectors[IRQn + NVIC_USER_IRQ_OFFSET] = vector;
}

uint32_t NVIC_GetVector(IRQn_Type IRQn)
{
    uint32_t *vectors = (uint32_t*)SCB->VTOR;
    return vectors[IRQn + NVIC_USER_IRQ_OFFSET];
}

void NVIC_DisableAllIRQs(void)
{
    int i;

    for (i = 0; i < (NVIC_NUM_VECTORS - NVIC_USER_IRQ_OFFSET + 31) / 32; i++) {
        NVIC->ICER[i] = ~0;
    }
    SCB->VTOR = 0;
}

static void NVIC_default_handler(void)
{
    ASSERT(0, "%s", __func__);
}

#define FAULT_HANDLER  __attribute__((weak,alias("NVIC_default_handler")))

void FAULT_HANDLER Reset_Handler(void);
void FAULT_HANDLER NMI_Handler(void);
void FAULT_HANDLER HardFault_Handler(void);
void FAULT_HANDLER MemManage_Handler(void);
void FAULT_HANDLER BusFault_Handler(void);
void FAULT_HANDLER UsageFault_Handler(void);
void FAULT_HANDLER SVC_Handler(void);
void FAULT_HANDLER DebugMon_Handler(void);
void FAULT_HANDLER PendSV_Handler(void);
void FAULT_HANDLER SysTick_Handler(void);
extern uint32_t __rom_stack[];
extern uint32_t __stack[];

static const uint32_t fault_handlers[NVIC_USER_IRQ_OFFSET] = {
#ifdef ROM_BUILD
    (uint32_t)__rom_stack,
#else
    (uint32_t)__stack,
#endif
    (uint32_t)Reset_Handler,
    (uint32_t)NMI_Handler,
    (uint32_t)HardFault_Handler,
    (uint32_t)MemManage_Handler,
    (uint32_t)BusFault_Handler,
    (uint32_t)UsageFault_Handler,
    (uint32_t)NVIC_default_handler,
    (uint32_t)NVIC_default_handler,
    (uint32_t)NVIC_default_handler,
    (uint32_t)NVIC_default_handler,
    (uint32_t)SVC_Handler,
    (uint32_t)DebugMon_Handler,
    (uint32_t)NVIC_default_handler,
    (uint32_t)PendSV_Handler,
    (uint32_t)SysTick_Handler,
};

void NVIC_InitVectors(void)
{
    int i;

    for (i = 0; i < NVIC_NUM_VECTORS; i++) {
        vector_table[i] = (i < ARRAY_SIZE(fault_handlers)) ?
            fault_handlers[i] : (uint32_t)NVIC_default_handler;
    }

    SCB->VTOR = (uint32_t)vector_table;
}

