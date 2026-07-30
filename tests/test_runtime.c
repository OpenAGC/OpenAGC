/*
 * openagc native runtime contract tests.
 */

#include "test.h"

#include <stdint.h>

#include "agc_driver_debug.h"
#include "agc_graphics.h"
#include "agc_pm4.h"
#include "agc_registers.h"
#include "agc_texture.h"
#include "openagc/runtime.h"

static AgcDevice create_device(void)
{
    AgcDeviceDesc desc = AGC_DEVICE_DESC_INIT;
    AgcDevice device = NULL;

    TEST_ASSERT_EQ(agcCreateDevice(&desc, &device), AGC_OK,
        "native device creation succeeds");
    TEST_ASSERT(device != NULL, "native device handle is non-null");
    return device;
}

typedef struct RuntimeShaderFixture {
    AgcShaderRecord record;
    AgcShaderSpecials specials;
    AgcRegisterValue sh_registers[8];
    AgcRegisterValue cx_registers[1];
    uint8_t code_padding[40];
    uint32_t code[4];
} RuntimeShaderFixture;

typedef struct RuntimeDepthStencilStateV1Fixture {
    uint32_t struct_size;
    uint32_t version;
    uint32_t format;
    uint32_t depth_test_enable;
    uint32_t depth_write_enable;
    AgcCompareOperation depth_compare_operation;
    uint32_t depth_bounds_enable;
    uint32_t stencil_test_enable;
    uint32_t flags;
    uint32_t reserved0;
    uint64_t reserved[3];
} RuntimeDepthStencilStateV1Fixture;

_Static_assert(offsetof(RuntimeShaderFixture, code) == 256u,
    "runtime shader fixture code must be program-aligned");
_Static_assert(sizeof(RuntimeDepthStencilStateV1Fixture) == 64u,
    "runtime legacy depth-stencil fixture size mismatch");

static int runtime_find_context_register(const uint32_t *commands,
    uint32_t used, uint32_t offset, uint32_t *value)
{
    uint32_t cursor = 0u;
    int found = 0;

    while (cursor < used) {
        uint32_t length = agcPm4Length(commands[cursor]);
        if (length < 2u || length > used - cursor)
            return 0;
        if (agcPm4Opcode(commands[cursor]) ==
                AGC_PM4_OP_SET_CONTEXT_REG && length >= 3u) {
            uint32_t first = commands[cursor + 1u];
            uint32_t count = length - 2u;
            if (offset >= first && offset - first < count) {
                *value = commands[cursor + 2u + offset - first];
                found = 1;
            }
        }
        cursor += length;
    }
    return found;
}

static int runtime_find_shader_register(const uint32_t *commands,
    uint32_t used, uint32_t offset, uint32_t *value)
{
    uint32_t cursor = 0u;
    int found = 0;

    while (cursor < used) {
        uint32_t length = agcPm4Length(commands[cursor]);
        if (length < 2u || length > used - cursor)
            return 0;
        if (agcPm4Opcode(commands[cursor]) == AGC_PM4_OP_SET_SH_REG &&
            length >= 3u) {
            uint32_t first = commands[cursor + 1u];
            uint32_t count = length - 2u;
            if (offset >= first && offset - first < count) {
                *value = commands[cursor + 2u + offset - first];
                found = 1;
            }
        }
        cursor += length;
    }
    return found;
}

static int runtime_find_uconfig_register(const uint32_t *commands,
    uint32_t used, uint32_t offset, uint32_t *value)
{
    uint32_t cursor = 0u;
    int found = 0;

    while (cursor < used) {
        uint32_t length = agcPm4Length(commands[cursor]);
        if (length < 2u || length > used - cursor)
            return 0;
        if (agcPm4Opcode(commands[cursor]) == AGC_PM4_OP_SET_UCONFIG_REG &&
            length >= 3u) {
            uint32_t first = commands[cursor + 1u];
            uint32_t count = length - 2u;
            if (offset >= first && offset - first < count) {
                *value = commands[cursor + 2u + offset - first];
                found = 1;
            }
        }
        cursor += length;
    }
    return found;
}

static uint64_t shader_fixture_hash_update(
    uint64_t hash, const void *data, size_t size)
{
    const uint8_t *bytes = data;

    for (size_t i = 0u; i < size; ++i) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t shader_fixture_hash(const void *data, size_t size)
{
    return shader_fixture_hash_update(
        UINT64_C(14695981039346656037), data, size);
}

static uint64_t shader_fixture_linkage_hash(
    const AgcShaderReflection *reflection)
{
    uint64_t hash = UINT64_C(14695981039346656037);

    hash = shader_fixture_hash_update(hash, &reflection->stage_input_mask,
        sizeof(reflection->stage_input_mask));
    hash = shader_fixture_hash_update(hash, &reflection->stage_output_mask,
        sizeof(reflection->stage_output_mask));
    hash = shader_fixture_hash_update(hash, &reflection->patch_input_mask,
        sizeof(reflection->patch_input_mask));
    return shader_fixture_hash_update(hash, &reflection->patch_output_mask,
        sizeof(reflection->patch_output_mask));
}

static AgcShader create_shader_with_reflection(AgcDevice device,
    AgcShaderStage stage, const AgcShaderReflection *requirements)
{
    RuntimeShaderFixture binary = {0};
    AgcShaderReflection reflection = AGC_SHADER_REFLECTION_INIT;
    AgcShaderDesc desc = AGC_SHADER_DESC_INIT;
    AgcShader shader = NULL;

    if (requirements)
        reflection = *requirements;
    binary.record.magic = AGC_SHADER_RECORD_MAGIC;
    binary.record.version = AGC_SHADER_RECORD_VERSION_GEN5;
    binary.record.code = offsetof(RuntimeShaderFixture, code);
    if (stage == kAgcShaderStageCs) {
        binary.record.shader_type = kAgcShaderTypeCs;
        binary.record.num_sh_registers = 3u;
        binary.record.sh_registers =
            offsetof(RuntimeShaderFixture, sh_registers);
        binary.sh_registers[0] = (AgcRegisterValue){
            AGC_REG_COMPUTE_PGM_RSRC1, 0u};
        binary.sh_registers[1] = (AgcRegisterValue){
            AGC_REG_COMPUTE_PGM_RSRC2, 0u};
        binary.sh_registers[2] = (AgcRegisterValue){
            AGC_REG_COMPUTE_PGM_RSRC3, 0u};
    } else if (stage == kAgcShaderStagePs) {
        binary.record.shader_type = kAgcShaderTypePs;
        binary.record.num_sh_registers = 2u;
        binary.record.sh_registers =
            offsetof(RuntimeShaderFixture, sh_registers);
        binary.record.num_cx_registers = 1u;
        binary.record.cx_registers =
            offsetof(RuntimeShaderFixture, cx_registers);
        binary.sh_registers[0] = (AgcRegisterValue){
            AGC_REG_SPI_SHADER_PGM_LO_PS, 0u};
        binary.sh_registers[1] = (AgcRegisterValue){
            AGC_REG_SPI_SHADER_PGM_HI_PS, 0u};
        binary.cx_registers[0] = (AgcRegisterValue){
            AGC_REG_SPI_PS_IN_CONTROL,
            AGC_GFX1013_SPI_PS_IN_CONTROL_PS_W32_EN};
    } else {
        binary.record.shader_type = (uint8_t)kAgcShaderBinaryTypeGs;
        binary.record.num_sh_registers = 2u;
        binary.record.sh_registers =
            offsetof(RuntimeShaderFixture, sh_registers);
        binary.record.specials = offsetof(RuntimeShaderFixture, specials);
        binary.sh_registers[0] = (AgcRegisterValue){
            AGC_REG_SPI_SHADER_PGM_LO_GS, 0u};
        binary.sh_registers[1] = (AgcRegisterValue){
            AGC_REG_SPI_SHADER_PGM_HI_GS, 0u};
        binary.specials.ge_cntl = (AgcShaderSpecialRegister){
            AGC_REG_GE_CNTL, 0x10u};
        binary.specials.vgt_shader_stages_en =
            (AgcShaderSpecialRegister){
                AGC_REG_VGT_SHADER_STAGES_EN,
                AGC_GFX1013_VGT_SHADER_STAGES_EN_GS_W32_EN};
        binary.specials.vgt_gs_out_prim_type =
            (AgcShaderSpecialRegister){
                AGC_REG_VGT_GS_OUT_PRIM_TYPE, 0u};
        binary.specials.ge_user_vgpr_en = (AgcShaderSpecialRegister){
            AGC_REG_GE_USER_VGPR_EN, 0u};
    }
    binary.code[0] = 0xBF810000u;
    reflection.stage = stage;
    reflection.shader_record_version = AGC_SHADER_RECORD_VERSION_GEN5;
    if (reflection.compiler_api_version == 0u)
        reflection.compiler_api_version = AGC_SHADER_COMPILER_API_VERSION;
    if (reflection.wave_size == 0u)
        reflection.wave_size = 32u;
    reflection.hash_algorithm = AGC_SHADER_HASH_FNV1A64;
    reflection.code_offset = offsetof(RuntimeShaderFixture, code);
    reflection.code_size = sizeof(binary.code);
    if (reflection.entry_point[0] == '\0')
        memcpy(reflection.entry_point, "main", sizeof("main"));
    if (stage == kAgcShaderStageCs) {
        if (reflection.local_size_x == 0u)
            reflection.local_size_x = 64u;
        if (reflection.local_size_y == 0u)
            reflection.local_size_y = 1u;
        if (reflection.local_size_z == 0u)
            reflection.local_size_z = 1u;
    } else if (stage == kAgcShaderStageVs) {
        reflection.flags |= AGC_SHADER_REFLECTION_NGG_BIT;
    } else if (stage == kAgcShaderStagePs) {
        if (reflection.pixel_shader_sample_count == 0u)
            reflection.pixel_shader_sample_count = 1u;
    }
    reflection.stage_linkage_hash = shader_fixture_linkage_hash(&reflection);
    reflection.code_hash = shader_fixture_hash(&binary, sizeof(binary));
    desc.stage = stage;
    desc.code = &binary;
    desc.code_size = sizeof(binary);
    desc.reflection = &reflection;
    TEST_ASSERT_EQ(agcCreateShader(device, &desc, &shader), AGC_OK,
        "native shader creation succeeds");
    return shader;
}

static AgcShader create_shader(AgcDevice device, AgcShaderStage stage)
{
    return create_shader_with_reflection(device, stage, NULL);
}

static AgcShader create_ngg_shader_bundle(AgcDevice device,
    AgcShaderStage stage, const AgcShaderReflection *requirements)
{
    RuntimeShaderFixture binary = {0};
    RuntimeShaderFixture front_binary = {0};
    AgcShaderReflection reflection = AGC_SHADER_REFLECTION_INIT;
    AgcShaderDesc desc = AGC_SHADER_DESC_INIT;
    AgcShader shader = NULL;

    if (requirements)
        reflection = *requirements;
    if (reflection.front_stage == kAgcShaderStageCount)
        reflection.front_stage = stage == kAgcShaderStageGs ?
            kAgcShaderStageVs : stage;
    binary.record.magic = AGC_SHADER_RECORD_MAGIC;
    binary.record.version = AGC_SHADER_RECORD_VERSION_GEN5;
    binary.record.code = offsetof(RuntimeShaderFixture, code);
    binary.record.shader_type = (uint8_t)kAgcShaderBinaryTypeGsBack;
    binary.record.num_sh_registers =
        reflection.front_stage == kAgcShaderStageDs ? 6u : 3u;
    binary.record.sh_registers = offsetof(RuntimeShaderFixture, sh_registers);
    binary.record.num_cx_registers = 1u;
    binary.record.cx_registers = offsetof(RuntimeShaderFixture, cx_registers);
    binary.record.specials = offsetof(RuntimeShaderFixture, specials);
    binary.sh_registers[0] = (AgcRegisterValue){
        AGC_REG_SPI_SHADER_PGM_LO_GS, 0u};
    binary.sh_registers[1] = (AgcRegisterValue){
        AGC_REG_SPI_SHADER_PGM_HI_GS, 0u};
    binary.sh_registers[2] = (AgcRegisterValue){
        AGC_REG_SPI_SHADER_USER_DATA_GS_0 + 15u,
        OPENAGC_NEXT_STAGE_PC_PLACEHOLDER};
    if (reflection.front_stage == kAgcShaderStageDs) {
        binary.sh_registers[2] = (AgcRegisterValue){
            AGC_REG_SPI_SHADER_USER_DATA_ADDR_LO_GS,
            OPENAGC_RING_OFFSETS_LO_PLACEHOLDER};
        binary.sh_registers[3] = (AgcRegisterValue){
            AGC_REG_SPI_SHADER_USER_DATA_ADDR_HI_GS,
            OPENAGC_RING_OFFSETS_HI_PLACEHOLDER};
        binary.sh_registers[4] = (AgcRegisterValue){
            AGC_REG_SPI_SHADER_USER_DATA_GS_0 + 11u,
            OPENAGC_TCS_OFFCHIP_LAYOUT_PLACEHOLDER};
        binary.sh_registers[5] = (AgcRegisterValue){
            AGC_REG_SPI_SHADER_USER_DATA_GS_0 + 15u,
            OPENAGC_NEXT_STAGE_PC_PLACEHOLDER};
    }
    binary.cx_registers[0] = (AgcRegisterValue){
        AGC_REG_SPI_PS_IN_CONTROL, 0u};
    binary.specials.ge_cntl = (AgcShaderSpecialRegister){
        AGC_REG_GE_CNTL, 0x10u};
    binary.specials.vgt_shader_stages_en = (AgcShaderSpecialRegister){
        AGC_REG_VGT_SHADER_STAGES_EN,
        AGC_GFX1013_VGT_SHADER_STAGES_EN_GS_W32_EN};
    binary.specials.vgt_gs_out_prim_type = (AgcShaderSpecialRegister){
        AGC_REG_VGT_GS_OUT_PRIM_TYPE, 0u};
    binary.specials.ge_user_vgpr_en = (AgcShaderSpecialRegister){
        AGC_REG_GE_USER_VGPR_EN, 0u};
    binary.code[0] = 0xBF810000u;

    front_binary.record.magic = AGC_SHADER_RECORD_MAGIC;
    front_binary.record.version = AGC_SHADER_RECORD_VERSION_GEN5;
    front_binary.record.code = offsetof(RuntimeShaderFixture, code);
    front_binary.record.shader_type =
        (uint8_t)kAgcShaderBinaryTypeGsFront;
    front_binary.code[0] = 0xBF810000u;

    reflection.stage = stage;
    reflection.flags |= AGC_SHADER_REFLECTION_NGG_BIT;
    if (stage != kAgcShaderStageVs)
        reflection.flags |= AGC_SHADER_REFLECTION_FUSED_STAGE_BIT;
    reflection.shader_record_version = AGC_SHADER_RECORD_VERSION_GEN5;
    reflection.compiler_api_version = AGC_SHADER_COMPILER_API_VERSION;
    reflection.wave_size = 32u;
    reflection.hash_algorithm = AGC_SHADER_HASH_FNV1A64;
    reflection.code_offset = offsetof(RuntimeShaderFixture, code);
    reflection.code_size = sizeof(binary.code);
    reflection.front_code_offset = offsetof(RuntimeShaderFixture, code);
    reflection.front_code_size = sizeof(front_binary.code);
    if (stage == kAgcShaderStageGs) {
        if (reflection.geometry_input_primitive ==
            AGC_SHADER_PRIMITIVE_UNDEFINED)
            reflection.geometry_input_primitive =
                AGC_SHADER_PRIMITIVE_TRIANGLES;
        if (reflection.geometry_output_primitive ==
            AGC_SHADER_PRIMITIVE_UNDEFINED)
            reflection.geometry_output_primitive =
                AGC_SHADER_PRIMITIVE_TRIANGLE_STRIP;
        if (reflection.geometry_vertices_in == 0u)
            reflection.geometry_vertices_in = 3u;
        if (reflection.geometry_vertices_out == 0u)
            reflection.geometry_vertices_out = 3u;
        if (reflection.geometry_invocations == 0u)
            reflection.geometry_invocations = 1u;
    }
    reflection.code_hash = shader_fixture_hash(&binary, sizeof(binary));
    reflection.code_hash = shader_fixture_hash_update(reflection.code_hash,
        &front_binary, sizeof(front_binary));
    if (reflection.entry_point[0] == '\0')
        memcpy(reflection.entry_point, "main", sizeof("main"));
    reflection.stage_linkage_hash = shader_fixture_linkage_hash(&reflection);

    desc.stage = stage;
    desc.code = &binary;
    desc.code_size = sizeof(binary);
    desc.front_code = &front_binary;
    desc.front_code_size = sizeof(front_binary);
    desc.reflection = &reflection;
    TEST_ASSERT_EQ(agcCreateShader(device, &desc, &shader), AGC_OK,
        "native NGG shader bundle creation succeeds");
    return shader;
}

static AgcShader create_tessellation_control_bundle(AgcDevice device,
    const AgcShaderReflection *requirements)
{
    RuntimeShaderFixture binary = {0};
    RuntimeShaderFixture front_binary = {0};
    AgcShaderReflection reflection = AGC_SHADER_REFLECTION_INIT;
    AgcShaderDesc desc = AGC_SHADER_DESC_INIT;
    AgcShader shader = NULL;

    if (requirements)
        reflection = *requirements;
    binary.record.magic = AGC_SHADER_RECORD_MAGIC;
    binary.record.version = AGC_SHADER_RECORD_VERSION_GEN5;
    binary.record.code = offsetof(RuntimeShaderFixture, code);
    binary.record.shader_type = (uint8_t)kAgcShaderBinaryTypeHsBack;
    binary.record.num_sh_registers = 7u;
    binary.record.sh_registers = offsetof(RuntimeShaderFixture, sh_registers);
    binary.record.specials = offsetof(RuntimeShaderFixture, specials);
    binary.sh_registers[0] = (AgcRegisterValue){
        AGC_REG_SPI_SHADER_PGM_LO_HS, 0u};
    binary.sh_registers[1] = (AgcRegisterValue){
        AGC_REG_SPI_SHADER_PGM_HI_HS, 0u};
    binary.sh_registers[2] = (AgcRegisterValue){
        AGC_REG_SPI_SHADER_USER_DATA_ADDR_LO_HS,
        OPENAGC_RING_OFFSETS_LO_PLACEHOLDER};
    binary.sh_registers[3] = (AgcRegisterValue){
        AGC_REG_SPI_SHADER_USER_DATA_ADDR_HI_HS,
        OPENAGC_RING_OFFSETS_HI_PLACEHOLDER};
    binary.sh_registers[4] = (AgcRegisterValue){
        AGC_REG_SPI_SHADER_USER_DATA_HS_0 + 11u,
        OPENAGC_TCS_OFFCHIP_LAYOUT_PLACEHOLDER};
    binary.sh_registers[5] = (AgcRegisterValue){
        AGC_REG_SPI_SHADER_USER_DATA_HS_0 + 13u,
        OPENAGC_NEXT_STAGE_PC_PLACEHOLDER};
    binary.sh_registers[6] = (AgcRegisterValue){
        AGC_REG_SPI_SHADER_PGM_RSRC2_HS, 0x1cu};
    binary.specials.vgt_shader_stages_en =
        (AgcShaderSpecialRegister){
            AGC_REG_VGT_SHADER_STAGES_EN, 0x00200105u};
    binary.code[0] = 0xBF810000u;

    front_binary.record.magic = AGC_SHADER_RECORD_MAGIC;
    front_binary.record.version = AGC_SHADER_RECORD_VERSION_GEN5;
    front_binary.record.code = offsetof(RuntimeShaderFixture, code);
    front_binary.record.shader_type =
        (uint8_t)kAgcShaderBinaryTypeHsFront;
    front_binary.code[0] = 0xBF810000u;

    reflection.stage = kAgcShaderStageHs;
    reflection.front_stage = kAgcShaderStageVs;
    reflection.flags |= AGC_SHADER_REFLECTION_FUSED_STAGE_BIT;
    reflection.shader_record_version = AGC_SHADER_RECORD_VERSION_GEN5;
    reflection.compiler_api_version = AGC_SHADER_COMPILER_API_VERSION;
    reflection.wave_size = 32u;
    reflection.hash_algorithm = AGC_SHADER_HASH_FNV1A64;
    reflection.code_offset = offsetof(RuntimeShaderFixture, code);
    reflection.code_size = sizeof(binary.code);
    reflection.front_code_offset = offsetof(RuntimeShaderFixture, code);
    reflection.front_code_size = sizeof(front_binary.code);
    reflection.code_hash = shader_fixture_hash(&binary, sizeof(binary));
    reflection.code_hash = shader_fixture_hash_update(reflection.code_hash,
        &front_binary, sizeof(front_binary));
    if (reflection.entry_point[0] == '\0')
        memcpy(reflection.entry_point, "main", sizeof("main"));
    reflection.stage_linkage_hash = shader_fixture_linkage_hash(&reflection);

    desc.stage = kAgcShaderStageHs;
    desc.code = &binary;
    desc.code_size = sizeof(binary);
    desc.front_code = &front_binary;
    desc.front_code_size = sizeof(front_binary);
    desc.reflection = &reflection;
    TEST_ASSERT_EQ(agcCreateShader(device, &desc, &shader), AGC_OK,
        "native tessellation-control bundle creation succeeds");
    return shader;
}

static AgcQueue create_queue(AgcDevice device, AgcQueueType type)
{
    AgcQueueDesc desc = AGC_QUEUE_DESC_INIT;
    AgcQueue queue = NULL;

    desc.type = type;
    TEST_ASSERT_EQ(agcCreateQueue(device, &desc, &queue), AGC_OK,
        "native queue creation succeeds");
    return queue;
}

static void test_runtime_descriptor_and_info_contract(void)
{
    AgcDeviceDesc desc = AGC_DEVICE_DESC_INIT;
    AgcRuntimeInfo info = AGC_RUNTIME_INFO_INIT;
    AgcDevice device = NULL;

    desc.version++;
    TEST_ASSERT_EQ(agcCreateDevice(&desc, &device),
        AGC_ERROR_INVALID_ARGUMENT,
        "device rejects unknown descriptor version");
    desc.version = AGC_RUNTIME_STRUCTURE_VERSION_1;
    desc.reserved[2] = 1u;
    TEST_ASSERT_EQ(agcCreateDevice(&desc, &device),
        AGC_ERROR_INVALID_ARGUMENT,
        "device rejects nonzero reserved fields");
    desc.reserved[2] = 0u;
    desc.required_capability_bits = UINT64_C(1) << 63;
    TEST_ASSERT_EQ(agcCreateDevice(&desc, &device),
        AGC_ERROR_INVALID_ARGUMENT,
        "device rejects unknown required capability bit");

    desc.required_capability_bits = AGC_RUNTIME_CAP_BASELINE;
    TEST_ASSERT_EQ(agcCreateDevice(&desc, &device), AGC_OK,
        "device accepts required baseline capabilities");
    TEST_ASSERT_EQ(agcGetRuntimeInfo(device, &info), AGC_OK,
        "runtime info query succeeds");
    TEST_ASSERT_EQ(info.runtime_api_version, AGC_RUNTIME_API_VERSION,
        "runtime info reports API version");
    TEST_ASSERT_EQ(info.firmware_version, 0u,
        "generic runtime reports no firmware version");
    TEST_ASSERT_EQ(info.firmware_abi_key, 0u,
        "generic runtime reports no firmware ABI key");
    TEST_ASSERT_EQ(info.hardware_family, AGC_HARDWARE_FAMILY_HOST_TEST,
        "generic backend reports a host-test environment");
    TEST_ASSERT_EQ(info.agc_version, 7u,
        "runtime info reports caller AGC version");
    TEST_ASSERT_EQ(info.capability_bits, AGC_RUNTIME_CAP_BASELINE,
        "runtime info reports baseline capabilities");
    TEST_ASSERT(strcmp(info.profile_name, "generic-host") == 0,
        "runtime info reports exact generic profile");
    for (uint32_t i = 0; i < AGC_RUNTIME_CAPABILITY_COUNT; ++i) {
        TEST_ASSERT_EQ(info.qualification[i], AGC_QUALIFICATION_HOST_TESTED,
            "generic runtime capabilities are host-tested");
    }

    info = (AgcRuntimeInfo)AGC_RUNTIME_INFO_INIT;
    info.reserved[0] = 1u;
    TEST_ASSERT_EQ(agcGetRuntimeInfo(device, &info),
        AGC_ERROR_INVALID_ARGUMENT,
        "runtime info rejects nonzero reserved fields");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "native device destruction succeeds");
}

