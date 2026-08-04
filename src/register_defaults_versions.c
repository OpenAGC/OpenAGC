/*
 * openagc — SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* register_defaults_versions.c — Version selection for register defaults
 * Auto-generated from reference agcRegisterDefaults.inc */

#include "agc_context.h"

#include <stdint.h>

const AgcRegisterDefaultsGroup *agcRegisterDefaultsV0GetPrimaryGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsV0GetInternalGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsV4GetPrimaryGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsV4GetInternalGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsV5GetPrimaryGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsV5GetInternalGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsV7GetPrimaryGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsV7GetInternalGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsV8GetPrimaryGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsV8GetInternalGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsV9GetPrimaryGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsV9GetInternalGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsV10GetPrimaryGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsV10GetInternalGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsV11GetPrimaryGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsV11GetInternalGroups(uint32_t *out_count);

#define AGC_REGISTER_DEFAULTS_MAX_VERSION   12
#define AGC_REGISTER_DEFAULTS_FALLBACK_VERSION  11

typedef const AgcRegisterDefaultsGroup *(*GetGroupsFunc)(uint32_t *);

static GetGroupsFunc s_primary_getters[] = {
    agcRegisterDefaultsV0GetPrimaryGroups,  /* version 0 → v0 */
    agcRegisterDefaultsV0GetPrimaryGroups,  /* version 1 → v0 */
    agcRegisterDefaultsV0GetPrimaryGroups,  /* version 2 → v0 */
    agcRegisterDefaultsV0GetPrimaryGroups,  /* version 3 → v0 */
    agcRegisterDefaultsV4GetPrimaryGroups,  /* version 4 → v4 */
    agcRegisterDefaultsV5GetPrimaryGroups,  /* version 5 → v5 */
    agcRegisterDefaultsV5GetPrimaryGroups,  /* version 6 → v5 */
    agcRegisterDefaultsV7GetPrimaryGroups,  /* version 7 → v7 */
    agcRegisterDefaultsV8GetPrimaryGroups,  /* version 8 → v8 */
    agcRegisterDefaultsV9GetPrimaryGroups,  /* version 9 → v9 */
    agcRegisterDefaultsV10GetPrimaryGroups,  /* version 10 → v10 */
    agcRegisterDefaultsV11GetPrimaryGroups,  /* version 11 → v11 */
    agcRegisterDefaultsV10GetPrimaryGroups,  /* version 12 → v10 */
};

static GetGroupsFunc s_internal_getters[] = {
    agcRegisterDefaultsV0GetInternalGroups,  /* version 0 → v0 */
    agcRegisterDefaultsV0GetInternalGroups,  /* version 1 → v0 */
    agcRegisterDefaultsV0GetInternalGroups,  /* version 2 → v0 */
    agcRegisterDefaultsV0GetInternalGroups,  /* version 3 → v0 */
    agcRegisterDefaultsV4GetInternalGroups,  /* version 4 → v4 */
    agcRegisterDefaultsV5GetInternalGroups,  /* version 5 → v5 */
    agcRegisterDefaultsV5GetInternalGroups,  /* version 6 → v5 */
    agcRegisterDefaultsV7GetInternalGroups,  /* version 7 → v7 */
    agcRegisterDefaultsV8GetInternalGroups,  /* version 8 → v8 */
    agcRegisterDefaultsV9GetInternalGroups,  /* version 9 → v9 */
    agcRegisterDefaultsV10GetInternalGroups,  /* version 10 → v10 */
    agcRegisterDefaultsV11GetInternalGroups,  /* version 11 → v11 */
    agcRegisterDefaultsV10GetInternalGroups,  /* version 12 → v10 */
};

const AgcRegisterDefaultsGroup *agcRegisterDefaultsGetPrimaryGroupsForVersion(
    uint32_t version, uint32_t *out_count) {
    if (version > AGC_REGISTER_DEFAULTS_MAX_VERSION)
        version = AGC_REGISTER_DEFAULTS_FALLBACK_VERSION;
    return s_primary_getters[version](out_count);
}

const AgcRegisterDefaultsGroup *agcRegisterDefaultsGetInternalGroupsForVersion(
    uint32_t version, uint32_t *out_count) {
    if (version > AGC_REGISTER_DEFAULTS_MAX_VERSION)
        version = AGC_REGISTER_DEFAULTS_FALLBACK_VERSION;
    return s_internal_getters[version](out_count);
}
