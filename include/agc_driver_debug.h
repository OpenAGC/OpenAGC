#ifndef _AGC_DRIVER_DEBUG_H_
#define _AGC_DRIVER_DEBUG_H_

#include <stdint.h>

#include "agc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

const AgcCommandBufferSubmit *agcDriverDebugLastDcbSubmit(void);
const AgcCommandBufferSubmit *agcDriverDebugLastAcbSubmit(uint32_t *owner_handle);

#ifdef __cplusplus
}
#endif

#endif /* _AGC_DRIVER_DEBUG_H_ */