typedef struct AllocationProbe {
    uint32_t allocations;
    uint32_t frees;
    uint32_t attempts;
    uint32_t fail_on_attempt;
} AllocationProbe;

static void *PS5_SYSV_ABI probe_allocate(
    void *user_data, size_t size, size_t alignment)
{
    AllocationProbe *probe = user_data;
    (void)alignment;
    probe->attempts++;
    if (probe->fail_on_attempt != 0u &&
        probe->attempts == probe->fail_on_attempt)
        return NULL;
    probe->allocations++;
    return malloc(size);
}

static void PS5_SYSV_ABI probe_free(void *user_data, void *memory)
{
    AllocationProbe *probe = user_data;
    probe->frees++;
    free(memory);
}

static void test_runtime_allocation_callbacks(void)
{
    AllocationProbe probe = {0};
    AgcAllocationCallbacks callbacks = {
        &probe, probe_allocate, probe_free
    };
    AgcDeviceDesc desc = AGC_DEVICE_DESC_INIT;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcDevice device = NULL;
    AgcBuffer buffer = NULL;

    desc.allocation_callbacks = &callbacks;
    buffer_desc.size = 64u;
    buffer_desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT;
    TEST_ASSERT_EQ(agcCreateDevice(&desc, &device), AGC_OK,
        "device accepts paired allocation callbacks");
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &buffer), AGC_OK,
        "buffer uses application allocation callbacks");
    TEST_ASSERT_EQ(agcDestroyBuffer(buffer), AGC_OK,
        "callback-owned buffer destruction succeeds");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "callback-owned device destruction succeeds");
    TEST_ASSERT_EQ(probe.allocations, probe.frees,
        "allocation callbacks receive balanced frees");
}

static void test_runtime_allocation_failure_rollback(void)
{
    AllocationProbe probe = {0};
    AgcAllocationCallbacks callbacks = {
        &probe, probe_allocate, probe_free
    };
    AgcDeviceDesc device_desc = AGC_DEVICE_DESC_INIT;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcMemoryStats stats = AGC_MEMORY_STATS_INIT;
    AgcDevice device = NULL;
    AgcBuffer buffer = NULL;

    device_desc.allocation_callbacks = &callbacks;
    buffer_desc.size = 4096u;
    buffer_desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT;
    TEST_ASSERT_EQ(agcCreateDevice(&device_desc, &device), AGC_OK,
        "rollback test device creation succeeds");
    probe.fail_on_attempt = 4u;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &buffer),
        AGC_ERROR_OUT_OF_MEMORY,
        "allocation-record failure propagates out of resource creation");
    TEST_ASSERT(buffer == NULL,
        "allocation-record failure leaves resource output null");
    TEST_ASSERT_EQ(agcGetMemoryStats(device, &stats), AGC_OK,
        "post-failure memory statistics query succeeds");
    TEST_ASSERT_EQ(stats.live_allocation_count, 0u,
        "failed creation leaves no live allocation");
    TEST_ASSERT_EQ(stats.block_count[AGC_MEMORY_HEAP_GARLIC], 0u,
        "failed creation rolls back its newly allocated heap block");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "rollback test device destruction succeeds");
    TEST_ASSERT_EQ(probe.allocations, probe.frees,
        "failed allocation path balances every successful callback allocation");
}

static void test_runtime_all_object_lifecycle(void)
{
    AgcDevice device = create_device();
    AgcQueue graphics_queue = create_queue(device, kAgcQueueGraphics);
    AgcQueue compute_queue = create_queue(device, kAgcQueueCompute);
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
    AgcImageViewDesc view_desc = AGC_IMAGE_VIEW_DESC_INIT;
    AgcSamplerDesc sampler_desc = AGC_SAMPLER_DESC_INIT;
    AgcGraphicsPipelineDesc graphics_desc = AGC_GRAPHICS_PIPELINE_DESC_INIT;
    AgcComputePipelineDesc compute_desc = AGC_COMPUTE_PIPELINE_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcBuffer buffer = NULL;
    AgcImage image = NULL;
    AgcImageView view = NULL;
    AgcSampler sampler = NULL;
    AgcShader vs = create_shader(device, kAgcShaderStageVs);
    AgcShader ps = create_shader(device, kAgcShaderStagePs);
    AgcShader cs = create_shader(device, kAgcShaderStageCs);
    AgcGraphicsPipeline graphics_pipeline = NULL;
    AgcComputePipeline compute_pipeline = NULL;
    AgcCommandBuffer command_buffer = NULL;
    AgcFence fence = NULL;

    buffer_desc.size = 256u;
    buffer_desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT;
    image_desc.format = AGC_FORMAT_RGBA8_UNORM;
    image_desc.usage = AGC_IMAGE_USAGE_SAMPLED_BIT;
    sampler_desc.min_filter = AGC_FILTER_LINEAR;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &buffer), AGC_OK,
        "buffer object creation succeeds");
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &image), AGC_OK,
        "image object creation succeeds");
    view_desc.image = image;
    view_desc.format = image_desc.format;
    TEST_ASSERT_EQ(agcCreateImageView(device, &view_desc, &view), AGC_OK,
        "image-view object creation succeeds");
    TEST_ASSERT_EQ(agcCreateSampler(device, &sampler_desc, &sampler), AGC_OK,
        "sampler object creation succeeds");

    graphics_desc.vertex_shader = vs;
    graphics_desc.pixel_shader = ps;
    compute_desc.shader = cs;
    compute_desc.local_size_x = 64u;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &graphics_desc,
        &graphics_pipeline), AGC_OK,
        "graphics-pipeline object creation succeeds");
    TEST_ASSERT_EQ(agcCreateComputePipeline(device, &compute_desc,
        &compute_pipeline), AGC_OK,
        "compute-pipeline object creation succeeds");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
        &command_buffer), AGC_OK,
        "command-buffer object creation succeeds");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "fence object creation succeeds");

    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_ERROR_BUSY,
        "device rejects destruction with live children");
    TEST_ASSERT_EQ(agcDestroyImage(image), AGC_ERROR_BUSY,
        "image rejects destruction while a view owns it");
    TEST_ASSERT_EQ(agcDestroyShader(vs), AGC_ERROR_BUSY,
        "shader rejects destruction while a pipeline owns it");

    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK, "fence destruction succeeds");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command_buffer), AGC_OK,
        "command-buffer destruction succeeds");
    TEST_ASSERT_EQ(agcDestroyComputePipeline(compute_pipeline), AGC_OK,
        "compute-pipeline destruction succeeds");
    TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(graphics_pipeline), AGC_OK,
        "graphics-pipeline destruction succeeds");
    TEST_ASSERT_EQ(agcDestroyShader(cs), AGC_OK, "compute shader destruction succeeds");
    TEST_ASSERT_EQ(agcDestroyShader(ps), AGC_OK, "pixel shader destruction succeeds");
    TEST_ASSERT_EQ(agcDestroyShader(vs), AGC_OK, "vertex shader destruction succeeds");
    TEST_ASSERT_EQ(agcDestroySampler(sampler), AGC_OK, "sampler destruction succeeds");
    TEST_ASSERT_EQ(agcDestroyImageView(view), AGC_OK, "image-view destruction succeeds");
    TEST_ASSERT_EQ(agcDestroyImage(image), AGC_OK, "image destruction succeeds");
    TEST_ASSERT_EQ(agcDestroyBuffer(buffer), AGC_OK, "buffer destruction succeeds");
    TEST_ASSERT_EQ(agcDestroyQueue(compute_queue), AGC_OK,
        "compute queue destruction succeeds");
    TEST_ASSERT_EQ(agcDestroyQueue(graphics_queue), AGC_OK,
        "graphics queue destruction succeeds");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "device destroys after all children");
}

static void test_runtime_fence_and_command_states(void)
{
    AgcDevice device = create_device();
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFence fence = NULL;
    AgcCommandBuffer command_buffer = NULL;
    AgcCommandBufferState state = AGC_COMMAND_BUFFER_STATE_PENDING;

    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "unsignaled fence creation succeeds");
    TEST_ASSERT_EQ(agcGetFenceStatus(fence), AGC_ERROR_BUSY,
        "unsignaled fence status is busy");
    TEST_ASSERT_EQ(agcWaitFence(fence, 0u), AGC_ERROR_TIMEOUT,
        "zero-duration finite fence wait times out");
    TEST_ASSERT_EQ(agcWaitFence(fence, 1000u), AGC_ERROR_TIMEOUT,
        "positive finite fence wait times out");
    TEST_ASSERT_EQ(agcWaitFence(fence, AGC_RUNTIME_INFINITE_TIMEOUT),
        AGC_ERROR_INVALID_ARGUMENT,
        "infinite fence wait is rejected");

    command_desc.capacity_dwords = 4u;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
        &command_buffer), AGC_OK, "small command buffer creation succeeds");
    TEST_ASSERT_EQ(agcGetCommandBufferState(command_buffer, &state), AGC_OK,
        "command state query succeeds");
    TEST_ASSERT_EQ(state, AGC_COMMAND_BUFFER_STATE_INITIAL,
        "new command buffer is initial");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command_buffer), AGC_ERROR_INVALID_STATE,
        "end before begin is rejected");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command_buffer), AGC_OK,
        "begin transitions to recording");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command_buffer), AGC_ERROR_INVALID_STATE,
        "double begin is rejected");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command_buffer), AGC_ERROR_BUSY,
        "recording command buffer cannot be destroyed");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command_buffer), AGC_ERROR_INVALID_STATE,
        "empty command buffer cannot become executable");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command_buffer), AGC_OK,
        "recording command buffer can recover through reset");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command_buffer), AGC_OK,
        "reset command buffer can be destroyed");
    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK, "fence destruction succeeds");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK, "device destruction succeeds");
}

static void test_runtime_compute_submission(void)
{
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueCompute);
    AgcShader shader = create_shader(device, kAgcShaderStageCs);
    AgcComputePipelineDesc pipeline_desc = AGC_COMPUTE_PIPELINE_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcComputePipeline pipeline = NULL;
    AgcCommandBuffer command_buffer = NULL;
    AgcFence fence = NULL;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    const AgcCommandBufferSubmit *captured;
    const uint32_t *words;
    uint32_t owner = UINT32_MAX;

    pipeline_desc.shader = shader;
    pipeline_desc.local_size_x = 64u;
    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 64u;
    TEST_ASSERT_EQ(agcCreateComputePipeline(device, &pipeline_desc, &pipeline),
        AGC_OK, "compute pipeline creation succeeds");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
        &command_buffer), AGC_OK, "compute command buffer creation succeeds");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "compute fence creation succeeds");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command_buffer), AGC_OK,
        "compute command buffer begins");
    TEST_ASSERT_EQ(agcCmdDispatch(command_buffer, 1u, 1u, 1u),
        AGC_ERROR_INVALID_STATE, "dispatch requires a bound pipeline");
    TEST_ASSERT_EQ(agcCmdBindComputePipeline(command_buffer, pipeline), AGC_OK,
        "compute pipeline bind succeeds");
    TEST_ASSERT_EQ(agcCmdDispatch(command_buffer, 3u, 2u, 1u), AGC_OK,
        "compute dispatch records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command_buffer), AGC_OK,
        "compute command buffer becomes executable");
    TEST_ASSERT_EQ(agcDestroyComputePipeline(pipeline), AGC_ERROR_BUSY,
        "recorded compute pipeline cannot be destroyed");

    submit.command_buffer_count = 1u;
    submit.command_buffers = &command_buffer;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "compute command buffer submits");
    captured = agcDriverDebugLastAcbSubmit(&owner);
    TEST_ASSERT_EQ(captured->dword_count, 36u,
        "compute submission captures validated pipeline state and dispatch");
    words = (const uint32_t *)(uintptr_t)captured->command_address;
    words += captured->dword_count - 5u;
    TEST_ASSERT_EQ((words[0] >> 8) & 0xffu, AGC_PM4_OP_DISPATCH_DIRECT,
        "compute submission records DISPATCH_DIRECT");
    TEST_ASSERT_EQ(words[0] & 1u, 1u,
        "compute dispatch carries shader-type bit");
    TEST_ASSERT_EQ(words[1], 3u, "compute dispatch records group count X");
    TEST_ASSERT_EQ(words[2], 2u, "compute dispatch records group count Y");
    TEST_ASSERT_EQ(agcGetFenceStatus(fence), AGC_OK,
        "successful compute submission signals fence");
    TEST_ASSERT_EQ(agcWaitFence(fence, 1u), AGC_OK,
        "finite wait observes signaled compute fence");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "completed compute fence resets");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command_buffer), AGC_OK,
        "completed compute command buffer resets");

    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK, "compute fence destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command_buffer), AGC_OK,
        "compute command buffer destroys");
    TEST_ASSERT_EQ(agcDestroyComputePipeline(pipeline), AGC_OK,
        "compute pipeline destroys after reset");
    TEST_ASSERT_EQ(agcDestroyShader(shader), AGC_OK, "compute shader destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK, "compute queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK, "compute device destroys");
}

