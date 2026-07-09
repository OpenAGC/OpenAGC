#ifndef _AGC_ERROR_H_
#define _AGC_ERROR_H_

#include <stdint.h>

/*
 * AGC error codes.
 *
 * PS5 AGC uses the same SCE error code convention:
 * 0x00000000 = success
 * 0x80AAxxxx = AGC module errors (AA = module, xxxx = code)
 *
 * The AGC module ID is 0x89 (from firmware analysis of libSceAgc/libSceAgcDriver).
 */

#define AGC_OK                          0

/* Generic errors */
#define AGC_ERROR_INVALID_ARGUMENT      ((int32_t)0x80890001)
#define AGC_ERROR_NOT_INITIALIZED       ((int32_t)0x80890002)
#define AGC_ERROR_INVALID_STATE         ((int32_t)0x80890003)
#define AGC_ERROR_OUT_OF_MEMORY         ((int32_t)0x80890004)
#define AGC_ERROR_BUFFER_TOO_SMALL      ((int32_t)0x80890005)
#define AGC_ERROR_INVALID_ALIGNMENT     ((int32_t)0x80890006)
#define AGC_ERROR_TIMEOUT               ((int32_t)0x80890007)
#define AGC_ERROR_BUSY                  ((int32_t)0x80890008)
#define AGC_ERROR_NOT_FOUND             ((int32_t)0x80890009)
#define AGC_ERROR_INTERNAL              ((int32_t)0x8089000A)

/* Command buffer errors */
#define AGC_ERROR_CB_INVALID_SIZE       ((int32_t)0x80890101)
#define AGC_ERROR_CB_OVERFLOW           ((int32_t)0x80890102)
#define AGC_ERROR_CB_INVALID_QUEUE      ((int32_t)0x80890103)

/* Submission errors */
#define AGC_ERROR_SUBMIT_FAILED         ((int32_t)0x80890201)
#define AGC_ERROR_SUBMIT_NOT_ALLOWED    ((int32_t)0x80890202)

/* Shader errors */
#define AGC_ERROR_SHADER_INVALID        ((int32_t)0x80890301)
#define AGC_ERROR_SHADER_COMPILE        ((int32_t)0x80890302)

/* Resource errors */
#define AGC_ERROR_RESOURCE_INVALID      ((int32_t)0x80890401)
#define AGC_ERROR_RESOURCE_NOT_BOUND    ((int32_t)0x80890402)

/* Validation errors */
#define AGC_ERROR_VALIDATION_FAILED     ((int32_t)0x80890501)

static inline const char* agcErrorString(int32_t err) {
    switch (err) {
    case AGC_OK:                        return "AGC_OK";
    case AGC_ERROR_INVALID_ARGUMENT:    return "AGC_ERROR_INVALID_ARGUMENT";
    case AGC_ERROR_NOT_INITIALIZED:     return "AGC_ERROR_NOT_INITIALIZED";
    case AGC_ERROR_INVALID_STATE:       return "AGC_ERROR_INVALID_STATE";
    case AGC_ERROR_OUT_OF_MEMORY:       return "AGC_ERROR_OUT_OF_MEMORY";
    case AGC_ERROR_BUFFER_TOO_SMALL:    return "AGC_ERROR_BUFFER_TOO_SMALL";
    case AGC_ERROR_INVALID_ALIGNMENT:   return "AGC_ERROR_INVALID_ALIGNMENT";
    case AGC_ERROR_TIMEOUT:             return "AGC_ERROR_TIMEOUT";
    case AGC_ERROR_BUSY:                return "AGC_ERROR_BUSY";
    case AGC_ERROR_NOT_FOUND:           return "AGC_ERROR_NOT_FOUND";
    case AGC_ERROR_INTERNAL:            return "AGC_ERROR_INTERNAL";
    case AGC_ERROR_CB_INVALID_SIZE:     return "AGC_ERROR_CB_INVALID_SIZE";
    case AGC_ERROR_CB_OVERFLOW:         return "AGC_ERROR_CB_OVERFLOW";
    case AGC_ERROR_CB_INVALID_QUEUE:    return "AGC_ERROR_CB_INVALID_QUEUE";
    case AGC_ERROR_SUBMIT_FAILED:       return "AGC_ERROR_SUBMIT_FAILED";
    case AGC_ERROR_SUBMIT_NOT_ALLOWED:  return "AGC_ERROR_SUBMIT_NOT_ALLOWED";
    case AGC_ERROR_SHADER_INVALID:      return "AGC_ERROR_SHADER_INVALID";
    case AGC_ERROR_SHADER_COMPILE:      return "AGC_ERROR_SHADER_COMPILE";
    case AGC_ERROR_RESOURCE_INVALID:    return "AGC_ERROR_RESOURCE_INVALID";
    case AGC_ERROR_RESOURCE_NOT_BOUND:  return "AGC_ERROR_RESOURCE_NOT_BOUND";
    case AGC_ERROR_VALIDATION_FAILED:   return "AGC_ERROR_VALIDATION_FAILED";
    default:                            return "AGC_ERROR_UNKNOWN";
    }
}

#endif /* _AGC_ERROR_H_ */
