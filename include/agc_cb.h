#ifndef _AGC_CB_H_
#define _AGC_CB_H_

#include <stddef.h>
#include <stdint.h>

#include "agc_error.h"
#include "agc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void     agcCbInit(SceAgcCb *cb, void *buffer, size_t size_bytes);
void     agcCbReset(SceAgcCb *cb, void *buffer, size_t size_bytes);
uint32_t agcCbCapacityDwords(const SceAgcCb *cb);
uint32_t agcCbUsedDwords(const SceAgcCb *cb);
uint32_t agcCbRemainingDwords(const SceAgcCb *cb);
uint32_t *agcCbAllocDwords(SceAgcCb *cb, uint32_t dword_count);

#ifdef __cplusplus
}
#endif

#endif /* _AGC_CB_H_ */