static void test_runtime_indexed_graphics_submission(void)
{
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueGraphics);
    AgcShader vs = create_shader(device, kAgcShaderStageVs);
    AgcShader ps = create_shader(device, kAgcShaderStagePs);
    AgcGraphicsPipelineDesc pipeline_desc = AGC_GRAPHICS_PIPELINE_DESC_INIT;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcGraphicsPipeline pipeline = NULL;
    AgcBuffer index_buffer = NULL;
    AgcCommandBuffer command_buffer = NULL;
    AgcFence fence = NULL;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    const AgcCommandBufferSubmit *captured;
    const uint32_t *words;

    pipeline_desc.vertex_shader = vs;
    pipeline_desc.pixel_shader = ps;
    buffer_desc.size = 64u;
    buffer_desc.usage = AGC_BUFFER_USAGE_INDEX_BIT;
    command_desc.capacity_dwords = 97u;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &pipeline_desc, &pipeline),
        AGC_OK, "graphics pipeline creation succeeds");
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &index_buffer), AGC_OK,
        "index buffer creation succeeds");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
        &command_buffer), AGC_OK, "graphics command buffer creation succeeds");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "graphics fence creation succeeds");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command_buffer), AGC_OK,
        "graphics command buffer begins");
    TEST_ASSERT_EQ(agcCmdBindGraphicsPipeline(command_buffer, pipeline), AGC_OK,
        "graphics pipeline bind succeeds");
    TEST_ASSERT_EQ(agcCmdBindIndexBuffer(command_buffer, index_buffer, 0u,
        kAgcIndexSize16), AGC_OK, "index buffer bind succeeds");
    TEST_ASSERT_EQ(agcCmdDrawIndexed(command_buffer, 6u, 2u, 1u, 0, 0u),
        AGC_OK, "indexed draw records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command_buffer), AGC_OK,
        "graphics command buffer becomes executable");
    TEST_ASSERT_EQ(agcDestroyBuffer(index_buffer), AGC_ERROR_BUSY,
        "recorded index buffer cannot be destroyed");

    submit.command_buffer_count = 1u;
    submit.command_buffers = &command_buffer;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "indexed graphics command buffer submits");
    captured = agcDriverDebugLastDcbSubmit();
    TEST_ASSERT_EQ(captured->dword_count, 97u,
        "indexed graphics submission captures pipeline bind and draw");
    words = (const uint32_t *)(uintptr_t)captured->command_address;
    words += captured->dword_count - 11u;
    TEST_ASSERT_EQ((words[0] >> 8) & 0xffu, AGC_PM4_OP_SET_INDEX_SIZE,
        "indexed submission records SET_INDEX_SIZE");
    TEST_ASSERT_EQ((words[3] >> 8) & 0xffu, AGC_PM4_OP_NUM_INSTANCES,
        "indexed submission records NUM_INSTANCES");
    TEST_ASSERT_EQ(words[4], 2u, "indexed submission records instance count");
    TEST_ASSERT_EQ((words[5] >> 8) & 0xffu, AGC_PM4_OP_DRAW_INDEX_2,
        "indexed submission records DRAW_INDEX_2");
    TEST_ASSERT_EQ(words[9], 6u, "indexed submission records index count");
    TEST_ASSERT_EQ(agcGetFenceStatus(fence), AGC_OK,
        "successful graphics submission signals fence");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command_buffer), AGC_OK,
        "completed graphics command buffer resets");
    TEST_ASSERT_EQ(agcDestroyBuffer(index_buffer), AGC_OK,
        "index buffer destroys after command reset");
    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK, "graphics fence destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command_buffer), AGC_OK,
        "graphics command buffer destroys");
    TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(pipeline), AGC_OK,
        "graphics pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyShader(ps), AGC_OK, "pixel shader destroys");
    TEST_ASSERT_EQ(agcDestroyShader(vs), AGC_OK, "vertex shader destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK, "graphics queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK, "graphics device destroys");
}

static void test_runtime_command_space_atomic_failure(void)
{
    AgcDevice device = create_device();
    AgcShader vs = create_shader(device, kAgcShaderStageVs);
    AgcShader ps = create_shader(device, kAgcShaderStagePs);
    AgcGraphicsPipelineDesc pipeline_desc = AGC_GRAPHICS_PIPELINE_DESC_INIT;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcGraphicsPipeline pipeline = NULL;
    AgcBuffer index_buffer = NULL;
    AgcCommandBuffer command_buffer = NULL;

    pipeline_desc.vertex_shader = vs;
    pipeline_desc.pixel_shader = ps;
    buffer_desc.size = 64u;
    buffer_desc.usage = AGC_BUFFER_USAGE_INDEX_BIT;
    command_desc.capacity_dwords = 93u;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &pipeline_desc, &pipeline),
        AGC_OK, "small-buffer graphics pipeline creation succeeds");
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &index_buffer), AGC_OK,
        "small-buffer index buffer creation succeeds");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
        &command_buffer), AGC_OK,
        "bind-plus-ten-dword command buffer creation succeeds");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command_buffer), AGC_OK,
        "bind-plus-ten-dword command buffer begins");
    TEST_ASSERT_EQ(agcCmdBindGraphicsPipeline(command_buffer, pipeline), AGC_OK,
        "small-buffer graphics pipeline binds");
    TEST_ASSERT_EQ(agcCmdBindIndexBuffer(command_buffer, index_buffer, 0u,
        kAgcIndexSize16), AGC_OK, "small-buffer index buffer binds");
    TEST_ASSERT_EQ(agcCmdDrawIndexed(command_buffer, 3u, 1u, 0u, 0, 0u),
        AGC_ERROR_COMMAND_SPACE_EXHAUSTED,
        "indexed draw reports stable command-space exhaustion");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command_buffer), AGC_OK,
        "failed draw preserves the already-recorded pipeline bind");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command_buffer), AGC_OK,
        "failed recording resets cleanly");

    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command_buffer), AGC_OK,
        "small command buffer destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(index_buffer), AGC_OK,
        "small-buffer index buffer destroys");
    TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(pipeline), AGC_OK,
        "small-buffer graphics pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyShader(ps), AGC_OK, "small-buffer PS destroys");
    TEST_ASSERT_EQ(agcDestroyShader(vs), AGC_OK, "small-buffer VS destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "small-buffer device destroys");
}

static void test_runtime_dynamic_graphics_state(void)
{
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueGraphics);
    AgcShader vs = create_shader(device, kAgcShaderStageVs);
    AgcShader ps = create_shader(device, kAgcShaderStagePs);
    AgcGraphicsPipelineDesc pipeline_desc = AGC_GRAPHICS_PIPELINE_DESC_INIT;
    AgcDepthStencilPipelineState depth_stencil =
        AGC_DEPTH_STENCIL_PIPELINE_STATE_INIT;
    AgcRasterizationState rasterization = AGC_RASTERIZATION_STATE_INIT;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcViewport viewport = AGC_VIEWPORT_INIT;
    AgcScissor scissor = AGC_SCISSOR_INIT;
    AgcDepthBias depth_bias = AGC_DEPTH_BIAS_INIT;
    AgcGraphicsPipeline pipeline = NULL;
    AgcBuffer index_buffer = NULL;
    AgcCommandBuffer command = NULL;
    AgcFence fence = NULL;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    const AgcCommandBufferSubmit *captured;
    const uint32_t *words;
    uint32_t value = 0u;
    const float blend_constants[4] = {0.25f, 0.5f, 0.75f, 1.0f};

    pipeline_desc.vertex_shader = vs;
    pipeline_desc.pixel_shader = ps;
    depth_stencil.format = AGC_FORMAT_D32_FLOAT_S8_UINT;
    depth_stencil.stencil_test_enable = 1u;
    depth_stencil.back_face_enable = 1u;
    depth_stencil.front.compare_mask = 0x3fu;
    depth_stencil.front.write_mask = 0x5au;
    depth_stencil.back.compare_mask = 0xc3u;
    depth_stencil.back.write_mask = 0xa5u;
    pipeline_desc.depth_stencil = &depth_stencil;
    rasterization.cull_mode = AGC_CULL_MODE_FRONT_BIT;
    rasterization.front_face = AGC_FRONT_FACE_CLOCKWISE;
    rasterization.depth_bias_enable = 1u;
    pipeline_desc.rasterization = &rasterization;
    pipeline_desc.dynamic_state_mask = AGC_DYNAMIC_STATE_VIEWPORT_BIT |
        AGC_DYNAMIC_STATE_SCISSOR_BIT |
        AGC_DYNAMIC_STATE_BLEND_CONSTANTS_BIT |
        AGC_DYNAMIC_STATE_STENCIL_REFERENCE_BIT |
        AGC_DYNAMIC_STATE_DEPTH_BIAS_BIT;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &pipeline_desc,
        &pipeline), AGC_OK, "dynamic-state graphics pipeline creates");
    buffer_desc.size = 64u;
    buffer_desc.usage = AGC_BUFFER_USAGE_INDEX_BIT;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &index_buffer),
        AGC_OK, "dynamic-state index buffer creates");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &command),
        AGC_OK, "dynamic-state command buffer creates");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "dynamic-state fence creates");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "dynamic-state command buffer begins");
    TEST_ASSERT_EQ(agcCmdBindGraphicsPipeline(command, pipeline), AGC_OK,
        "dynamic-state graphics pipeline binds");
    TEST_ASSERT_EQ(agcCmdBindIndexBuffer(command, index_buffer, 0u,
        kAgcIndexSize16), AGC_OK, "dynamic-state index buffer binds");
    TEST_ASSERT_EQ(agcCmdDrawIndexed(command, 3u, 1u, 0u, 0, 0u),
        AGC_ERROR_INVALID_STATE,
        "draw rejects unset required dynamic state");
    viewport.width = 0.0f;
    TEST_ASSERT_EQ(agcCmdSetViewport(command, &viewport),
        AGC_ERROR_INVALID_ARGUMENT,
        "invalid viewport fails before satisfying dynamic state");
    viewport.width = 1280.0f;
    viewport.height = 720.0f;
    scissor.width = 1280u;
    scissor.height = 720u;
    TEST_ASSERT_EQ(agcCmdSetViewport(command, &viewport), AGC_OK,
        "valid viewport records");
    TEST_ASSERT_EQ(agcCmdSetScissor(command, &scissor), AGC_OK,
        "valid scissor records");
    TEST_ASSERT_EQ(agcCmdSetBlendConstants(command, blend_constants), AGC_OK,
        "valid blend constants record");
    TEST_ASSERT_EQ(agcCmdSetStencilReference(command, 3u, 7u), AGC_OK,
        "valid stencil references record");
    TEST_ASSERT_EQ(agcCmdSetDepthBias(command, NULL),
        AGC_ERROR_INVALID_ARGUMENT,
        "declared dynamic depth bias validates its descriptor");
    depth_bias.constant_factor = 2.0f;
    depth_bias.clamp = 0.25f;
    depth_bias.slope_factor = -1.5f;
    TEST_ASSERT_EQ(agcCmdSetDepthBias(command, &depth_bias), AGC_OK,
        "valid dynamic depth bias records");
    TEST_ASSERT_EQ(agcCmdDrawIndexed(command, 3u, 1u, 0u, 0, 0u), AGC_OK,
        "draw records after all required dynamic state");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command), AGC_OK,
        "dynamic-state command buffer becomes executable");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &command;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "dynamic-state command buffer submits");
    captured = agcDriverDebugLastDcbSubmit();
    words = (const uint32_t *)(uintptr_t)captured->command_address;
    TEST_ASSERT(runtime_find_context_register(words, captured->dword_count,
        AGC_REG_PA_SU_SC_MODE_CNTL, &value),
        "static fill/cull/front-face rasterization state is emitted");
    TEST_ASSERT_EQ(value,
        (1u << AGC_REG_PA_SU_SC_MODE_CNTL_CULL_FRONT_SHIFT) |
        ((uint32_t)AGC_FRONT_FACE_CLOCKWISE <<
            AGC_REG_PA_SU_SC_MODE_CNTL_FACE_SHIFT) |
        AGC_GFX1013_DEPTH_BIAS_RASTER_MODE,
        "static rasterization state preserves fill, cull, face, and depth bias");
    TEST_ASSERT(runtime_find_context_register(words, captured->dword_count,
        AGC_REG_DB_STENCILREFMASK, &value),
        "dynamic front stencil reference is emitted");
    TEST_ASSERT_EQ(value, 0x035a3f03u,
        "dynamic front reference preserves static compare/write masks");
    TEST_ASSERT(runtime_find_context_register(words, captured->dword_count,
        AGC_REG_DB_STENCILREFMASK_BF, &value),
        "dynamic back stencil reference is emitted");
    TEST_ASSERT_EQ(value, 0x07a5c307u,
        "dynamic back reference preserves static compare/write masks");
    TEST_ASSERT(runtime_find_context_register(words, captured->dword_count,
        AGC_REG_PA_SU_POLY_OFFSET_DB_FMT_CNTL, &value),
        "dynamic depth bias emits its depth-format control");
    TEST_ASSERT_EQ(value, 0x000001e9u,
        "dynamic D32 depth-bias format control is exact");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
        "dynamic-state command buffer resets");
    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK,
        "dynamic-state fence destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command), AGC_OK,
        "dynamic-state command buffer destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(index_buffer), AGC_OK,
        "dynamic-state index buffer destroys");
    TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(pipeline), AGC_OK,
        "dynamic-state pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyShader(ps), AGC_OK,
        "dynamic-state pixel shader destroys");
    TEST_ASSERT_EQ(agcDestroyShader(vs), AGC_OK,
        "dynamic-state vertex shader destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK,
        "dynamic-state queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "dynamic-state device destroys");
}

static void test_runtime_depth_stencil_pipeline_state(void)
{
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueGraphics);
    AgcShader vs = create_shader(device, kAgcShaderStageVs);
    AgcShader ps = create_shader(device, kAgcShaderStagePs);
    AgcGraphicsPipelineDesc desc = AGC_GRAPHICS_PIPELINE_DESC_INIT;
    AgcDepthStencilPipelineState state =
        AGC_DEPTH_STENCIL_PIPELINE_STATE_INIT;
    AgcDepthStencilPipelineState invalid;
    RuntimeDepthStencilStateV1Fixture legacy = {
        sizeof(RuntimeDepthStencilStateV1Fixture),
        AGC_RUNTIME_STRUCTURE_VERSION_1,
        AGC_FORMAT_D32_FLOAT, 1u, 1u, AGC_COMPARE_OPERATION_LESS,
        0u, 0u, 0u, 0u, {0u, 0u, 0u}};
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcGraphicsPipeline pipeline = NULL;
    AgcGraphicsPipeline rejected = NULL;
    AgcCommandBuffer command = NULL;
    AgcFence fence = NULL;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    const AgcCommandBufferSubmit *captured;
    const uint32_t *words;
    uint32_t value = 0u;

    state.format = AGC_FORMAT_D32_FLOAT_S8_UINT;
    state.depth_test_enable = 1u;
    state.depth_write_enable = 1u;
    state.depth_compare_operation = AGC_COMPARE_OPERATION_LESS_OR_EQUAL;
    state.depth_bounds_enable = 1u;
    state.min_depth_bounds = 0.25f;
    state.max_depth_bounds = 0.75f;
    state.stencil_test_enable = 1u;
    state.back_face_enable = 1u;
    state.front.compare_operation = AGC_COMPARE_OPERATION_ALWAYS;
    state.front.fail_operation = AGC_STENCIL_OPERATION_KEEP;
    state.front.depth_fail_operation =
        AGC_STENCIL_OPERATION_INCREMENT_AND_CLAMP;
    state.front.pass_operation = AGC_STENCIL_OPERATION_REPLACE;
    state.front.reference = 0x12u;
    state.front.compare_mask = 0xabu;
    state.front.write_mask = 0xcdu;
    state.back.compare_operation = AGC_COMPARE_OPERATION_LESS;
    state.back.fail_operation = AGC_STENCIL_OPERATION_ZERO;
    state.back.depth_fail_operation =
        AGC_STENCIL_OPERATION_DECREMENT_AND_WRAP;
    state.back.pass_operation = AGC_STENCIL_OPERATION_INVERT;
    state.back.reference = 0x34u;
    state.back.compare_mask = 0x56u;
    state.back.write_mask = 0x78u;
    desc.vertex_shader = vs;
    desc.pixel_shader = ps;
    desc.depth_stencil = &state;

    desc.depth_stencil = (const AgcDepthStencilPipelineState *)
        (const void *)&legacy;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &desc, &rejected),
        AGC_OK, "legacy v1 depth-only state normalizes into v2");
    TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(rejected), AGC_OK,
        "normalized legacy depth pipeline destroys");
    rejected = NULL;
    legacy.stencil_test_enable = 1u;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &desc, &rejected),
        AGC_ERROR_NOT_SUPPORTED,
        "legacy stencil request fails without invented operations");
    TEST_ASSERT(rejected == NULL,
        "legacy stencil rejection leaves pipeline output null");
    legacy.stencil_test_enable = 0u;

    invalid = state;
    invalid.format = AGC_FORMAT_D32_FLOAT;
    desc.depth_stencil = &invalid;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &desc, &rejected),
        AGC_ERROR_INVALID_ARGUMENT,
        "stencil test rejects a depth-only attachment format");
    TEST_ASSERT(rejected == NULL,
        "invalid stencil format leaves pipeline output null");
    invalid = state;
    invalid.min_depth_bounds = -0.25f;
    desc.depth_stencil = &invalid;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &desc, &rejected),
        AGC_ERROR_INVALID_ARGUMENT,
        "out-of-range depth bounds reject pipeline creation");
    invalid = state;
    invalid.stencil_test_enable = 0u;
    desc.depth_stencil = &invalid;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &desc, &rejected),
        AGC_ERROR_INVALID_ARGUMENT,
        "back-face stencil state requires stencil testing");

    desc.depth_stencil = &state;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &desc, &pipeline),
        AGC_OK, "full front/back depth-stencil pipeline creates");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &command),
        AGC_OK, "depth-stencil command buffer creates");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "depth-stencil fence creates");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "depth-stencil command buffer begins");
    TEST_ASSERT_EQ(agcCmdBindGraphicsPipeline(command, pipeline), AGC_OK,
        "depth-stencil pipeline bind records immutable state");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command), AGC_OK,
        "depth-stencil bind command buffer becomes executable");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &command;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "depth-stencil bind submits on the host backend");
    captured = agcDriverDebugLastDcbSubmit();
    words = (const uint32_t *)(uintptr_t)captured->command_address;
    TEST_ASSERT(runtime_find_context_register(words, captured->dword_count,
        AGC_REG_DB_DEPTH_CONTROL, &value),
        "depth-stencil bind emits DB_DEPTH_CONTROL");
    TEST_ASSERT_EQ(value, 0x001007bfu,
        "native depth-stencil state preserves exact depth control");
    TEST_ASSERT(runtime_find_context_register(words, captured->dword_count,
        AGC_REG_DB_STENCIL_CONTROL, &value),
        "depth-stencil bind emits DB_STENCIL_CONTROL");
    TEST_ASSERT_EQ(value, 0x00971530u,
        "native stencil operations preserve exact front/back encoding");
    TEST_ASSERT(runtime_find_context_register(words, captured->dword_count,
        AGC_REG_DB_STENCILREFMASK, &value),
        "depth-stencil bind emits front stencil masks");
    TEST_ASSERT_EQ(value, 0x12cdab12u,
        "native front stencil reference and masks are exact");
    TEST_ASSERT(runtime_find_context_register(words, captured->dword_count,
        AGC_REG_DB_STENCILREFMASK_BF, &value),
        "depth-stencil bind emits back stencil masks");
    TEST_ASSERT_EQ(value, 0x34785634u,
        "native back stencil reference and masks are exact");
    TEST_ASSERT(runtime_find_context_register(words, captured->dword_count,
        AGC_REG_DB_DEPTH_BOUNDS_MIN, &value),
        "depth-stencil bind emits minimum depth bound");
    TEST_ASSERT_EQ(value, 0x3e800000u,
        "native minimum depth bound is exact");
    TEST_ASSERT(runtime_find_context_register(words, captured->dword_count,
        AGC_REG_DB_DEPTH_BOUNDS_MAX, &value),
        "depth-stencil bind emits maximum depth bound");
    TEST_ASSERT_EQ(value, 0x3f400000u,
        "native maximum depth bound is exact");

    TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
        "depth-stencil command buffer resets");
    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK,
        "depth-stencil fence destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command), AGC_OK,
        "depth-stencil command buffer destroys");
    TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(pipeline), AGC_OK,
        "depth-stencil pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyShader(ps), AGC_OK,
        "depth-stencil pixel shader destroys");
    TEST_ASSERT_EQ(agcDestroyShader(vs), AGC_OK,
        "depth-stencil vertex shader destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK,
        "depth-stencil queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "depth-stencil device destroys");
}

