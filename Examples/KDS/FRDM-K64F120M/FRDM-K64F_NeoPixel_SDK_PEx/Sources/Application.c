/*
 * Application.c
 *
 *  Created on: 26.07.2015
 *      Author: tastyger
 */

#include "Application.h"
#include "gpio1.h"
#include "osa1.h"
#include <stdbool.h>

static const ftm_pwm_param_t flexTimer1_ChnConfig0 = {
  .mode = kFtmEdgeAlignedPWM,
  .edgeMode = kFtmHighTrue,
  .uFrequencyHZ = 1000U,
  .uDutyCyclePercent = 25U,
  .uFirstEdgeDelayPercent = 0U,
};

static const ftm_pwm_param_t flexTimer1_ChnConfig1 = {
  .mode = kFtmEdgeAlignedPWM,
  .edgeMode = kFtmHighTrue,
  .uFrequencyHZ = 1000U,
  .uDutyCyclePercent = 50U,
  .uFirstEdgeDelayPercent = 0U,
};

static const ftm_user_config_t flexTimer1_InitConfig0 = {
  .tofFrequency      = 0U,
  .isWriteProtection = false,
  .BDMMode           = kFtmBdmMode_00,
  .syncMethod        = (uint32_t)(kFtmUseSoftwareTrig)
};

static void InitFlexTimer(uint32_t instance) {
  FTM_Type *ftmBase = g_ftmBase[instance];

  FTM_DRV_Init(instance, &flexTimer1_InitConfig0);
  FTM_DRV_SetClock(instance, kClock_source_FTM_SystemClk, kFtmDividedBy1);
  FTM_DRV_PwmStart(instance, (ftm_pwm_param_t*)&flexTimer1_ChnConfig0, 0U);
  /* stop clock, so we can init second channel */
  FTM_HAL_SetClockSource(ftmBase, kClock_source_FTM_None);
  FTM_DRV_PwmStart(instance, (ftm_pwm_param_t*)&flexTimer1_ChnConfig1, 1U);
  /* stop it again */
  FTM_HAL_SetClockSource(ftmBase, kClock_source_FTM_None);

  FTM_DRV_SetTimeOverflowIntCmd(instance,false);
  FTM_DRV_SetFaultIntCmd(instance,false);
}

static void StartTransfer(uint32_t instance) {
  FTM_Type *ftmBase = g_ftmBase[instance];

  /*! \todo signals need to be low, are random now */
  /* enable DMA */
  FTM_HAL_SetChnDmaCmd(ftmBase, 0, true);
  //FTM_HAL_SetChnDmaCmd(ftmBase, 1, true);
  /* clock it */
  FTM_HAL_SetClockSource(ftmBase, kClock_source_FTM_SystemClk);
  OSA_TimeDelay(100);
  FTM_HAL_SetClockSource(ftmBase, kClock_source_FTM_None);
  /* disable DMA */
  FTM_HAL_SetChnDmaCmd(ftmBase, 0, false);
  //FTM_HAL_SetChnDmaCmd(ftmBase, 1, false);
}


#define BUFFER_SIZE               16/*! Total transfer size */
#define EDMA_TRANSFER_SIZE        2 /*! Transfer size on basic loop */
#define EDMA_CHAIN_LENGTH         1 /*! Number of srcSG and destSG */
#define EDMA_WARTERMARK_LEVEL     8 /*! number of bytes transfered on each EDMA request(minor loop) */
// Source address in flash.
uint8_t srcAddr[BUFFER_SIZE] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
// Destination address in ram.
uint8_t destAddr[BUFFER_SIZE] = {0};
static volatile bool dmaDone = false;

static const uint8_t OneValue = 0xFF; /* value to clear or set the port bits */

void EDMA_Callback(void *param, edma_chn_status_t chanStatus) {
  dmaDone = true;
}

