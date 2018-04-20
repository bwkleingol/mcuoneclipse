/*
 * Shell.c
 *
 *      Author: Erich Styger
 */

#include "Platform.h"
#include "Application.h"
#include "FRTOS1.h"
#include "Shell.h"
#include "CLS1.h"
#include "KIN1.h"
#if PL_CONFIG_HAS_I2C_SPY
  #include "I2CSPY1.h"
#endif
#include "CDC1.h"
#if PL_CONFIG_HAS_SEGGER_RTT
  #include "RTT1.h"
#endif
#include "Feeder.h"

static const CLS1_ParseCommandCallback CmdParserTable[] =
{
  CLS1_ParseCommand,
  FEED_ParseCommand,
  FRTOS1_ParseCommand,
  KIN1_ParseCommand,
#if PL_CONFIG_HAS_I2C_SPY
  I2CSPY1_ParseCommand,
#endif
  NULL /* sentinel */
};

#if PL_CONFIG_HAS_SEGGER_RTT
static CLS1_ConstStdIOType RTT_Stdio = {
  (CLS1_StdIO_In_FctType)RTT1_StdIOReadChar, /* stdin */
  (CLS1_StdIO_OutErr_FctType)RTT1_StdIOSendChar, /* stdout */
  (CLS1_StdIO_OutErr_FctType)RTT1_StdIOSendChar, /* stderr */
  RTT1_StdIOKeyPressed /* if input is not empty */
};
#endif

typedef struct {
  CLS1_ConstStdIOType *stdio;
  unsigned char *buf;
  size_t bufSize;
} SHELL_IODesc;

static const SHELL_IODesc ios[] =
{
  {&RTT1_stdio, RTT1_DefaultShellBuffer, sizeof(RTT1_DefaultShellBuffer)},
  {&CDC1_stdio, CDC1_DefaultShellBuffer, sizeof(CDC1_DefaultShellBuffer)},
};


static void ShellTask(void *pvParameters) {
  int i;

  (void)pvParameters; /* not used */
  CLS1_SetStdio(&RTT1_stdio); /* make sure that Shell is using our custom I/O handler */
  (void)CLS1_ParseWithCommandTable((unsigned char*)CLS1_CMD_HELP, CLS1_GetStdio(), CmdParserTable);
  for(;;) {
    for(i=0;i<sizeof(ios)/sizeof(ios[0]);i++) {
      (void)CLS1_ReadAndParseWithCommandTable(ios[i].buf, ios[i].bufSize, ios[i].stdio, CmdParserTable);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void SHELL_Init(void) {
  if (xTaskCreate(ShellTask, "Shell", configMINIMAL_STACK_SIZE+300, NULL, tskIDLE_PRIORITY+2, NULL) != pdPASS) {
    for(;;){} /* error */
  }
}