static void test_runtime_multisample_pipeline_state(void)
{
    AgcDevice device = create_device();
    AgcShader vs = create_shader(device, kAgcShaderStageVs);
    AgcShaderReflection ps_requirements = AGC_SHADER_REFLECTION_INIT;
    AgcShader ps;
    AgcGraphicsPipelineDesc desc = AGC_GRAPHICS_PIPELINE_DESC_INIT;
    AgcMultisampleState multisample = AGC_MULTISAMPLE_STATE_INIT;
    AgcGraphicsPipeline pipeline = NULL;

    ps_requirements.flags = AGC_SHADER_REFLECTION_USES_SAMPLE_SHADING_BIT;
    ps_requirements.pixel_shader_sample_count = 1u;
    ps = create_shader_with_reflection(
        device, kAgcShaderStagePs, &ps_requirements);
    desc.vertex_shader = vs;
    desc.pixel_shader = ps;
    desc.multisample = &multisample;
    multisample.rasterization_samples = 4u;
    multisample.sample_shading_enable = 1u;
    multisample.minimum_sample_shading = 0.5f;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &desc, &pipeline),
        AGC_ERROR_VALIDATION_FAILED,
        "minimum sample shading rejects insufficient shader iterations");
    TEST_ASSERT(pipeline == NULL,
        "sample-shading mismatch leaves pipeline output null");

    multisample.minimum_sample_shading = 0.25f;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &desc, &pipeline),
        AGC_OK, "qualified one-of-four sample shading creates pipeline");
    TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(pipeline), AGC_OK,
        "sample-shading pipeline destroys");
    pipeline = NULL;

    multisample.alpha_to_coverage_enable = 1u;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &desc, &pipeline),
        AGC_ERROR_NOT_SUPPORTED,
        "unqualified alpha-to-coverage fails closed");
    TEST_ASSERT(pipeline == NULL,
        "alpha-to-coverage rejection leaves pipeline output null");
    multisample.alpha_to_coverage_enable = 0u;
    multisample.alpha_to_one_enable = 1u;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &desc, &pipeline),
        AGC_ERROR_NOT_SUPPORTED,
        "unqualified alpha-to-one fails closed");
    TEST_ASSERT(pipeline == NULL,
        "alpha-to-one rejection leaves pipeline output null");
    multisample.alpha_to_one_enable = 0u;
    multisample.sample_shading_enable = 0u;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &desc, &pipeline),
        AGC_ERROR_INVALID_ARGUMENT,
        "nonzero minimum shading requires sample shading enable");
    TEST_ASSERT(pipeline == NULL,
        "invalid minimum shading leaves pipeline output null");

    TEST_ASSERT_EQ(agcDestroyShader(ps), AGC_OK,
        "sample-shading pixel shader destroys");
    TEST_ASSERT_EQ(agcDestroyShader(vs), AGC_OK,
        "sample-shading vertex shader destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "sample-shading device destroys");
}

static void test_runtime_shader_reflection_contract(void)
{
    AgcDevice device = create_device();
    AgcShader shader = create_shader(device, kAgcShaderStageCs);
    AgcShader ngg_shader = NULL;
    AgcShaderReflection reflection = AGC_SHADER_REFLECTION_INIT;

    TEST_ASSERT_EQ(agcGetShaderReflection(shader, &reflection), AGC_OK,
        "shader reflection query succeeds");
    TEST_ASSERT_EQ(reflection.struct_size, sizeof(reflection),
        "shader reflection reports its exact ABI size");
    TEST_ASSERT_EQ(reflection.version, AGC_SHADER_REFLECTION_VERSION,
        "shader reflection reports its contract version");
    TEST_ASSERT_EQ(reflection.compiler_api_version,
        AGC_SHADER_COMPILER_API_VERSION,
        "shader reflection reports the supported compiler API");
    TEST_ASSERT_EQ(reflection.stage, kAgcShaderStageCs,
        "shader reflection preserves the compiler stage");
    TEST_ASSERT_EQ(reflection.local_size_x, 64u,
        "shader reflection preserves compute local size");
    TEST_ASSERT_EQ(reflection.hash_algorithm, AGC_SHADER_HASH_FNV1A64,
        "shader reflection names its deterministic hash algorithm");
    TEST_ASSERT(reflection.code_hash != 0u,
        "shader reflection preserves a nonzero code hash");
    TEST_ASSERT_EQ(reflection.stage_linkage_hash,
        shader_fixture_linkage_hash(&reflection),
        "shader reflection preserves an integrity-checked linkage hash");

    reflection = (AgcShaderReflection)AGC_SHADER_REFLECTION_INIT;
    reflection.version++;
    TEST_ASSERT_EQ(agcGetShaderReflection(shader, &reflection),
        AGC_ERROR_INVALID_ARGUMENT,
        "shader reflection query rejects unknown output versions");
    TEST_ASSERT_EQ(agcDestroyShader(shader), AGC_OK,
        "reflected shader destroys");

    ngg_shader = create_ngg_shader_bundle(
        device, kAgcShaderStageVs, NULL);
    reflection = (AgcShaderReflection)AGC_SHADER_REFLECTION_INIT;
    TEST_ASSERT_EQ(agcGetShaderReflection(ngg_shader, &reflection), AGC_OK,
        "compiler-style NGG front/back reflection is queryable");
    TEST_ASSERT(reflection.front_code_size != 0u,
        "compiler-style NGG bundle retains its front program range");
    TEST_ASSERT_EQ(agcDestroyShader(ngg_shader), AGC_OK,
        "compiler-style NGG shader bundle destroys");

    {
        AgcShaderReflection legacy = {0};
        AgcShader legacy_shader;

        legacy.struct_size = sizeof(legacy);
        legacy.version = AGC_SHADER_REFLECTION_VERSION_1;
        legacy.compiler_api_version = AGC_SHADER_COMPILER_API_VERSION_14;
        legacy_shader = create_shader_with_reflection(
            device, kAgcShaderStageCs, &legacy);
        reflection = (AgcShaderReflection)AGC_SHADER_REFLECTION_INIT;
        TEST_ASSERT_EQ(agcGetShaderReflection(legacy_shader, &reflection),
            AGC_OK, "API-14 reflection remains queryable");
        TEST_ASSERT_EQ(reflection.version,
            AGC_SHADER_REFLECTION_VERSION_1,
            "legacy shader preserves reflection v1");
        TEST_ASSERT_EQ(reflection.compiler_api_version,
            AGC_SHADER_COMPILER_API_VERSION_14,
            "legacy shader preserves compiler API 14");
        TEST_ASSERT_EQ(agcDestroyShader(legacy_shader), AGC_OK,
            "legacy reflected shader destroys");
    }

    {
        RuntimeShaderFixture back_only = {0};
        AgcShaderDesc desc = AGC_SHADER_DESC_INIT;
        AgcShader invalid = NULL;

        back_only.record.magic = AGC_SHADER_RECORD_MAGIC;
        back_only.record.version = AGC_SHADER_RECORD_VERSION_GEN5;
        back_only.record.shader_type =
            (uint8_t)kAgcShaderBinaryTypeGsBack;
        back_only.record.code = offsetof(RuntimeShaderFixture, code);
        back_only.code[0] = 0xBF810000u;
        reflection = (AgcShaderReflection)AGC_SHADER_REFLECTION_INIT;
        reflection.stage = kAgcShaderStageVs;
        reflection.flags = AGC_SHADER_REFLECTION_NGG_BIT;
        reflection.shader_record_version = AGC_SHADER_RECORD_VERSION_GEN5;
        reflection.compiler_api_version = AGC_SHADER_COMPILER_API_VERSION;
        reflection.wave_size = 32u;
        reflection.hash_algorithm = AGC_SHADER_HASH_FNV1A64;
        reflection.code_offset = offsetof(RuntimeShaderFixture, code);
        reflection.code_size = sizeof(back_only.code);
        reflection.code_hash = shader_fixture_hash(
            &back_only, sizeof(back_only));
        memcpy(reflection.entry_point, "main", sizeof("main"));
        reflection.stage_linkage_hash = shader_fixture_linkage_hash(
            &reflection);
        desc.stage = kAgcShaderStageVs;
        desc.code = &back_only;
        desc.code_size = sizeof(back_only);
        desc.reflection = &reflection;
        reflection.stage_linkage_hash ^= UINT64_C(1);
        TEST_ASSERT_EQ(agcCreateShader(device, &desc, &invalid),
            AGC_ERROR_SHADER_INVALID,
            "corrupt stage linkage reflection fails closed");
        TEST_ASSERT(invalid == NULL,
            "corrupt linkage leaves shader output null");
        reflection.stage_linkage_hash = shader_fixture_linkage_hash(
            &reflection);
        TEST_ASSERT_EQ(agcCreateShader(device, &desc, &invalid),
            AGC_ERROR_SHADER_INVALID_TYPE,
            "NGG back record without its front program fails closed");
        TEST_ASSERT(invalid == NULL,
            "incomplete NGG bundle leaves shader output null");
    }
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "reflection test device destroys");
}

typedef struct PipelineCompatibilityCase {
    AgcShaderColorExportFormat export_format;
    AgcShaderComponentClass component_class;
    AgcFormat attachment_format;
} PipelineCompatibilityCase;

static void test_runtime_graphics_pipeline_compatibility_matrix(void)
{
    static const PipelineCompatibilityCase cases[] = {
        {AGC_SHADER_COLOR_EXPORT_FP16_ABGR,
         AGC_SHADER_COMPONENT_FLOAT_OR_NORMALIZED, AGC_FORMAT_RGBA8_UNORM},
        {AGC_SHADER_COLOR_EXPORT_32_ABGR,
         AGC_SHADER_COMPONENT_FLOAT_OR_NORMALIZED, AGC_FORMAT_RGBA32_FLOAT},
        {AGC_SHADER_COLOR_EXPORT_UINT16_ABGR,
         AGC_SHADER_COMPONENT_UINT, AGC_FORMAT_RGBA16_UINT},
        {AGC_SHADER_COLOR_EXPORT_32_ABGR,
         AGC_SHADER_COMPONENT_UINT, AGC_FORMAT_RGBA32_UINT},
        {AGC_SHADER_COLOR_EXPORT_SINT16_ABGR,
         AGC_SHADER_COMPONENT_SINT, AGC_FORMAT_RGBA16_SINT},
        {AGC_SHADER_COLOR_EXPORT_32_ABGR,
         AGC_SHADER_COMPONENT_SINT, AGC_FORMAT_RGBA32_SINT},
    };
    static const AgcFormat mismatched_formats[] = {
        AGC_FORMAT_RGBA8_UNORM,
        AGC_FORMAT_RGBA16_UINT,
        AGC_FORMAT_RGBA16_SINT,
    };
    AgcDevice device = create_device();
    AgcShader vs = create_shader(device, kAgcShaderStageVs);
    uint32_t i;
    uint32_t j;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        AgcShaderReflection requirements = AGC_SHADER_REFLECTION_INIT;
        AgcGraphicsPipelineDesc desc = AGC_GRAPHICS_PIPELINE_DESC_INIT;
        AgcColorBlendAttachmentState attachment =
            AGC_COLOR_BLEND_ATTACHMENT_STATE_INIT;
        AgcShader ps;
        AgcGraphicsPipeline pipeline = NULL;

        requirements.color_export_count = 1u;
        requirements.color_exports[0].location = 0u;
        requirements.color_exports[0].format = cases[i].export_format;
        requirements.color_exports[0].component_class =
            cases[i].component_class;
        requirements.color_exports[0].write_mask = 0xfu;
        ps = create_shader_with_reflection(
            device, kAgcShaderStagePs, &requirements);
        attachment.format = cases[i].attachment_format;
        desc.vertex_shader = vs;
        desc.pixel_shader = ps;
        desc.color_attachment_count = 1u;
        desc.color_attachments = &attachment;
        TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &desc, &pipeline),
            AGC_OK, "matching export and attachment class creates pipeline");
        TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(pipeline), AGC_OK,
            "compatible graphics pipeline destroys");

        for (j = 0u; j < sizeof(mismatched_formats) /
             sizeof(mismatched_formats[0]); ++j) {
            AgcShaderComponentClass attachment_class =
                j == 0u ? AGC_SHADER_COMPONENT_FLOAT_OR_NORMALIZED :
                j == 1u ? AGC_SHADER_COMPONENT_UINT :
                    AGC_SHADER_COMPONENT_SINT;
            if (attachment_class == cases[i].component_class)
                continue;
            attachment.format = mismatched_formats[j];
            pipeline = NULL;
            TEST_ASSERT_EQ(agcCreateGraphicsPipeline(
                device, &desc, &pipeline), AGC_ERROR_VALIDATION_FAILED,
                "integer and non-integer attachment mismatch fails closed");
            TEST_ASSERT(pipeline == NULL,
                "rejected attachment mismatch leaves pipeline output null");
        }

        attachment.format = cases[i].attachment_format;
        if (cases[i].component_class !=
            AGC_SHADER_COMPONENT_FLOAT_OR_NORMALIZED) {
            attachment.blend_enable = 1u;
            pipeline = NULL;
            TEST_ASSERT_EQ(agcCreateGraphicsPipeline(
                device, &desc, &pipeline), AGC_ERROR_VALIDATION_FAILED,
                "integer attachment rejects blending before recording");
            TEST_ASSERT(pipeline == NULL,
                "integer-blend rejection leaves pipeline output null");
            attachment.blend_enable = 0u;
        }
        TEST_ASSERT_EQ(agcDestroyShader(ps), AGC_OK,
            "matrix pixel shader destroys");
    }

    {
        AgcShaderReflection requirements = AGC_SHADER_REFLECTION_INIT;
        AgcGraphicsPipelineDesc desc = AGC_GRAPHICS_PIPELINE_DESC_INIT;
        AgcColorBlendAttachmentState attachments[2] = {
            AGC_COLOR_BLEND_ATTACHMENT_STATE_INIT,
            AGC_COLOR_BLEND_ATTACHMENT_STATE_INIT,
        };
        AgcShader ps;
        AgcGraphicsPipeline pipeline = NULL;

        requirements.color_export_count = 1u;
        requirements.color_exports[0] = (AgcShaderColorExport){
            0u, AGC_SHADER_COLOR_EXPORT_FP16_ABGR,
            AGC_SHADER_COMPONENT_FLOAT_OR_NORMALIZED, 0xfu, 0u};
        ps = create_shader_with_reflection(
            device, kAgcShaderStagePs, &requirements);
        attachments[0].format = AGC_FORMAT_RGBA8_UNORM;
        attachments[1].format = AGC_FORMAT_RGBA8_UNORM;
        desc.vertex_shader = vs;
        desc.pixel_shader = ps;
        desc.color_attachments = attachments;
        TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &desc, &pipeline),
            AGC_ERROR_VALIDATION_FAILED,
            "missing color attachment rejects required shader export");
        desc.color_attachment_count = 2u;
        TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &desc, &pipeline),
            AGC_ERROR_VALIDATION_FAILED,
            "extra color attachment rejects absent shader export");
        desc.color_attachment_count = 1u;
        attachments[0].format = AGC_FORMAT_BC7_UNORM;
        TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &desc, &pipeline),
            AGC_ERROR_VALIDATION_FAILED,
            "unsupported attachment encoding fails closed");
        TEST_ASSERT_EQ(agcDestroyShader(ps), AGC_OK,
            "export-count pixel shader destroys");
    }

    TEST_ASSERT_EQ(agcDestroyShader(vs), AGC_OK,
        "matrix vertex shader destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "graphics compatibility matrix device destroys");
}