static void doDMA(void) {
  edma_chn_state_t     chnState;
  edma_software_tcd_t *stcd; /* transfer control descriptor */
  edma_state_t         edmaState;
  edma_user_config_t   edmaUserConfig;
  edma_scatter_gather_list_t srcSG, destSG;
  bool                 result;
  uint32_t             i, channel = 0;
  uint8_t res;

  /* Init eDMA modules. */
  edmaUserConfig.chnArbitration = kEDMAChnArbitrationRoundrobin;
  edmaUserConfig.notHaltOnError = false;
  EDMA_DRV_Init(&edmaState, &edmaUserConfig);

  /* EDMA channel request. */
  res = EDMA_DRV_RequestChannel(channel, kDmaRequestMux0AlwaysOn62, &chnState);
  if (res==kEDMAInvalidChannel) {
    for(;;);
  }

#if 0
  /* Fill zero to destination buffer */
  for (i = 0; i < BUFFER_SIZE; i ++) {
    destAddr[i] = 0x00;
  }
#endif
  /* Prepare memory pointing to software TCDs. */
  stcd = OSA_MemAllocZero(STCD_SIZE(EDMA_CHAIN_LENGTH));

  /* Configure EDMA channel. */
#if 0
  srcSG.address  = (uint32_t)srcAddr;
  destSG.address = (uint32_t)destAddr;
  srcSG.length   = BUFFER_SIZE;
  destSG.length  = BUFFER_SIZE;
#else
  srcSG.address  = (uint32_t)&OneValue;
  destSG.address = (uint32_t)&GPIO_PSOR_REG(PTD_BASE_PTR);
  srcSG.length   = 1;
  destSG.length  = 1;
#endif

  /* configure single end descriptor chain. */
  EDMA_DRV_ConfigScatterGatherTransfer(
          &chnState, /* channel information */
          stcd, /* transfer control descriptor */
          kEDMAMemoryToMemory, /* perform memory to memory operation */
          1, //EDMA_TRANSFER_SIZE, /* 2: transfer size of basic loop */
          1, //EDMA_WARTERMARK_LEVEL, /* 8: number of bytes transfered on each EDMA request(minor loop) */
          &srcSG, &destSG, /* source and destination scatter-gather */
          EDMA_CHAIN_LENGTH /* number of scatter-gather in chain */
          );

  /* Install callback for eDMA handler */
  EDMA_DRV_InstallCallback(&chnState, EDMA_Callback, NULL);

  /* Initialize transfer. */
  dmaDone = false;
  EDMA_DRV_StartChannel(&chnState);
 /* Wait until transfer is complete */
  do {
  } while(!dmaDone);

  // Stop channel
  EDMA_DRV_StopChannel(&chnState);

  /* Release channel */
  EDMA_DRV_ReleaseChannel(&chnState);

  /* Free stcd */
  OSA_MemFree((void *)stcd);
}

static bool start = false;

void APP_Run(void) {
  int i;

  InitFlexTimer(FTM0_IDX);
  for(;;) {
    GPIO_DRV_TogglePinOutput(LEDRGB_BLUE);
#if 0
    for(i=0;i<10;i++) {
      GPIO_DRV_TogglePinOutput(WS2812_0);
      GPIO_DRV_TogglePinOutput(J2_12);
    }
#endif
   // OSA_TimeDelay(1000);
    if (start) {
      GPIO_PTOR_REG(PTD_BASE_PTR) = (1<<0); /* Port Toggle Output PTD0 */
      GPIO_PSOR_REG(PTD_BASE_PTR) = (1<<0); /* Port Set Output PTD0 */
      GPIO_PCOR_REG(PTD_BASE_PTR) = (1<<0); /* Port Clear Output PTD0 */
//      GPIO_PTOR_REG(PTE_BASE_PTR) = (1<<26); /* toggle PTE26 (GREEN RGB) */
//      GPIO_DRV_ClearPinOutput(WS2812_0); /* put low PTD0 */
      doDMA();
      //StartTransfer(FTM0_IDX);
    }
  }
}

void DMA_Error_IRQHandler(void)
{
    EDMA_DRV_ErrorIRQHandler(0);
}
