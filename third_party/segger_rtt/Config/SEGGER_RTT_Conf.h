/*********************************************************************
*                   (c) SEGGER Microcontroller GmbH                  *
*                        The Embedded Experts                        *
*                           www.segger.com                           *
**********************************************************************
*                                                                    *
*        SEGGER RTT * Real Time Transfer for embedded targets        *
*                  https://github.com/SEGGERMicro/RTT                *
*                                                                    *
**********************************************************************

---------------------------END-OF-HEADER------------------------------
Purpose : User configuration file for RTT.
          For available configuration,
          refer to SEGGER_RTT_ConfDefaults.h.

----------------------------------------------------------------------
*/

#ifndef SEGGER_RTT_CONF_H
#define SEGGER_RTT_CONF_H

/*********************************************************************
*
*       Bmelod 默认 RTT 缓冲（可在应用 bm_config 前覆盖）
*
**********************************************************************
*/
#ifndef BUFFER_SIZE_UP
#define BUFFER_SIZE_UP                            1024
#endif
#ifndef BUFFER_SIZE_DOWN
#define BUFFER_SIZE_DOWN                          64
#endif
#ifndef SEGGER_RTT_MODE_DEFAULT
#define SEGGER_RTT_MODE_DEFAULT                   SEGGER_RTT_MODE_NO_BLOCK_SKIP
#endif

#endif
/*************************** End of file ****************************/