static void test_runtime_pipeline_layout_and_stage_validation(void)
{
    AgcDevice device = create_device();
    AgcShaderReflection vs_requirements = AGC_SHADER_REFLECTION_INIT;
    AgcShaderReflection ps_requirements = AGC_SHADER_REFLECTION_INIT;
    AgcGraphicsPipelineDesc graphics_desc = AGC_GRAPHICS_PIPELINE_DESC_INIT;
    AgcShaderVertexInput vertex_input = {
        0u, 0u, 0u, 16u, AGC_SHADER_VERTEX_FORMAT_R32G32B32A32_SFLOAT,
        AGC_SHADER_VERTEX_INPUT_RATE_VERTEX, 0u, 0xfu};
    AgcShader vs;
    AgcShader ps;
    AgcGraphicsPipeline graphics = NULL;

    vs_requirements.stage_output_mask = UINT64_C(1) << 32;
    vs_requirements.vertex_input_count = 1u;
    vs_requirements.vertex_inputs[0] = vertex_input;
    vs_requirements.user_sgpr_count = 1u;
    vs_requirements.user_sgprs[0] = (AgcShaderUserSgpr){
        AGC_SHADER_USER_SGPR_VERTEX_BUFFER_TABLE, 0u,
        AGC_REG_SPI_SHADER_USER_DATA_GS_0, 1u};
    ps_requirements.stage_input_mask = UINT64_C(1) << 33;
    vs = create_shader_with_reflection(
        device, kAgcShaderStageVs, &vs_requirements);
    ps = create_shader_with_reflection(
        device, kAgcShaderStagePs, &ps_requirements);
    graphics_desc.vertex_shader = vs;
    graphics_desc.pixel_shader = ps;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(
        device, &graphics_desc, &graphics), AGC_ERROR_VALIDATION_FAILED,
        "missing producer output rejects stage-linkage mismatch");
    TEST_ASSERT(graphics == NULL,
        "stage-linkage rejection leaves pipeline output null");
    TEST_ASSERT_EQ(agcDestroyShader(ps), AGC_OK,
        "mismatched pixel shader destroys");

    ps_requirements.stage_input_mask = UINT64_C(1) << 32;
    ps = create_shader_with_reflection(
        device, kAgcShaderStagePs, &ps_requirements);
    graphics_desc.pixel_shader = ps;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(
        device, &graphics_desc, &graphics), AGC_ERROR_VALIDATION_FAILED,
        "missing vertex layout rejects reflected vertex input");
    graphics_desc.vertex_inputs = &vertex_input;
    graphics_desc.vertex_input_count = 1u;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(
        device, &graphics_desc, &graphics), AGC_OK,
        "matching linkage and vertex layout create pipeline");
    {
        AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
        AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
        AgcVertexBufferBinding binding = AGC_VERTEX_BUFFER_BINDING_INIT;
        AgcBuffer vertex_buffer = NULL;
        AgcBuffer index_buffer = NULL;
        AgcCommandBuffer command = NULL;

        buffer_desc.size = 256u;
        buffer_desc.usage = AGC_BUFFER_USAGE_VERTEX_BIT;
        TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &vertex_buffer),
            AGC_OK, "reflected vertex buffer creates");
        buffer_desc.usage = AGC_BUFFER_USAGE_INDEX_BIT;
        TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &index_buffer),
            AGC_OK, "reflected index buffer creates");
        TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
            &command), AGC_OK, "reflected graphics command buffer creates");
        TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
            "reflected graphics command buffer begins");
        TEST_ASSERT_EQ(agcCmdBindGraphicsPipeline(command, graphics), AGC_OK,
            "reflected graphics pipeline binds");
        TEST_ASSERT_EQ(agcCmdBindIndexBuffer(command, index_buffer, 0u,
            kAgcIndexSize16), AGC_OK, "reflected index buffer binds");
        TEST_ASSERT_EQ(agcCmdDrawIndexed(command, 3u, 1u, 0u, 0, 0u),
            AGC_ERROR_RESOURCE_NOT_BOUND,
            "missing reflected vertex table rejects draw");
        binding.buffer = vertex_buffer;
        binding.stride = 32u;
        TEST_ASSERT_EQ(agcCmdBindVertexBuffers(command, 1u, &binding),
            AGC_ERROR_VALIDATION_FAILED,
            "reflected vertex stride mismatch fails before retention");
        TEST_ASSERT_EQ(agcDestroyBuffer(vertex_buffer), AGC_OK,
            "failed vertex bind does not retain its buffer");
        buffer_desc.usage = AGC_BUFFER_USAGE_VERTEX_BIT;
        TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &vertex_buffer),
            AGC_OK, "replacement reflected vertex buffer creates");
        binding.buffer = vertex_buffer;
        binding.stride = 16u;
        TEST_ASSERT_EQ(agcCmdBindVertexBuffers(command, 1u, &binding), AGC_OK,
            "matching reflected vertex table binds");
        TEST_ASSERT_EQ(agcCmdDrawIndexed(command, 3u, 1u, 0u, 0, 0u),
            AGC_OK, "fully bound reflected graphics draw records");
        TEST_ASSERT_EQ(agcEndCommandBuffer(command), AGC_OK,
            "reflected graphics command buffer becomes executable");
        TEST_ASSERT_EQ(agcDestroyBuffer(vertex_buffer), AGC_ERROR_BUSY,
            "successful vertex bind retains its buffer");
        TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
            "reflected graphics command buffer resets");
        TEST_ASSERT_EQ(agcDestroyBuffer(vertex_buffer), AGC_OK,
            "command reset releases reflected vertex buffer");
        TEST_ASSERT_EQ(agcDestroyBuffer(index_buffer), AGC_OK,
            "reflected index buffer destroys after reset");
        TEST_ASSERT_EQ(agcDestroyCommandBuffer(command), AGC_OK,
            "reflected graphics command buffer destroys");
    }
    TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(graphics), AGC_OK,
        "matching stage-linkage pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyShader(ps), AGC_OK,
        "matching pixel shader destroys");
    TEST_ASSERT_EQ(agcDestroyShader(vs), AGC_OK,
        "vertex-input shader destroys");

    {
        AgcShaderReflection cs_requirements = AGC_SHADER_REFLECTION_INIT;
        AgcShaderDescriptorMapping mapping = {
            1u, 3u, AGC_SHADER_DESCRIPTOR_STORAGE_BUFFER, 1u, 0u, 16u};
        AgcShaderPushConstantRange push_range = {
            0u, 16u, 4u, 1u << kAgcShaderStageCs};
        AgcComputePipelineDesc compute_desc = AGC_COMPUTE_PIPELINE_DESC_INIT;
        AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
        AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
        AgcDescriptorWrite write = AGC_DESCRIPTOR_WRITE_INIT;
        AgcComputePipeline compute = NULL;
        AgcCommandBuffer command = NULL;
        AgcBuffer storage = NULL;
        uint32_t push_data[4] = {1u, 2u, 3u, 4u};
        AgcShader cs;

        cs_requirements.local_size_x = 64u;
        cs_requirements.local_size_y = 1u;
        cs_requirements.local_size_z = 1u;
        cs_requirements.descriptor_mapping_count = 1u;
        cs_requirements.descriptor_mappings[0] = mapping;
        cs_requirements.push_constant_size = 16u;
        cs_requirements.push_constant_alignment = 4u;
        cs_requirements.push_constant_range_count = 1u;
        cs_requirements.push_constant_ranges[0] = push_range;
        cs_requirements.user_sgpr_count = 2u;
        cs_requirements.user_sgprs[0] = (AgcShaderUserSgpr){
            AGC_SHADER_USER_SGPR_DESCRIPTOR_SET, 1u,
            AGC_REG_COMPUTE_USER_DATA_0, 1u};
        cs_requirements.user_sgprs[1] = (AgcShaderUserSgpr){
            AGC_SHADER_USER_SGPR_PUSH_CONSTANT_POINTER, 0u,
            AGC_REG_COMPUTE_USER_DATA_0 + 1u, 1u};
        cs = create_shader_with_reflection(
            device, kAgcShaderStageCs, &cs_requirements);
        compute_desc.shader = cs;
        compute_desc.local_size_x = 64u;
        compute_desc.descriptor_mapping_count = 1u;
        compute_desc.descriptor_mappings = &mapping;
        compute_desc.push_constant_range_count = 1u;
        compute_desc.push_constant_ranges = &push_range;
        TEST_ASSERT_EQ(agcCreateComputePipeline(device, &compute_desc,
            &compute), AGC_OK,
            "matching descriptor and push layouts create compute pipeline");

        command_desc.queue_type = kAgcQueueCompute;
        TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
            &command), AGC_OK, "layout-negative command buffer creates");
        TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
            "layout-negative command buffer begins");
        TEST_ASSERT_EQ(agcCmdBindComputePipeline(command, compute),
            AGC_OK, "reflected-resource compute pipeline binds");
        TEST_ASSERT_EQ(agcCmdDispatch(command, 1u, 1u, 1u),
            AGC_ERROR_RESOURCE_NOT_BOUND,
            "unbound reflected resources reject dispatch");
        buffer_desc.size = 256u;
        buffer_desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT;
        TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &storage),
            AGC_OK, "reflected storage buffer creates");
        write.set = 1u;
        write.binding = 3u;
        write.type = AGC_SHADER_DESCRIPTOR_UNIFORM_BUFFER;
        write.buffer = storage;
        TEST_ASSERT_EQ(agcCmdBindDescriptors(command, 1u, &write),
            AGC_ERROR_VALIDATION_FAILED,
            "descriptor type mismatch fails before resource retention");
        TEST_ASSERT_EQ(agcDestroyBuffer(storage), AGC_OK,
            "failed descriptor bind does not retain its buffer");
        TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &storage),
            AGC_OK, "replacement reflected storage buffer creates");
        write.type = AGC_SHADER_DESCRIPTOR_STORAGE_BUFFER;
        write.buffer = storage;
        TEST_ASSERT_EQ(agcCmdBindDescriptors(command, 1u, &write), AGC_OK,
            "matching reflected descriptor binds");
        TEST_ASSERT_EQ(agcCmdDispatch(command, 1u, 1u, 1u),
            AGC_ERROR_RESOURCE_NOT_BOUND,
            "missing reflected push constants reject dispatch");
        TEST_ASSERT_EQ(agcCmdPushConstants(command,
            1u << kAgcShaderStageCs, 0u, sizeof(push_data), push_data),
            AGC_OK, "matching reflected push constants bind");
        TEST_ASSERT_EQ(agcCmdDispatch(command, 1u, 1u, 1u), AGC_OK,
            "fully bound reflected compute dispatch records");
        TEST_ASSERT_EQ(agcEndCommandBuffer(command), AGC_OK,
            "reflected compute command buffer becomes executable");
        TEST_ASSERT_EQ(agcDestroyBuffer(storage), AGC_ERROR_BUSY,
            "successful descriptor bind retains its buffer");
        TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
            "reflected compute command buffer resets cleanly");
        TEST_ASSERT_EQ(agcDestroyBuffer(storage), AGC_OK,
            "command reset releases reflected storage buffer");
        TEST_ASSERT_EQ(agcDestroyCommandBuffer(command), AGC_OK,
            "layout-negative command buffer destroys");
        TEST_ASSERT_EQ(agcDestroyComputePipeline(compute), AGC_OK,
            "reflected-resource compute pipeline destroys");

        mapping.byte_stride = 32u;
        compute = NULL;
        TEST_ASSERT_EQ(agcCreateComputePipeline(device, &compute_desc,
            &compute), AGC_ERROR_VALIDATION_FAILED,
            "descriptor stride mismatch rejects compute pipeline");
        TEST_ASSERT(compute == NULL,
            "descriptor mismatch leaves compute output null");
        TEST_ASSERT_EQ(agcDestroyShader(cs), AGC_OK,
            "descriptor-reflected compute shader destroys");

        mapping.byte_stride = 16u;
        cs_requirements.user_sgprs[0].register_offset =
            AGC_REG_SPI_SHADER_USER_DATA_GS_0;
        cs = create_shader_with_reflection(
            device, kAgcShaderStageCs, &cs_requirements);
        compute_desc.shader = cs;
        TEST_ASSERT_EQ(agcCreateComputePipeline(device, &compute_desc,
            &compute), AGC_ERROR_NOT_SUPPORTED,
            "cross-stage user-SGPR register fails pipeline creation");
        TEST_ASSERT(compute == NULL,
            "invalid user-SGPR register leaves compute pipeline null");
        TEST_ASSERT_EQ(agcDestroyShader(cs), AGC_OK,
            "invalid user-SGPR compute shader destroys");
    }
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "pipeline layout validation device destroys");
}

static void test_runtime_indirect_descriptor_set_table(void)
{
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueCompute);
    AgcShaderReflection requirements = AGC_SHADER_REFLECTION_INIT;
    AgcShaderDescriptorMapping mapping = {
        1u, 3u, AGC_SHADER_DESCRIPTOR_STORAGE_BUFFER, 1u, 0u, 16u};
    AgcComputePipelineDesc pipeline_desc = AGC_COMPUTE_PIPELINE_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcDescriptorWrite write = AGC_DESCRIPTOR_WRITE_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcComputePipeline pipeline = NULL;
    AgcCommandBuffer command = NULL;
    AgcBuffer storage = NULL;
    AgcShader shader;
    const AgcCommandBufferSubmit *captured;
    const uint32_t *words;
    uint32_t owner = UINT32_MAX;
    uint32_t table_address = 0u;

    requirements.local_size_x = 64u;
    requirements.local_size_y = 1u;
    requirements.local_size_z = 1u;
    requirements.descriptor_mapping_count = 1u;
    requirements.descriptor_mappings[0] = mapping;
    requirements.user_sgpr_count = 1u;
    requirements.user_sgprs[0] = (AgcShaderUserSgpr){
        AGC_SHADER_USER_SGPR_INDIRECT_DESCRIPTOR_SETS, 0u,
        AGC_REG_COMPUTE_USER_DATA_0, 1u};
    shader = create_shader_with_reflection(
        device, kAgcShaderStageCs, &requirements);
    pipeline_desc.shader = shader;
    pipeline_desc.local_size_x = 64u;
    pipeline_desc.descriptor_mapping_count = 1u;
    pipeline_desc.descriptor_mappings = &mapping;
    TEST_ASSERT_EQ(agcCreateComputePipeline(device, &pipeline_desc,
        &pipeline), AGC_OK,
        "indirect descriptor-set reflection creates compute pipeline");

    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 128u;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &command),
        AGC_OK, "indirect descriptor command buffer creates");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "indirect descriptor command buffer begins");
    TEST_ASSERT_EQ(agcCmdBindComputePipeline(command, pipeline), AGC_OK,
        "indirect descriptor compute pipeline binds");
    TEST_ASSERT_EQ(agcCmdDispatch(command, 1u, 1u, 1u),
        AGC_ERROR_RESOURCE_NOT_BOUND,
        "indirect descriptor dispatch requires its reflected set");
    buffer_desc.size = 256u;
    buffer_desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &storage), AGC_OK,
        "indirect descriptor storage buffer creates");
    write.set = 1u;
    write.binding = 3u;
    write.type = AGC_SHADER_DESCRIPTOR_STORAGE_BUFFER;
    write.buffer = storage;
    TEST_ASSERT_EQ(agcCmdBindDescriptors(command, 1u, &write), AGC_OK,
        "indirect descriptor table receives reflected set");
    TEST_ASSERT_EQ(agcCmdDispatch(command, 1u, 1u, 1u), AGC_OK,
        "indirect descriptor dispatch records after binding");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command), AGC_OK,
        "indirect descriptor command buffer becomes executable");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &command;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, NULL), AGC_OK,
        "indirect descriptor command buffer submits on host");
    captured = agcDriverDebugLastAcbSubmit(&owner);
    TEST_ASSERT(captured != NULL && owner != UINT32_MAX,
        "indirect descriptor submission is captured");
    words = (const uint32_t *)(uintptr_t)captured->command_address;
    TEST_ASSERT(runtime_find_shader_register(words, captured->dword_count,
        AGC_REG_COMPUTE_USER_DATA_0, &table_address),
        "indirect descriptor table pointer is emitted");
    TEST_ASSERT(table_address != 0u && (table_address & 0xffu) == 0u,
        "indirect descriptor table pointer is nonzero and aligned");

    TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
        "indirect descriptor command buffer resets");
    TEST_ASSERT_EQ(agcDestroyBuffer(storage), AGC_OK,
        "indirect descriptor storage buffer destroys after reset");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command), AGC_OK,
        "indirect descriptor command buffer destroys");
    TEST_ASSERT_EQ(agcDestroyComputePipeline(pipeline), AGC_OK,
        "indirect descriptor pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyShader(shader), AGC_OK,
        "indirect descriptor shader destroys");

    requirements.user_sgpr_count = 2u;
    requirements.user_sgprs[1] = (AgcShaderUserSgpr){
        AGC_SHADER_USER_SGPR_DESCRIPTOR_SET, 1u,
        AGC_REG_COMPUTE_USER_DATA_0 + 1u, 1u};
    shader = create_shader_with_reflection(
        device, kAgcShaderStageCs, &requirements);
    pipeline_desc.shader = shader;
    pipeline = NULL;
    TEST_ASSERT_EQ(agcCreateComputePipeline(device, &pipeline_desc,
        &pipeline), AGC_ERROR_VALIDATION_FAILED,
        "mixed direct and indirect descriptor-set ABI fails closed");
    TEST_ASSERT(pipeline == NULL,
        "mixed descriptor ABI leaves pipeline output null");
    TEST_ASSERT_EQ(agcDestroyShader(shader), AGC_OK,
        "mixed descriptor ABI shader destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK,
        "indirect descriptor queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "indirect descriptor device destroys");
}

static void test_runtime_geometry_pipeline_bundle(void)
{
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueGraphics);
    AgcShaderReflection gs_requirements = AGC_SHADER_REFLECTION_INIT;
    AgcShaderReflection ps_requirements = AGC_SHADER_REFLECTION_INIT;
    AgcShaderVertexInput vertex_input = {
        0u, 0u, 0u, 16u, AGC_SHADER_VERTEX_FORMAT_R32G32B32A32_SFLOAT,
        AGC_SHADER_VERTEX_INPUT_RATE_VERTEX, 0u, 0xfu};
    AgcGraphicsPipelineDesc pipeline_desc = AGC_GRAPHICS_PIPELINE_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcVertexBufferBinding vertex_binding = AGC_VERTEX_BUFFER_BINDING_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcGraphicsPipeline pipeline = NULL;
    AgcCommandBuffer command = NULL;
    AgcBuffer vertex_buffer = NULL;
    AgcBuffer index_buffer = NULL;
    AgcShader geometry;
    AgcShader pixel;
    AgcShader redundant_vertex;
    const AgcCommandBufferSubmit *captured;
    const uint32_t *words;
    uint32_t continuation_address = 0u;

    gs_requirements.stage_output_mask = UINT64_C(1) << 32;
    gs_requirements.front_stage_input_mask = UINT64_C(1) << 0;
    gs_requirements.front_stage_output_mask = UINT64_C(1) << 32;
    gs_requirements.vertex_input_count = 1u;
    gs_requirements.vertex_inputs[0] = vertex_input;
    gs_requirements.user_sgpr_count = 1u;
    gs_requirements.user_sgprs[0] = (AgcShaderUserSgpr){
        AGC_SHADER_USER_SGPR_VERTEX_BUFFER_TABLE, 0u,
        AGC_REG_SPI_SHADER_USER_DATA_GS_0, 1u};
    ps_requirements.stage_input_mask = UINT64_C(1) << 32;
    geometry = create_ngg_shader_bundle(
        device, kAgcShaderStageGs, &gs_requirements);
    pixel = create_shader_with_reflection(
        device, kAgcShaderStagePs, &ps_requirements);
    pipeline_desc.geometry_shader = geometry;
    pipeline_desc.pixel_shader = pixel;
    pipeline_desc.vertex_inputs = &vertex_input;
    pipeline_desc.vertex_input_count = 1u;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &pipeline_desc,
        &pipeline), AGC_OK,
        "fused VS-front/GS-back geometry bundle creates pipeline");

    redundant_vertex = create_shader(device, kAgcShaderStageVs);
    pipeline_desc.vertex_shader = redundant_vertex;
    {
        AgcGraphicsPipeline invalid = NULL;
        TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &pipeline_desc,
            &invalid), AGC_ERROR_VALIDATION_FAILED,
            "geometry bundle rejects an unused standalone vertex shader");
        TEST_ASSERT(invalid == NULL,
            "redundant geometry stage leaves pipeline output null");
    }
    pipeline_desc.vertex_shader = NULL;
    TEST_ASSERT_EQ(agcDestroyShader(redundant_vertex), AGC_OK,
        "redundant vertex shader destroys");

    buffer_desc.size = 256u;
    buffer_desc.usage = AGC_BUFFER_USAGE_VERTEX_BIT;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &vertex_buffer),
        AGC_OK, "geometry vertex buffer creates");
    buffer_desc.usage = AGC_BUFFER_USAGE_INDEX_BIT;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &index_buffer),
        AGC_OK, "geometry index buffer creates");
    command_desc.capacity_dwords = 256u;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &command),
        AGC_OK, "geometry command buffer creates");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "geometry command buffer begins");
    TEST_ASSERT_EQ(agcCmdBindGraphicsPipeline(command, pipeline), AGC_OK,
        "geometry pipeline bind records cached state");
    vertex_binding.buffer = vertex_buffer;
    vertex_binding.stride = 16u;
    TEST_ASSERT_EQ(agcCmdBindVertexBuffers(command, 1u, &vertex_binding),
        AGC_OK, "geometry front-stage vertex table binds");
    TEST_ASSERT_EQ(agcCmdBindIndexBuffer(command, index_buffer, 0u,
        kAgcIndexSize16), AGC_OK, "geometry index buffer binds");
    TEST_ASSERT_EQ(agcCmdDrawIndexed(command, 3u, 1u, 0u, 0, 0u),
        AGC_OK, "triangle-input geometry draw records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command), AGC_OK,
        "geometry command buffer becomes executable");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &command;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, NULL), AGC_OK,
        "geometry command buffer submits on host");
    captured = agcDriverDebugLastDcbSubmit();
    TEST_ASSERT(captured != NULL,
        "geometry command submission is captured");
    words = (const uint32_t *)(uintptr_t)captured->command_address;
    TEST_ASSERT(runtime_find_shader_register(words, captured->dword_count,
        AGC_REG_SPI_SHADER_USER_DATA_GS_0 + 15u,
        &continuation_address),
        "geometry bind emits its front-stage continuation address");
    TEST_ASSERT(continuation_address != 0u &&
        continuation_address != OPENAGC_NEXT_STAGE_PC_PLACEHOLDER,
        "geometry continuation placeholder is patched by the pipeline");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
        "geometry command buffer resets");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command), AGC_OK,
        "geometry command buffer destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(index_buffer), AGC_OK,
        "geometry index buffer destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(vertex_buffer), AGC_OK,
        "geometry vertex buffer destroys");
    TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(pipeline), AGC_OK,
        "geometry pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyShader(pixel), AGC_OK,
        "geometry pixel shader destroys");
    TEST_ASSERT_EQ(agcDestroyShader(geometry), AGC_OK,
        "geometry bundle destroys");

    gs_requirements.geometry_input_primitive =
        AGC_SHADER_PRIMITIVE_LINES;
    gs_requirements.geometry_output_primitive =
        AGC_SHADER_PRIMITIVE_LINE_STRIP;
    gs_requirements.geometry_vertices_in = 2u;
    geometry = create_ngg_shader_bundle(
        device, kAgcShaderStageGs, &gs_requirements);
    pixel = create_shader_with_reflection(
        device, kAgcShaderStagePs, &ps_requirements);
    pipeline_desc.geometry_shader = geometry;
    pipeline_desc.pixel_shader = pixel;
    pipeline = NULL;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &pipeline_desc,
        &pipeline), AGC_OK,
        "compiler-reflected line-input geometry creates pipeline");
    buffer_desc.usage = AGC_BUFFER_USAGE_VERTEX_BIT;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &vertex_buffer),
        AGC_OK, "line geometry vertex buffer creates");
    buffer_desc.usage = AGC_BUFFER_USAGE_INDEX_BIT;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &index_buffer),
        AGC_OK, "line geometry index buffer creates");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &command),
        AGC_OK, "line geometry command buffer creates");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "line geometry command buffer begins");
    TEST_ASSERT_EQ(agcCmdBindGraphicsPipeline(command, pipeline), AGC_OK,
        "line geometry pipeline binds");
    vertex_binding.buffer = vertex_buffer;
    TEST_ASSERT_EQ(agcCmdBindVertexBuffers(command, 1u, &vertex_binding),
        AGC_OK, "line geometry vertex table binds");
    TEST_ASSERT_EQ(agcCmdBindIndexBuffer(command, index_buffer, 0u,
        kAgcIndexSize16), AGC_OK, "line geometry index buffer binds");
    TEST_ASSERT_EQ(agcCmdDrawIndexed(command, 1u, 1u, 0u, 0, 0u),
        AGC_ERROR_VALIDATION_FAILED,
        "line geometry rejects an incomplete input primitive");
    TEST_ASSERT_EQ(agcCmdDrawIndexed(command, 2u, 1u, 0u, 0, 0u),
        AGC_OK, "complete line geometry input records");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
        "line geometry command buffer resets");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command), AGC_OK,
        "line geometry command buffer destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(index_buffer), AGC_OK,
        "line geometry index buffer destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(vertex_buffer), AGC_OK,
        "line geometry vertex buffer destroys");
    TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(pipeline), AGC_OK,
        "line geometry pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyShader(pixel), AGC_OK,
        "line-input geometry pixel shader destroys");
    TEST_ASSERT_EQ(agcDestroyShader(geometry), AGC_OK,
        "line-input geometry bundle destroys");

    gs_requirements.geometry_input_primitive =
        AGC_SHADER_PRIMITIVE_TRIANGLES;
    gs_requirements.geometry_output_primitive =
        AGC_SHADER_PRIMITIVE_TRIANGLE_STRIP;
    gs_requirements.geometry_vertices_in = 3u;
    gs_requirements.geometry_invocations = 2u;
    geometry = create_ngg_shader_bundle(
        device, kAgcShaderStageGs, &gs_requirements);
    pixel = create_shader_with_reflection(
        device, kAgcShaderStagePs, &ps_requirements);
    pipeline_desc.geometry_shader = geometry;
    pipeline_desc.pixel_shader = pixel;
    pipeline = NULL;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &pipeline_desc,
        &pipeline), AGC_OK,
        "compiler-reflected multi-invocation geometry creates pipeline");
    TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(pipeline), AGC_OK,
        "multi-invocation geometry pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyShader(pixel), AGC_OK,
        "multi-invocation geometry pixel shader destroys");
    TEST_ASSERT_EQ(agcDestroyShader(geometry), AGC_OK,
        "multi-invocation geometry bundle destroys");

    gs_requirements.scratch_bytes_per_wave = 256u;
    geometry = create_ngg_shader_bundle(
        device, kAgcShaderStageGs, &gs_requirements);
    pixel = create_shader_with_reflection(
        device, kAgcShaderStagePs, &ps_requirements);
    pipeline_desc.geometry_shader = geometry;
    pipeline_desc.pixel_shader = pixel;
    pipeline = NULL;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &pipeline_desc,
        &pipeline), AGC_ERROR_NOT_SUPPORTED,
        "graphics scratch requirement fails before PM4 emission");
    TEST_ASSERT(pipeline == NULL,
        "unsupported graphics scratch leaves pipeline output null");
    TEST_ASSERT_EQ(agcDestroyShader(pixel), AGC_OK,
        "scratch-requiring geometry pixel shader destroys");
    TEST_ASSERT_EQ(agcDestroyShader(geometry), AGC_OK,
        "scratch-requiring geometry bundle destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK,
        "geometry pipeline queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "geometry pipeline device destroys");
}

static void test_runtime_tessellation_pipeline_bundles(void)
{
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueGraphics);
    AgcShaderReflection hs_requirements = AGC_SHADER_REFLECTION_INIT;
    AgcShaderReflection ds_requirements = AGC_SHADER_REFLECTION_INIT;
    AgcShaderReflection gs_requirements = AGC_SHADER_REFLECTION_INIT;
    AgcShaderReflection ps_requirements = AGC_SHADER_REFLECTION_INIT;
    AgcShaderVertexInput vertex_input = {
        0u, 0u, 0u, 16u, AGC_SHADER_VERTEX_FORMAT_R32G32B32A32_SFLOAT,
        AGC_SHADER_VERTEX_INPUT_RATE_VERTEX, 0u, 0xfu};
    AgcShaderPushConstantRange hull_push_range = {
        0u, 4u, 4u, 1u << kAgcShaderStageHs};
    AgcGraphicsPipelineDesc pipeline_desc = AGC_GRAPHICS_PIPELINE_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcVertexBufferBinding vertex_binding = AGC_VERTEX_BUFFER_BINDING_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcMemoryStats before = AGC_MEMORY_STATS_INIT;
    AgcMemoryStats after = AGC_MEMORY_STATS_INIT;
    AgcGraphicsPipeline tess_pipeline = NULL;
    AgcGraphicsPipeline tess_geometry_pipeline = NULL;
    AgcCommandBuffer command = NULL;
    AgcBuffer vertex_buffer = NULL;
    AgcBuffer index_buffer = NULL;
    AgcShader hull;
    AgcShader evaluation;
    AgcShader geometry;
    AgcShader pixel;
    const AgcCommandBufferSubmit *captured;
    const uint32_t *words;
    uint32_t value = 0u;
    const uint32_t push_value = UINT32_C(0x12345678);

    hs_requirements.stage_input_mask = UINT64_C(1) << 32;
    hs_requirements.stage_output_mask = UINT64_C(1) << 33;
    hs_requirements.patch_output_mask = UINT64_C(1) << 1;
    hs_requirements.front_stage_input_mask = UINT64_C(1) << 0;
    hs_requirements.front_stage_output_mask = UINT64_C(1) << 32;
    hs_requirements.vertex_input_count = 1u;
    hs_requirements.vertex_inputs[0] = vertex_input;
    hs_requirements.user_sgpr_count = 2u;
    hs_requirements.user_sgprs[0] = (AgcShaderUserSgpr){
        AGC_SHADER_USER_SGPR_VERTEX_BUFFER_TABLE, 0u,
        AGC_REG_SPI_SHADER_USER_DATA_HS_0 + 4u, 1u};
    hs_requirements.user_sgprs[1] = (AgcShaderUserSgpr){
        AGC_SHADER_USER_SGPR_PUSH_CONSTANT_POINTER, 0u,
        AGC_REG_SPI_SHADER_USER_DATA_HS_0 + 5u, 1u};
    hs_requirements.push_constant_range_count = 1u;
    hs_requirements.push_constant_ranges[0] = hull_push_range;
    hs_requirements.push_constant_size = 4u;
    hs_requirements.push_constant_alignment = 4u;
    hs_requirements.tessellation_patch_count = 8u;
    hs_requirements.tessellation_input_control_points = 3u;
    hs_requirements.tessellation_output_control_points = 3u;
    hs_requirements.tessellation_vertex_output_count = 2u;
    hs_requirements.tessellation_control_output_count = 1u;
    hs_requirements.tessellation_lds_size = 1536u;

    ds_requirements.stage_input_mask = UINT64_C(1) << 33;
    ds_requirements.stage_output_mask = UINT64_C(1) << 34;
    ds_requirements.patch_input_mask = UINT64_C(1) << 1;
    ds_requirements.front_stage = kAgcShaderStageDs;
    ds_requirements.front_stage_input_mask = UINT64_C(1) << 33;
    ds_requirements.front_stage_output_mask = UINT64_C(1) << 34;
    ds_requirements.front_patch_input_mask = UINT64_C(1) << 1;
    ds_requirements.tessellation_patch_count = 8u;
    ds_requirements.tessellation_output_control_points = 3u;
    ds_requirements.tessellation_primitive_mode = 1u;

    gs_requirements.stage_input_mask = UINT64_C(1) << 35;
    gs_requirements.stage_output_mask = UINT64_C(1) << 34;
    gs_requirements.front_stage = kAgcShaderStageDs;
    gs_requirements.front_stage_input_mask = UINT64_C(1) << 33;
    gs_requirements.front_stage_output_mask = UINT64_C(1) << 35;
    gs_requirements.front_patch_input_mask = UINT64_C(1) << 1;
    gs_requirements.tessellation_patch_count = 8u;
    gs_requirements.tessellation_output_control_points = 3u;
    gs_requirements.tessellation_primitive_mode = 1u;
    gs_requirements.geometry_input_primitive =
        AGC_SHADER_PRIMITIVE_TRIANGLES;
    gs_requirements.geometry_output_primitive =
        AGC_SHADER_PRIMITIVE_TRIANGLE_STRIP;
    gs_requirements.geometry_vertices_in = 3u;
    gs_requirements.geometry_vertices_out = 3u;
    gs_requirements.geometry_invocations = 1u;
    ps_requirements.stage_input_mask = UINT64_C(1) << 34;

    hull = create_tessellation_control_bundle(device, &hs_requirements);
    evaluation = create_ngg_shader_bundle(
        device, kAgcShaderStageDs, &ds_requirements);
    geometry = create_ngg_shader_bundle(
        device, kAgcShaderStageGs, &gs_requirements);
    pixel = create_shader_with_reflection(
        device, kAgcShaderStagePs, &ps_requirements);
    pipeline_desc.tessellation_control_shader = hull;
    pipeline_desc.tessellation_evaluation_shader = evaluation;
    pipeline_desc.pixel_shader = pixel;
    pipeline_desc.vertex_inputs = &vertex_input;
    pipeline_desc.vertex_input_count = 1u;
    pipeline_desc.push_constant_ranges = &hull_push_range;
    pipeline_desc.push_constant_range_count = 1u;
    TEST_ASSERT_EQ(agcGetMemoryStats(device, &before), AGC_OK,
        "tessellation storage baseline queries");
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &pipeline_desc,
        &tess_pipeline), AGC_OK,
        "fused VS/TCS plus TES/NGG pipeline creates");
    TEST_ASSERT_EQ(agcGetMemoryStats(device, &after), AGC_OK,
        "tessellation storage allocation queries");
    TEST_ASSERT_EQ(after.live_allocation_count,
        before.live_allocation_count + 3u,
        "first tessellation pipeline creates one device-wide ring set");

    before = (AgcMemoryStats)AGC_MEMORY_STATS_INIT;
    TEST_ASSERT_EQ(agcGetMemoryStats(device, &before), AGC_OK,
        "shared tessellation storage baseline queries");
    pipeline_desc.tessellation_evaluation_shader = NULL;
    pipeline_desc.geometry_shader = geometry;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &pipeline_desc,
        &tess_geometry_pipeline), AGC_OK,
        "fused VS/TCS plus TES/geometry pipeline creates");
    after = (AgcMemoryStats)AGC_MEMORY_STATS_INIT;
    TEST_ASSERT_EQ(agcGetMemoryStats(device, &after), AGC_OK,
        "shared tessellation storage result queries");
    TEST_ASSERT_EQ(after.live_allocation_count, before.live_allocation_count,
        "additional tessellation pipelines reuse the device ring set");

    pipeline_desc.tessellation_evaluation_shader = evaluation;
    {
        AgcGraphicsPipeline invalid = NULL;
        TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &pipeline_desc,
            &invalid), AGC_ERROR_VALIDATION_FAILED,
            "combined tessellation geometry rejects redundant TES handle");
        TEST_ASSERT(invalid == NULL,
            "redundant tessellation stage leaves pipeline output null");
    }
    pipeline_desc.tessellation_evaluation_shader = NULL;
    {
        AgcShaderReflection invocation_requirements = gs_requirements;
        AgcShader invocation_geometry;
        AgcGraphicsPipeline invocation_pipeline = NULL;
        invocation_requirements.geometry_invocations = 2u;
        invocation_geometry = create_ngg_shader_bundle(
            device, kAgcShaderStageGs, &invocation_requirements);
        pipeline_desc.geometry_shader = invocation_geometry;
        TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &pipeline_desc,
            &invocation_pipeline), AGC_OK,
            "tessellated multi-invocation geometry creates pipeline");
        TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(invocation_pipeline),
            AGC_OK,
            "tessellated multi-invocation geometry pipeline destroys");
        TEST_ASSERT_EQ(agcDestroyShader(invocation_geometry), AGC_OK,
            "tessellated multi-invocation geometry bundle destroys");
    }
    pipeline_desc.geometry_shader = NULL;
    {
        AgcShaderReflection mismatch_requirements = ds_requirements;
        AgcShader mismatch;
        AgcGraphicsPipeline invalid = NULL;
        mismatch_requirements.tessellation_output_control_points = 4u;
        mismatch = create_ngg_shader_bundle(
            device, kAgcShaderStageDs, &mismatch_requirements);
        pipeline_desc.tessellation_evaluation_shader = mismatch;
        TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &pipeline_desc,
            &invalid), AGC_ERROR_NOT_SUPPORTED,
            "TCS and TES control-point mismatch fails before PM4");
        TEST_ASSERT(invalid == NULL,
            "mismatched tessellation metadata leaves pipeline output null");
        TEST_ASSERT_EQ(agcDestroyShader(mismatch), AGC_OK,
            "mismatched tessellation shader destroys");
        pipeline_desc.tessellation_evaluation_shader = NULL;
    }

    buffer_desc.size = 256u;
    buffer_desc.usage = AGC_BUFFER_USAGE_VERTEX_BIT;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &vertex_buffer),
        AGC_OK, "tessellation vertex buffer creates");
    buffer_desc.usage = AGC_BUFFER_USAGE_INDEX_BIT;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &index_buffer),
        AGC_OK, "tessellation index buffer creates");
    command_desc.capacity_dwords = 512u;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &command),
        AGC_OK, "tessellation command buffer creates");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "tessellation command buffer begins");
    TEST_ASSERT_EQ(agcCmdBindGraphicsPipeline(command, tess_pipeline), AGC_OK,
        "tessellation pipeline binds its cached ring and shader state");
    vertex_binding.buffer = vertex_buffer;
    vertex_binding.stride = 16u;
    TEST_ASSERT_EQ(agcCmdBindVertexBuffers(command, 1u, &vertex_binding),
        AGC_OK, "tessellation VS-front vertex table binds");
    TEST_ASSERT_EQ(agcCmdPushConstants(command, 1u << kAgcShaderStageHs,
        0u, sizeof(push_value), &push_value), AGC_OK,
        "tessellation-control push constants bind");
    TEST_ASSERT_EQ(agcCmdBindIndexBuffer(command, index_buffer, 0u,
        kAgcIndexSize16), AGC_OK, "tessellation index buffer binds");
    TEST_ASSERT_EQ(agcCmdDrawIndexed(command, 4u, 1u, 0u, 0, 0u),
        AGC_ERROR_VALIDATION_FAILED,
        "draw rejects an incomplete three-control-point patch");
    TEST_ASSERT_EQ(agcCmdDrawIndexed(command, 3u, 1u, 0u, 0, 0u),
        AGC_OK, "complete tessellation patch draw records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command), AGC_OK,
        "tessellation command buffer becomes executable");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &command;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, NULL), AGC_OK,
        "tessellation command buffer submits on host");
    captured = agcDriverDebugLastDcbSubmit();
    TEST_ASSERT(captured != NULL,
        "tessellation command submission is captured");
    words = (const uint32_t *)(uintptr_t)captured->command_address;
    TEST_ASSERT(runtime_find_shader_register(words, captured->dword_count,
        AGC_REG_SPI_SHADER_USER_DATA_ADDR_LO_HS, &value) &&
        value != OPENAGC_RING_OFFSETS_LO_PLACEHOLDER,
        "TCS receives the runtime-owned ring table address");
    TEST_ASSERT(runtime_find_shader_register(words, captured->dword_count,
        AGC_REG_SPI_SHADER_USER_DATA_HS_0 + 5u, &value) && value != 0u,
        "TCS receives its runtime-owned push-constant address");
    TEST_ASSERT(runtime_find_shader_register(words, captured->dword_count,
        AGC_REG_SPI_SHADER_USER_DATA_HS_0 + 11u, &value),
        "TCS receives its compiler-derived offchip layout");
    TEST_ASSERT_EQ(value, 0x20842108u,
        "TCS offchip layout matches reflected patch facts");
    TEST_ASSERT(runtime_find_shader_register(words, captured->dword_count,
        AGC_REG_SPI_SHADER_USER_DATA_GS_0 + 11u, &value),
        "TES receives its compiler-derived offchip layout");
    TEST_ASSERT_EQ(value, 0x20842108u,
        "TES offchip layout matches reflected patch facts");
    TEST_ASSERT(runtime_find_uconfig_register(words, captured->dword_count,
        AGC_REG_VGT_TF_RING_SIZE, &value),
        "tessellation bind emits factor-ring size state");
    TEST_ASSERT_EQ(value, AGC_GFX1013_TESS_FACTOR_RING_SIZE / 4u,
        "factor-ring register uses the device-owned allocation size");

    TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
        "tessellation command buffer resets for combined geometry");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "combined tessellation geometry command buffer begins");
    TEST_ASSERT_EQ(agcCmdBindGraphicsPipeline(
        command, tess_geometry_pipeline), AGC_OK,
        "combined tessellation geometry pipeline binds");
    TEST_ASSERT_EQ(agcCmdBindVertexBuffers(command, 1u, &vertex_binding),
        AGC_OK, "combined tessellation VS-front vertex table binds");
    TEST_ASSERT_EQ(agcCmdPushConstants(command, 1u << kAgcShaderStageHs,
        0u, sizeof(push_value), &push_value), AGC_OK,
        "combined tessellation-control push constants bind");
    TEST_ASSERT_EQ(agcCmdBindIndexBuffer(command, index_buffer, 0u,
        kAgcIndexSize16), AGC_OK,
        "combined tessellation geometry index buffer binds");
    TEST_ASSERT_EQ(agcCmdDrawIndexed(command, 3u, 1u, 0u, 0, 0u),
        AGC_OK, "combined tessellation geometry draw records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command), AGC_OK,
        "combined tessellation geometry command buffer becomes executable");
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, NULL), AGC_OK,
        "combined tessellation geometry submits on host");
    captured = agcDriverDebugLastDcbSubmit();
    words = (const uint32_t *)(uintptr_t)captured->command_address;
    TEST_ASSERT(runtime_find_shader_register(words, captured->dword_count,
        AGC_REG_SPI_SHADER_USER_DATA_GS_0 + 15u, &value),
        "combined tessellation geometry emits its TES continuation");
    TEST_ASSERT(value != 0u &&
        value != OPENAGC_NEXT_STAGE_PC_PLACEHOLDER,
        "combined tessellation geometry continuation is patched");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
        "combined tessellation geometry command buffer resets");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command), AGC_OK,
        "tessellation command buffer destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(index_buffer), AGC_OK,
        "tessellation index buffer destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(vertex_buffer), AGC_OK,
        "tessellation vertex buffer destroys");
    TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(tess_geometry_pipeline),
        AGC_OK, "tessellation geometry pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(tess_pipeline), AGC_OK,
        "tessellation pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyShader(pixel), AGC_OK,
        "tessellation pixel shader destroys");
    TEST_ASSERT_EQ(agcDestroyShader(geometry), AGC_OK,
        "tessellation geometry bundle destroys");
    TEST_ASSERT_EQ(agcDestroyShader(evaluation), AGC_OK,
        "tessellation evaluation bundle destroys");
    TEST_ASSERT_EQ(agcDestroyShader(hull), AGC_OK,
        "tessellation control bundle destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK,
        "tessellation queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "tessellation device releases its ring storage");
}

static void test_runtime_ps5_image_layouts(void)
{
    AgcDevice device = create_device();
    AgcImageDesc desc = AGC_IMAGE_DESC_INIT;
    AgcImageLayout layout = AGC_IMAGE_LAYOUT_INIT;
    AgcImageSubresourceLayout subresource = AGC_IMAGE_SUBRESOURCE_LAYOUT_INIT;
    AgcGfx1013LinearBcSurfaceLayoutInput bc_input;
    AgcGfx1013LinearBcSurfaceLayout bc_layout;
    AgcGfx1013LinearBcSubresourceLayout bc_subresource;

    desc.width = 17u;
    desc.height = 9u;
    desc.mip_levels = 5u;
    desc.array_layers = 6u;
    desc.format = AGC_FORMAT_BC7_UNORM;
    desc.usage = AGC_IMAGE_USAGE_SAMPLED_BIT |
        AGC_IMAGE_USAGE_CUBE_COMPATIBLE_BIT;
    TEST_ASSERT_EQ(agcGetImageLayout(device, &desc, &layout), AGC_OK,
        "BC cube-array layout succeeds");
    bc_input = (AgcGfx1013LinearBcSurfaceLayoutInput){
        desc.width, desc.height, desc.array_layers, desc.mip_levels,
        desc.format
    };
    TEST_ASSERT_EQ(agcGfx1013GetLinearBcSurfaceLayout(&bc_input, &bc_layout),
        AGC_OK, "qualified BC reference layout succeeds");
    TEST_ASSERT_EQ(layout.allocation_size, bc_layout.allocation_size,
        "runtime BC size matches qualified gfx1013 layout");
    TEST_ASSERT_EQ(layout.block_width, 4u, "BC layout exposes block width");
    TEST_ASSERT_EQ(layout.bytes_per_block, 16u,
        "BC7 layout exposes 16-byte blocks");
    TEST_ASSERT_EQ(layout.plane_count, 1u, "BC layout has one plane");
    TEST_ASSERT_EQ(layout.subresource_count, 30u,
        "cube faces participate in subresource count");
    TEST_ASSERT_EQ(layout.alignment, 256u,
        "linear BC layout retains qualified 256-byte alignment");
    TEST_ASSERT_EQ(agcGetImageSubresourceLayout(device, &desc, 4u, 5u, 0u,
        &subresource), AGC_OK, "last BC cube subresource is queryable");
    TEST_ASSERT_EQ(agcGfx1013GetLinearBcSubresourceLayout(&bc_input, 4u, 5u,
        &bc_subresource), AGC_OK, "qualified BC subresource layout succeeds");
    TEST_ASSERT_EQ(subresource.offset, bc_subresource.offset,
        "runtime BC subresource offset matches qualified layout");
    TEST_ASSERT_EQ(subresource.size, bc_subresource.size,
        "runtime BC subresource size matches qualified layout");
    TEST_ASSERT_EQ(subresource.width, 2u,
        "last BC mip uses qualified ceil-divide dimension");
    TEST_ASSERT_EQ(subresource.row_pitch, 256u,
        "last BC7 mip retains qualified transfer-row alignment");

    desc = (AgcImageDesc)AGC_IMAGE_DESC_INIT;
    layout = (AgcImageLayout)AGC_IMAGE_LAYOUT_INIT;
    desc.width = 1920u;
    desc.height = 1080u;
    desc.format = AGC_FORMAT_D32_FLOAT_S8_UINT;
    desc.usage = AGC_IMAGE_USAGE_DEPTH_STENCIL_BIT | AGC_IMAGE_USAGE_HTILE_BIT;
    TEST_ASSERT_EQ(agcGetImageLayout(device, &desc, &layout), AGC_OK,
        "depth-stencil layout with HTILE succeeds");
    TEST_ASSERT_EQ(layout.plane_count, 3u,
        "depth, stencil, and HTILE are separate planes");
    TEST_ASSERT(layout.metadata_size != 0u, "HTILE metadata has storage");
    {
        AgcGfx1013DepthSurfaceLayoutInput depth_input = {
            desc.width, desc.height, desc.array_layers, desc.mip_levels,
            desc.sample_count, AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT_S8_UINT,
            AGC_GFX1013_SWIZZLE_64KB_Z_X,
            AGC_GFX1013_SWIZZLE_64KB_Z_X
        };
        AgcGfx1013DepthSurfaceLayout depth_layout;
        AgcGfx1013HtileLayoutInput htile_input;
        AgcGfx1013HtileLayout htile_layout;
        AgcGfx1013HtileSubresourceLayout htile_subresource;
        TEST_ASSERT_EQ(agcGfx1013GetDepthSurfaceLayout(&depth_input,
            &depth_layout), AGC_OK, "qualified depth reference layout succeeds");
        htile_input = (AgcGfx1013HtileLayoutInput){
            desc.width, desc.height, desc.array_layers, desc.mip_levels,
            depth_layout.depth.first_mip_in_tail, 8u,
            AGC_GFX1013_SWIZZLE_64KB_Z_X
        };
        TEST_ASSERT_EQ(agcGfx1013GetHtileLayout(&htile_input, &htile_layout),
            AGC_OK, "qualified HTILE reference layout succeeds");
        TEST_ASSERT_EQ(layout.metadata_size, htile_layout.allocation_size,
            "runtime HTILE size matches qualified gfx1013 layout");
        subresource = (AgcImageSubresourceLayout)
            AGC_IMAGE_SUBRESOURCE_LAYOUT_INIT;
        TEST_ASSERT_EQ(agcGetImageSubresourceLayout(device, &desc, 0u, 0u,
            2u, &subresource), AGC_OK,
            "runtime HTILE subresource query succeeds");
        TEST_ASSERT_EQ(agcGfx1013GetHtileSubresourceLayout(&htile_input, 0u,
            0u, &htile_subresource), AGC_OK,
            "qualified HTILE subresource query succeeds");
        TEST_ASSERT_EQ(subresource.offset, layout.metadata_offset +
            htile_subresource.offset,
            "runtime HTILE offset matches qualified gfx1013 layout");
        TEST_ASSERT_EQ(subresource.size, htile_subresource.size,
            "runtime HTILE size matches qualified subresource layout");
    }

    desc = (AgcImageDesc)AGC_IMAGE_DESC_INIT;
    layout = (AgcImageLayout)AGC_IMAGE_LAYOUT_INIT;
    desc.width = 1280u;
    desc.height = 720u;
    desc.format = AGC_FORMAT_RGBA8_UNORM;
    desc.sample_count = 4u;
    desc.usage = AGC_IMAGE_USAGE_COLOR_TARGET_BIT;
    TEST_ASSERT_EQ(agcGetImageLayout(device, &desc, &layout), AGC_OK,
        "runtime 4x color layout succeeds");
    {
        AgcGfx1013ColorSurfaceLayoutInput color_input = {
            desc.width, desc.height, desc.array_layers, desc.mip_levels,
            desc.sample_count, AGC_GFX1013_RT_FORMAT_RGBA8_UNORM,
            AGC_GFX1013_SWIZZLE_64KB_R_X
        };
        AgcGfx1013ColorSurfaceLayout color_layout;
        TEST_ASSERT_EQ(agcGfx1013GetColorSurfaceLayout(&color_input,
            &color_layout), AGC_OK, "qualified 4x color layout succeeds");
        TEST_ASSERT_EQ(layout.allocation_size, color_layout.allocation_size,
            "runtime 4x color size matches qualified gfx1013 layout");
        TEST_ASSERT_EQ(layout.alignment, color_layout.alignment,
            "runtime 4x color alignment matches qualified layout");
    }

    desc = (AgcImageDesc)AGC_IMAGE_DESC_INIT;
    layout = (AgcImageLayout)AGC_IMAGE_LAYOUT_INIT;
    subresource = (AgcImageSubresourceLayout)
        AGC_IMAGE_SUBRESOURCE_LAYOUT_INIT;
    desc.width = 32u;
    desc.height = 16u;
    desc.depth = 8u;
    desc.mip_levels = 6u;
    desc.format = AGC_FORMAT_RGBA8_UNORM;
    desc.usage = AGC_IMAGE_USAGE_STORAGE_BIT;
    TEST_ASSERT_EQ(agcGetImageLayout(device, &desc, &layout), AGC_OK,
        "3D mip-chain layout succeeds");
    TEST_ASSERT_EQ(agcGetImageSubresourceLayout(device, &desc, 2u, 0u, 0u,
        &subresource), AGC_OK, "3D mip subresource query succeeds");
    TEST_ASSERT_EQ(subresource.depth, 2u,
        "3D mip subresource reduces depth dimension");
    TEST_ASSERT_EQ(subresource.size, subresource.slice_pitch * 2u,
        "3D mip size includes every depth slice");
    TEST_ASSERT_EQ(layout.allocation_size & 0xffffu, 0u,
        "linear 3D aggregate retains 64-KiB allocation padding");

    desc = (AgcImageDesc)AGC_IMAGE_DESC_INIT;
    layout = (AgcImageLayout)AGC_IMAGE_LAYOUT_INIT;
    desc.width = UINT32_MAX;
    desc.height = UINT32_MAX;
    desc.depth = UINT32_MAX;
    desc.format = AGC_FORMAT_RGBA8_UNORM;
    desc.usage = AGC_IMAGE_USAGE_SAMPLED_BIT;
    TEST_ASSERT_EQ(agcGetImageLayout(device, &desc, &layout),
        AGC_ERROR_INVALID_ARGUMENT, "overflowing 3D layout fails closed");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "layout test device destruction succeeds");
}

static void test_runtime_all_backing_categories(void)
{
    AgcDevice device = create_device();
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
    AgcImageViewDesc view_desc = AGC_IMAGE_VIEW_DESC_INIT;
    AgcSamplerDesc sampler_desc = AGC_SAMPLER_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcImage image = NULL;
    AgcImage color_target = NULL;
    AgcImage depth_target = NULL;
    AgcImageView view = NULL;
    AgcSampler sampler = NULL;
    AgcShader shader = create_shader(device, kAgcShaderStageCs);
    AgcCommandBuffer command_buffer = NULL;
    AgcAllocationInfo info = AGC_ALLOCATION_INFO_INIT;

    image_desc.width = 64u;
    image_desc.height = 64u;
    image_desc.format = AGC_FORMAT_RGBA8_UNORM;
    image_desc.usage = AGC_IMAGE_USAGE_SAMPLED_BIT;
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &image), AGC_OK,
        "backing-category image creation succeeds");
    image_desc.width = 1280u;
    image_desc.height = 720u;
    image_desc.sample_count = 4u;
    image_desc.usage = AGC_IMAGE_USAGE_COLOR_TARGET_BIT;
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &color_target), AGC_OK,
        "render-target backing creation succeeds");
    image_desc.width = 1024u;
    image_desc.height = 768u;
    image_desc.sample_count = 1u;
    image_desc.format = AGC_FORMAT_D32_FLOAT;
    image_desc.usage = AGC_IMAGE_USAGE_DEPTH_STENCIL_BIT |
        AGC_IMAGE_USAGE_HTILE_BIT;
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &depth_target), AGC_OK,
        "depth/HTILE backing creation succeeds");
    view_desc.image = image;
    view_desc.format = AGC_FORMAT_RGBA8_UNORM;
    TEST_ASSERT_EQ(agcCreateImageView(device, &view_desc, &view), AGC_OK,
        "GPU-visible image descriptor storage creation succeeds");
    TEST_ASSERT_EQ(agcCreateSampler(device, &sampler_desc, &sampler), AGC_OK,
        "GPU-visible sampler descriptor storage creation succeeds");
    command_desc.capacity_dwords = 64u;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
        &command_buffer), AGC_OK, "command storage creation succeeds");

    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device,
        AGC_OBJECT_TYPE_IMAGE_VIEW, view, &info), AGC_OK,
        "image-view descriptor allocation is queryable");
    TEST_ASSERT_EQ(info.heap, AGC_MEMORY_HEAP_FLEXIBLE,
        "image descriptor storage uses flexible memory");
    TEST_ASSERT_EQ(info.requested_size, sizeof(AgcGfx1013ImageDescriptor),
        "image descriptor storage has one hardware descriptor slot");
    TEST_ASSERT_EQ(info.gpu_address & 31u, 0u,
        "image descriptor storage retains 32-byte alignment");
    info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_SAMPLER,
        sampler, &info), AGC_OK, "sampler descriptor allocation is queryable");
    TEST_ASSERT_EQ(info.requested_size, sizeof(AgcSamplerDescriptor),
        "sampler descriptor storage has one hardware descriptor slot");
    TEST_ASSERT_EQ(info.gpu_address & 15u, 0u,
        "sampler descriptor storage retains 16-byte alignment");
    info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_SHADER,
        shader, &info), AGC_OK, "shader-code allocation is queryable");
    TEST_ASSERT_EQ(info.heap, AGC_MEMORY_HEAP_FLEXIBLE,
        "shader code uses CPU-visible flexible memory");
    info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device,
        AGC_OBJECT_TYPE_COMMAND_BUFFER, command_buffer, &info), AGC_OK,
        "command-storage allocation is queryable");
    TEST_ASSERT_EQ(info.requested_size, command_desc.capacity_dwords * 4u,
        "command allocation matches requested dword capacity");
    TEST_ASSERT_EQ(info.gpu_address & 255u, 0u,
        "command storage retains 256-byte alignment");
    info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_IMAGE,
        color_target, &info), AGC_OK,
        "render-target allocation is queryable");
    TEST_ASSERT_EQ(info.heap, AGC_MEMORY_HEAP_GARLIC,
        "render target uses garlic memory");
    TEST_ASSERT_EQ(info.gpu_address & 0xffffu, 0u,
        "4x render target retains qualified 64-KiB alignment");
    info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_IMAGE,
        depth_target, &info), AGC_OK,
        "depth/HTILE allocation is queryable");
    TEST_ASSERT_EQ(info.heap, AGC_MEMORY_HEAP_GARLIC,
        "depth/HTILE target uses garlic memory");

    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command_buffer), AGC_OK,
        "backing-category command buffer destroys");
    TEST_ASSERT_EQ(agcDestroyShader(shader), AGC_OK,
        "backing-category shader destroys");
    TEST_ASSERT_EQ(agcDestroySampler(sampler), AGC_OK,
        "backing-category sampler destroys");
    TEST_ASSERT_EQ(agcDestroyImageView(view), AGC_OK,
        "backing-category view destroys");
    TEST_ASSERT_EQ(agcDestroyImage(depth_target), AGC_OK,
        "backing-category depth target destroys");
    TEST_ASSERT_EQ(agcDestroyImage(color_target), AGC_OK,
        "backing-category color target destroys");
    TEST_ASSERT_EQ(agcDestroyImage(image), AGC_OK,
        "backing-category image destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "backing-category device destroys");
}

static void test_runtime_heap_staging_and_stats(void)
{
    enum { BUFFER_COUNT = 128 };
    AgcDevice device = create_device();
    AgcBuffer buffers[BUFFER_COUNT] = {0};
    AgcImage images[16] = {0};
    AgcBufferDesc desc = AGC_BUFFER_DESC_INIT;
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
    AgcMemoryStats stats = AGC_MEMORY_STATS_INIT;
    AgcAllocationInfo info = AGC_ALLOCATION_INFO_INIT;
    uint32_t upload_data[4] = {1u, 2u, 3u, 4u};
    uint32_t read_data[4] = {0u};
    uint64_t first_buffer_offset = 0u;
    uint64_t reload_buffer_offset = 0u;
    uint64_t reload_image_offset = 0u;
    uint32_t i;
    uint32_t cycle;

    desc.size = 4096u;
    desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT;
    for (i = 0u; i < BUFFER_COUNT; ++i) {
        TEST_ASSERT_EQ(agcCreateBuffer(device, &desc, &buffers[i]), AGC_OK,
            "pooled garlic buffer creation succeeds");
    }
    image_desc.width = 64u;
    image_desc.height = 64u;
    image_desc.format = AGC_FORMAT_RGBA8_UNORM;
    image_desc.usage = AGC_IMAGE_USAGE_SAMPLED_BIT |
        AGC_IMAGE_USAGE_TRANSFER_DST_BIT;
    for (i = 0u; i < 16u; ++i) {
        TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &images[i]), AGC_OK,
            "pooled garlic image creation succeeds");
    }
    TEST_ASSERT_EQ(agcGetMemoryStats(device, &stats), AGC_OK,
        "memory statistics query succeeds");
    TEST_ASSERT_EQ(stats.live_allocation_count, BUFFER_COUNT + 16u,
        "statistics count live resource allocations");
    TEST_ASSERT_EQ(stats.block_count[AGC_MEMORY_HEAP_GARLIC], 1u,
        "many small buffers share one garlic block");
    TEST_ASSERT_EQ(agcSetObjectDebugName(device, AGC_OBJECT_TYPE_BUFFER,
        buffers[0], "streaming-vertices"), AGC_OK,
        "allocation debug name is accepted");
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_BUFFER,
        buffers[0], &info), AGC_OK, "buffer allocation info is queryable");
    TEST_ASSERT_EQ(info.heap, AGC_MEMORY_HEAP_GARLIC,
        "default GPU buffer resides in garlic");
    TEST_ASSERT(info.owner == buffers[0],
        "allocation info identifies its exact resource owner");
    first_buffer_offset = info.heap_offset;
    TEST_ASSERT(strcmp(info.debug_name, "streaming-vertices") == 0,
        "allocation info preserves debug name");
    for (i = 0u; i < BUFFER_COUNT; ++i)
        TEST_ASSERT_EQ(agcDestroyBuffer(buffers[i]), AGC_OK,
            "pooled garlic buffer destruction succeeds");
    for (i = 0u; i < 16u; ++i)
        TEST_ASSERT_EQ(agcDestroyImage(images[i]), AGC_OK,
            "pooled garlic image destruction succeeds");

    TEST_ASSERT_EQ(agcCreateBuffer(device, &desc, &buffers[0]), AGC_OK,
        "freed pooled range accepts a replacement buffer");
    info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_BUFFER,
        buffers[0], &info), AGC_OK, "replacement allocation info succeeds");
    TEST_ASSERT_EQ(info.heap_offset, first_buffer_offset,
        "replacement buffer reuses the first freed aligned range");
    TEST_ASSERT_EQ(agcDestroyBuffer(buffers[0]), AGC_OK,
        "replacement pooled buffer destroys");

    for (cycle = 0u; cycle < 8u; ++cycle) {
        for (i = 0u; i < 32u; ++i) {
            TEST_ASSERT_EQ(agcCreateBuffer(device, &desc, &buffers[i]), AGC_OK,
                "reload-set GPU buffer creation succeeds");
        }
        for (i = 0u; i < 8u; ++i) {
            TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &images[i]),
                AGC_OK, "reload-set GPU image creation succeeds");
        }
        desc.flags = AGC_BUFFER_CREATE_UPLOAD_BIT;
        TEST_ASSERT_EQ(agcCreateBuffer(device, &desc, &buffers[32]), AGC_OK,
            "reload-set upload staging creation succeeds");
        upload_data[0] = cycle;
        TEST_ASSERT_EQ(agcWriteBuffer(buffers[32], 0u, upload_data,
            sizeof(upload_data)), AGC_OK,
            "reload-set asset staging write succeeds");
        info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
        TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device,
            AGC_OBJECT_TYPE_BUFFER, buffers[0], &info), AGC_OK,
            "reload-set first buffer allocation is queryable");
        if (cycle == 0u)
            reload_buffer_offset = info.heap_offset;
        else
            TEST_ASSERT_EQ(info.heap_offset, reload_buffer_offset,
                "reloaded buffer asset reuses its prior range");
        info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
        TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device,
            AGC_OBJECT_TYPE_IMAGE, images[0], &info), AGC_OK,
            "reload-set first image allocation is queryable");
        if (cycle == 0u)
            reload_image_offset = info.heap_offset;
        else
            TEST_ASSERT_EQ(info.heap_offset, reload_image_offset,
                "reloaded image asset reuses its prior range");
        stats = (AgcMemoryStats)AGC_MEMORY_STATS_INIT;
        TEST_ASSERT_EQ(agcGetMemoryStats(device, &stats), AGC_OK,
            "reload-set live statistics query succeeds");
        TEST_ASSERT_EQ(stats.live_allocation_count, 41u,
            "reload set tracks all buffers, images, and staging");
        TEST_ASSERT_EQ(stats.block_count[AGC_MEMORY_HEAP_GARLIC], 1u,
            "reload set remains inside one direct-memory block");
        TEST_ASSERT_EQ(stats.block_count[AGC_MEMORY_HEAP_FLEXIBLE], 1u,
            "reload staging remains inside one flexible-memory block");
        TEST_ASSERT_EQ(agcDestroyBuffer(buffers[32]), AGC_OK,
            "reload-set upload staging destroys");
        desc.flags = 0u;
        for (i = 0u; i < 8u; ++i)
            TEST_ASSERT_EQ(agcDestroyImage(images[i]), AGC_OK,
                "reload-set GPU image destruction succeeds");
        for (i = 0u; i < 32u; ++i)
            TEST_ASSERT_EQ(agcDestroyBuffer(buffers[i]), AGC_OK,
                "reload-set GPU buffer destruction succeeds");
        stats = (AgcMemoryStats)AGC_MEMORY_STATS_INIT;
        TEST_ASSERT_EQ(agcGetMemoryStats(device, &stats), AGC_OK,
            "post-reload statistics query succeeds");
        TEST_ASSERT_EQ(stats.live_allocation_count, 0u,
            "each asset reload returns live allocations to baseline");
    }

    desc.flags = AGC_BUFFER_CREATE_DEDICATED_BIT;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &desc, &buffers[0]), AGC_OK,
        "explicit dedicated buffer creation succeeds");
    info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_BUFFER,
        buffers[0], &info), AGC_OK, "dedicated allocation info succeeds");
    TEST_ASSERT_EQ(info.dedicated, 1u,
        "explicit dedicated buffer owns its block");
    TEST_ASSERT_EQ(agcDestroyBuffer(buffers[0]), AGC_OK,
        "dedicated block is released with its buffer");

    desc.size = UINT64_C(0x01100000);
    desc.flags = 0u;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &desc, &buffers[0]), AGC_OK,
        "oversized buffer creation succeeds");
    info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_BUFFER,
        buffers[0], &info), AGC_OK, "oversized allocation info succeeds");
    TEST_ASSERT_EQ(info.dedicated, 1u,
        "oversized buffer automatically receives a dedicated block");
    TEST_ASSERT_EQ(agcDestroyBuffer(buffers[0]), AGC_OK,
        "oversized dedicated buffer destroys");

    {
        AgcImageDesc scanout_desc = AGC_IMAGE_DESC_INIT;
        scanout_desc.width = 1920u;
        scanout_desc.height = 1080u;
        scanout_desc.format = AGC_FORMAT_RGBA8_UNORM;
        scanout_desc.usage = AGC_IMAGE_USAGE_SCANOUT_BIT;
        TEST_ASSERT_EQ(agcCreateImage(device, &scanout_desc, &images[0]),
            AGC_OK, "scanout-capable image creation succeeds");
        info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
        TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device,
            AGC_OBJECT_TYPE_IMAGE, images[0], &info), AGC_OK,
            "scanout allocation info succeeds");
        TEST_ASSERT_EQ(info.dedicated, 1u,
            "scanout-capable image receives a dedicated block");
        TEST_ASSERT_EQ(agcDestroyImage(images[0]), AGC_OK,
            "scanout dedicated image destroys");
    }

    desc.size = 4096u;
    desc.flags = AGC_BUFFER_CREATE_UPLOAD_BIT;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &desc, &buffers[0]), AGC_OK,
        "persistently mapped upload buffer creation succeeds");
    TEST_ASSERT_EQ(agcWriteBuffer(buffers[0], 8u, upload_data,
        sizeof(upload_data)), AGC_OK, "bounded upload flush succeeds");
    info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_BUFFER,
        buffers[0], &info), AGC_OK, "upload allocation info succeeds");
    TEST_ASSERT_EQ(info.heap, AGC_MEMORY_HEAP_FLEXIBLE,
        "upload buffer resides in flexible memory");
    TEST_ASSERT(memcmp((uint8_t *)info.cpu_address + 8u, upload_data,
        sizeof(upload_data)) == 0, "upload writes persistent CPU mapping");
    TEST_ASSERT_EQ(agcWriteBuffer(buffers[0], desc.size - 8u, upload_data,
        sizeof(upload_data)), AGC_ERROR_INVALID_ARGUMENT,
        "upload rejects an overflowing range without mutation");
    TEST_ASSERT_EQ(agcReadBuffer(buffers[0], 0u, read_data,
        sizeof(read_data)), AGC_ERROR_INVALID_ARGUMENT,
        "upload-only staging rejects readback operations");
    TEST_ASSERT_EQ(agcDestroyBuffer(buffers[0]), AGC_OK,
        "upload buffer destroys");

    desc.flags = AGC_BUFFER_CREATE_READBACK_BIT;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &desc, &buffers[0]), AGC_OK,
        "persistently mapped readback buffer creation succeeds");
    info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_BUFFER,
        buffers[0], &info), AGC_OK, "readback allocation info succeeds");
    memcpy((uint8_t *)info.cpu_address + 16u, upload_data, sizeof(upload_data));
    TEST_ASSERT_EQ(agcWriteBuffer(buffers[0], 0u, upload_data,
        sizeof(upload_data)), AGC_ERROR_INVALID_ARGUMENT,
        "readback-only staging rejects upload operations");
    TEST_ASSERT_EQ(agcReadBuffer(buffers[0], 16u, read_data,
        sizeof(read_data)), AGC_OK, "bounded readback invalidate succeeds");
    TEST_ASSERT(memcmp(read_data, upload_data, sizeof(read_data)) == 0,
        "readback copies from persistent CPU mapping");
    TEST_ASSERT_EQ(agcReadBuffer(buffers[0], desc.size, read_data,
        sizeof(read_data)), AGC_ERROR_INVALID_ARGUMENT,
        "readback rejects an overflowing range without copying");
    TEST_ASSERT_EQ(agcDestroyBuffer(buffers[0]), AGC_OK,
        "readback buffer destroys");

    desc.size = UINT64_MAX;
    desc.flags = 0u;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &desc, &buffers[0]),
        AGC_ERROR_INVALID_ARGUMENT,
        "buffer allocation alignment overflow fails before mutation");
    TEST_ASSERT(buffers[0] == NULL,
        "overflowing buffer creation leaves the output null");

    stats = (AgcMemoryStats)AGC_MEMORY_STATS_INIT;
    TEST_ASSERT_EQ(agcGetMemoryStats(device, &stats), AGC_OK,
        "post-reload memory statistics query succeeds");
    TEST_ASSERT_EQ(stats.live_allocation_count, 0u,
        "repeated resource reloads return allocation count to baseline");
    TEST_ASSERT_EQ(stats.live_bytes, 0u,
        "repeated resource reloads return live bytes to baseline");
    TEST_ASSERT(stats.high_water_allocation_count >= BUFFER_COUNT,
        "statistics retain allocation high-water mark");
    TEST_ASSERT(stats.high_water_bytes != 0u,
        "statistics retain byte high-water mark");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "heap test device destruction succeeds");
}

static void test_runtime_fence_deferred_free(void)
{
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueCompute);
    AgcShader shader = create_shader(device, kAgcShaderStageCs);
    AgcComputePipelineDesc pipeline_desc = AGC_COMPUTE_PIPELINE_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcComputePipeline pipeline = NULL;
    AgcCommandBuffer command_buffer = NULL;
    AgcFence fence = NULL;
    AgcBuffer buffer = NULL;
    AgcBuffer replacement = NULL;
    AgcBuffer recycled = NULL;
    AgcImage image = NULL;
    AgcImage image_replacement = NULL;
    AgcImage image_recycled = NULL;
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
    AgcImageViewDesc view_desc = AGC_IMAGE_VIEW_DESC_INIT;
    AgcImageView view = NULL;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcMemoryStats stats = AGC_MEMORY_STATS_INIT;
    AgcAllocationInfo retired_info = AGC_ALLOCATION_INFO_INIT;
    AgcAllocationInfo replacement_info = AGC_ALLOCATION_INFO_INIT;

    pipeline_desc.shader = shader;
    pipeline_desc.local_size_x = 64u;
    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 64u;
    buffer_desc.size = 1024u;
    buffer_desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT;
    TEST_ASSERT_EQ(agcCreateComputePipeline(device, &pipeline_desc, &pipeline),
        AGC_OK, "deferred-free compute pipeline creation succeeds");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
        &command_buffer), AGC_OK, "deferred-free command buffer creation succeeds");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command_buffer), AGC_OK,
        "deferred-free command buffer begins");
    TEST_ASSERT_EQ(agcCmdBindComputePipeline(command_buffer, pipeline), AGC_OK,
        "deferred-free compute pipeline binds");
    TEST_ASSERT_EQ(agcCmdDispatch(command_buffer, 1u, 1u, 1u), AGC_OK,
        "deferred-free dispatch records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command_buffer), AGC_OK,
        "deferred-free command buffer ends");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "deferred-free fence creation succeeds");
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &buffer), AGC_OK,
        "deferred resource creation succeeds");
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_BUFFER,
        buffer, &retired_info), AGC_OK,
        "retiring buffer allocation is queryable");
    TEST_ASSERT_EQ(agcDestroyBufferDeferred(buffer, fence), AGC_OK,
        "resource retirement queues against unsignaled fence");
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &replacement), AGC_OK,
        "replacement buffer creation succeeds before retirement fence");
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_BUFFER,
        replacement, &replacement_info), AGC_OK,
        "replacement buffer allocation is queryable");
    TEST_ASSERT_NE(replacement_info.heap_offset, retired_info.heap_offset,
        "pending buffer range is not returned to its heap early");
    TEST_ASSERT_EQ(agcCollectDeferredFrees(device), AGC_OK,
        "collection before fence completion is a safe no-op");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_ERROR_BUSY,
        "retirement owner prevents premature fence reset");
    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_ERROR_BUSY,
        "retirement owner prevents premature fence destruction");
    TEST_ASSERT_EQ(agcGetMemoryStats(device, &stats), AGC_OK,
        "deferred memory stats query succeeds");
    TEST_ASSERT_EQ(stats.deferred_free_count, 1u,
        "deferred resource remains resident before fence");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &command_buffer;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "submission signals deferred retirement fence");
    TEST_ASSERT_EQ(agcCollectDeferredFrees(device), AGC_OK,
        "signaled deferred resources are collected");
    stats = (AgcMemoryStats)AGC_MEMORY_STATS_INIT;
    TEST_ASSERT_EQ(agcGetMemoryStats(device, &stats), AGC_OK,
        "collected memory stats query succeeds");
    TEST_ASSERT_EQ(stats.deferred_free_count, 0u,
        "deferred queue returns to baseline after fence");
    TEST_ASSERT_EQ(agcDestroyBuffer(replacement), AGC_OK,
        "pre-fence replacement buffer destroys");
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &recycled), AGC_OK,
        "post-fence recycled buffer creation succeeds");
    replacement_info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_BUFFER,
        recycled, &replacement_info), AGC_OK,
        "post-fence recycled allocation is queryable");
    TEST_ASSERT_EQ(replacement_info.heap_offset, retired_info.heap_offset,
        "signaled buffer range becomes reusable after collection");
    TEST_ASSERT_EQ(agcDestroyBuffer(recycled), AGC_OK,
        "post-fence recycled buffer destroys");

    image_desc.width = 64u;
    image_desc.height = 64u;
    image_desc.format = AGC_FORMAT_RGBA8_UNORM;
    image_desc.usage = AGC_IMAGE_USAGE_SAMPLED_BIT;
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "deferred image fence resets after buffer collection");
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &image), AGC_OK,
        "deferred image creation succeeds");
    retired_info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_IMAGE,
        image, &retired_info), AGC_OK,
        "retiring image allocation is queryable");
    TEST_ASSERT_EQ(agcDestroyImageDeferred(image, fence), AGC_OK,
        "image retirement queues against unsignaled fence");
    view_desc.image = image;
    view_desc.format = image_desc.format;
    TEST_ASSERT_EQ(agcCreateImageView(device, &view_desc, &view),
        AGC_ERROR_INVALID_ARGUMENT,
        "pending image cannot acquire a new descriptor owner");
    TEST_ASSERT(view == NULL,
        "rejected pending-image view leaves its output null");
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &image_replacement),
        AGC_OK, "replacement image creation succeeds before fence");
    replacement_info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_IMAGE,
        image_replacement, &replacement_info), AGC_OK,
        "replacement image allocation is queryable");
    TEST_ASSERT_NE(replacement_info.heap_offset, retired_info.heap_offset,
        "pending image range is not returned to its heap early");
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "second submission signals image retirement fence");
    TEST_ASSERT_EQ(agcCollectDeferredFrees(device), AGC_OK,
        "signaled deferred image is collected");
    TEST_ASSERT_EQ(agcDestroyImage(image_replacement), AGC_OK,
        "pre-fence replacement image destroys");
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &image_recycled), AGC_OK,
        "post-fence recycled image creation succeeds");
    replacement_info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_IMAGE,
        image_recycled, &replacement_info), AGC_OK,
        "post-fence recycled image allocation is queryable");
    TEST_ASSERT_EQ(replacement_info.heap_offset, retired_info.heap_offset,
        "signaled image range becomes reusable after collection");
    TEST_ASSERT_EQ(agcDestroyImage(image_recycled), AGC_OK,
        "post-fence recycled image destroys");
    stats = (AgcMemoryStats)AGC_MEMORY_STATS_INIT;
    TEST_ASSERT_EQ(agcGetMemoryStats(device, &stats), AGC_OK,
        "post-image-retirement statistics query succeeds");
    TEST_ASSERT_EQ(stats.deferred_free_count, 0u,
        "all deferred resources return to baseline after fences");

    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK, "deferred fence destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command_buffer), AGC_OK,
        "deferred command buffer destroys");
    TEST_ASSERT_EQ(agcDestroyComputePipeline(pipeline), AGC_OK,
        "deferred pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyShader(shader), AGC_OK, "deferred shader destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK, "deferred queue destroys");
    stats = (AgcMemoryStats)AGC_MEMORY_STATS_INIT;
    TEST_ASSERT_EQ(agcGetMemoryStats(device, &stats), AGC_OK,
        "final deferred-free statistics query succeeds");
    TEST_ASSERT_EQ(stats.live_allocation_count, 0u,
        "all fence-complete allocation owners return to baseline");
    TEST_ASSERT_EQ(stats.live_bytes, 0u,
        "all fence-complete allocation bytes return to baseline");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "deferred-free device destruction succeeds");
}

void test_suite_runtime(void)
{
    TEST_SUITE("Firmware-neutral Native Runtime");
    TEST_RUN(test_runtime_descriptor_and_info_contract);
    TEST_RUN(test_runtime_shader_reflection_contract);
    TEST_RUN(test_runtime_graphics_pipeline_compatibility_matrix);
    TEST_RUN(test_runtime_pipeline_layout_and_stage_validation);
    TEST_RUN(test_runtime_indirect_descriptor_set_table);
    TEST_RUN(test_runtime_geometry_pipeline_bundle);
    TEST_RUN(test_runtime_tessellation_pipeline_bundles);
    TEST_RUN(test_runtime_allocation_callbacks);
    TEST_RUN(test_runtime_allocation_failure_rollback);
    TEST_RUN(test_runtime_all_object_lifecycle);
    TEST_RUN(test_runtime_fence_and_command_states);
    TEST_RUN(test_runtime_compute_submission);
    TEST_RUN(test_runtime_indexed_graphics_submission);
    TEST_RUN(test_runtime_command_space_atomic_failure);
    TEST_RUN(test_runtime_dynamic_graphics_state);
    TEST_RUN(test_runtime_depth_stencil_pipeline_state);
    TEST_RUN(test_runtime_multisample_pipeline_state);
    TEST_RUN(test_runtime_ps5_image_layouts);
    TEST_RUN(test_runtime_all_backing_categories);
    TEST_RUN(test_runtime_heap_staging_and_stats);
    TEST_RUN(test_runtime_fence_deferred_free);
}
