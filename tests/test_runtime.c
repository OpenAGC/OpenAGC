/*
 * openagc native runtime contract tests.
 */

#include "test.h"

#include <stdint.h>
#include <string.h>

#include "agc_cb.h"
#include "agc_driver_debug.h"
#include "agc_graphics.h"
#include "agc_pm4.h"
#include "agc_registers.h"
#include "agc_texture.h"
#include "openagc/capture.h"
#include "openagc/runtime.h"

#include "../samples/hw_test/shaders/fill_color_native_reflection.h"
#include "../samples/hw_test/shaders/fill_color_native_sb.h"
#include "../samples/hw_test/shaders/runtime_triangle_frag_reflection.h"
#include "../samples/hw_test/shaders/runtime_triangle_frag_sb.h"
#include "../samples/hw_test/shaders/runtime_triangle_vert_back_sb.h"
#include "../samples/hw_test/shaders/runtime_triangle_vert_front_sb.h"
#include "../samples/hw_test/shaders/runtime_triangle_vert_reflection.h"

extern bool agcDriverDebugIsQueueInUse(uint32_t index);

static AgcDevice create_device(void)
{
    AgcDeviceDesc desc = AGC_DEVICE_DESC_INIT;
    AgcDevice device = NULL;

    TEST_ASSERT_EQ(agcCreateDevice(&desc, &device), AGC_OK,
        "native device creation succeeds");
    TEST_ASSERT(device != NULL, "native device handle is non-null");
    return device;
}

typedef struct RuntimeDebugProbe {
    uint32_t callback_count;
    AgcDebugMessage last;
} RuntimeDebugProbe;

typedef struct RuntimeCaptureSink {
    uint8_t bytes[65536];
    size_t size;
    uint32_t overflow;
} RuntimeCaptureSink;

static void PS5_SYSV_ABI runtime_debug_callback(
    void *user_data, const AgcDebugMessage *message)
{
    RuntimeDebugProbe *probe = user_data;

    probe->callback_count++;
    probe->last = *message;
}

static void expect_runtime_debug(const RuntimeDebugProbe *probe,
    uint32_t count, AgcDebugMessageCategoryFlags category, int32_t result,
    const char *function_name, const char *message_fragment)
{
    TEST_ASSERT_EQ(probe->callback_count, count,
        "invalid program emits exactly one diagnostic");
    TEST_ASSERT_EQ(probe->last.category, category,
        "invalid program diagnostic category is exact");
    TEST_ASSERT_EQ(probe->last.result, result,
        "invalid program diagnostic preserves public result");
    TEST_ASSERT(strcmp(probe->last.function_name, function_name) == 0,
        "invalid program diagnostic names the public entry point");
    TEST_ASSERT(strstr(probe->last.message, message_fragment) != NULL,
        "invalid program diagnostic explains the corrective contract");
}

static void PS5_SYSV_ABI runtime_capture_write(
    void *user_data, const void *data, size_t size)
{
    RuntimeCaptureSink *sink = user_data;

    if (size > sizeof(sink->bytes) - sink->size) {
        sink->overflow = 1u;
        return;
    }
    memcpy(sink->bytes + sink->size, data, size);
    sink->size += size;
}

static uint16_t runtime_capture_u16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static uint32_t runtime_capture_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t runtime_capture_u64(const uint8_t *bytes)
{
    uint64_t value = 0u;
    uint32_t i;

    for (i = 0u; i < 8u; ++i)
        value |= (uint64_t)bytes[i] << (i * 8u);
    return value;
}

static int32_t runtime_transition_buffer_to_graphics_read(
    AgcCommandBuffer command_buffer, AgcBuffer buffer, uint64_t size,
    AgcResourceUsage before, uint32_t flags)
{
    AgcResourceTransition transition = flags == 0u ?
        (AgcResourceTransition)AGC_RESOURCE_TRANSITION_INIT :
        (AgcResourceTransition)AGC_RESOURCE_TRANSITION_V2_INIT;

    transition.resource_type = kAgcResourceTypeBuffer;
    transition.buffer = buffer;
    transition.buffer_size = size;
    transition.before = before;
    transition.after = kAgcResourceUsageShaderRead;
    transition.before_owner = before == kAgcResourceUsageUndefined ?
        kAgcResourceOwnerHost : kAgcResourceOwnerGraphics;
    transition.after_owner = kAgcResourceOwnerGraphics;
    transition.flags = flags;
    return agcCmdTransitionResources(command_buffer, 1u, &transition);
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

static int runtime_has_opcode(const uint32_t *commands, uint32_t used,
    uint32_t opcode)
{
    uint32_t cursor = 0u;

    while (cursor < used) {
        uint32_t length = agcPm4Length(commands[cursor]);
        if (length < 2u || length > used - cursor)
            return 0;
        if (agcPm4Opcode(commands[cursor]) == opcode)
            return 1;
        cursor += length;
    }
    return 0;
}

static uint32_t runtime_count_opcode(const uint32_t *commands, uint32_t used,
    uint32_t opcode)
{
    uint32_t cursor = 0u;
    uint32_t count = 0u;

    while (cursor < used) {
        uint32_t length = agcPm4Length(commands[cursor]);
        if (length < 2u || length > used - cursor)
            return 0u;
        count += agcPm4Opcode(commands[cursor]) == opcode;
        cursor += length;
    }
    return count;
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
    if (stage == kAgcShaderStageGs)
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
    AgcDeviceProperties properties = AGC_DEVICE_PROPERTIES_INIT;
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
    TEST_ASSERT_EQ(agcGetDeviceProperties(device, &properties), AGC_OK,
        "firmware-neutral device properties query succeeds");
    TEST_ASSERT(properties.max_image_dimension_2d != 0u &&
        properties.max_compute_workgroup_invocations != 0u,
        "device properties expose qualified image and compute limits");
    TEST_ASSERT_EQ(properties.memory_heap_count, AGC_MEMORY_HEAP_COUNT,
        "device properties expose every native heap profile");
    TEST_ASSERT((properties.memory_heaps[AGC_MEMORY_HEAP_FLEXIBLE].
            property_flags & AGC_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0u &&
        (properties.memory_heaps[AGC_MEMORY_HEAP_GARLIC].property_flags &
            AGC_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0u,
        "device properties expose portable heap property flags");
    properties.reserved[0] = 1u;
    TEST_ASSERT_EQ(agcGetDeviceProperties(device, &properties),
        AGC_ERROR_INVALID_ARGUMENT,
        "device properties reject nonzero reserved fields");

    info = (AgcRuntimeInfo)AGC_RUNTIME_INFO_INIT;
    info.reserved[0] = 1u;
    TEST_ASSERT_EQ(agcGetRuntimeInfo(device, &info),
        AGC_ERROR_INVALID_ARGUMENT,
        "runtime info rejects nonzero reserved fields");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "native device destruction succeeds");
}

static void test_runtime_multiple_logical_devices(void)
{
    AgcDeviceDesc desc = AGC_DEVICE_DESC_INIT;
    AgcQueueDesc queue_desc = AGC_QUEUE_DESC_INIT;
    AgcRuntimeInfo info = AGC_RUNTIME_INFO_INIT;
    AgcDevice first = NULL;
    AgcDevice second = NULL;
    AgcDevice mismatched = NULL;
    AgcQueue first_queue = NULL;
    AgcQueue second_queue = NULL;
    AgcQueue first_compute = NULL;
    AgcQueue second_compute = NULL;

    desc.required_capability_bits = AGC_RUNTIME_CAP_BASELINE;
    TEST_ASSERT_EQ(agcCreateDevice(&desc, &first), AGC_OK,
        "first logical device creates");
    TEST_ASSERT_EQ(agcCreateDevice(&desc, &second), AGC_OK,
        "second logical device shares the initialized physical backend");
    desc.agc_version++;
    TEST_ASSERT_EQ(agcCreateDevice(&desc, &mismatched), AGC_ERROR_BUSY,
        "concurrent logical device rejects a different AGC defaults version");
    TEST_ASSERT(mismatched == NULL,
        "rejected mismatched logical device returns no handle");

    TEST_ASSERT_EQ(agcCreateQueue(first, &queue_desc, &first_queue), AGC_OK,
        "first logical device owns an independent graphics queue");
    TEST_ASSERT_EQ(agcCreateQueue(second, &queue_desc, &second_queue), AGC_OK,
        "second logical device owns an independent graphics queue");
    queue_desc.type = kAgcQueueCompute;
    TEST_ASSERT_EQ(agcCreateQueue(first, &queue_desc, &first_compute), AGC_OK,
        "first logical device owns an independent compute queue");
    TEST_ASSERT_EQ(agcCreateQueue(second, &queue_desc, &second_compute), AGC_OK,
        "second logical device owns an independent compute queue");
    TEST_ASSERT(agcDriverDebugIsQueueInUse(0u) &&
        agcDriverDebugIsQueueInUse(1u),
        "both logical compute queue backend handles are live");
    TEST_ASSERT_EQ(agcDestroyQueue(second_compute), AGC_OK,
        "second logical compute queue destroys out of creation order");
    TEST_ASSERT(agcDriverDebugIsQueueInUse(0u) &&
        !agcDriverDebugIsQueueInUse(1u),
        "compute queue destruction releases its exact backend handle");
    TEST_ASSERT_EQ(agcDestroyDevice(first), AGC_ERROR_BUSY,
        "first logical device retains its own children");
    TEST_ASSERT_EQ(agcDestroyQueue(first_queue), AGC_OK,
        "first logical device queue destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(first_compute), AGC_OK,
        "first logical compute queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(first), AGC_OK,
        "first logical device destroys without shutting down shared backend");
    TEST_ASSERT_EQ(agcGetRuntimeInfo(second, &info), AGC_OK,
        "second logical device remains valid after first device destruction");
    TEST_ASSERT_EQ(agcDestroyQueue(second_queue), AGC_OK,
        "second logical device queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(second), AGC_OK,
        "last logical device destroys and shuts down physical backend");
}

static void test_runtime_optional_debug_callback(void)
{
    AgcDevice device = create_device();
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcDebugCallbackDesc debug_desc = AGC_DEBUG_CALLBACK_DESC_INIT;
    AgcDebugMessage message = AGC_DEBUG_MESSAGE_INIT;
    RuntimeDebugProbe probe = {0};
    AgcCommandBuffer command = NULL;

    TEST_ASSERT_EQ(sizeof(AgcDebugMessage), 368u,
        "debug-message ABI size is stable");
    TEST_ASSERT_EQ(offsetof(AgcDebugMessage, message), 144u,
        "debug-message text offset is stable");
    TEST_ASSERT_EQ(sizeof(AgcDebugCallbackDesc), 64u,
        "debug-callback descriptor ABI size is stable");
    TEST_ASSERT_EQ(agcGetLastDebugMessage(device, &message),
        AGC_ERROR_NOT_FOUND, "debug query reports no message initially");

    debug_desc.user_data = &probe;
    debug_desc.reserved[0] = 1u;
    TEST_ASSERT_EQ(agcSetDebugCallback(device, &debug_desc),
        AGC_ERROR_INVALID_ARGUMENT,
        "debug callback rejects nonzero reserved fields");
    debug_desc.reserved[0] = 0u;
    TEST_ASSERT_EQ(agcSetDebugCallback(device, &debug_desc),
        AGC_ERROR_INVALID_ARGUMENT,
        "debug callback requires a function");
    debug_desc.callback = runtime_debug_callback;
    debug_desc.severity_mask = 0u;
    TEST_ASSERT_EQ(agcSetDebugCallback(device, &debug_desc),
        AGC_ERROR_INVALID_ARGUMENT,
        "debug callback rejects an empty severity mask");
    debug_desc.severity_mask = UINT32_C(1) << 31;
    TEST_ASSERT_EQ(agcSetDebugCallback(device, &debug_desc),
        AGC_ERROR_INVALID_ARGUMENT,
        "debug callback rejects unknown severity bits");
    debug_desc.severity_mask = AGC_DEBUG_MESSAGE_SEVERITY_ALL;
    debug_desc.category_mask = 0u;
    TEST_ASSERT_EQ(agcSetDebugCallback(device, &debug_desc),
        AGC_ERROR_INVALID_ARGUMENT,
        "debug callback rejects an empty category mask");
    debug_desc.category_mask = UINT32_C(1) << 31;
    TEST_ASSERT_EQ(agcSetDebugCallback(device, &debug_desc),
        AGC_ERROR_INVALID_ARGUMENT,
        "debug callback rejects unknown category bits");
    debug_desc.category_mask = AGC_DEBUG_MESSAGE_CATEGORY_ALL;
    TEST_ASSERT_EQ(agcSetDebugCallback(device, &debug_desc), AGC_OK,
        "debug callback installs without allocation");

    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 64u;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &command),
        AGC_OK, "debug command buffer creates");
    TEST_ASSERT_EQ(agcSetObjectDebugName(device,
        AGC_OBJECT_TYPE_COMMAND_BUFFER, command, "debug-command"), AGC_OK,
        "debug command receives a stable name");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command), AGC_ERROR_INVALID_STATE,
        "invalid end still returns the required safety error");
    TEST_ASSERT_EQ(probe.callback_count, 1u,
        "invalid end emits one actionable callback");
    TEST_ASSERT_EQ(probe.last.sequence, 1u,
        "first debug callback has deterministic sequence one");
    TEST_ASSERT_EQ(probe.last.severity,
        AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
        "invalid state is an error diagnostic");
    TEST_ASSERT_EQ(probe.last.category,
        AGC_DEBUG_MESSAGE_CATEGORY_OBJECT_STATE_BIT,
        "invalid end identifies object-state category");
    TEST_ASSERT_EQ(probe.last.result, AGC_ERROR_INVALID_STATE,
        "debug callback preserves the public result");
    TEST_ASSERT(strcmp(probe.last.function_name,
        "agcEndCommandBuffer") == 0,
        "debug callback names the failing public function");
    TEST_ASSERT(strcmp(probe.last.object_name, "debug-command") == 0,
        "debug callback includes the application debug name");
    TEST_ASSERT(strstr(probe.last.message, "Recording") != NULL,
        "debug callback explains the required state");

    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "debug command begins normally");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_ERROR_INVALID_STATE,
        "duplicate begin remains fail-closed");
    TEST_ASSERT_EQ(probe.callback_count, 2u,
        "duplicate begin emits one callback");
    TEST_ASSERT_EQ(probe.last.sequence, 2u,
        "debug sequence increases monotonically");
    TEST_ASSERT(strcmp(probe.last.function_name,
        "agcBeginCommandBuffer") == 0,
        "duplicate begin names its public function");

    debug_desc.category_mask = AGC_DEBUG_MESSAGE_CATEGORY_LIFETIME_BIT;
    TEST_ASSERT_EQ(agcSetDebugCallback(device, &debug_desc), AGC_OK,
        "debug category filter updates deterministically");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command), AGC_OK,
        "debug command ends for lifetime check");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_ERROR_INVALID_STATE,
        "filtered object-state error still preserves safety");
    TEST_ASSERT_EQ(probe.callback_count, 2u,
        "category filter suppresses only the optional callback");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_ERROR_BUSY,
        "premature device destruction remains fail-closed");
    TEST_ASSERT_EQ(probe.callback_count, 3u,
        "premature destruction emits a lifetime diagnostic");
    TEST_ASSERT_EQ(probe.last.sequence, 3u,
        "filtered messages do not consume sequence numbers");
    TEST_ASSERT_EQ(probe.last.category,
        AGC_DEBUG_MESSAGE_CATEGORY_LIFETIME_BIT,
        "premature destruction identifies lifetime category");
    TEST_ASSERT(strstr(probe.last.message, "child objects") != NULL,
        "lifetime diagnostic identifies the retained dependency");

    message = (AgcDebugMessage)AGC_DEBUG_MESSAGE_INIT;
    TEST_ASSERT_EQ(agcGetLastDebugMessage(device, &message), AGC_OK,
        "last debug message is queryable without callback state");
    TEST_ASSERT_EQ(message.sequence, probe.last.sequence,
        "queried debug message matches callback sequence");
    TEST_ASSERT(strcmp(message.function_name, "agcDestroyDevice") == 0,
        "queried debug message preserves function name");
    TEST_ASSERT_EQ(agcSetDebugCallback(device, NULL), AGC_OK,
        "NULL descriptor disables optional diagnostics");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command), AGC_OK,
        "debug command destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "debug device destroys after dependencies release");
}

static void test_runtime_invalid_program_diagnostic_matrix(void)
{
    AgcDevice device = create_device();
    AgcDebugCallbackDesc debug_desc = AGC_DEBUG_CALLBACK_DESC_INIT;
    RuntimeDebugProbe probe = {0};
    AgcSamplerDesc sampler_desc = AGC_SAMPLER_DESC_INIT;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcResourceTransition transition = AGC_RESOURCE_TRANSITION_INIT;
    AgcComputePipelineDesc compute_desc = AGC_COMPUTE_PIPELINE_DESC_INIT;
    AgcSampler sampler = NULL;
    AgcBuffer source = NULL;
    AgcBuffer destination = NULL;
    AgcImage image = NULL;
    AgcCommandBuffer command = NULL;
    AgcShader shader = NULL;
    AgcComputePipeline compute = NULL;
    uint32_t word = 0u;

    debug_desc.callback = runtime_debug_callback;
    debug_desc.user_data = &probe;
    TEST_ASSERT_EQ(agcSetDebugCallback(device, &debug_desc), AGC_OK,
        "invalid-program diagnostic callback installs");

    sampler_desc.min_filter = (AgcFilter)UINT32_MAX;
    TEST_ASSERT_EQ(agcCreateSampler(device, &sampler_desc, &sampler),
        AGC_ERROR_INVALID_ARGUMENT, "invalid sampler enum fails closed");
    expect_runtime_debug(&probe, 1u,
        AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
        AGC_ERROR_INVALID_ARGUMENT, "agcCreateSampler", "invalid enum");

    buffer_desc.size = 64u;
    buffer_desc.usage = UINT32_C(1) << 31;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &source),
        AGC_ERROR_INVALID_ARGUMENT, "unknown buffer usage fails closed");
    expect_runtime_debug(&probe, 2u,
        AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
        AGC_ERROR_INVALID_ARGUMENT, "agcCreateBuffer", "usage");

    buffer_desc.usage = AGC_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer_desc.flags = AGC_BUFFER_CREATE_UPLOAD_BIT;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &source), AGC_OK,
        "diagnostic source buffer creates");
    TEST_ASSERT_EQ(agcSetObjectDebugName(device, AGC_OBJECT_TYPE_BUFFER,
        source, "diagnostic-source"), AGC_OK,
        "diagnostic source receives a name");
    buffer_desc.usage = AGC_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_desc.flags = AGC_BUFFER_CREATE_READBACK_BIT;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &destination), AGC_OK,
        "diagnostic destination buffer creates");
    TEST_ASSERT_EQ(agcWriteBuffer(source, 60u, &word, 8u),
        AGC_ERROR_INVALID_ARGUMENT, "buffer upload overrun fails closed");
    expect_runtime_debug(&probe, 3u,
        AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
        AGC_ERROR_INVALID_ARGUMENT, "agcWriteBuffer", "in-range");

    image_desc.width = 4u;
    image_desc.height = 4u;
    image_desc.format = AGC_FORMAT_RGBA8_UNORM;
    image_desc.usage = AGC_IMAGE_USAGE_TRANSFER_SRC_BIT;
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &image), AGC_OK,
        "diagnostic image creates");
    TEST_ASSERT_EQ(agcReadImage(image, UINT64_MAX, &word, sizeof(word)),
        AGC_ERROR_INVALID_ARGUMENT, "image readback overrun fails closed");
    expect_runtime_debug(&probe, 4u,
        AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
        AGC_ERROR_INVALID_ARGUMENT, "agcReadImage", "in-range");

    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 64u;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &command),
        AGC_OK, "diagnostic copy command creates");
    TEST_ASSERT_EQ(agcSetObjectDebugName(device,
        AGC_OBJECT_TYPE_COMMAND_BUFFER, command, "diagnostic-copy"), AGC_OK,
        "diagnostic copy command receives a name");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "diagnostic copy command begins");
    TEST_ASSERT_EQ(agcCmdCopyBuffer(command, source, 1u, destination, 0u, 4u),
        AGC_ERROR_INVALID_ARGUMENT, "misaligned GPU-backed range fails closed");
    expect_runtime_debug(&probe, 5u,
        AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
        AGC_ERROR_INVALID_ARGUMENT, "agcCmdCopyBuffer", "four-byte aligned");
    TEST_ASSERT_EQ(agcCmdCopyBuffer(command, source, 0u, destination, 0u, 4u),
        AGC_ERROR_INVALID_STATE, "copy without transitions fails closed");
    expect_runtime_debug(&probe, 6u,
        AGC_DEBUG_MESSAGE_CATEGORY_RESOURCE_STATE_BIT,
        AGC_ERROR_INVALID_STATE, "agcCmdCopyBuffer", "transitioned");

    transition.resource_type = kAgcResourceTypeBuffer;
    transition.before = kAgcResourceUsageUndefined;
    transition.before_owner = kAgcResourceOwnerHost;
    transition.after = kAgcResourceUsageCopySource;
    transition.after_owner = kAgcResourceOwnerCompute;
    transition.buffer = source;
    transition.buffer_size = buffer_desc.size;
    TEST_ASSERT_EQ(agcCmdTransitionResources(command, 1u, &transition),
        AGC_OK, "diagnostic source transition records");
    transition.after = kAgcResourceUsageCopyDestination;
    transition.buffer = destination;
    TEST_ASSERT_EQ(agcCmdTransitionResources(command, 1u, &transition),
        AGC_OK, "diagnostic destination transition records");
    TEST_ASSERT_EQ(agcDestroyBuffer(source), AGC_ERROR_BUSY,
        "premature recorded-buffer destruction fails closed");
    expect_runtime_debug(&probe, 7u,
        AGC_DEBUG_MESSAGE_CATEGORY_LIFETIME_BIT, AGC_ERROR_BUSY,
        "agcDestroyBuffer", "recorded references");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
        "diagnostic copy command reset releases resources");
    TEST_ASSERT_EQ(agcReadBuffer(destination, 60u, &word, 8u),
        AGC_ERROR_INVALID_ARGUMENT, "buffer readback overrun fails closed");
    expect_runtime_debug(&probe, 8u,
        AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
        AGC_ERROR_INVALID_ARGUMENT, "agcReadBuffer", "in-range");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command), AGC_OK,
        "diagnostic copy command destroys");
    TEST_ASSERT_EQ(agcDestroyImage(image), AGC_OK,
        "diagnostic image destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(destination), AGC_OK,
        "diagnostic destination destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(source), AGC_OK,
        "diagnostic source destroys after reset");

    {
        AgcShaderReflection requirements = AGC_SHADER_REFLECTION_INIT;

        requirements.wave_size = 64u;
        shader = create_shader_with_reflection(
            device, kAgcShaderStageCs, &requirements);
        compute_desc.shader = shader;
        compute_desc.local_size_x = 64u;
        TEST_ASSERT_EQ(agcCreateComputePipeline(device, &compute_desc,
            &compute), AGC_ERROR_NOT_SUPPORTED,
            "unsupported compute wave fails closed");
        expect_runtime_debug(&probe, 9u,
            AGC_DEBUG_MESSAGE_CATEGORY_CAPABILITY_BIT,
            AGC_ERROR_NOT_SUPPORTED, "agcCreateComputePipeline", "wave");
        TEST_ASSERT_EQ(agcDestroyShader(shader), AGC_OK,
            "unsupported-wave shader destroys");
    }

    shader = create_shader(device, kAgcShaderStageCs);
    compute_desc = (AgcComputePipelineDesc)AGC_COMPUTE_PIPELINE_DESC_INIT;
    compute_desc.shader = shader;
    compute_desc.local_size_x = 64u;
    TEST_ASSERT_EQ(agcCreateComputePipeline(device, &compute_desc, &compute),
        AGC_OK, "capacity diagnostic compute pipeline creates");
    command_desc.capacity_dwords = 2u;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &command),
        AGC_OK, "capacity diagnostic command creates");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "capacity diagnostic command begins");
    TEST_ASSERT_EQ(agcCmdBindComputePipeline(command, compute), AGC_OK,
        "capacity diagnostic pipeline binds");
    TEST_ASSERT_EQ(agcCmdDispatch(command, 1u, 1u, 1u),
        AGC_ERROR_COMMAND_SPACE_EXHAUSTED,
        "dispatch command exhaustion fails closed");
    expect_runtime_debug(&probe, 10u,
        AGC_DEBUG_MESSAGE_CATEGORY_COMMAND_CAPACITY_BIT,
        AGC_ERROR_COMMAND_SPACE_EXHAUSTED, "agcCmdDispatch",
        "insufficient dwords");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
        "capacity diagnostic command resets");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command), AGC_OK,
        "capacity diagnostic command destroys");
    TEST_ASSERT_EQ(agcDestroyComputePipeline(compute), AGC_OK,
        "capacity diagnostic pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyShader(shader), AGC_OK,
        "capacity diagnostic shader destroys");

    {
        AgcShaderReflection requirements = AGC_SHADER_REFLECTION_INIT;
        AgcShaderDescriptorMapping mapping = {
            0u, 0u, AGC_SHADER_DESCRIPTOR_STORAGE_BUFFER, 1u, 0u, 16u};
        AgcDescriptorWrite write = AGC_DESCRIPTOR_WRITE_INIT;

        requirements.descriptor_mapping_count = 1u;
        requirements.descriptor_mappings[0] = mapping;
        requirements.user_sgpr_count = 1u;
        requirements.user_sgprs[0] = (AgcShaderUserSgpr){
            AGC_SHADER_USER_SGPR_DESCRIPTOR_SET, 0u,
            AGC_REG_COMPUTE_USER_DATA_0, 1u};
        shader = create_shader_with_reflection(
            device, kAgcShaderStageCs, &requirements);
        compute_desc = (AgcComputePipelineDesc)AGC_COMPUTE_PIPELINE_DESC_INIT;
        compute_desc.shader = shader;
        compute_desc.local_size_x = 64u;
        compute_desc.descriptor_mapping_count = 1u;
        compute_desc.descriptor_mappings = &mapping;
        TEST_ASSERT_EQ(agcCreateComputePipeline(device, &compute_desc,
            &compute), AGC_OK, "descriptor diagnostic pipeline creates");
        command_desc.capacity_dwords = 512u;
        TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &command),
            AGC_OK, "descriptor diagnostic command creates");
        TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
            "descriptor diagnostic command begins");
        TEST_ASSERT_EQ(agcCmdBindComputePipeline(command, compute), AGC_OK,
            "descriptor diagnostic pipeline binds");
        buffer_desc.size = 256u;
        buffer_desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT;
        buffer_desc.flags = 0u;
        TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &source), AGC_OK,
            "descriptor diagnostic storage buffer creates");
        write.type = AGC_SHADER_DESCRIPTOR_UNIFORM_BUFFER;
        write.buffer = source;
        TEST_ASSERT_EQ(agcCmdBindDescriptors(command, 1u, &write),
            AGC_ERROR_VALIDATION_FAILED,
            "descriptor/reflection mismatch fails closed");
        expect_runtime_debug(&probe, 11u,
            AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
            AGC_ERROR_VALIDATION_FAILED, "agcCmdBindDescriptors",
            "does not match shader reflection");
        TEST_ASSERT_EQ(agcCmdDispatch(command, 1u, 1u, 1u),
            AGC_ERROR_RESOURCE_NOT_BOUND,
            "dispatch with missing reflected descriptors fails closed");
        expect_runtime_debug(&probe, 12u,
            AGC_DEBUG_MESSAGE_CATEGORY_RESOURCE_STATE_BIT,
            AGC_ERROR_RESOURCE_NOT_BOUND, "agcCmdDispatch", "missing");
        TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
            "descriptor diagnostic command resets");
        TEST_ASSERT_EQ(agcDestroyBuffer(source), AGC_OK,
            "descriptor diagnostic storage buffer destroys");
        TEST_ASSERT_EQ(agcDestroyCommandBuffer(command), AGC_OK,
            "descriptor diagnostic command destroys");
        TEST_ASSERT_EQ(agcDestroyComputePipeline(compute), AGC_OK,
            "descriptor diagnostic pipeline destroys");
        TEST_ASSERT_EQ(agcDestroyShader(shader), AGC_OK,
            "descriptor diagnostic shader destroys");
    }

    {
        AgcShaderReflection requirements = AGC_SHADER_REFLECTION_INIT;
        AgcGraphicsPipelineDesc graphics_desc =
            AGC_GRAPHICS_PIPELINE_DESC_INIT;
        AgcColorBlendAttachmentState attachment =
            AGC_COLOR_BLEND_ATTACHMENT_STATE_INIT;
        AgcShader vertex = create_shader(device, kAgcShaderStageVs);
        AgcShader pixel;
        AgcGraphicsPipeline graphics = NULL;

        requirements.color_export_count = 1u;
        requirements.color_exports[0].location = 0u;
        requirements.color_exports[0].format =
            AGC_SHADER_COLOR_EXPORT_UINT16_ABGR;
        requirements.color_exports[0].component_class =
            AGC_SHADER_COMPONENT_UINT;
        requirements.color_exports[0].write_mask = 0xfu;
        pixel = create_shader_with_reflection(
            device, kAgcShaderStagePs, &requirements);
        attachment.format = AGC_FORMAT_RGBA16_UINT;
        attachment.blend_enable = 1u;
        graphics_desc.vertex_shader = vertex;
        graphics_desc.pixel_shader = pixel;
        graphics_desc.color_attachment_count = 1u;
        graphics_desc.color_attachments = &attachment;
        TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &graphics_desc,
            &graphics), AGC_ERROR_VALIDATION_FAILED,
            "integer-target blending fails closed");
        expect_runtime_debug(&probe, 13u,
            AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
            AGC_ERROR_VALIDATION_FAILED, "agcCreateGraphicsPipeline",
            "integer targets cannot enable blending");
        TEST_ASSERT_EQ(agcDestroyShader(pixel), AGC_OK,
            "integer-export shader destroys");
        TEST_ASSERT_EQ(agcDestroyShader(vertex), AGC_OK,
            "integer-blend vertex shader destroys");
    }

    {
        AgcQueueDesc queue_desc = AGC_QUEUE_DESC_INIT;
        AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
        AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
        AgcQueue queue = NULL;
        AgcFence fence = NULL;

        command_desc.capacity_dwords = 64u;
        TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &command),
            AGC_OK, "use-after-submit command creates");
        TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
            "use-after-submit command begins");
        TEST_ASSERT_EQ(agcEndCommandBuffer(command), AGC_OK,
            "use-after-submit command ends");
        queue_desc.type = kAgcQueueCompute;
        TEST_ASSERT_EQ(agcCreateQueue(device, &queue_desc, &queue), AGC_OK,
            "use-after-submit queue creates");
        TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
            "use-after-submit fence creates");
        submit.command_buffer_count = 1u;
        submit.command_buffers = &command;
        TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
            "use-after-submit command submits");
        TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_ERROR_INVALID_STATE,
            "submitted command reuse without reset fails closed");
        expect_runtime_debug(&probe, 14u,
            AGC_DEBUG_MESSAGE_CATEGORY_OBJECT_STATE_BIT,
            AGC_ERROR_INVALID_STATE, "agcBeginCommandBuffer", "Initial");
        TEST_ASSERT_EQ(agcWaitFence(fence, UINT64_C(1000000)), AGC_OK,
            "use-after-submit fence completes");
        TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
            "use-after-submit command resets after completion");
        TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK,
            "use-after-submit fence destroys");
        TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK,
            "use-after-submit queue destroys");
        TEST_ASSERT_EQ(agcDestroyCommandBuffer(command), AGC_OK,
            "use-after-submit command destroys");
    }

    TEST_ASSERT_EQ(probe.callback_count, 14u,
        "invalid-program matrix emits a deterministic diagnostic count");
    TEST_ASSERT_EQ(agcSetDebugCallback(device, NULL), AGC_OK,
        "invalid-program diagnostic callback disables");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "invalid-program diagnostic device destroys without leaks");
}

static void test_runtime_capture_v1_stream(void)
{
    AgcDevice device = create_device();
    AgcCaptureDesc capture_desc = AGC_CAPTURE_DESC_INIT;
    AgcCaptureInfo capture_info = AGC_CAPTURE_INFO_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcGpuLabelDesc label_desc = AGC_GPU_LABEL_DESC_INIT;
    AgcQueueDesc queue_desc = AGC_QUEUE_DESC_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_V2_INIT;
    AgcGpuLabelPoint wait = AGC_GPU_LABEL_POINT_INIT;
    AgcGpuLabelPoint signal = AGC_GPU_LABEL_POINT_INIT;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
    AgcImageViewDesc view_desc = AGC_IMAGE_VIEW_DESC_INIT;
    AgcSamplerDesc sampler_desc = AGC_SAMPLER_DESC_INIT;
    AgcGraphicsPipelineDesc graphics_desc = AGC_GRAPHICS_PIPELINE_DESC_INIT;
    AgcComputePipelineDesc pipeline_desc = AGC_COMPUTE_PIPELINE_DESC_INIT;
    AgcResourceTransition transition = AGC_RESOURCE_TRANSITION_INIT;
    RuntimeCaptureSink sink = {0};
    AgcCapture capture = NULL;
    AgcCommandBuffer command = NULL;
    AgcQueue queue = NULL;
    AgcFence fence = NULL;
    AgcGpuLabel source_label = NULL;
    AgcGpuLabel destination_label = NULL;
    AgcBuffer buffer = NULL;
    AgcImage image = NULL;
    AgcImageView view = NULL;
    AgcSampler sampler = NULL;
    AgcShader shader = NULL;
    AgcShader vertex_shader = NULL;
    AgcShader pixel_shader = NULL;
    AgcGraphicsPipeline graphics_pipeline = NULL;
    AgcComputePipeline pipeline = NULL;
    uint32_t counts[AGC_CAPTURE_RECORD_RESOURCE_TRANSITION + 1u] = {0};
    uint64_t expected_sequence = 1u;
    size_t offset;

    TEST_ASSERT_EQ(sizeof(AgcCaptureDesc), 64u,
        "capture descriptor ABI size is stable");
    TEST_ASSERT_EQ(sizeof(AgcCaptureInfo), 72u,
        "capture info ABI size is stable");
    capture_desc.user_data = &sink;
    TEST_ASSERT_EQ(agcCreateCapture(device, &capture_desc, &capture),
        AGC_ERROR_INVALID_ARGUMENT,
        "capture creation requires a streaming callback");
    capture_desc.write = runtime_capture_write;
    capture_desc.flags = UINT32_C(1) << 31;
    TEST_ASSERT_EQ(agcCreateCapture(device, &capture_desc, &capture),
        AGC_ERROR_INVALID_ARGUMENT,
        "capture creation rejects unknown flags");
    capture_desc.flags = 0u;
    TEST_ASSERT_EQ(agcCreateCapture(device, &capture_desc, &capture), AGC_OK,
        "capture object creates");
    TEST_ASSERT_EQ(agcGetCaptureInfo(capture, &capture_info), AGC_OK,
        "inactive capture info queries");
    TEST_ASSERT_EQ(capture_info.active, 0u,
        "new capture starts inactive");
    TEST_ASSERT_EQ(agcBeginCapture(capture), AGC_OK,
        "capture stream begins");
    TEST_ASSERT_EQ(agcBeginCapture(capture), AGC_ERROR_BUSY,
        "active capture cannot begin twice");
    TEST_ASSERT_EQ(agcDestroyCapture(capture), AGC_ERROR_BUSY,
        "active capture cannot be destroyed");

    buffer_desc.size = 256u;
    buffer_desc.usage = AGC_BUFFER_USAGE_TRANSFER_SRC_BIT |
        AGC_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_desc.flags = AGC_BUFFER_CREATE_READBACK_BIT;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &buffer), AGC_OK,
        "captured readback buffer creates");
    image_desc.width = 4u;
    image_desc.height = 4u;
    image_desc.format = AGC_FORMAT_RGBA8_UNORM;
    image_desc.usage = AGC_IMAGE_USAGE_SAMPLED_BIT |
        AGC_IMAGE_USAGE_TRANSFER_SRC_BIT;
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &image), AGC_OK,
        "captured image creates");
    view_desc.image = image;
    view_desc.format = image_desc.format;
    TEST_ASSERT_EQ(agcCreateImageView(device, &view_desc, &view), AGC_OK,
        "captured image view creates");
    TEST_ASSERT_EQ(agcCreateSampler(device, &sampler_desc, &sampler), AGC_OK,
        "captured sampler creates");
    shader = create_shader(device, kAgcShaderStageCs);
    pipeline_desc.shader = shader;
    pipeline_desc.local_size_x = 64u;
    TEST_ASSERT_EQ(agcCreateComputePipeline(device, &pipeline_desc,
        &pipeline), AGC_OK, "captured compute pipeline creates");
    vertex_shader = create_shader(device, kAgcShaderStageVs);
    pixel_shader = create_shader(device, kAgcShaderStagePs);
    graphics_desc.vertex_shader = vertex_shader;
    graphics_desc.pixel_shader = pixel_shader;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &graphics_desc,
        &graphics_pipeline), AGC_OK,
        "captured graphics pipeline creates");
    TEST_ASSERT_EQ(agcCaptureRecordReadbackHash(capture,
        AGC_CAPTURE_OBJECT_BUFFER, buffer, 0u, buffer_desc.size), AGC_OK,
        "selected readback range hashes into capture");
    TEST_ASSERT_EQ(agcCaptureRecordReadbackHash(capture,
        AGC_CAPTURE_OBJECT_SAMPLER, sampler, 0u, 1u),
        AGC_ERROR_INVALID_ARGUMENT,
        "non-resource readback hash fails closed");

    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 64u;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &command),
        AGC_OK, "captured command buffer creates");
    TEST_ASSERT_EQ(agcSetObjectDebugName(device,
        AGC_OBJECT_TYPE_COMMAND_BUFFER, command, "captured-command"), AGC_OK,
        "captured command receives a debug name");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command), AGC_ERROR_INVALID_STATE,
        "capture records validation without a debug callback");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "captured command begins");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command), AGC_OK,
        "captured empty command ends");

    queue_desc.type = kAgcQueueCompute;
    TEST_ASSERT_EQ(agcCreateQueue(device, &queue_desc, &queue), AGC_OK,
        "captured compute queue creates");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "captured fence creates");
    TEST_ASSERT_EQ(agcCreateGpuLabel(device, &label_desc, &source_label),
        AGC_OK, "captured source label creates");
    TEST_ASSERT_EQ(agcCreateGpuLabel(device, &label_desc, &destination_label),
        AGC_OK, "captured destination label creates");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &command;
    signal.label = source_label;
    signal.value = 1u;
    submit.signal_count = 1u;
    submit.signals = &signal;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "captured signal submission submits");
    TEST_ASSERT_EQ(agcWaitFence(fence, UINT64_C(1000000)), AGC_OK,
        "captured fence wait completes");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
        "captured command resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "captured fence resets");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "captured transition command begins");
    transition.resource_type = kAgcResourceTypeBuffer;
    transition.before = kAgcResourceUsageUndefined;
    transition.before_owner = kAgcResourceOwnerHost;
    transition.after = kAgcResourceUsageCopyDestination;
    transition.after_owner = kAgcResourceOwnerCompute;
    transition.buffer = buffer;
    transition.buffer_size = buffer_desc.size;
    TEST_ASSERT_EQ(agcCmdTransitionResources(command, 1u, &transition),
        AGC_OK, "typed captured transition records");
    transition = (AgcResourceTransition)AGC_RESOURCE_TRANSITION_INIT;
    transition.resource_type = kAgcResourceTypeImage;
    transition.image = image;
    transition.after = kAgcResourceUsageCopySource;
    transition.after_owner = kAgcResourceOwnerCompute;
    transition.image_range.aspect_mask = AGC_IMAGE_ASPECT_COLOR_BIT;
    transition.image_range.mip_level_count = 1u;
    transition.image_range.array_layer_count = 1u;
    TEST_ASSERT_EQ(agcCmdTransitionResources(command, 1u, &transition),
        AGC_OK, "typed captured image transition records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command), AGC_OK,
        "captured transition command ends");
    wait.label = source_label;
    wait.value = 1u;
    signal.label = destination_label;
    signal.value = 2u;
    submit.wait_count = 1u;
    submit.waits = &wait;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "captured wait/signal submission submits");
    TEST_ASSERT_EQ(agcWaitFence(fence, UINT64_C(1000000)), AGC_OK,
        "captured dependency fence completes");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
        "captured transition command resets and releases resource");
    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK,
        "captured fence destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command), AGC_OK,
        "captured command destroys");
    TEST_ASSERT_EQ(agcDestroyGpuLabel(destination_label), AGC_OK,
        "captured destination label destroys");
    TEST_ASSERT_EQ(agcDestroyGpuLabel(source_label), AGC_OK,
        "captured source label destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK,
        "captured queue destroys");
    TEST_ASSERT_EQ(agcDestroyComputePipeline(pipeline), AGC_OK,
        "captured compute pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(graphics_pipeline), AGC_OK,
        "captured graphics pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyShader(pixel_shader), AGC_OK,
        "captured pixel shader destroys");
    TEST_ASSERT_EQ(agcDestroyShader(vertex_shader), AGC_OK,
        "captured vertex shader destroys");
    TEST_ASSERT_EQ(agcDestroyShader(shader), AGC_OK,
        "captured shader destroys");
    TEST_ASSERT_EQ(agcDestroySampler(sampler), AGC_OK,
        "captured sampler destroys");
    TEST_ASSERT_EQ(agcDestroyImageView(view), AGC_OK,
        "captured image view destroys");
    TEST_ASSERT_EQ(agcDestroyImage(image), AGC_OK,
        "captured image destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(buffer), AGC_OK,
        "captured readback buffer destroys");
    TEST_ASSERT_EQ(agcEndCapture(capture), AGC_OK,
        "capture stream ends cleanly");
    TEST_ASSERT_EQ(sink.overflow, 0u,
        "capture sink did not overflow");
    TEST_ASSERT(sink.size >= AGC_CAPTURE_FILE_HEADER_SIZE,
        "capture contains a complete file header");
    TEST_ASSERT(memcmp(sink.bytes, "OAGCCAP", 7u) == 0 &&
        sink.bytes[7] == 0u, "capture magic is exact");
    TEST_ASSERT_EQ(runtime_capture_u32(sink.bytes + 8u),
        AGC_CAPTURE_FORMAT_VERSION, "capture format version is exact");
    TEST_ASSERT_EQ(runtime_capture_u32(sink.bytes + 12u),
        AGC_CAPTURE_FILE_HEADER_SIZE, "capture header size is exact");
    TEST_ASSERT_EQ(runtime_capture_u32(sink.bytes + 16u),
        AGC_CAPTURE_ENDIAN_TAG, "capture endian tag is little-endian");
    TEST_ASSERT_EQ(runtime_capture_u32(sink.bytes + 20u),
        AGC_RUNTIME_API_VERSION, "capture records runtime API version");

    offset = AGC_CAPTURE_FILE_HEADER_SIZE;
    while (offset < sink.size) {
        uint16_t type;
        uint16_t version;
        uint32_t size;
        uint64_t sequence;

        TEST_ASSERT(sink.size - offset >= AGC_CAPTURE_RECORD_HEADER_SIZE,
            "capture record header is in bounds");
        if (sink.size - offset < AGC_CAPTURE_RECORD_HEADER_SIZE)
            break;
        type = runtime_capture_u16(sink.bytes + offset);
        version = runtime_capture_u16(sink.bytes + offset + 2u);
        size = runtime_capture_u32(sink.bytes + offset + 4u);
        sequence = runtime_capture_u64(sink.bytes + offset + 8u);
        TEST_ASSERT_EQ(version, AGC_CAPTURE_FORMAT_VERSION,
            "capture record version is exact");
        TEST_ASSERT(size >= AGC_CAPTURE_RECORD_HEADER_SIZE &&
            size <= sink.size - offset, "capture record size is in bounds");
        TEST_ASSERT_EQ(sequence, expected_sequence,
            "capture record sequence is contiguous");
        expected_sequence++;
        if (type <= AGC_CAPTURE_RECORD_RESOURCE_TRANSITION)
            counts[type]++;
        if (type == AGC_CAPTURE_RECORD_COMMAND_END) {
            if (counts[type] == 1u) {
                TEST_ASSERT_EQ(runtime_capture_u32(
                    sink.bytes + offset + 28u), 0u,
                    "first command-end preserves empty application body");
            }
        } else if (type == AGC_CAPTURE_RECORD_COMMAND_STREAM) {
            uint32_t used = runtime_capture_u32(sink.bytes + offset + 28u);
            TEST_ASSERT(used > 2u,
                "submitted stream includes injected dependency packets");
        } else if (type == AGC_CAPTURE_RECORD_SUBMISSION) {
            uint32_t wait_count = runtime_capture_u32(
                sink.bytes + offset + 32u);
            uint32_t signal_count = runtime_capture_u32(
                sink.bytes + offset + 36u);
            uint64_t first_label = runtime_capture_u64(
                sink.bytes + offset + 64u);
            uint32_t first_value = runtime_capture_u32(
                sink.bytes + offset + 72u);

            if (counts[type] == 1u) {
                TEST_ASSERT_EQ(wait_count, 0u,
                    "first captured submit has no wait");
                TEST_ASSERT_EQ(signal_count, 1u,
                    "first captured submit has one signal");
                TEST_ASSERT(first_label != 0u && first_value == 1u,
                    "first captured signal uses a stable label ID/value");
            } else {
                uint64_t second_label = runtime_capture_u64(
                    sink.bytes + offset + 80u);
                uint32_t second_value = runtime_capture_u32(
                    sink.bytes + offset + 88u);

                TEST_ASSERT_EQ(wait_count, 1u,
                    "second captured submit has one wait");
                TEST_ASSERT_EQ(signal_count, 1u,
                    "second captured submit has one signal");
                TEST_ASSERT(first_label != 0u && first_value == 1u,
                    "captured wait uses a stable label ID/value");
                TEST_ASSERT(second_label != 0u &&
                    second_label != first_label && second_value == 2u,
                    "captured signal preserves a distinct label ID/value");
            }
        } else if (type == AGC_CAPTURE_RECORD_RESOURCE_TRANSITION) {
            uint32_t resource_type = runtime_capture_u32(
                sink.bytes + offset + 40u);

            if (counts[type] == 1u) {
                TEST_ASSERT_EQ(resource_type, kAgcResourceTypeBuffer,
                    "captured buffer transition preserves resource type");
                TEST_ASSERT_EQ(runtime_capture_u64(
                    sink.bytes + offset + 80u), buffer_desc.size,
                    "captured buffer transition preserves its byte range");
            } else {
                TEST_ASSERT_EQ(resource_type, kAgcResourceTypeImage,
                    "captured image transition preserves resource type");
                TEST_ASSERT_EQ(runtime_capture_u32(
                    sink.bytes + offset + 72u),
                    AGC_IMAGE_ASPECT_COLOR_BIT,
                    "captured image transition preserves its aspect");
                TEST_ASSERT_EQ(runtime_capture_u32(
                    sink.bytes + offset + 80u), 1u,
                    "captured image transition preserves its mip count");
                TEST_ASSERT_EQ(runtime_capture_u32(
                    sink.bytes + offset + 88u), 1u,
                    "captured image transition preserves its layer count");
            }
        } else if (type == AGC_CAPTURE_RECORD_END) {
            TEST_ASSERT_EQ(runtime_capture_u64(sink.bytes + offset + 32u),
                sink.size, "end record authenticates final byte count");
        }
        if (size < AGC_CAPTURE_RECORD_HEADER_SIZE || size > sink.size - offset)
            break;
        offset += size;
    }
    TEST_ASSERT_EQ(offset, sink.size,
        "capture parser consumes the complete stream");
    TEST_ASSERT_EQ(counts[AGC_CAPTURE_RECORD_RUNTIME_INFO], 1u,
        "capture contains one runtime profile record");
    TEST_ASSERT_EQ(counts[AGC_CAPTURE_RECORD_OBJECT_CREATE], 14u,
        "capture contains every captured object creation");
    TEST_ASSERT_EQ(counts[AGC_CAPTURE_RECORD_OBJECT_NAME], 1u,
        "capture contains the command debug name");
    TEST_ASSERT_EQ(counts[AGC_CAPTURE_RECORD_OBJECT_DESTROY], 14u,
        "capture contains matching object destruction");
    TEST_ASSERT_EQ(counts[AGC_CAPTURE_RECORD_COMMAND_BEGIN], 2u,
        "capture contains both command begin boundaries");
    TEST_ASSERT_EQ(counts[AGC_CAPTURE_RECORD_COMMAND_END], 2u,
        "capture contains both command end boundaries");
    TEST_ASSERT_EQ(counts[AGC_CAPTURE_RECORD_COMMAND_STREAM], 2u,
        "capture contains exact submitted PM4 dwords");
    TEST_ASSERT_EQ(counts[AGC_CAPTURE_RECORD_SUBMISSION], 2u,
        "capture contains dependency submission ordering");
    TEST_ASSERT_EQ(counts[AGC_CAPTURE_RECORD_FENCE_RESULT], 2u,
        "capture contains both bounded fence results");
    TEST_ASSERT_EQ(counts[AGC_CAPTURE_RECORD_VALIDATION_MESSAGE], 1u,
        "capture contains actionable validation warning");
    TEST_ASSERT_EQ(counts[AGC_CAPTURE_RECORD_READBACK_HASH], 1u,
        "capture contains one selected readback hash");
    TEST_ASSERT_EQ(counts[AGC_CAPTURE_RECORD_RESOURCE_DESC], 4u,
        "capture contains buffer, image, view, and sampler descriptions");
    TEST_ASSERT_EQ(counts[AGC_CAPTURE_RECORD_SHADER_DESC], 3u,
        "capture contains shader record version and hashes");
    TEST_ASSERT_EQ(counts[AGC_CAPTURE_RECORD_SHADER_BYTES], 0u,
        "capture omits shader bytes without explicit opt-in");
    TEST_ASSERT_EQ(counts[AGC_CAPTURE_RECORD_PIPELINE_DESC], 2u,
        "capture contains normalized compute and graphics pipelines");
    TEST_ASSERT_EQ(counts[AGC_CAPTURE_RECORD_RESOURCE_TRANSITION], 2u,
        "capture contains typed buffer and image transitions");
    TEST_ASSERT_EQ(counts[AGC_CAPTURE_RECORD_END], 1u,
        "capture contains one final record");

    capture_info = (AgcCaptureInfo)AGC_CAPTURE_INFO_INIT;
    TEST_ASSERT_EQ(agcGetCaptureInfo(capture, &capture_info), AGC_OK,
        "completed capture info queries");
    TEST_ASSERT_EQ(capture_info.active, 0u,
        "completed capture is inactive");
    TEST_ASSERT_EQ(capture_info.status, AGC_OK,
        "completed capture status is successful");
    TEST_ASSERT_EQ(capture_info.byte_count, sink.size,
        "capture info reports exact byte count");
    TEST_ASSERT_EQ(capture_info.record_count, expected_sequence - 1u,
        "capture info reports exact record count");
    TEST_ASSERT_EQ(capture_info.next_object_id, 15u,
        "capture-local object IDs are dense and pointer-independent");
    TEST_ASSERT_EQ(agcDestroyCapture(capture), AGC_OK,
        "completed capture destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "capture device destroys without leaks");
}

static void test_runtime_capture_shader_bytes_opt_in(void)
{
    AgcDevice device = create_device();
    AgcCaptureDesc desc = AGC_CAPTURE_DESC_INIT;
    RuntimeCaptureSink sink = {0};
    AgcCapture capture = NULL;
    AgcShader shader = NULL;
    uint32_t byte_records = 0u;
    uint32_t primary_records = 0u;
    uint32_t front_records = 0u;
    size_t offset;

    desc.flags = AGC_CAPTURE_INCLUDE_SHADER_BYTES_BIT;
    desc.write = runtime_capture_write;
    desc.user_data = &sink;
    TEST_ASSERT_EQ(agcCreateCapture(device, &desc, &capture), AGC_OK,
        "shader-byte opt-in capture creates");
    TEST_ASSERT_EQ(agcBeginCapture(capture), AGC_OK,
        "shader-byte opt-in capture begins");
    shader = create_ngg_shader_bundle(device, kAgcShaderStageVs, NULL);
    TEST_ASSERT_EQ(agcDestroyShader(shader), AGC_OK,
        "opt-in captured shader destroys");
    TEST_ASSERT_EQ(agcEndCapture(capture), AGC_OK,
        "shader-byte opt-in capture ends");
    offset = AGC_CAPTURE_FILE_HEADER_SIZE;
    while (offset < sink.size) {
        uint16_t type = runtime_capture_u16(sink.bytes + offset);
        uint32_t size = runtime_capture_u32(sink.bytes + offset + 4u);

        if (type == AGC_CAPTURE_RECORD_SHADER_BYTES) {
            uint32_t half = runtime_capture_u32(sink.bytes + offset + 24u);
            uint32_t byte_count = runtime_capture_u32(
                sink.bytes + offset + 28u);
            byte_records++;
            primary_records += half == 0u;
            front_records += half == 1u;
            TEST_ASSERT_EQ(size, AGC_CAPTURE_RECORD_HEADER_SIZE + 16u +
                byte_count, "opt-in shader byte record size is exact");
        }
        offset += size;
    }
    TEST_ASSERT_EQ(byte_records, 2u,
        "explicit opt-in captures both shader halves exactly once");
    TEST_ASSERT_EQ(primary_records, 1u,
        "explicit opt-in identifies primary shader bytes");
    TEST_ASSERT_EQ(front_records, 1u,
        "explicit opt-in identifies front shader bytes");
    TEST_ASSERT_EQ(agcDestroyCapture(capture), AGC_OK,
        "shader-byte opt-in capture destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "shader-byte opt-in device destroys without leaks");
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

static void test_runtime_validation_is_allocation_free(void)
{
    AllocationProbe allocation_probe = {0};
    AgcAllocationCallbacks callbacks = {
        &allocation_probe, probe_allocate, probe_free
    };
    AgcDeviceDesc device_desc = AGC_DEVICE_DESC_INIT;
    AgcDebugCallbackDesc debug_desc = AGC_DEBUG_CALLBACK_DESC_INIT;
    AgcSamplerDesc sampler_desc = AGC_SAMPLER_DESC_INIT;
    RuntimeDebugProbe debug_probe = {0};
    AgcDevice device = NULL;
    AgcSampler sampler = NULL;
    uint32_t attempts_before;

    device_desc.allocation_callbacks = &callbacks;
    TEST_ASSERT_EQ(agcCreateDevice(&device_desc, &device), AGC_OK,
        "allocation-free validation device creates");
    debug_desc.callback = runtime_debug_callback;
    debug_desc.user_data = &debug_probe;
    TEST_ASSERT_EQ(agcSetDebugCallback(device, &debug_desc), AGC_OK,
        "allocation-free validation callback installs");
    attempts_before = allocation_probe.attempts;
    allocation_probe.fail_on_attempt = attempts_before + 1u;
    sampler_desc.address_w = (AgcAddressMode)UINT32_MAX;
    TEST_ASSERT_EQ(agcCreateSampler(device, &sampler_desc, &sampler),
        AGC_ERROR_INVALID_ARGUMENT,
        "invalid sampler remains diagnostic when the next allocation would fail");
    TEST_ASSERT_EQ(allocation_probe.attempts, attempts_before,
        "validation and callback delivery perform no allocation attempt");
    expect_runtime_debug(&debug_probe, 1u,
        AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
        AGC_ERROR_INVALID_ARGUMENT, "agcCreateSampler", "invalid enum");
    allocation_probe.fail_on_attempt = 0u;
    TEST_ASSERT_EQ(agcSetDebugCallback(device, NULL), AGC_OK,
        "allocation-free validation callback disables");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "allocation-free validation device destroys");
    TEST_ASSERT_EQ(allocation_probe.allocations, allocation_probe.frees,
        "allocation-free validation preserves balanced application allocations");
}

static void test_runtime_allocation_callbacks(void)
{
    AllocationProbe probe = {0};
    AgcAllocationCallbacks callbacks = {
        &probe, probe_allocate, probe_free
    };
    AgcDeviceDesc desc = AGC_DEVICE_DESC_INIT;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcResourceTransition transition = AGC_RESOURCE_TRANSITION_INIT;
    AgcResourceStateInfo state_info = AGC_RESOURCE_STATE_INFO_INIT;
    AgcDevice device = NULL;
    AgcBuffer buffer = NULL;
    AgcCommandBuffer command = NULL;

    desc.allocation_callbacks = &callbacks;
    buffer_desc.size = 64u;
    buffer_desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT;
    TEST_ASSERT_EQ(agcCreateDevice(&desc, &device), AGC_OK,
        "device accepts paired allocation callbacks");
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &buffer), AGC_OK,
        "buffer uses application allocation callbacks");
    command_desc.queue_type = kAgcQueueCompute;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &command),
        AGC_OK, "range-state command uses allocation callbacks");
    transition.resource_type = kAgcResourceTypeBuffer;
    transition.buffer = buffer;
    transition.buffer_offset = 16u;
    transition.buffer_size = 32u;
    transition.before = kAgcResourceUsageUndefined;
    transition.after = kAgcResourceUsageShaderRead;
    transition.before_owner = kAgcResourceOwnerHost;
    transition.after_owner = kAgcResourceOwnerCompute;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "range-state allocation rollback command begins");
    probe.fail_on_attempt = probe.attempts + 1u;
    TEST_ASSERT_EQ(agcCmdTransitionResources(command, 1u, &transition),
        AGC_ERROR_OUT_OF_MEMORY,
        "range-state allocation failure propagates before packet mutation");
    TEST_ASSERT_EQ(agcGetBufferStateInfo(buffer, &state_info), AGC_OK,
        "failed range-state growth preserves whole committed state");
    TEST_ASSERT_EQ(state_info.usage, kAgcResourceUsageUndefined,
        "failed range-state growth preserves committed usage");
    probe.fail_on_attempt = 0u;
    TEST_ASSERT_EQ(agcCmdTransitionResources(command, 1u, &transition),
        AGC_OK, "range-state transition retries after allocation failure");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
        "unsubmitted range-state transition resets without publication");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command), AGC_OK,
        "range-state callback command destruction succeeds");
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
    AgcFenceInfo fence_info = AGC_FENCE_INFO_INIT;

    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "unsignaled fence creation succeeds");
    fence_info.reserved[0] = 1u;
    TEST_ASSERT_EQ(agcGetFenceInfo(fence, &fence_info),
        AGC_ERROR_INVALID_ARGUMENT,
        "fence info rejects nonzero reserved fields");
    fence_info.reserved[0] = 0u;
    TEST_ASSERT_EQ(agcGetFenceInfo(fence, &fence_info), AGC_OK,
        "unsignaled fence info query succeeds");
    TEST_ASSERT_EQ(fence_info.state, AGC_FENCE_STATE_UNSIGNALED,
        "new fence diagnostic state is unsignaled");
    TEST_ASSERT_EQ(fence_info.queue_type, UINT32_MAX,
        "new fence has no submission owner");
    TEST_ASSERT_EQ(fence_info.completion_value, 1u,
        "new fence diagnostic reports its expected completion marker");
    TEST_ASSERT_EQ(fence_info.observed_completion_value, 0u,
        "new fence has not observed a completion marker");
    TEST_ASSERT_EQ(fence_info.last_wait_result, AGC_ERROR_BUSY,
        "new fence diagnostic reports no successful wait");
    TEST_ASSERT(strcmp(fence_info.profile_name, "generic-host") == 0,
        "fence diagnostic names the active runtime profile");
    TEST_ASSERT_EQ(agcGetFenceStatus(fence), AGC_ERROR_BUSY,
        "unsignaled fence status is busy");
    TEST_ASSERT_EQ(agcWaitFence(fence, 0u), AGC_ERROR_TIMEOUT,
        "zero-duration finite fence wait times out");
    TEST_ASSERT_EQ(agcWaitFence(fence, 1000u), AGC_ERROR_TIMEOUT,
        "positive finite fence wait times out");
    TEST_ASSERT_EQ(agcWaitFence(fence, AGC_RUNTIME_INFINITE_TIMEOUT),
        AGC_ERROR_INVALID_ARGUMENT,
        "infinite fence wait is rejected");
    TEST_ASSERT_EQ(agcGetFenceInfo(fence, &fence_info), AGC_OK,
        "timed-out fence info query succeeds");
    TEST_ASSERT_EQ(fence_info.timeout_count, 2u,
        "fence diagnostic counts bounded timeouts");
    TEST_ASSERT_EQ(fence_info.last_timeout_ns, 1000u,
        "fence diagnostic records the most recent timeout deadline");
    TEST_ASSERT_EQ(fence_info.last_wait_result, AGC_ERROR_TIMEOUT,
        "fence diagnostic reports its most recent timeout");

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
    TEST_ASSERT_EQ(agcEndCommandBuffer(command_buffer), AGC_OK,
        "empty command buffer becomes executable for fence-only submission");
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
    AgcBufferDesc argument_desc = AGC_BUFFER_DESC_INIT;
    AgcResourceTransition argument_transition =
        AGC_RESOURCE_TRANSITION_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcComputePipeline pipeline = NULL;
    AgcCommandBuffer command_buffer = NULL;
    AgcBuffer argument_buffer = NULL;
    AgcFence fence = NULL;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcFenceInfo fence_info = AGC_FENCE_INFO_INIT;
    const AgcCommandBufferSubmit *captured;
    const uint32_t *words;
    uint32_t defaults[512] = {0};
    SceAgcCb defaults_cb;
    uint32_t owner = UINT32_MAX;
    const uint32_t indirect_groups[3] = {5u, 4u, 3u};

    pipeline_desc.shader = shader;
    pipeline_desc.local_size_x = 64u;
    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 1024u;
    argument_desc.size = 16u;
    argument_desc.usage = AGC_BUFFER_USAGE_INDIRECT_BIT;
    argument_desc.flags = AGC_BUFFER_CREATE_UPLOAD_BIT;
    TEST_ASSERT_EQ(agcCreateComputePipeline(device, &pipeline_desc, &pipeline),
        AGC_OK, "compute pipeline creation succeeds");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
        &command_buffer), AGC_OK, "compute command buffer creation succeeds");
    TEST_ASSERT_EQ(agcCreateBuffer(device, &argument_desc, &argument_buffer),
        AGC_OK, "indirect argument buffer creation succeeds");
    TEST_ASSERT_EQ(agcWriteBuffer(argument_buffer, 4u, indirect_groups,
        sizeof(indirect_groups)), AGC_OK,
        "indirect dispatch record uploads at an aligned offset");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "compute fence creation succeeds");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command_buffer), AGC_OK,
        "compute command buffer begins");
    TEST_ASSERT_EQ(agcCmdDispatch(command_buffer, 1u, 1u, 1u),
        AGC_ERROR_INVALID_STATE, "dispatch requires a bound pipeline");
    TEST_ASSERT_EQ(agcCmdBindComputePipeline(command_buffer, pipeline), AGC_OK,
        "compute pipeline bind succeeds");
    TEST_ASSERT_EQ(agcCmdDispatchIndirect(command_buffer, argument_buffer,
        4u), AGC_ERROR_INVALID_STATE,
        "indirect dispatch requires graphics-readable argument state");
    argument_transition.resource_type = kAgcResourceTypeBuffer;
    argument_transition.buffer = argument_buffer;
    argument_transition.buffer_offset = 4u;
    argument_transition.buffer_size = sizeof(indirect_groups);
    argument_transition.before = kAgcResourceUsageUndefined;
    argument_transition.after = kAgcResourceUsageShaderRead;
    argument_transition.before_owner = kAgcResourceOwnerHost;
    argument_transition.after_owner = kAgcResourceOwnerCompute;
    TEST_ASSERT_EQ(agcCmdTransitionResources(command_buffer, 1u,
        &argument_transition), AGC_OK,
        "indirect dispatch range transitions to compute ownership");
    TEST_ASSERT_EQ(agcCmdDispatchIndirect(command_buffer, argument_buffer,
        2u), AGC_ERROR_INVALID_ARGUMENT,
        "indirect dispatch rejects a non-dword offset");
    TEST_ASSERT_EQ(agcCmdDispatchIndirect(command_buffer, argument_buffer,
        8u), AGC_ERROR_INVALID_ARGUMENT,
        "indirect dispatch rejects a truncated 12-byte record");
    TEST_ASSERT_EQ(agcCmdDispatch(command_buffer, 3u, 2u, 1u), AGC_OK,
        "compute dispatch records");
    TEST_ASSERT_EQ(agcCmdDispatchIndirect(command_buffer, argument_buffer,
        4u), AGC_OK, "compute indirect dispatch records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command_buffer), AGC_OK,
        "compute command buffer becomes executable");
    TEST_ASSERT_EQ(agcDestroyComputePipeline(pipeline), AGC_ERROR_BUSY,
        "recorded compute pipeline cannot be destroyed");
    TEST_ASSERT_EQ(agcDestroyBuffer(argument_buffer), AGC_ERROR_BUSY,
        "recorded indirect buffer is retained until command reset");

    submit.command_buffer_count = 1u;
    submit.command_buffers = &command_buffer;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "compute command buffer submits");
    captured = agcDriverDebugLastAcbSubmit(&owner);
    agcCbInit(&defaults_cb, defaults, sizeof(defaults));
    TEST_ASSERT_EQ(agcGfx1013ApplyComputeDefaultsV8(&defaults_cb, NULL),
        AGC_OK, "qualified compute defaults build for runtime comparison");
    words = (const uint32_t *)(uintptr_t)captured->command_address;
    TEST_ASSERT_EQ(agcPm4Opcode(words[0]),
        AGC_PM4_OP_SET_SH_REG,
        "runtime compute submission begins with qualified SH defaults");
    TEST_ASSERT(captured->dword_count > 36u,
        "compute submission includes qualified defaults and dispatch state");
    TEST_ASSERT_EQ(captured->dword_count,
        2u * agcCbUsedDwords(&defaults_cb) + 74u,
        "runtime compute submission contains defaults plus direct and indirect dispatches");
    TEST_ASSERT_EQ(captured->command_address & 0xffu, 0u,
        "compute submission command allocation is GPU-address aligned");
    words += captured->dword_count - 3u;
    TEST_ASSERT_EQ((words[0] >> 8) & 0xffu, AGC_PM4_OP_DISPATCH_INDIRECT,
        "compute submission records DISPATCH_INDIRECT");
    TEST_ASSERT_EQ(words[0] & 1u, 1u,
        "compute dispatch carries shader-type bit");
    TEST_ASSERT_EQ(words[1], 4u,
        "compute indirect dispatch records its dword-aligned byte offset");
    TEST_ASSERT_EQ(agcGetFenceStatus(fence), AGC_OK,
        "successful compute submission signals fence");
    TEST_ASSERT_EQ(agcWaitFence(fence, 1u), AGC_OK,
        "finite wait observes signaled compute fence");
    TEST_ASSERT_EQ(agcGetFenceInfo(fence, &fence_info), AGC_OK,
        "completed compute fence info query succeeds");
    TEST_ASSERT_EQ(fence_info.state, AGC_FENCE_STATE_SIGNALED,
        "completed compute fence diagnostic state is signaled");
    TEST_ASSERT_EQ(fence_info.queue_type, kAgcQueueCompute,
        "completed compute fence records its queue owner");
    TEST_ASSERT_EQ(fence_info.command_buffer_state,
        AGC_COMMAND_BUFFER_STATE_EXECUTABLE,
        "completed compute fence records released command ownership");
    TEST_ASSERT_EQ(fence_info.submission_id, 1u,
        "completed compute fence records its queue submission identity");
    TEST_ASSERT_EQ(fence_info.last_completed_submission_id, 1u,
        "completed compute fence records its completed submission identity");
    TEST_ASSERT_EQ(fence_info.completion_value, 1u,
        "completed compute fence reports its expected marker");
    TEST_ASSERT_EQ(fence_info.observed_completion_value, 1u,
        "completed compute fence reports its observed marker");
    TEST_ASSERT_EQ(fence_info.last_wait_result, AGC_OK,
        "completed compute fence records successful wait status");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "completed compute fence resets");
    TEST_ASSERT_EQ(agcGetFenceInfo(fence, &fence_info), AGC_OK,
        "reset compute fence info query succeeds");
    TEST_ASSERT_EQ(fence_info.state, AGC_FENCE_STATE_UNSIGNALED,
        "reset compute fence clears its signal state");
    TEST_ASSERT_EQ(fence_info.observed_completion_value, 0u,
        "reset compute fence clears its observed marker");
    TEST_ASSERT_EQ(fence_info.submission_id, 1u,
        "reset compute fence preserves latest submission identity");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command_buffer), AGC_OK,
        "completed compute command buffer resets");

    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK, "compute fence destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command_buffer), AGC_OK,
        "compute command buffer destroys");
    TEST_ASSERT_EQ(agcDestroyComputePipeline(pipeline), AGC_OK,
        "compute pipeline destroys after reset");
    TEST_ASSERT_EQ(agcDestroyBuffer(argument_buffer), AGC_OK,
        "indirect argument buffer destroys after reset");
    TEST_ASSERT_EQ(agcDestroyShader(shader), AGC_OK, "compute shader destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK, "compute queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK, "compute device destroys");
}

static void test_runtime_compute_on_graphics_queue(void)
{
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueGraphics);
    AgcShader shader = create_shader(device, kAgcShaderStageCs);
    AgcComputePipelineDesc pipeline_desc = AGC_COMPUTE_PIPELINE_DESC_INIT;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcResourceTransition transition = AGC_RESOURCE_TRANSITION_INIT;
    AgcComputePipeline pipeline = NULL;
    AgcBuffer storage = NULL;
    AgcCommandBuffer command = NULL;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    const AgcCommandBufferSubmit *captured;
    const uint32_t *words;

    pipeline_desc.shader = shader;
    pipeline_desc.local_size_x = 64u;
    buffer_desc.size = 256u;
    buffer_desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT;
    command_desc.queue_type = kAgcQueueGraphics;
    command_desc.capacity_dwords = 4096u;
    TEST_ASSERT_EQ(agcCreateComputePipeline(device, &pipeline_desc,
        &pipeline), AGC_OK,
        "graphics-queue compute pipeline creates");
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &storage), AGC_OK,
        "graphics-queue compute storage buffer creates");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &command),
        AGC_OK, "graphics command buffer for compute creates");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "graphics command buffer for compute begins");
    transition.after = kAgcResourceUsageShaderWrite;
    transition.after_owner = kAgcResourceOwnerGraphics;
    transition.buffer = storage;
    transition.buffer_size = buffer_desc.size;
    TEST_ASSERT_EQ(agcCmdTransitionResources(command, 1u, &transition),
        AGC_OK,
        "graphics command buffer accepts compute shader-write state");
    TEST_ASSERT_EQ(agcCmdBindComputePipeline(command, pipeline), AGC_OK,
        "graphics command buffer accepts compute pipeline");
    TEST_ASSERT_EQ(agcCmdDispatch(command, 7u, 3u, 1u), AGC_OK,
        "graphics command stream records compute dispatch in order");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command), AGC_OK,
        "graphics command stream with compute ends");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &command;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, NULL), AGC_OK,
        "graphics queue submits compute packets through one DCB");
    captured = agcDriverDebugLastDcbSubmit();
    TEST_ASSERT(captured != NULL && captured->dword_count >= 5u,
        "graphics-queue compute submission is captured as a DCB");
    words = (const uint32_t *)(uintptr_t)captured->command_address +
        captured->dword_count - 5u;
    TEST_ASSERT_EQ(agcPm4Opcode(words[0]), AGC_PM4_OP_DISPATCH_DIRECT,
        "graphics DCB ends with compute dispatch");
    TEST_ASSERT_EQ(words[0] & 1u, 1u,
        "graphics DCB compute dispatch retains the shader-type bit");
    TEST_ASSERT_EQ(words[1], 7u,
        "graphics DCB compute dispatch preserves group count X");
    TEST_ASSERT_EQ(words[2], 3u,
        "graphics DCB compute dispatch preserves group count Y");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
        "graphics-queue compute command resets");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command), AGC_OK,
        "graphics-queue compute command destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(storage), AGC_OK,
        "graphics-queue compute storage buffer destroys");
    TEST_ASSERT_EQ(agcDestroyComputePipeline(pipeline), AGC_OK,
        "graphics-queue compute pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyShader(shader), AGC_OK,
        "graphics-queue compute shader destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK,
        "graphics queue used for compute destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "graphics-queue compute device destroys");
}

static void test_runtime_empty_submission_eop_diagnostic(void)
{
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueCompute);
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcCommandBuffer command_buffer = NULL;
    AgcFence fence = NULL;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    const AgcCommandBufferSubmit *captured;
    const uint32_t *words;
    uint32_t owner = UINT32_MAX;

    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 2u;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
        &command_buffer), AGC_OK, "EOP-only diagnostic command buffer creates");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "EOP-only diagnostic fence creates");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command_buffer), AGC_OK,
        "EOP-only diagnostic command buffer begins");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command_buffer), AGC_OK,
        "empty EOP-only diagnostic command buffer becomes executable");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &command_buffer;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "EOP-only diagnostic submits");
    captured = agcDriverDebugLastAcbSubmit(&owner);
    TEST_ASSERT(captured != NULL && owner != UINT32_MAX,
        "EOP-only diagnostic captures the generic compute carrier");
    words = (const uint32_t *)(uintptr_t)captured->command_address;
    TEST_ASSERT_EQ(captured->dword_count, 2u,
        "EOP-only diagnostic uses the generic two-dword no-op carrier");
    TEST_ASSERT_EQ(agcPm4Opcode(words[0]), AGC_PM4_OP_NOP,
        "EOP-only diagnostic generic carrier is a no-op");
    TEST_ASSERT(!runtime_has_opcode(words, captured->dword_count,
        AGC_PM4_OP_DISPATCH_DIRECT),
        "EOP-only diagnostic contains no workload dispatch");
    TEST_ASSERT_EQ(agcGetFenceStatus(fence), AGC_OK,
        "EOP-only diagnostic host fence signals");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command_buffer), AGC_OK,
        "EOP-only diagnostic command buffer resets");
    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK,
        "EOP-only diagnostic fence destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command_buffer), AGC_OK,
        "EOP-only diagnostic command buffer destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK,
        "EOP-only diagnostic queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "EOP-only diagnostic device destroys");
}

static void test_runtime_compiler_reflection_sidecar(void)
{
    const uint32_t push_constants[] = {64u, UINT32_C(0xff00ff00)};
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueCompute);
    AgcShaderReflection reflection;
    AgcShaderDesc shader_desc = AGC_SHADER_DESC_INIT;
    AgcComputePipelineDesc pipeline_desc = AGC_COMPUTE_PIPELINE_DESC_INIT;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcDescriptorWrite write = AGC_DESCRIPTOR_WRITE_INIT;
    AgcResourceTransition transition = AGC_RESOURCE_TRANSITION_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcShader shader = NULL;
    AgcComputePipeline pipeline = NULL;
    AgcBuffer buffer = NULL;
    AgcCommandBuffer command_buffer = NULL;
    AgcFence fence = NULL;

    TEST_ASSERT_EQ(fill_color_native_reflection_bytes_size,
        sizeof(reflection),
        "serialized native compute reflection retains the ABI size");
    memcpy(&reflection, fill_color_native_reflection_bytes,
        sizeof(reflection));
    TEST_ASSERT_EQ(reflection.stage, kAgcShaderStageCs,
        "serialized native compute reflection identifies compute stage");
    TEST_ASSERT_EQ(reflection.descriptor_mapping_count, 1u,
        "serialized native compute reflection retains its storage binding");
    TEST_ASSERT_EQ(reflection.push_constant_size, sizeof(push_constants),
        "serialized native compute reflection retains push constants");

    shader_desc.stage = reflection.stage;
    shader_desc.code = fill_color_native_data;
    shader_desc.code_size = fill_color_native_data_len;
    shader_desc.reflection = &reflection;
    TEST_ASSERT_EQ(agcCreateShader(device, &shader_desc, &shader), AGC_OK,
        "serialized compute artifact creates a native shader");

    pipeline_desc.shader = shader;
    pipeline_desc.local_size_x = reflection.local_size_x;
    pipeline_desc.local_size_y = reflection.local_size_y;
    pipeline_desc.local_size_z = reflection.local_size_z;
    pipeline_desc.descriptor_mapping_count =
        reflection.descriptor_mapping_count;
    pipeline_desc.descriptor_mappings = reflection.descriptor_mappings;
    pipeline_desc.push_constant_range_count =
        reflection.push_constant_range_count;
    pipeline_desc.push_constant_ranges = reflection.push_constant_ranges;
    TEST_ASSERT_EQ(agcCreateComputePipeline(device, &pipeline_desc, &pipeline),
        AGC_OK, "serialized compute artifact creates a native pipeline");

    buffer_desc.size = 256u;
    buffer_desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT;
    buffer_desc.flags = AGC_BUFFER_CREATE_READBACK_BIT;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &buffer), AGC_OK,
        "native compute output buffer creation succeeds");
    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 512u;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
        &command_buffer), AGC_OK,
        "native compute sidecar command buffer creation succeeds");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "native compute sidecar fence creation succeeds");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command_buffer), AGC_OK,
        "native compute sidecar command buffer begins");
    TEST_ASSERT_EQ(agcCmdBindComputePipeline(command_buffer, pipeline), AGC_OK,
        "native compute sidecar pipeline binds");
    write.set = reflection.descriptor_mappings[0].set;
    write.binding = reflection.descriptor_mappings[0].binding;
    write.type = reflection.descriptor_mappings[0].type;
    write.buffer = buffer;
    write.buffer_range = buffer_desc.size;
    TEST_ASSERT_EQ(agcCmdBindDescriptors(command_buffer, 1u, &write),
        AGC_ERROR_INVALID_STATE,
        "native compute sidecar rejects untransitioned storage descriptor");
    transition.resource_type = kAgcResourceTypeBuffer;
    transition.buffer = buffer;
    transition.buffer_offset = 64u;
    transition.buffer_size = 128u;
    transition.before = kAgcResourceUsageUndefined;
    transition.after = kAgcResourceUsageShaderWrite;
    transition.before_owner = kAgcResourceOwnerHost;
    transition.after_owner = kAgcResourceOwnerCompute;
    TEST_ASSERT_EQ(agcCmdTransitionResources(command_buffer, 1u, &transition),
        AGC_OK, "native compute sidecar storage state records");
    TEST_ASSERT_EQ(agcCmdBindDescriptors(command_buffer, 1u, &write),
        AGC_ERROR_INVALID_STATE,
        "descriptor rejects a range wider than transitioned bytes");
    write.buffer_offset = transition.buffer_offset;
    write.buffer_range = transition.buffer_size;
    TEST_ASSERT_EQ(agcCmdBindDescriptors(command_buffer, 1u, &write), AGC_OK,
        "descriptor accepts its exact transitioned byte range");
    TEST_ASSERT_EQ(agcCmdPushConstants(command_buffer,
        1u << kAgcShaderStageCs, 0u, sizeof(push_constants),
        push_constants), AGC_OK,
        "native compute sidecar push constants bind");
    TEST_ASSERT_EQ(agcCmdDispatch(command_buffer, 1u, 1u, 1u), AGC_OK,
        "native compute sidecar dispatch records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command_buffer), AGC_OK,
        "native compute sidecar command buffer ends");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &command_buffer;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "native compute sidecar command buffer submits");
    TEST_ASSERT_EQ(agcGetFenceStatus(fence), AGC_OK,
        "generic native compute sidecar submission signals its fence");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command_buffer), AGC_OK,
        "completed native compute sidecar command buffer resets");
    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK,
        "native compute sidecar fence destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command_buffer), AGC_OK,
        "native compute sidecar command buffer destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(buffer), AGC_OK,
        "native compute sidecar output buffer destroys");
    TEST_ASSERT_EQ(agcDestroyComputePipeline(pipeline), AGC_OK,
        "native compute sidecar pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyShader(shader), AGC_OK,
        "native compute sidecar shader destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK,
        "native compute sidecar queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "native compute sidecar device destroys");
}

static void test_runtime_compiler_graphics_sidecar(void)
{
    static const float vertices[] = {
        -0.75f, -0.75f, 0.0f, 1.0f, 0.0f, 0.0f,
         0.75f, -0.75f, 0.0f, 0.0f, 1.0f, 0.0f,
         0.00f,  0.75f, 0.0f, 0.0f, 0.0f, 1.0f,
    };
    static const uint16_t indices[] = {0u, 1u, 2u};
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueGraphics);
    AgcShaderDesc vertex_desc = AGC_SHADER_DESC_INIT;
    AgcShaderDesc pixel_desc = AGC_SHADER_DESC_INIT;
    AgcGraphicsPipelineDesc pipeline_desc = AGC_GRAPHICS_PIPELINE_DESC_INIT;
    AgcColorBlendAttachmentState attachments[2] = {
        AGC_COLOR_BLEND_ATTACHMENT_STATE_INIT,
        AGC_COLOR_BLEND_ATTACHMENT_STATE_INIT,
    };
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcVertexBufferBinding vertex_binding = AGC_VERTEX_BUFFER_BINDING_INIT;
    AgcResourceTransition target_transitions[2] = {
        AGC_RESOURCE_TRANSITION_INIT,
        AGC_RESOURCE_TRANSITION_INIT,
    };
    AgcResourceTransition buffer_transition = AGC_RESOURCE_TRANSITION_INIT;
    AgcColorTargetBinding targets[2] = {
        AGC_COLOR_TARGET_BINDING_INIT,
        AGC_COLOR_TARGET_BINDING_INIT,
    };
    AgcViewport viewport = AGC_VIEWPORT_INIT;
    AgcScissor scissor = AGC_SCISSOR_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcShaderReflection vertex_reflection;
    AgcShaderReflection pixel_reflection;
    AgcShader vertex = NULL;
    AgcShader pixel = NULL;
    AgcGraphicsPipeline pipeline = NULL;
    AgcBuffer vertex_buffer = NULL;
    AgcBuffer index_buffer = NULL;
    AgcImage first_image = NULL;
    AgcImage second_image = NULL;
    AgcCommandBuffer command = NULL;
    const AgcCommandBufferSubmit *captured;
    const uint32_t *words;
    uint32_t expected_defaults[2184] = {0};
    SceAgcCb defaults_cb;
    uint32_t value;

    TEST_ASSERT_EQ(runtime_triangle_vert_reflection_bytes_size,
        sizeof(vertex_reflection),
        "native graphics vertex reflection sidecar retains ABI size");
    TEST_ASSERT_EQ(runtime_triangle_frag_reflection_bytes_size,
        sizeof(pixel_reflection),
        "native graphics fragment reflection sidecar retains ABI size");
    memcpy(&vertex_reflection, runtime_triangle_vert_reflection_bytes,
        sizeof(vertex_reflection));
    memcpy(&pixel_reflection, runtime_triangle_frag_reflection_bytes,
        sizeof(pixel_reflection));
    TEST_ASSERT_EQ(vertex_reflection.stage, kAgcShaderStageVs,
        "native graphics vertex reflection identifies VS");
    TEST_ASSERT_EQ(pixel_reflection.stage, kAgcShaderStagePs,
        "native graphics fragment reflection identifies PS");
    TEST_ASSERT_EQ(vertex_reflection.vertex_input_count, 2u,
        "native graphics vertex reflection carries both attributes");
    TEST_ASSERT_EQ(pixel_reflection.color_export_count, 2u,
        "native graphics fragment reflection carries both color exports");

    vertex_desc.stage = vertex_reflection.stage;
    vertex_desc.code = runtime_triangle_vert_back_data;
    vertex_desc.code_size = runtime_triangle_vert_back_data_len;
    vertex_desc.front_code = runtime_triangle_vert_front_data;
    vertex_desc.front_code_size = runtime_triangle_vert_front_data_len;
    vertex_desc.reflection = &vertex_reflection;
    TEST_ASSERT_EQ(agcCreateShader(device, &vertex_desc, &vertex), AGC_OK,
        "compiler-sidecar NGG vertex shader creates");
    pixel_desc.stage = pixel_reflection.stage;
    pixel_desc.code = runtime_triangle_frag_data;
    pixel_desc.code_size = runtime_triangle_frag_data_len;
    pixel_desc.reflection = &pixel_reflection;
    TEST_ASSERT_EQ(agcCreateShader(device, &pixel_desc, &pixel), AGC_OK,
        "compiler-sidecar fragment shader creates");
    attachments[0].format = AGC_FORMAT_RGBA8_UNORM;
    attachments[1].format = AGC_FORMAT_RGBA8_UNORM;
    pipeline_desc.vertex_shader = vertex;
    pipeline_desc.pixel_shader = pixel;
    pipeline_desc.vertex_inputs = vertex_reflection.vertex_inputs;
    pipeline_desc.vertex_input_count = vertex_reflection.vertex_input_count;
    pipeline_desc.color_attachments = attachments;
    pipeline_desc.color_attachment_count = 2u;
    pipeline_desc.dynamic_state_mask = AGC_DYNAMIC_STATE_VIEWPORT_BIT |
        AGC_DYNAMIC_STATE_SCISSOR_BIT;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &pipeline_desc, &pipeline),
        AGC_OK, "compiler-sidecar graphics pipeline creates");

    buffer_desc.size = sizeof(vertices) + 24u;
    buffer_desc.usage = AGC_BUFFER_USAGE_VERTEX_BIT;
    buffer_desc.flags = AGC_BUFFER_CREATE_UPLOAD_BIT;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &vertex_buffer), AGC_OK,
        "compiler-sidecar vertex buffer creates");
    TEST_ASSERT_EQ(agcWriteBuffer(vertex_buffer, 24u, vertices,
        sizeof(vertices)), AGC_OK, "compiler-sidecar vertex data uploads");
    buffer_desc.size = sizeof(indices) + 2u;
    buffer_desc.usage = AGC_BUFFER_USAGE_INDEX_BIT;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &index_buffer), AGC_OK,
        "compiler-sidecar index buffer creates");
    TEST_ASSERT_EQ(agcWriteBuffer(index_buffer, 2u, indices, sizeof(indices)),
        AGC_OK, "compiler-sidecar index data uploads");
    image_desc.width = 64u;
    image_desc.height = 64u;
    image_desc.format = AGC_FORMAT_RGBA8_UNORM;
    image_desc.usage = AGC_IMAGE_USAGE_COLOR_TARGET_BIT;
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &first_image), AGC_OK,
        "compiler-sidecar first color target creates");
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &second_image), AGC_OK,
        "compiler-sidecar second color target creates");
    command_desc.capacity_dwords = 4096u;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &command),
        AGC_OK, "compiler-sidecar graphics command buffer creates");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "compiler-sidecar graphics command buffer begins");
    TEST_ASSERT_EQ(agcCmdBindGraphicsPipeline(command, pipeline), AGC_OK,
        "compiler-sidecar graphics pipeline binds");
    vertex_binding.buffer = vertex_buffer;
    vertex_binding.offset = 24u;
    vertex_binding.stride = 24u;
    TEST_ASSERT_EQ(agcCmdBindVertexBuffers(command, 1u, &vertex_binding),
        AGC_ERROR_INVALID_STATE,
        "untransitioned vertex buffer cannot bind on graphics");
    buffer_transition.resource_type = kAgcResourceTypeBuffer;
    buffer_transition.buffer = vertex_buffer;
    buffer_transition.buffer_offset = 24u;
    buffer_transition.buffer_size = sizeof(vertices);
    buffer_transition.before = kAgcResourceUsageUndefined;
    buffer_transition.after = kAgcResourceUsageShaderRead;
    buffer_transition.before_owner = kAgcResourceOwnerHost;
    buffer_transition.after_owner = kAgcResourceOwnerGraphics;
    TEST_ASSERT_EQ(agcCmdTransitionResources(command, 1u,
        &buffer_transition), AGC_OK,
        "compiler-sidecar vertex byte range transitions to graphics read");
    TEST_ASSERT_EQ(agcCmdBindVertexBuffers(command, 1u, &vertex_binding),
        AGC_OK, "compiler-sidecar vertex table binds");
    targets[0].image = first_image;
    targets[1].image = second_image;
    target_transitions[0].resource_type = kAgcResourceTypeImage;
    target_transitions[0].image = first_image;
    target_transitions[0].before = kAgcResourceUsageUndefined;
    target_transitions[0].after = kAgcResourceUsageColorTarget;
    target_transitions[0].before_owner = kAgcResourceOwnerHost;
    target_transitions[0].after_owner = kAgcResourceOwnerGraphics;
    target_transitions[1] = target_transitions[0];
    target_transitions[1].image = second_image;
    TEST_ASSERT_EQ(agcCmdTransitionResources(command, 2u,
        target_transitions), AGC_OK,
        "compiler-sidecar color targets transition to graphics ownership");
    TEST_ASSERT_EQ(agcCmdBindColorTargets(command, 2u, targets), AGC_OK,
        "compiler-sidecar MRT targets bind");
    viewport.width = 64.0f;
    viewport.height = 64.0f;
    scissor.width = 64u;
    scissor.height = 64u;
    TEST_ASSERT_EQ(agcCmdSetViewport(command, &viewport), AGC_OK,
        "compiler-sidecar viewport binds");
    TEST_ASSERT_EQ(agcCmdSetScissor(command, &scissor), AGC_OK,
        "compiler-sidecar scissor binds");
    TEST_ASSERT_EQ(agcCmdDraw(command, 3u, 1u, 0u, 0u), AGC_OK,
        "compiler-sidecar non-indexed draw records");
    TEST_ASSERT_EQ(agcCmdBindIndexBuffer(command, index_buffer, 2u,
        kAgcIndexSize16), AGC_ERROR_INVALID_STATE,
        "untransitioned index buffer cannot bind on graphics");
    buffer_transition.buffer = index_buffer;
    buffer_transition.buffer_offset = 2u;
    buffer_transition.buffer_size = sizeof(indices);
    TEST_ASSERT_EQ(agcCmdTransitionResources(command, 1u,
        &buffer_transition), AGC_OK,
        "compiler-sidecar index byte range transitions to graphics read");
    TEST_ASSERT_EQ(agcCmdBindIndexBuffer(command, index_buffer, 0u,
        kAgcIndexSize16), AGC_ERROR_INVALID_STATE,
        "index bind rejects bytes outside the transitioned tail range");
    TEST_ASSERT_EQ(agcCmdBindIndexBuffer(command, index_buffer, 2u,
        kAgcIndexSize16), AGC_OK, "compiler-sidecar index buffer binds");
    TEST_ASSERT_EQ(agcCmdDrawIndexed(command, 3u, 1u, 0u, 0, 0u), AGC_OK,
        "compiler-sidecar indexed draw records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command), AGC_OK,
        "compiler-sidecar graphics command buffer becomes executable");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &command;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, NULL), AGC_OK,
        "compiler-sidecar graphics command buffer submits on host");
    captured = agcDriverDebugLastDcbSubmit();
    words = (const uint32_t *)(uintptr_t)captured->command_address;
    agcCbInit(&defaults_cb, expected_defaults, sizeof(expected_defaults));
    TEST_ASSERT_EQ(agcGfx1013ApplyGraphicsDefaultsV8(&defaults_cb, NULL),
        AGC_OK, "native graphics default fixture emits");
    TEST_ASSERT_EQ(agcCbUsedDwords(&defaults_cb), 2184u,
        "native graphics default fixture has qualified V8 size");
    TEST_ASSERT(memcmp(words, expected_defaults, sizeof(expected_defaults)) == 0,
        "native graphics submission begins with qualified V8 defaults");
    TEST_ASSERT(runtime_find_context_register(words, captured->dword_count,
        AGC_REG_CB_COLOR0_BASE, &value) && value != 0u,
        "compiler-sidecar submission includes its first color target");
    TEST_ASSERT(runtime_find_context_register(words, captured->dword_count,
        AGC_REG_CB_COLOR0_BASE + 15u, &value) && value != 0u,
        "compiler-sidecar submission includes its second color target");
    TEST_ASSERT(runtime_find_context_register(words, captured->dword_count,
        AGC_REG_SPI_SHADER_COL_FORMAT, &value) && value == 0x44u,
        "compiler-sidecar pipeline derives its two reflected color formats");
    TEST_ASSERT(runtime_find_shader_register(words, captured->dword_count,
        AGC_REG_SPI_SHADER_USER_DATA_GS_0, &value) && value != 0u,
        "compiler-sidecar submission publishes its vertex table address");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
        "compiler-sidecar command reset releases recorded objects");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command), AGC_OK,
        "compiler-sidecar command buffer destroys");
    TEST_ASSERT_EQ(agcDestroyImage(second_image), AGC_OK,
        "compiler-sidecar second color target destroys");
    TEST_ASSERT_EQ(agcDestroyImage(first_image), AGC_OK,
        "compiler-sidecar first color target destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(index_buffer), AGC_OK,
        "compiler-sidecar index buffer destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(vertex_buffer), AGC_OK,
        "compiler-sidecar vertex buffer destroys");
    TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(pipeline), AGC_OK,
        "compiler-sidecar graphics pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyShader(pixel), AGC_OK,
        "compiler-sidecar fragment shader destroys");
    TEST_ASSERT_EQ(agcDestroyShader(vertex), AGC_OK,
        "compiler-sidecar vertex shader destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK,
        "compiler-sidecar graphics queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "compiler-sidecar graphics device destroys");
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
    command_desc.capacity_dwords = 4096u;
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
    TEST_ASSERT_EQ(runtime_transition_buffer_to_graphics_read(command_buffer,
        index_buffer, buffer_desc.size, kAgcResourceUsageUndefined, 0u),
        AGC_OK, "index buffer transitions to graphics read");
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
    TEST_ASSERT_EQ(captured->dword_count, 2287u,
        "indexed graphics submission includes qualified defaults and draw");
    words = (const uint32_t *)(uintptr_t)captured->command_address;
    TEST_ASSERT_EQ(captured->command_address & 0xffu, 0u,
        "graphics submission command allocation is GPU-address aligned");
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

static void test_runtime_multi_graphics_submission(void)
{
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueGraphics);
    AgcShader vertex = create_shader(device, kAgcShaderStageVs);
    AgcShader pixel = create_shader(device, kAgcShaderStagePs);
    AgcGraphicsPipelineDesc pipeline_desc = AGC_GRAPHICS_PIPELINE_DESC_INIT;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcSubmitInfo single_submit = AGC_SUBMIT_INFO_INIT;
    AgcSubmitInfo list_submit = AGC_SUBMIT_INFO_V2_INIT;
    AgcGpuLabelDesc label_desc = AGC_GPU_LABEL_DESC_INIT;
    AgcGpuLabelInfo label_info = AGC_GPU_LABEL_INFO_INIT;
    AgcGpuLabelPoint waits[1] = { AGC_GPU_LABEL_POINT_INIT };
    AgcGpuLabelPoint signals[1] = { AGC_GPU_LABEL_POINT_INIT };
    AgcGraphicsPipeline pipeline = NULL;
    AgcBuffer index_buffer = NULL;
    AgcCommandBuffer first = NULL;
    AgcCommandBuffer second = NULL;
    AgcCommandBuffer commands[2];
    AgcFence fence = NULL;
    AgcGpuLabel source_label = NULL;
    AgcGpuLabel destination_label = NULL;
    AgcFenceInfo fence_info = AGC_FENCE_INFO_INIT;
    AgcCommandBufferState state;

    pipeline_desc.vertex_shader = vertex;
    pipeline_desc.pixel_shader = pixel;
    buffer_desc.size = 64u;
    buffer_desc.usage = AGC_BUFFER_USAGE_INDEX_BIT;
    command_desc.capacity_dwords = 4096u;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &pipeline_desc, &pipeline),
        AGC_OK, "multi-submit graphics pipeline creates");
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &index_buffer), AGC_OK,
        "multi-submit index buffer creates");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &first), AGC_OK,
        "first multi-submit command creates");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &second), AGC_OK,
        "second multi-submit command creates");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "multi-submit fence creates");
    commands[0] = first;
    commands[1] = second;
    for (uint32_t i = 0u; i < 2u; ++i) {
        TEST_ASSERT_EQ(agcBeginCommandBuffer(commands[i]), AGC_OK,
            "multi-submit command begins");
        TEST_ASSERT_EQ(agcCmdBindGraphicsPipeline(commands[i], pipeline), AGC_OK,
            "multi-submit graphics pipeline binds");
        TEST_ASSERT_EQ(runtime_transition_buffer_to_graphics_read(commands[i],
            index_buffer, buffer_desc.size, i == 0u ?
            kAgcResourceUsageUndefined : kAgcResourceUsageShaderRead,
            i == 0u ? 0u : AGC_RESOURCE_TRANSITION_BATCH_DEPENDENCY_BIT),
            AGC_OK, "multi-submit index buffer transitions to graphics read");
        TEST_ASSERT_EQ(agcCmdBindIndexBuffer(commands[i], index_buffer, 0u,
            kAgcIndexSize16), AGC_OK, "multi-submit index buffer binds");
        TEST_ASSERT_EQ(agcCmdDrawIndexed(commands[i], 3u, 1u, 0u, 0, 0u),
            AGC_OK, "multi-submit indexed draw records");
        TEST_ASSERT_EQ(agcEndCommandBuffer(commands[i]), AGC_OK,
            "multi-submit command ends");
    }
    submit.command_buffer_count = 2u;
    submit.command_buffers = commands;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "two graphics command buffers submit as one frame");
    TEST_ASSERT_EQ(agcGetFenceInfo(fence, &fence_info), AGC_OK,
        "multi-submit fence diagnostics query succeeds");
    TEST_ASSERT_EQ(fence_info.state, AGC_FENCE_STATE_SIGNALED,
        "generic multi-submit fence completes");
    TEST_ASSERT_EQ(fence_info.submission_id, 1u,
        "multi-submit consumes one queue submission identity");
    TEST_ASSERT_EQ(fence_info.last_completed_submission_id, 1u,
        "multi-submit reports completed batch identity");
    for (uint32_t i = 0u; i < 2u; ++i) {
        TEST_ASSERT_EQ(agcGetCommandBufferState(commands[i], &state), AGC_OK,
            "completed multi-submit command state queries");
        TEST_ASSERT_EQ(state, AGC_COMMAND_BUFFER_STATE_EXECUTABLE,
            "completed multi-submit command is released");
        TEST_ASSERT_EQ(agcResetCommandBuffer(commands[i]), AGC_OK,
            "completed multi-submit command resets");
    }
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "completed multi-submit fence resets");
    TEST_ASSERT_EQ(agcCreateGpuLabel(device, &label_desc, &source_label),
        AGC_OK, "multi-submit source label creates");
    TEST_ASSERT_EQ(agcCreateGpuLabel(device, &label_desc, &destination_label),
        AGC_OK, "multi-submit destination label creates");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(first), AGC_OK,
        "multi-submit list producer begins");
    TEST_ASSERT_EQ(agcCmdSignalGpuLabel(first, source_label, 1u), AGC_OK,
        "multi-submit list producer signal records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(first), AGC_OK,
        "multi-submit list producer ends");
    single_submit.command_buffer_count = 1u;
    single_submit.command_buffers = &first;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &single_submit, fence), AGC_OK,
        "multi-submit list producer submits");
    TEST_ASSERT_EQ(agcResetCommandBuffer(first), AGC_OK,
        "multi-submit list producer resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "multi-submit list producer fence resets");
    for (uint32_t i = 0u; i < 2u; ++i) {
        TEST_ASSERT_EQ(agcBeginCommandBuffer(commands[i]), AGC_OK,
            "multi-submit list command begins");
        TEST_ASSERT_EQ(agcCmdBindGraphicsPipeline(commands[i], pipeline), AGC_OK,
            "multi-submit list graphics pipeline binds");
        TEST_ASSERT_EQ(runtime_transition_buffer_to_graphics_read(commands[i],
            index_buffer, buffer_desc.size, kAgcResourceUsageShaderRead, 0u),
            AGC_OK,
            "multi-submit list index buffer retains graphics-read state");
        TEST_ASSERT_EQ(agcCmdBindIndexBuffer(commands[i], index_buffer, 0u,
            kAgcIndexSize16), AGC_OK,
            "multi-submit list index buffer binds");
        TEST_ASSERT_EQ(agcCmdDrawIndexed(commands[i], 3u, 1u, 0u, 0, 0u),
            AGC_OK, "multi-submit list indexed draw records");
        TEST_ASSERT_EQ(agcEndCommandBuffer(commands[i]), AGC_OK,
            "multi-submit list command ends");
    }
    waits[0].label = source_label;
    waits[0].value = 1u;
    signals[0].label = destination_label;
    signals[0].value = 2u;
    list_submit.command_buffer_count = 2u;
    list_submit.command_buffers = commands;
    list_submit.wait_count = 1u;
    list_submit.waits = waits;
    list_submit.signal_count = 1u;
    list_submit.signals = signals;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &list_submit, fence), AGC_OK,
        "multi-submit list atomically injects first wait and final signal");
    TEST_ASSERT_EQ(agcGetGpuLabelInfo(destination_label, &label_info), AGC_OK,
        "multi-submit list destination label diagnostics query succeeds");
    TEST_ASSERT_EQ(label_info.scheduled_value, 2u,
        "multi-submit list commits final signal only after batch submit");
    for (uint32_t i = 0u; i < 2u; ++i) {
        TEST_ASSERT_EQ(agcResetCommandBuffer(commands[i]), AGC_OK,
            "multi-submit list command resets");
    }
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "multi-submit list fence resets");
    commands[1] = commands[0];
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence),
        AGC_ERROR_INVALID_ARGUMENT,
        "duplicate multi-submit command buffer rejects before mutation");
    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK,
        "multi-submit fence destroys");
    TEST_ASSERT_EQ(agcDestroyGpuLabel(destination_label), AGC_OK,
        "multi-submit destination label destroys");
    TEST_ASSERT_EQ(agcDestroyGpuLabel(source_label), AGC_OK,
        "multi-submit source label destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(second), AGC_OK,
        "second multi-submit command destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(first), AGC_OK,
        "first multi-submit command destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(index_buffer), AGC_OK,
        "multi-submit index buffer destroys");
    TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(pipeline), AGC_OK,
        "multi-submit graphics pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyShader(pixel), AGC_OK,
        "multi-submit pixel shader destroys");
    TEST_ASSERT_EQ(agcDestroyShader(vertex), AGC_OK,
        "multi-submit vertex shader destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK,
        "multi-submit graphics queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "multi-submit device destroys");
}

static void test_runtime_multi_compute_submission(void)
{
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueCompute);
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcGpuLabelDesc label_desc = AGC_GPU_LABEL_DESC_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcSubmitInfo list_submit = AGC_SUBMIT_INFO_V2_INIT;
    AgcGpuLabelPoint waits[1] = { AGC_GPU_LABEL_POINT_INIT };
    AgcGpuLabelPoint signals[1] = { AGC_GPU_LABEL_POINT_INIT };
    AgcGpuLabelInfo label_info = AGC_GPU_LABEL_INFO_INIT;
    AgcCommandBuffer producer = NULL;
    AgcCommandBuffer first = NULL;
    AgcCommandBuffer last = NULL;
    AgcCommandBuffer commands[2] = { NULL, NULL };
    AgcFence fence = NULL;
    AgcGpuLabel source = NULL;
    AgcGpuLabel destination = NULL;
    AgcGpuLabel first_body = NULL;
    AgcGpuLabel last_body = NULL;
    AgcCommandBufferState state;

    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 64u;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &producer),
        AGC_OK, "compute multi-submit producer command creates");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &first),
        AGC_OK, "compute multi-submit first command creates");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &last),
        AGC_OK, "compute multi-submit last command creates");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "compute multi-submit fence creates");
    TEST_ASSERT_EQ(agcCreateGpuLabel(device, &label_desc, &source), AGC_OK,
        "compute multi-submit source label creates");
    TEST_ASSERT_EQ(agcCreateGpuLabel(device, &label_desc, &destination), AGC_OK,
        "compute multi-submit destination label creates");
    TEST_ASSERT_EQ(agcCreateGpuLabel(device, &label_desc, &first_body), AGC_OK,
        "compute multi-submit first body label creates");
    TEST_ASSERT_EQ(agcCreateGpuLabel(device, &label_desc, &last_body), AGC_OK,
        "compute multi-submit last body label creates");

    TEST_ASSERT_EQ(agcBeginCommandBuffer(producer), AGC_OK,
        "compute multi-submit producer begins");
    TEST_ASSERT_EQ(agcCmdSignalGpuLabel(producer, source, 1u), AGC_OK,
        "compute multi-submit producer signal records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(producer), AGC_OK,
        "compute multi-submit producer ends");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &producer;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "compute multi-submit producer submits");
    TEST_ASSERT_EQ(agcResetCommandBuffer(producer), AGC_OK,
        "compute multi-submit producer resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "compute multi-submit producer fence resets");

    TEST_ASSERT_EQ(agcBeginCommandBuffer(first), AGC_OK,
        "compute multi-submit first begins");
    TEST_ASSERT_EQ(agcCmdSignalGpuLabel(first, first_body, 1u), AGC_OK,
        "compute multi-submit first body signal records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(first), AGC_OK,
        "compute multi-submit first ends");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(last), AGC_OK,
        "compute multi-submit last begins");
    TEST_ASSERT_EQ(agcCmdSignalGpuLabel(last, last_body, 1u), AGC_OK,
        "compute multi-submit last body signal records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(last), AGC_OK,
        "compute multi-submit last ends");
    commands[0] = first;
    commands[1] = last;
    waits[0].label = source;
    waits[0].value = 1u;
    signals[0].label = destination;
    signals[0].value = 2u;
    list_submit.command_buffer_count = 2u;
    list_submit.command_buffers = commands;
    list_submit.wait_count = 1u;
    list_submit.waits = waits;
    list_submit.signal_count = 1u;
    list_submit.signals = signals;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &list_submit, fence), AGC_OK,
        "compute batch inserts first wait and final signal atomically");
    TEST_ASSERT_EQ(agcGetGpuLabelInfo(destination, &label_info), AGC_OK,
        "compute batch destination label diagnostics query succeeds");
    TEST_ASSERT_EQ(label_info.scheduled_value, 2u,
        "compute batch commits final list signal after submit");
    TEST_ASSERT_EQ(agcGetCommandBufferState(first, &state), AGC_OK,
        "compute batch first command state queries");
    TEST_ASSERT_EQ(state, AGC_COMMAND_BUFFER_STATE_EXECUTABLE,
        "generic compute batch first command completes");
    TEST_ASSERT_EQ(agcResetCommandBuffer(first), AGC_OK,
        "compute batch first command resets");
    TEST_ASSERT_EQ(agcResetCommandBuffer(last), AGC_OK,
        "compute batch last command resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "compute batch fence resets");

    TEST_ASSERT_EQ(agcBeginCommandBuffer(first), AGC_OK,
        "compute batch rollback first begins");
    TEST_ASSERT_EQ(agcCmdSignalGpuLabel(first, first_body, 2u), AGC_OK,
        "compute batch rollback first body signal records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(first), AGC_OK,
        "compute batch rollback first ends");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(last), AGC_OK,
        "compute batch rollback last begins");
    TEST_ASSERT_EQ(agcCmdSignalGpuLabel(last, last_body, 2u), AGC_OK,
        "compute batch rollback last body signal records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(last), AGC_OK,
        "compute batch rollback last ends");
    signals[0].value = 3u;
    agcDriverDebugFailNextSubmit(AGC_ERROR_SUBMIT_FAILED);
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &list_submit, fence),
        AGC_ERROR_SUBMIT_FAILED,
        "compute batch driver failure restores both list endpoints");
    TEST_ASSERT_EQ(agcGetGpuLabelInfo(destination, &label_info), AGC_OK,
        "compute batch rollback label diagnostics query succeeds");
    TEST_ASSERT_EQ(label_info.scheduled_value, 2u,
        "failed compute batch does not publish list signal");
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &list_submit, fence), AGC_OK,
        "compute batch retries after endpoint rollback");
    TEST_ASSERT_EQ(agcGetGpuLabelInfo(destination, &label_info), AGC_OK,
        "retried compute batch label diagnostics query succeeds");
    TEST_ASSERT_EQ(label_info.scheduled_value, 3u,
        "retried compute batch publishes next list signal");
    TEST_ASSERT_EQ(agcResetCommandBuffer(first), AGC_OK,
        "retried compute batch first command resets");
    TEST_ASSERT_EQ(agcResetCommandBuffer(last), AGC_OK,
        "retried compute batch last command resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "retried compute batch fence resets");

    TEST_ASSERT_EQ(agcDestroyGpuLabel(last_body), AGC_OK,
        "compute multi-submit last body label destroys");
    TEST_ASSERT_EQ(agcDestroyGpuLabel(first_body), AGC_OK,
        "compute multi-submit first body label destroys");
    TEST_ASSERT_EQ(agcDestroyGpuLabel(destination), AGC_OK,
        "compute multi-submit destination label destroys");
    TEST_ASSERT_EQ(agcDestroyGpuLabel(source), AGC_OK,
        "compute multi-submit source label destroys");
    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK,
        "compute multi-submit fence destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(last), AGC_OK,
        "compute multi-submit last command destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(first), AGC_OK,
        "compute multi-submit first command destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(producer), AGC_OK,
        "compute multi-submit producer command destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK,
        "compute multi-submit queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "compute multi-submit device destroys");
}

static void test_runtime_batch_transition_chain(void)
{
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueCompute);
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcGpuLabelDesc label_desc = AGC_GPU_LABEL_DESC_INIT;
    AgcResourceTransition first_transition = AGC_RESOURCE_TRANSITION_INIT;
    AgcResourceTransition second_transition = AGC_RESOURCE_TRANSITION_V2_INIT;
    AgcResourceTransition final_transition = AGC_RESOURCE_TRANSITION_INIT;
    AgcSubmitInfo batch_submit = AGC_SUBMIT_INFO_INIT;
    AgcSubmitInfo single_submit = AGC_SUBMIT_INFO_INIT;
    AgcBuffer buffer = NULL;
    AgcCommandBuffer first = NULL;
    AgcCommandBuffer second = NULL;
    AgcCommandBuffer invalid = NULL;
    AgcCommandBuffer commands[2] = { NULL, NULL };
    AgcFence fence = NULL;
    AgcGpuLabel body_label = NULL;

    buffer_desc.size = 64u;
    buffer_desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT;
    buffer_desc.flags = AGC_BUFFER_CREATE_READBACK_BIT;
    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 128u;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &buffer), AGC_OK,
        "batch transition buffer creates");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &first), AGC_OK,
        "batch transition first command creates");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &second), AGC_OK,
        "batch transition second command creates");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &invalid), AGC_OK,
        "batch transition invalid command creates");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "batch transition fence creates");
    TEST_ASSERT_EQ(agcCreateGpuLabel(device, &label_desc, &body_label), AGC_OK,
        "batch transition body label creates");

    first_transition.resource_type = kAgcResourceTypeBuffer;
    first_transition.buffer = buffer;
    first_transition.buffer_offset = 16u;
    first_transition.buffer_size = 32u;
    first_transition.before = kAgcResourceUsageUndefined;
    first_transition.after = kAgcResourceUsageShaderWrite;
    first_transition.before_owner = kAgcResourceOwnerHost;
    first_transition.after_owner = kAgcResourceOwnerCompute;
    second_transition.resource_type = kAgcResourceTypeBuffer;
    second_transition.buffer = buffer;
    second_transition.buffer_offset = 16u;
    second_transition.buffer_size = 32u;
    second_transition.before = kAgcResourceUsageShaderWrite;
    second_transition.after = kAgcResourceUsageHostRead;
    second_transition.before_owner = kAgcResourceOwnerCompute;
    second_transition.after_owner = kAgcResourceOwnerHost;
    second_transition.flags = AGC_RESOURCE_TRANSITION_BATCH_DEPENDENCY_BIT;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(first), AGC_OK,
        "batch transition first command begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(first, 1u, &first_transition),
        AGC_OK, "batch transition first state records");
    TEST_ASSERT_EQ(agcCmdSignalGpuLabel(first, body_label, 1u), AGC_OK,
        "batch transition first body signal records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(first), AGC_OK,
        "batch transition first command ends");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(second), AGC_OK,
        "batch transition dependent command begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(second, 1u, &second_transition),
        AGC_OK, "batch transition dependent state records explicitly");
    TEST_ASSERT_EQ(agcCmdSignalGpuLabel(second, body_label, 2u), AGC_OK,
        "batch transition dependent body signal records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(second), AGC_OK,
        "batch transition dependent command ends");
    commands[0] = second;
    commands[1] = first;
    batch_submit.command_buffer_count = 2u;
    batch_submit.command_buffers = commands;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &batch_submit, fence),
        AGC_ERROR_INVALID_STATE,
        "reversed batch transition dependency rejects before driver mutation");
    commands[0] = first;
    commands[1] = second;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &batch_submit, fence), AGC_OK,
        "ordered batch commits cross-command transition chain atomically");
    TEST_ASSERT_EQ(agcResetCommandBuffer(first), AGC_OK,
        "ordered batch first command resets");
    TEST_ASSERT_EQ(agcResetCommandBuffer(second), AGC_OK,
        "ordered batch dependent command resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "ordered batch fence resets");

    final_transition.resource_type = kAgcResourceTypeBuffer;
    final_transition.buffer = buffer;
    final_transition.buffer_offset = 16u;
    final_transition.buffer_size = 32u;
    final_transition.before = kAgcResourceUsageHostRead;
    final_transition.after = kAgcResourceUsageShaderWrite;
    final_transition.before_owner = kAgcResourceOwnerHost;
    final_transition.after_owner = kAgcResourceOwnerCompute;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(first), AGC_OK,
        "batch transition final-state command begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(first, 1u, &final_transition),
        AGC_OK, "ordered batch commits its final resource state");
    TEST_ASSERT_EQ(agcCmdSignalGpuLabel(first, body_label, 3u), AGC_OK,
        "post-batch state body signal records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(first), AGC_OK,
        "batch transition final-state command ends");
    single_submit.command_buffer_count = 1u;
    single_submit.command_buffers = &first;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &single_submit, fence), AGC_OK,
        "post-batch state transition submits");
    TEST_ASSERT_EQ(agcResetCommandBuffer(first), AGC_OK,
        "post-batch state command resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "post-batch state fence resets");

    TEST_ASSERT_EQ(agcBeginCommandBuffer(invalid), AGC_OK,
        "single dependency command begins");
    second_transition.before = kAgcResourceUsageShaderWrite;
    second_transition.after = kAgcResourceUsageHostRead;
    second_transition.before_owner = kAgcResourceOwnerCompute;
    second_transition.after_owner = kAgcResourceOwnerHost;
    TEST_ASSERT_EQ(agcCmdTransitionResources(invalid, 1u, &second_transition),
        AGC_OK, "explicit batch dependency records without global state mutation");
    TEST_ASSERT_EQ(agcEndCommandBuffer(invalid), AGC_OK,
        "single dependency command ends");
    single_submit.command_buffers = &invalid;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &single_submit, fence),
        AGC_ERROR_INVALID_STATE,
        "batch dependency rejects on a single command submission");
    TEST_ASSERT_EQ(agcResetCommandBuffer(invalid), AGC_OK,
        "rejected single dependency command resets");

    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK,
        "batch transition fence destroys");
    TEST_ASSERT_EQ(agcDestroyGpuLabel(body_label), AGC_OK,
        "batch transition body label destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(invalid), AGC_OK,
        "batch transition invalid command destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(second), AGC_OK,
        "batch transition second command destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(first), AGC_OK,
        "batch transition first command destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(buffer), AGC_OK,
        "batch transition buffer destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK,
        "batch transition queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "batch transition device destroys");
}

static void test_runtime_fence_driven_command_reuse(void)
{
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueCompute);
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcGpuLabelDesc label_desc = AGC_GPU_LABEL_DESC_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcCommandBuffer commands[2] = { NULL, NULL };
    AgcCommandBuffer invalid = NULL;
    AgcCommandBuffer invalid_batch[2];
    AgcCommandBuffer duplicates[2];
    AgcGpuLabel labels[2] = { NULL, NULL };
    AgcFence fence = NULL;
    AgcCommandBufferState state;
    uint32_t i;

    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 64u;
    for (i = 0u; i < 2u; ++i) {
        TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
            &commands[i]), AGC_OK, "recycle batch command creates");
        TEST_ASSERT_EQ(agcCreateGpuLabel(device, &label_desc, &labels[i]),
            AGC_OK, "recycle batch label creates");
        TEST_ASSERT_EQ(agcBeginCommandBuffer(commands[i]), AGC_OK,
            "recycle first-cycle command begins");
        TEST_ASSERT_EQ(agcCmdSignalGpuLabel(commands[i], labels[i], 1u),
            AGC_OK, "recycle first-cycle signal records");
        TEST_ASSERT_EQ(agcEndCommandBuffer(commands[i]), AGC_OK,
            "recycle first-cycle command ends");
    }
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &invalid),
        AGC_OK, "recycle invalid-member command creates");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "recycle fence creates");

    TEST_ASSERT_EQ(agcRecycleCommandBuffers(fence, 2u, commands),
        AGC_ERROR_BUSY,
        "unsignaled fence cannot recycle executable command storage");
    TEST_ASSERT_EQ(agcGetCommandBufferState(commands[0], &state), AGC_OK,
        "busy recycle command state query succeeds");
    TEST_ASSERT_EQ(state, AGC_COMMAND_BUFFER_STATE_EXECUTABLE,
        "busy recycle leaves command state unchanged");

    submit.command_buffer_count = 2u;
    submit.command_buffers = commands;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "recycle first batch submits");
    duplicates[0] = commands[0];
    duplicates[1] = commands[0];
    TEST_ASSERT_EQ(agcRecycleCommandBuffers(fence, 2u, duplicates),
        AGC_ERROR_INVALID_ARGUMENT,
        "recycle rejects duplicate command storage atomically");
    TEST_ASSERT_EQ(agcGetCommandBufferState(commands[0], &state), AGC_OK,
        "duplicate recycle command state query succeeds");
    TEST_ASSERT_EQ(state, AGC_COMMAND_BUFFER_STATE_EXECUTABLE,
        "duplicate recycle leaves the first command executable");

    TEST_ASSERT_EQ(agcBeginCommandBuffer(invalid), AGC_OK,
        "recycle invalid member enters recording state");
    invalid_batch[0] = commands[0];
    invalid_batch[1] = invalid;
    TEST_ASSERT_EQ(agcRecycleCommandBuffers(fence, 2u, invalid_batch),
        AGC_ERROR_INVALID_STATE,
        "recycle rejects a non-executable member atomically");
    TEST_ASSERT_EQ(agcGetCommandBufferState(commands[0], &state), AGC_OK,
        "invalid-member first command state query succeeds");
    TEST_ASSERT_EQ(state, AGC_COMMAND_BUFFER_STATE_EXECUTABLE,
        "invalid-member rejection preserves earlier command state");
    TEST_ASSERT_EQ(agcResetCommandBuffer(invalid), AGC_OK,
        "recycle invalid member resets independently");

    TEST_ASSERT_EQ(agcDestroyGpuLabel(labels[0]), AGC_ERROR_BUSY,
        "submitted command label remains retained before recycle");
    TEST_ASSERT_EQ(agcRecycleCommandBuffers(fence, 2u, commands), AGC_OK,
        "signaled fence recycles the complete command batch");
    for (i = 0u; i < 2u; ++i) {
        TEST_ASSERT_EQ(agcGetCommandBufferState(commands[i], &state), AGC_OK,
            "recycled command state query succeeds");
        TEST_ASSERT_EQ(state, AGC_COMMAND_BUFFER_STATE_INITIAL,
            "recycled command returns to initial state");
    }

    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "recycle fence resets for storage reuse");
    for (i = 0u; i < 2u; ++i) {
        TEST_ASSERT_EQ(agcBeginCommandBuffer(commands[i]), AGC_OK,
            "recycled command begins a second cycle");
        TEST_ASSERT_EQ(agcCmdSignalGpuLabel(commands[i], labels[i], 2u),
            AGC_OK, "recycled storage records a later timeline point");
        TEST_ASSERT_EQ(agcEndCommandBuffer(commands[i]), AGC_OK,
            "recycled second-cycle command ends");
    }
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "recycled command storage submits a second batch");
    TEST_ASSERT_EQ(agcRecycleCommandBuffers(fence, 2u, commands), AGC_OK,
        "second completed batch recycles without allocation replacement");

    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK,
        "recycle fence destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(invalid), AGC_OK,
        "recycle invalid-member command destroys");
    for (i = 0u; i < 2u; ++i) {
        TEST_ASSERT_EQ(agcDestroyGpuLabel(labels[i]), AGC_OK,
            "recycled command label destroys");
        TEST_ASSERT_EQ(agcDestroyCommandBuffer(commands[i]), AGC_OK,
            "recycled command destroys");
    }
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK,
        "recycle queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "recycle device destroys");
}

static void test_runtime_copy_buffer_submission(void)
{
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueCompute);
    AgcBufferDesc source_desc = AGC_BUFFER_DESC_INIT;
    AgcBufferDesc destination_desc = AGC_BUFFER_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcResourceTransition transitions[2] = {
        AGC_RESOURCE_TRANSITION_INIT, AGC_RESOURCE_TRANSITION_INIT
    };
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcBuffer source = NULL;
    AgcBuffer destination = NULL;
    AgcCommandBuffer command_buffer = NULL;
    AgcFence fence = NULL;
    AgcAllocationInfo command_allocation = AGC_ALLOCATION_INFO_INIT;
    const AgcCommandBufferSubmit *captured;
    const uint32_t *words;
    const uint32_t update_words[2] = {
        UINT32_C(0x11223344), UINT32_C(0x55667788)
    };
    uint32_t owner = UINT32_MAX;

    source_desc.size = 64u;
    source_desc.usage = AGC_BUFFER_USAGE_TRANSFER_SRC_BIT;
    destination_desc.size = 64u;
    destination_desc.usage = AGC_BUFFER_USAGE_TRANSFER_DST_BIT;
    destination_desc.flags = AGC_BUFFER_CREATE_READBACK_BIT;
    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 64u;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &source_desc, &source), AGC_OK,
        "copy source buffer creates");
    TEST_ASSERT_EQ(agcCreateBuffer(device, &destination_desc, &destination), AGC_OK,
        "copy destination buffer creates");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
        &command_buffer), AGC_OK, "copy command buffer creates");
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device,
        AGC_OBJECT_TYPE_COMMAND_BUFFER, command_buffer,
        &command_allocation), AGC_OK,
        "copy command buffer allocation is queryable");
    TEST_ASSERT_EQ(command_allocation.dedicated, 1u,
        "kernel-submitted command storage has an isolated mapping");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "copy fence creates");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command_buffer), AGC_OK,
        "copy command begins");
    TEST_ASSERT_EQ(agcCmdCopyBuffer(command_buffer, source, 0u, destination,
        0u, 64u), AGC_ERROR_INVALID_STATE,
        "copy rejects before typed transitions");
    transitions[0].resource_type = kAgcResourceTypeBuffer;
    transitions[0].buffer = source;
    transitions[0].buffer_offset = 16u;
    transitions[0].buffer_size = 32u;
    transitions[0].before = kAgcResourceUsageUndefined;
    transitions[0].after = kAgcResourceUsageCopySource;
    transitions[0].before_owner = kAgcResourceOwnerHost;
    transitions[0].after_owner = kAgcResourceOwnerCompute;
    transitions[1].resource_type = kAgcResourceTypeBuffer;
    transitions[1].buffer = destination;
    transitions[1].buffer_offset = 16u;
    transitions[1].buffer_size = 32u;
    transitions[1].before = kAgcResourceUsageUndefined;
    transitions[1].after = kAgcResourceUsageCopyDestination;
    transitions[1].before_owner = kAgcResourceOwnerHost;
    transitions[1].after_owner = kAgcResourceOwnerCompute;
    TEST_ASSERT_EQ(agcCmdTransitionResources(command_buffer, 2u, transitions),
        AGC_OK, "copy source and destination transitions record");
    TEST_ASSERT_EQ(agcCmdCopyBuffer(command_buffer, source, 16u, destination,
        16u, 32u), AGC_OK, "typed partial-range copy records DMA packet");
    TEST_ASSERT_EQ(agcCmdUpdateBuffer(command_buffer, destination, 16u,
        update_words, sizeof(update_words)), AGC_OK,
        "typed buffer update embeds data in a WRITE_DATA packet");
    TEST_ASSERT_EQ(agcCmdFillBuffer(command_buffer, destination, 24u, 8u,
        UINT32_C(0xa5a5a5a5)), AGC_OK,
        "typed buffer fill embeds a repeated WRITE_DATA payload");
    TEST_ASSERT_EQ(agcCmdCopyBuffer(command_buffer, source, 0u, destination,
        0u, 4u), AGC_ERROR_INVALID_STATE,
        "copy rejects bytes outside transitioned ranges");
    TEST_ASSERT_EQ(agcCmdUpdateBuffer(command_buffer, destination, 0u,
        update_words, sizeof(update_words)), AGC_ERROR_INVALID_STATE,
        "buffer update rejects bytes outside transitioned ranges");
    TEST_ASSERT_EQ(agcCmdFillBuffer(command_buffer, destination, 16u, 6u,
        0u), AGC_ERROR_INVALID_ARGUMENT,
        "buffer fill rejects a non-dword range without command mutation");
    TEST_ASSERT_EQ(agcCmdCopyBuffer(command_buffer, destination, 0u,
        destination, 4u, 32u), AGC_ERROR_INVALID_ARGUMENT,
        "overlapping copy rejects without command mutation");
    transitions[1].before = kAgcResourceUsageCopyDestination;
    transitions[1].after = kAgcResourceUsageHostRead;
    transitions[1].before_owner = kAgcResourceOwnerCompute;
    transitions[1].after_owner = kAgcResourceOwnerHost;
    TEST_ASSERT_EQ(agcCmdTransitionResources(command_buffer, 1u,
        &transitions[1]), AGC_OK, "copy destination host-read transition records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command_buffer), AGC_OK,
        "copy command ends");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &command_buffer;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "typed copy submits");
    captured = agcDriverDebugLastAcbSubmit(&owner);
    words = (const uint32_t *)(uintptr_t)captured->command_address;
    TEST_ASSERT(captured != NULL && owner != UINT32_MAX &&
        runtime_has_opcode(words, captured->dword_count, AGC_PM4_OP_DMA_DATA),
        "typed copy submit contains DMA_DATA");
    TEST_ASSERT(runtime_has_opcode(words, captured->dword_count,
        AGC_PM4_OP_WRITE_DATA),
        "typed update and fill submit contains WRITE_DATA");
    TEST_ASSERT_EQ(agcDestroyBuffer(source), AGC_ERROR_BUSY,
        "submitted copy retains source through command reset");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command_buffer), AGC_OK,
        "copy command resets");
    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK, "copy fence destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command_buffer), AGC_OK,
        "copy command buffer destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(destination), AGC_OK,
        "copy destination buffer destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(source), AGC_OK,
        "copy source buffer destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK, "copy queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK, "copy device destroys");
}

static void test_runtime_buffer_range_fragmentation(void)
{
    enum { kRangeCount = 32u, kRangeSize = 8u };
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueCompute);
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcResourceTransition transition = AGC_RESOURCE_TRANSITION_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcResourceStateInfo info = AGC_RESOURCE_STATE_INFO_INIT;
    AgcBuffer buffer = NULL;
    AgcCommandBuffer command = NULL;
    AgcFence fence = NULL;
    uint32_t i;

    buffer_desc.size = kRangeCount * kRangeSize;
    buffer_desc.usage = AGC_BUFFER_USAGE_TRANSFER_SRC_BIT;
    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 512u;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &buffer), AGC_OK,
        "fragmentation buffer creates");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &command),
        AGC_OK, "fragmentation command creates");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "fragmentation fence creates");

    transition.resource_type = kAgcResourceTypeBuffer;
    transition.buffer = buffer;
    transition.buffer_size = kRangeSize;
    transition.before = kAgcResourceUsageUndefined;
    transition.after = kAgcResourceUsageCopySource;
    transition.before_owner = kAgcResourceOwnerHost;
    transition.after_owner = kAgcResourceOwnerCompute;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "fragmentation split command begins");
    for (i = 0u; i < kRangeCount; i += 2u) {
        transition.buffer_offset = (uint64_t)i * kRangeSize;
        TEST_ASSERT_EQ(agcCmdTransitionResources(command, 1u, &transition),
            AGC_OK, "alternating partial range records");
    }
    TEST_ASSERT_EQ(agcEndCommandBuffer(command), AGC_OK,
        "fragmentation split command ends");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &command;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "alternating partial ranges submit atomically");
    TEST_ASSERT_EQ(agcGetBufferStateInfo(buffer, &info),
        AGC_ERROR_NOT_SUPPORTED,
        "alternating intervals reject ambiguous whole state");
    for (i = 0u; i < kRangeCount; ++i) {
        info = (AgcResourceStateInfo)AGC_RESOURCE_STATE_INFO_INIT;
        TEST_ASSERT_EQ(agcGetBufferRangeStateInfo(buffer,
            (uint64_t)i * kRangeSize, kRangeSize, &info), AGC_OK,
            "alternating interval query succeeds");
        TEST_ASSERT_EQ(info.usage, (i & 1u) != 0u ?
            kAgcResourceUsageUndefined : kAgcResourceUsageCopySource,
            "alternating interval preserves exact usage");
        TEST_ASSERT_EQ(info.owner, (i & 1u) != 0u ?
            kAgcResourceOwnerHost : kAgcResourceOwnerCompute,
            "alternating interval preserves exact owner");
    }
    TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
        "fragmentation split command resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "fragmentation split fence resets");

    transition.before = kAgcResourceUsageCopySource;
    transition.after = kAgcResourceUsageUndefined;
    transition.before_owner = kAgcResourceOwnerCompute;
    transition.after_owner = kAgcResourceOwnerHost;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "fragmentation merge command begins");
    for (i = 0u; i < kRangeCount; i += 2u) {
        transition.buffer_offset = (uint64_t)i * kRangeSize;
        TEST_ASSERT_EQ(agcCmdTransitionResources(command, 1u, &transition),
            AGC_OK, "alternating partial range discard records");
    }
    TEST_ASSERT_EQ(agcEndCommandBuffer(command), AGC_OK,
        "fragmentation merge command ends");
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "alternating partial ranges merge on submit");
    info = (AgcResourceStateInfo)AGC_RESOURCE_STATE_INFO_INIT;
    TEST_ASSERT_EQ(agcGetBufferStateInfo(buffer, &info), AGC_OK,
        "merged alternating intervals restore whole query");
    TEST_ASSERT_EQ(info.usage, kAgcResourceUsageUndefined,
        "merged alternating intervals restore undefined usage");
    TEST_ASSERT_EQ(info.owner, kAgcResourceOwnerHost,
        "merged alternating intervals restore host owner");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
        "fragmentation merge command resets");
    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK,
        "fragmentation fence destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command), AGC_OK,
        "fragmentation command destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(buffer), AGC_OK,
        "fragmentation buffer destroys with dynamic state storage");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK,
        "fragmentation queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "fragmentation device destroys");
}

static void test_runtime_image_subresource_states(void)
{
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueCompute);
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcResourceTransition transition = AGC_RESOURCE_TRANSITION_INIT;
    AgcImageSubresourceRange selected = {
        AGC_IMAGE_ASPECT_COLOR_BIT, 1u, 1u, 1u, 1u, 0u };
    AgcImageSubresourceRange neighbor = {
        AGC_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 1u, 1u, 0u };
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcResourceStateInfo info = AGC_RESOURCE_STATE_INFO_INIT;
    AgcImage image = NULL;
    AgcImage depth_stencil = NULL;
    AgcCommandBuffer command = NULL;
    AgcFence fence = NULL;

    image_desc.width = 8u;
    image_desc.height = 8u;
    image_desc.mip_levels = 3u;
    image_desc.array_layers = 2u;
    image_desc.format = AGC_FORMAT_RGBA8_UNORM;
    image_desc.usage = AGC_IMAGE_USAGE_SAMPLED_BIT |
        AGC_IMAGE_USAGE_TRANSFER_SRC_BIT;
    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 1024u;
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &image), AGC_OK,
        "subresource-state image creates");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &command),
        AGC_OK, "subresource-state command creates");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "subresource-state fence creates");

    transition.resource_type = kAgcResourceTypeImage;
    transition.image = image;
    transition.image_range = selected;
    transition.before = kAgcResourceUsageUndefined;
    transition.after = kAgcResourceUsageShaderRead;
    transition.before_owner = kAgcResourceOwnerHost;
    transition.after_owner = kAgcResourceOwnerCompute;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "partial image transition command begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(command, 1u, &transition),
        AGC_OK, "one image mip/layer transition records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command), AGC_OK,
        "partial image transition command ends");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &command;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "partial image transition submits");
    TEST_ASSERT_EQ(agcGetImageStateInfo(image, &info),
        AGC_ERROR_NOT_SUPPORTED,
        "fragmented image rejects ambiguous whole-state query");
    info = (AgcResourceStateInfo)AGC_RESOURCE_STATE_INFO_INIT;
    TEST_ASSERT_EQ(agcGetImageSubresourceStateInfo(image, &selected, &info),
        AGC_OK, "selected image subresource state query succeeds");
    TEST_ASSERT_EQ(info.usage, kAgcResourceUsageShaderRead,
        "selected image subresource preserves usage");
    TEST_ASSERT_EQ(info.owner, kAgcResourceOwnerCompute,
        "selected image subresource preserves owner");
    info = (AgcResourceStateInfo)AGC_RESOURCE_STATE_INFO_INIT;
    TEST_ASSERT_EQ(agcGetImageSubresourceStateInfo(image, &neighbor, &info),
        AGC_OK, "untouched image subresource state query succeeds");
    TEST_ASSERT_EQ(info.usage, kAgcResourceUsageUndefined,
        "untouched image subresource remains undefined");
    TEST_ASSERT_EQ(info.owner, kAgcResourceOwnerHost,
        "untouched image subresource remains host-owned");
    neighbor.aspect_mask = AGC_IMAGE_ASPECT_DEPTH_BIT;
    info = (AgcResourceStateInfo)AGC_RESOURCE_STATE_INFO_INIT;
    TEST_ASSERT_EQ(agcGetImageSubresourceStateInfo(image, &neighbor, &info),
        AGC_ERROR_INVALID_ARGUMENT,
        "image state query rejects an unsupported aspect");

    TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
        "partial image transition command resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "partial image transition fence resets");
    transition.before = kAgcResourceUsageShaderRead;
    transition.after = kAgcResourceUsageUndefined;
    transition.before_owner = kAgcResourceOwnerCompute;
    transition.after_owner = kAgcResourceOwnerHost;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "image subresource merge command begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(command, 1u, &transition),
        AGC_OK, "selected image subresource discard records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command), AGC_OK,
        "image subresource merge command ends");
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "image subresource merge submits");
    info = (AgcResourceStateInfo)AGC_RESOURCE_STATE_INFO_INIT;
    TEST_ASSERT_EQ(agcGetImageStateInfo(image, &info), AGC_OK,
        "merged image restores whole-state query");
    TEST_ASSERT_EQ(info.usage, kAgcResourceUsageUndefined,
        "merged image restores undefined usage");
    TEST_ASSERT_EQ(info.owner, kAgcResourceOwnerHost,
        "merged image restores host ownership");

    TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
        "image subresource merge command resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "image aspect transition fence resets");
    image_desc = (AgcImageDesc)AGC_IMAGE_DESC_INIT;
    image_desc.width = 4u;
    image_desc.height = 4u;
    image_desc.format = AGC_FORMAT_D32_FLOAT_S8_UINT;
    image_desc.usage = AGC_IMAGE_USAGE_DEPTH_STENCIL_BIT |
        AGC_IMAGE_USAGE_SAMPLED_BIT;
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &depth_stencil), AGC_OK,
        "depth-stencil aspect-state image creates");
    selected = (AgcImageSubresourceRange){ AGC_IMAGE_ASPECT_DEPTH_BIT,
        0u, 1u, 0u, 1u, 0u };
    transition.image = depth_stencil;
    transition.image_range = selected;
    transition.before = kAgcResourceUsageUndefined;
    transition.after = kAgcResourceUsageShaderRead;
    transition.before_owner = kAgcResourceOwnerHost;
    transition.after_owner = kAgcResourceOwnerCompute;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "depth aspect transition command begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(command, 1u, &transition),
        AGC_OK, "depth-only aspect transition records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command), AGC_OK,
        "depth aspect transition command ends");
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "depth-only aspect transition submits");
    info = (AgcResourceStateInfo)AGC_RESOURCE_STATE_INFO_INIT;
    TEST_ASSERT_EQ(agcGetImageSubresourceStateInfo(depth_stencil, &selected,
        &info), AGC_OK, "depth aspect state query succeeds");
    TEST_ASSERT_EQ(info.usage, kAgcResourceUsageShaderRead,
        "depth aspect preserves its exact usage");
    neighbor = (AgcImageSubresourceRange){ AGC_IMAGE_ASPECT_STENCIL_BIT,
        0u, 1u, 0u, 1u, 0u };
    info = (AgcResourceStateInfo)AGC_RESOURCE_STATE_INFO_INIT;
    TEST_ASSERT_EQ(agcGetImageSubresourceStateInfo(depth_stencil, &neighbor,
        &info), AGC_OK, "stencil aspect state query succeeds");
    TEST_ASSERT_EQ(info.usage, kAgcResourceUsageUndefined,
        "stencil aspect remains independently undefined");
    TEST_ASSERT_EQ(agcGetImageStateInfo(depth_stencil, &info),
        AGC_ERROR_NOT_SUPPORTED,
        "split depth-stencil aspects reject ambiguous whole query");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
        "depth aspect transition command resets");
    TEST_ASSERT_EQ(agcDestroyImage(depth_stencil), AGC_OK,
        "fragmented depth-stencil state storage destroys safely");
    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK,
        "subresource-state fence destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command), AGC_OK,
        "subresource-state command destroys");
    TEST_ASSERT_EQ(agcDestroyImage(image), AGC_OK,
        "subresource-state image destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK,
        "subresource-state queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "subresource-state device destroys");
}

static void test_runtime_copy_image_submission(void)
{
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueCompute);
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
    AgcImageDesc mismatch_desc = AGC_IMAGE_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcResourceTransition transitions[2] = {
        AGC_RESOURCE_TRANSITION_INIT, AGC_RESOURCE_TRANSITION_INIT
    };
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcImage source = NULL;
    AgcImage destination = NULL;
    AgcImage mismatch = NULL;
    AgcCommandBuffer command = NULL;
    AgcFence fence = NULL;
    const AgcCommandBufferSubmit *captured;
    const uint32_t *words;
    uint32_t owner = UINT32_MAX;
    uint32_t source_pixels[8u * 8u];

    for (uint32_t i = 0u; i < 8u * 8u; ++i)
        source_pixels[i] = UINT32_C(0xff000000) | i;
    image_desc.width = 1024u;
    image_desc.height = 1024u;
    image_desc.format = AGC_FORMAT_RGBA8_UNORM;
    image_desc.usage = AGC_IMAGE_USAGE_TRANSFER_SRC_BIT |
        AGC_IMAGE_USAGE_TRANSFER_DST_BIT;
    mismatch_desc = image_desc;
    mismatch_desc.width = 512u;
    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 128u;
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &source), AGC_OK,
        "image-copy source creates");
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &destination), AGC_OK,
        "image-copy destination creates");
    TEST_ASSERT_EQ(agcCreateImage(device, &mismatch_desc, &mismatch), AGC_OK,
        "image-copy mismatched destination creates");
    TEST_ASSERT_EQ(agcWriteImage(source, 0u, source_pixels,
        sizeof(source_pixels)), AGC_OK, "image-copy source upload succeeds");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &command),
        AGC_OK, "image-copy command creates");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "image-copy fence creates");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "image-copy command begins");
    TEST_ASSERT_EQ(agcCmdCopyImage(command, source, destination),
        AGC_ERROR_INVALID_STATE,
        "image copy rejects before typed transitions");
    TEST_ASSERT_EQ(agcCmdCopyImage(command, source, mismatch),
        AGC_ERROR_INVALID_ARGUMENT,
        "image copy rejects incompatible complete layouts");
    TEST_ASSERT_EQ(agcDestroyImage(mismatch), AGC_OK,
        "rejected image copy retains no mismatched image");
    mismatch = NULL;

    transitions[0].resource_type = kAgcResourceTypeImage;
    transitions[0].image = source;
    transitions[0].before = kAgcResourceUsageUndefined;
    transitions[0].after = kAgcResourceUsageCopySource;
    transitions[0].before_owner = kAgcResourceOwnerHost;
    transitions[0].after_owner = kAgcResourceOwnerCompute;
    transitions[1] = transitions[0];
    transitions[1].image = destination;
    transitions[1].after = kAgcResourceUsageCopyDestination;
    TEST_ASSERT_EQ(agcCmdTransitionResources(command, 2u, transitions),
        AGC_OK, "image-copy source and destination states record");
    TEST_ASSERT_EQ(agcCmdCopyImage(command, source, destination), AGC_OK,
        "typed whole-image copy records DMA packets");
    TEST_ASSERT_EQ(agcCmdCopyImage(command, source, source),
        AGC_ERROR_INVALID_ARGUMENT,
        "overlapping whole-image copy rejects without command mutation");
    transitions[1].before = kAgcResourceUsageCopyDestination;
    transitions[1].after = kAgcResourceUsageHostRead;
    transitions[1].before_owner = kAgcResourceOwnerCompute;
    transitions[1].after_owner = kAgcResourceOwnerHost;
    TEST_ASSERT_EQ(agcCmdTransitionResources(command, 1u, &transitions[1]),
        AGC_OK, "image-copy destination host-read state records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command), AGC_OK,
        "image-copy command ends");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &command;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "typed whole-image copy submits");
    captured = agcDriverDebugLastAcbSubmit(&owner);
    words = (const uint32_t *)(uintptr_t)captured->command_address;
    TEST_ASSERT(captured != NULL && owner != UINT32_MAX &&
        runtime_has_opcode(words, captured->dword_count, AGC_PM4_OP_DMA_DATA),
        "typed image copy submit contains DMA_DATA");
    TEST_ASSERT_EQ(runtime_count_opcode(words, captured->dword_count,
        AGC_PM4_OP_DMA_DATA), 3u,
        "four-megabyte image copy splits at the qualified packet limit");
    TEST_ASSERT_EQ(agcDestroyImage(source), AGC_ERROR_BUSY,
        "submitted image copy retains source until command reset");
    TEST_ASSERT_EQ(agcDestroyImage(destination), AGC_ERROR_BUSY,
        "submitted image copy retains destination until command reset");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
        "image-copy command resets");
    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK,
        "image-copy fence destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command), AGC_OK,
        "image-copy command destroys");
    TEST_ASSERT_EQ(agcDestroyImage(destination), AGC_OK,
        "image-copy destination destroys");
    TEST_ASSERT_EQ(agcDestroyImage(source), AGC_OK,
        "image-copy source destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK,
        "image-copy queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "image-copy device destroys");
}

static void test_runtime_image_region_and_buffer_copies(void)
{
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueCompute);
    AgcBufferDesc upload_desc = AGC_BUFFER_DESC_INIT;
    AgcBufferDesc readback_desc = AGC_BUFFER_DESC_INIT;
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcResourceTransition transitions[4] = {
        AGC_RESOURCE_TRANSITION_INIT, AGC_RESOURCE_TRANSITION_INIT,
        AGC_RESOURCE_TRANSITION_INIT, AGC_RESOURCE_TRANSITION_INIT
    };
    AgcBufferImageCopyRegion buffer_region =
        AGC_BUFFER_IMAGE_COPY_REGION_INIT;
    AgcImageCopyRegion image_region = AGC_IMAGE_COPY_REGION_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcBuffer upload = NULL;
    AgcBuffer readback = NULL;
    AgcImage first = NULL;
    AgcImage second = NULL;
    AgcCommandBuffer command = NULL;
    AgcFence fence = NULL;
    const AgcCommandBufferSubmit *captured;
    const uint32_t *words;
    uint32_t owner = UINT32_MAX;
    uint8_t source[256];
    uint8_t copied[256];

    for (uint32_t i = 0u; i < sizeof(source); ++i)
        source[i] = (uint8_t)(i ^ 0x5au);
    memset(copied, 0, sizeof(copied));
    upload_desc.size = sizeof(source);
    upload_desc.usage = AGC_BUFFER_USAGE_TRANSFER_SRC_BIT;
    upload_desc.flags = AGC_BUFFER_CREATE_UPLOAD_BIT;
    readback_desc.size = sizeof(copied);
    readback_desc.usage = AGC_BUFFER_USAGE_TRANSFER_DST_BIT;
    readback_desc.flags = AGC_BUFFER_CREATE_READBACK_BIT;
    image_desc.width = 8u;
    image_desc.height = 8u;
    image_desc.format = AGC_FORMAT_RGBA8_UNORM;
    image_desc.usage = AGC_IMAGE_USAGE_TRANSFER_SRC_BIT |
        AGC_IMAGE_USAGE_TRANSFER_DST_BIT;
    image_desc.tiling = AGC_IMAGE_TILING_LINEAR;
    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 1024u;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &upload_desc, &upload), AGC_OK,
        "region-copy upload buffer creates");
    TEST_ASSERT_EQ(agcCreateBuffer(device, &readback_desc, &readback), AGC_OK,
        "region-copy readback buffer creates");
    TEST_ASSERT_EQ(agcWriteBuffer(upload, 0u, source, sizeof(source)), AGC_OK,
        "region-copy upload buffer fills");
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &first), AGC_OK,
        "region-copy first image creates");
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &second), AGC_OK,
        "region-copy second image creates");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &command),
        AGC_OK, "region-copy command creates");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "region-copy fence creates");

    transitions[0].buffer = upload;
    transitions[0].buffer_size = sizeof(source);
    transitions[0].before = kAgcResourceUsageUndefined;
    transitions[0].after = kAgcResourceUsageCopySource;
    transitions[0].before_owner = kAgcResourceOwnerHost;
    transitions[0].after_owner = kAgcResourceOwnerCompute;
    transitions[1].resource_type = kAgcResourceTypeImage;
    transitions[1].image = first;
    transitions[1].image_range =
        (AgcImageSubresourceRange)AGC_IMAGE_SUBRESOURCE_RANGE_INIT;
    transitions[1].before = kAgcResourceUsageUndefined;
    transitions[1].after = kAgcResourceUsageCopyDestination;
    transitions[1].before_owner = kAgcResourceOwnerHost;
    transitions[1].after_owner = kAgcResourceOwnerCompute;
    transitions[2] = transitions[1];
    transitions[2].image = second;
    transitions[3].buffer = readback;
    transitions[3].buffer_size = sizeof(copied);
    transitions[3].before = kAgcResourceUsageUndefined;
    transitions[3].after = kAgcResourceUsageCopyDestination;
    transitions[3].before_owner = kAgcResourceOwnerHost;
    transitions[3].after_owner = kAgcResourceOwnerCompute;

    buffer_region.buffer_row_length = 8u;
    buffer_region.buffer_image_height = 4u;
    buffer_region.image_offset = (AgcOffset3D){2, 2, 0};
    buffer_region.image_extent = (AgcExtent3D){4u, 3u, 1u};
    image_region.source_offset = (AgcOffset3D){2, 2, 0};
    image_region.destination_offset = (AgcOffset3D){0, 1, 0};
    image_region.extent = (AgcExtent3D){4u, 3u, 1u};

    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "region-copy command begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(command, 4u, transitions),
        AGC_OK, "region-copy initial states record");
    TEST_ASSERT_EQ(agcCmdCopyBufferToImage(command, upload, first, 1u,
        &buffer_region), AGC_OK, "strided buffer-to-image rows record");
    transitions[1].before = kAgcResourceUsageCopyDestination;
    transitions[1].after = kAgcResourceUsageCopySource;
    transitions[1].before_owner = kAgcResourceOwnerCompute;
    TEST_ASSERT_EQ(agcCmdTransitionResources(command, 1u, &transitions[1]),
        AGC_OK, "first image becomes copy source");
    TEST_ASSERT_EQ(agcCmdCopyImageRegions(command, first, second, 1u,
        &image_region), AGC_OK, "offset image rows record");
    transitions[2].before = kAgcResourceUsageCopyDestination;
    transitions[2].after = kAgcResourceUsageCopySource;
    transitions[2].before_owner = kAgcResourceOwnerCompute;
    TEST_ASSERT_EQ(agcCmdTransitionResources(command, 1u, &transitions[2]),
        AGC_OK, "second image becomes copy source");
    buffer_region.buffer_offset = 128u;
    buffer_region.buffer_row_length = 0u;
    buffer_region.buffer_image_height = 0u;
    buffer_region.image_offset = (AgcOffset3D){0, 1, 0};
    TEST_ASSERT_EQ(agcCmdCopyImageToBuffer(command, second, readback, 1u,
        &buffer_region), AGC_OK, "image-to-buffer tight rows record");
    transitions[3].before = kAgcResourceUsageCopyDestination;
    transitions[3].after = kAgcResourceUsageHostRead;
    transitions[3].before_owner = kAgcResourceOwnerCompute;
    transitions[3].after_owner = kAgcResourceOwnerHost;
    transitions[3].buffer_offset = 128u;
    transitions[3].buffer_size = 48u;
    TEST_ASSERT_EQ(agcCmdTransitionResources(command, 1u, &transitions[3]),
        AGC_OK, "copied buffer rows become host-readable");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command), AGC_OK,
        "region-copy command ends");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &command;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "region-copy command submits");
    captured = agcDriverDebugLastAcbSubmit(&owner);
    words = (const uint32_t *)(uintptr_t)captured->command_address;
    TEST_ASSERT(captured != NULL && owner != UINT32_MAX,
        "region-copy submission is captured by the generic backend");
    TEST_ASSERT_EQ(runtime_count_opcode(words, captured->dword_count,
        AGC_PM4_OP_DMA_DATA), 9u,
        "three row copies are emitted for each transfer leg");
    TEST_ASSERT_EQ(agcReadBuffer(readback, 128u, copied + 128u, 48u), AGC_OK,
        "region-copy readback succeeds");

    TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
        "region-copy command resets");
    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK,
        "region-copy fence destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command), AGC_OK,
        "region-copy command destroys");
    TEST_ASSERT_EQ(agcDestroyImage(second), AGC_OK,
        "region-copy second image destroys");
    TEST_ASSERT_EQ(agcDestroyImage(first), AGC_OK,
        "region-copy first image destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(readback), AGC_OK,
        "region-copy readback buffer destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(upload), AGC_OK,
        "region-copy upload buffer destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK,
        "region-copy queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "region-copy device destroys");
}

static void test_runtime_compute_copy_shader_batch(void)
{
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueCompute);
    AgcShader shader = create_shader(device, kAgcShaderStageCs);
    AgcComputePipelineDesc pipeline_desc = AGC_COMPUTE_PIPELINE_DESC_INIT;
    AgcBufferDesc produced_desc = AGC_BUFFER_DESC_INIT;
    AgcBufferDesc copied_desc = AGC_BUFFER_DESC_INIT;
    AgcBufferDesc output_desc = AGC_BUFFER_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcResourceTransition transition = AGC_RESOURCE_TRANSITION_INIT;
    AgcResourceTransition dependency = AGC_RESOURCE_TRANSITION_V2_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcComputePipeline pipeline = NULL;
    AgcBuffer produced = NULL;
    AgcBuffer copied = NULL;
    AgcBuffer output = NULL;
    AgcCommandBuffer producer = NULL;
    AgcCommandBuffer copy = NULL;
    AgcCommandBuffer consumer = NULL;
    AgcCommandBuffer commands[3] = {NULL, NULL, NULL};
    AgcFence fence = NULL;

    pipeline_desc.shader = shader;
    pipeline_desc.local_size_x = 64u;
    produced_desc.size = 64u;
    produced_desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT |
        AGC_BUFFER_USAGE_TRANSFER_SRC_BIT;
    copied_desc.size = 64u;
    copied_desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT |
        AGC_BUFFER_USAGE_TRANSFER_DST_BIT;
    output_desc.size = 64u;
    output_desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT;
    output_desc.flags = AGC_BUFFER_CREATE_READBACK_BIT;
    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 1024u;
    TEST_ASSERT_EQ(agcCreateComputePipeline(device, &pipeline_desc, &pipeline),
        AGC_OK, "compute-copy-shader pipeline creates");
    TEST_ASSERT_EQ(agcCreateBuffer(device, &produced_desc, &produced), AGC_OK,
        "compute-copy-shader produced buffer creates");
    TEST_ASSERT_EQ(agcCreateBuffer(device, &copied_desc, &copied), AGC_OK,
        "compute-copy-shader copied buffer creates");
    TEST_ASSERT_EQ(agcCreateBuffer(device, &output_desc, &output), AGC_OK,
        "compute-copy-shader output buffer creates");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &producer),
        AGC_OK, "compute-copy-shader producer command creates");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &copy), AGC_OK,
        "compute-copy-shader copy command creates");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &consumer),
        AGC_OK, "compute-copy-shader consumer command creates");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "compute-copy-shader fence creates");

    transition.resource_type = kAgcResourceTypeBuffer;
    transition.buffer = produced;
    transition.buffer_size = produced_desc.size;
    transition.before = kAgcResourceUsageUndefined;
    transition.after = kAgcResourceUsageShaderWrite;
    transition.before_owner = kAgcResourceOwnerHost;
    transition.after_owner = kAgcResourceOwnerCompute;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(producer), AGC_OK,
        "compute-copy-shader producer begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(producer, 1u, &transition), AGC_OK,
        "compute-copy-shader producer state records");
    TEST_ASSERT_EQ(agcCmdBindComputePipeline(producer, pipeline), AGC_OK,
        "compute-copy-shader producer pipeline binds");
    TEST_ASSERT_EQ(agcCmdDispatch(producer, 1u, 1u, 1u), AGC_OK,
        "compute-copy-shader producer dispatch records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(producer), AGC_OK,
        "compute-copy-shader producer ends");

    dependency.resource_type = kAgcResourceTypeBuffer;
    dependency.buffer = produced;
    dependency.buffer_size = produced_desc.size;
    dependency.before = kAgcResourceUsageShaderWrite;
    dependency.after = kAgcResourceUsageCopySource;
    dependency.before_owner = kAgcResourceOwnerCompute;
    dependency.after_owner = kAgcResourceOwnerCompute;
    dependency.flags = AGC_RESOURCE_TRANSITION_BATCH_DEPENDENCY_BIT;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(copy), AGC_OK,
        "compute-copy-shader copy command begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(copy, 1u, &dependency), AGC_OK,
        "compute-copy-shader source batch dependency records");
    transition = (AgcResourceTransition)AGC_RESOURCE_TRANSITION_INIT;
    transition.resource_type = kAgcResourceTypeBuffer;
    transition.buffer = copied;
    transition.buffer_size = copied_desc.size;
    transition.before = kAgcResourceUsageUndefined;
    transition.after = kAgcResourceUsageCopyDestination;
    transition.before_owner = kAgcResourceOwnerHost;
    transition.after_owner = kAgcResourceOwnerCompute;
    TEST_ASSERT_EQ(agcCmdTransitionResources(copy, 1u, &transition), AGC_OK,
        "compute-copy-shader destination copy state records");
    TEST_ASSERT_EQ(agcCmdCopyBuffer(copy, produced, 0u, copied, 0u, 64u),
        AGC_OK, "compute-copy-shader typed copy records");
    transition.before = kAgcResourceUsageCopyDestination;
    transition.after = kAgcResourceUsageShaderRead;
    transition.before_owner = kAgcResourceOwnerCompute;
    transition.after_owner = kAgcResourceOwnerCompute;
    TEST_ASSERT_EQ(agcCmdTransitionResources(copy, 1u, &transition), AGC_OK,
        "compute-copy-shader copied shader-read state records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(copy), AGC_OK,
        "compute-copy-shader copy command ends");

    dependency = (AgcResourceTransition)AGC_RESOURCE_TRANSITION_V2_INIT;
    dependency.resource_type = kAgcResourceTypeBuffer;
    dependency.buffer = copied;
    dependency.buffer_size = copied_desc.size;
    dependency.before = kAgcResourceUsageShaderRead;
    dependency.after = kAgcResourceUsageShaderRead;
    dependency.before_owner = kAgcResourceOwnerCompute;
    dependency.after_owner = kAgcResourceOwnerCompute;
    dependency.flags = AGC_RESOURCE_TRANSITION_BATCH_DEPENDENCY_BIT;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(consumer), AGC_OK,
        "compute-copy-shader consumer command begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(consumer, 1u, &dependency), AGC_OK,
        "compute-copy-shader shader-read batch dependency records");
    transition = (AgcResourceTransition)AGC_RESOURCE_TRANSITION_INIT;
    transition.resource_type = kAgcResourceTypeBuffer;
    transition.buffer = output;
    transition.buffer_size = output_desc.size;
    transition.before = kAgcResourceUsageUndefined;
    transition.after = kAgcResourceUsageShaderWrite;
    transition.before_owner = kAgcResourceOwnerHost;
    transition.after_owner = kAgcResourceOwnerCompute;
    TEST_ASSERT_EQ(agcCmdTransitionResources(consumer, 1u, &transition), AGC_OK,
        "compute-copy-shader output state records");
    TEST_ASSERT_EQ(agcCmdBindComputePipeline(consumer, pipeline), AGC_OK,
        "compute-copy-shader consumer pipeline binds");
    TEST_ASSERT_EQ(agcCmdDispatch(consumer, 1u, 1u, 1u), AGC_OK,
        "compute-copy-shader consumer dispatch records");
    transition.before = kAgcResourceUsageShaderWrite;
    transition.after = kAgcResourceUsageHostRead;
    transition.before_owner = kAgcResourceOwnerCompute;
    transition.after_owner = kAgcResourceOwnerHost;
    TEST_ASSERT_EQ(agcCmdTransitionResources(consumer, 1u, &transition), AGC_OK,
        "compute-copy-shader output readback state records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(consumer), AGC_OK,
        "compute-copy-shader consumer command ends");

    commands[0] = producer;
    commands[1] = copy;
    commands[2] = consumer;
    submit.command_buffer_count = 3u;
    submit.command_buffers = commands;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "compute-copy-shader ordered batch submits");
    TEST_ASSERT_EQ(agcWaitFence(fence, 1u), AGC_OK,
        "compute-copy-shader batch fence completes");
    TEST_ASSERT_EQ(agcResetCommandBuffer(producer), AGC_OK,
        "compute-copy-shader producer resets");
    TEST_ASSERT_EQ(agcResetCommandBuffer(copy), AGC_OK,
        "compute-copy-shader copy command resets");
    TEST_ASSERT_EQ(agcResetCommandBuffer(consumer), AGC_OK,
        "compute-copy-shader consumer resets");
    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK,
        "compute-copy-shader fence destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(producer), AGC_OK,
        "compute-copy-shader producer command destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(copy), AGC_OK,
        "compute-copy-shader copy command destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(consumer), AGC_OK,
        "compute-copy-shader consumer command destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(output), AGC_OK,
        "compute-copy-shader output destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(copied), AGC_OK,
        "compute-copy-shader copied buffer destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(produced), AGC_OK,
        "compute-copy-shader produced buffer destroys");
    TEST_ASSERT_EQ(agcDestroyComputePipeline(pipeline), AGC_OK,
        "compute-copy-shader pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyShader(shader), AGC_OK,
        "compute-copy-shader shader destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK,
        "compute-copy-shader queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "compute-copy-shader device destroys");
}

static void test_runtime_gpu_labels(void)
{
    AgcDevice device = create_device();
    AgcQueueDesc queue_desc = AGC_QUEUE_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcGpuLabelDesc label_desc = AGC_GPU_LABEL_DESC_INIT;
    AgcGpuLabelInfo label_info = AGC_GPU_LABEL_INFO_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcQueue compute_queue = NULL;
    AgcQueue graphics_queue = NULL;
    AgcCommandBuffer producer = NULL;
    AgcCommandBuffer consumer = NULL;
    AgcCommandBuffer cross_consumer = NULL;
    AgcFence producer_fence = NULL;
    AgcFence consumer_fence = NULL;
    AgcGpuLabel label = NULL;
    AgcGpuLabel cross_label = NULL;
    const AgcCommandBufferSubmit *captured;
    const uint32_t *words;

    queue_desc.type = kAgcQueueCompute;
    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 128u;
    label_desc.initial_value = 0u;
    TEST_ASSERT_EQ(agcCreateQueue(device, &queue_desc, &compute_queue), AGC_OK,
        "label compute queue creates");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &producer),
        AGC_OK, "label producer command creates");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &consumer),
        AGC_OK, "label consumer command creates");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &producer_fence), AGC_OK,
        "label producer fence creates");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &consumer_fence), AGC_OK,
        "label consumer fence creates");
    TEST_ASSERT_EQ(agcCreateGpuLabel(device, &label_desc, &label), AGC_OK,
        "same-queue GPU label creates");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(producer), AGC_OK,
        "label producer begins");
    TEST_ASSERT_EQ(agcCmdSignalGpuLabel(producer, label, UINT32_C(0x1234)),
        AGC_OK, "label producer signal records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(producer), AGC_OK,
        "label producer ends");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(consumer), AGC_OK,
        "label consumer begins");
    TEST_ASSERT_EQ(agcCmdWaitGpuLabel(consumer, label, UINT32_C(0x1234)),
        AGC_OK, "label consumer wait records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(consumer), AGC_OK,
        "label consumer ends");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &consumer;
    TEST_ASSERT_EQ(agcQueueSubmit(compute_queue, &submit, consumer_fence),
        AGC_ERROR_INVALID_STATE,
        "label wait rejects before its producer has submitted");
    TEST_ASSERT_EQ(agcDestroyGpuLabel(label), AGC_ERROR_BUSY,
        "recorded label cannot be destroyed before command reset");
    submit.command_buffers = &producer;
    TEST_ASSERT_EQ(agcQueueSubmit(compute_queue, &submit, producer_fence),
        AGC_OK, "label producer submits");
    TEST_ASSERT_EQ(agcDestroyQueue(compute_queue), AGC_ERROR_BUSY,
        "submitted label retains its exact producer queue");
    TEST_ASSERT_EQ(agcGetGpuLabelInfo(label, &label_info), AGC_OK,
        "label diagnostics query succeeds");
    TEST_ASSERT_EQ(label_info.scheduled_value, UINT32_C(0x1234),
        "label diagnostics report the scheduled timeline point");
    TEST_ASSERT_EQ(label_info.observed_value, UINT32_C(0x1234),
        "generic label diagnostics report the completed point");
    TEST_ASSERT_EQ(label_info.last_signal_submission_id, 1u,
        "label diagnostics report the producer submission identity");
    TEST_ASSERT_EQ(agcResetCommandBuffer(producer), AGC_OK,
        "completed label producer resets before stale-value check");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(producer), AGC_OK,
        "stale-value producer begins");
    TEST_ASSERT_EQ(agcCmdSignalGpuLabel(producer, label, UINT32_C(0x1234)),
        AGC_ERROR_INVALID_STATE,
        "label rejects a signal value that could satisfy a stale wait");
    TEST_ASSERT_EQ(agcCmdSignalGpuLabel(producer, label, UINT32_C(0x1233)),
        AGC_ERROR_INVALID_STATE,
        "label rejects a decreasing timeline point");
    TEST_ASSERT_EQ(agcResetCommandBuffer(producer), AGC_OK,
        "stale-value producer resets");
    submit.command_buffers = &consumer;
    TEST_ASSERT_EQ(agcQueueSubmit(compute_queue, &submit, consumer_fence),
        AGC_OK, "same-queue label consumer submits");
    TEST_ASSERT_EQ(agcGetFenceStatus(producer_fence), AGC_OK,
        "generic label producer completes");
    TEST_ASSERT_EQ(agcGetFenceStatus(consumer_fence), AGC_OK,
        "generic label consumer completes");
    TEST_ASSERT_EQ(agcResetCommandBuffer(consumer), AGC_OK,
        "label consumer resets");
    TEST_ASSERT_EQ(agcResetCommandBuffer(producer), AGC_OK,
        "label producer resets");
    TEST_ASSERT_EQ(agcDestroyGpuLabel(label), AGC_OK,
        "label destroys after producer and consumer reset");
    label = NULL;

    TEST_ASSERT_EQ(agcCreateGpuLabel(device, &label_desc, &cross_label),
        AGC_OK, "cross-queue label creates");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(producer), AGC_OK,
        "cross-queue producer begins");
    TEST_ASSERT_EQ(agcCmdSignalGpuLabel(producer, cross_label, 7u), AGC_OK,
        "cross-queue producer signal records");
    TEST_ASSERT_EQ(agcCmdSignalGpuLabel(producer, cross_label, 8u), AGC_OK,
        "cross-queue producer advances beyond the consumer target");
    TEST_ASSERT_EQ(agcEndCommandBuffer(producer), AGC_OK,
        "cross-queue producer ends");
    TEST_ASSERT_EQ(agcResetFence(producer_fence), AGC_OK,
        "cross-queue producer fence resets");
    submit.command_buffers = &producer;
    TEST_ASSERT_EQ(agcQueueSubmit(compute_queue, &submit, producer_fence),
        AGC_OK, "cross-queue producer submits");
    queue_desc.type = kAgcQueueGraphics;
    command_desc.queue_type = kAgcQueueGraphics;
    TEST_ASSERT_EQ(agcCreateQueue(device, &queue_desc, &graphics_queue), AGC_OK,
        "label graphics queue creates");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
        &cross_consumer), AGC_OK, "cross-queue consumer command creates");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(cross_consumer), AGC_OK,
        "cross-queue consumer begins");
    TEST_ASSERT_EQ(agcCmdWaitGpuLabel(cross_consumer, cross_label, 7u), AGC_OK,
        "cross-queue consumer wait records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(cross_consumer), AGC_OK,
        "cross-queue consumer ends");
    TEST_ASSERT_EQ(agcResetFence(consumer_fence), AGC_OK,
        "cross-queue consumer fence resets");
    submit.command_buffers = &cross_consumer;
    TEST_ASSERT_EQ(agcQueueSubmit(graphics_queue, &submit, consumer_fence),
        AGC_OK, "cross-queue wait accepts a reached-or-passed point");
    captured = agcDriverDebugLastDcbSubmit();
    words = (const uint32_t *)(uintptr_t)captured->command_address;
    TEST_ASSERT_EQ(words[1] & 7u, 5u,
        "GPU label wait encodes greater-or-equal timeline comparison");
    TEST_ASSERT_EQ(agcGetFenceStatus(consumer_fence), AGC_OK,
        "generic cross-queue label consumer completes");
    TEST_ASSERT_EQ(agcResetCommandBuffer(cross_consumer), AGC_OK,
        "cross-queue consumer resets after completion");
    TEST_ASSERT_EQ(agcResetCommandBuffer(producer), AGC_OK,
        "cross-queue producer resets");
    TEST_ASSERT_EQ(agcDestroyGpuLabel(cross_label), AGC_OK,
        "cross-queue label destroys after command reset");
    TEST_ASSERT_EQ(agcDestroyFence(consumer_fence), AGC_OK,
        "label consumer fence destroys");
    TEST_ASSERT_EQ(agcDestroyFence(producer_fence), AGC_OK,
        "label producer fence destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(cross_consumer), AGC_OK,
        "cross-queue consumer command destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(consumer), AGC_OK,
        "label consumer command destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(producer), AGC_OK,
        "label producer command destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(graphics_queue), AGC_OK,
        "label graphics queue destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(compute_queue), AGC_OK,
        "label compute queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "label device destroys");
}

static void test_runtime_gpu_label_timeline_waits(void)
{
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueCompute);
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcGpuLabelDesc label_desc = AGC_GPU_LABEL_DESC_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcGpuLabelInfo info = AGC_GPU_LABEL_INFO_INIT;
    union {
        AgcGpuLabelInfo aligned;
        struct {
            uint8_t prefix[AGC_GPU_LABEL_INFO_V1_SIZE];
            uint64_t canary;
        } legacy;
    } v1_storage = {0};
    AgcGpuLabelInfo *v1_info =
        (AgcGpuLabelInfo *)(void *)v1_storage.legacy.prefix;
    AgcCommandBuffer commands[2] = {NULL, NULL};
    AgcGpuLabel label = NULL;
    AgcFence fence = NULL;

    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 128u;
    label_desc.initial_value = 3u;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
        &commands[0]), AGC_OK, "timeline first command creates");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
        &commands[1]), AGC_OK, "timeline second command creates");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "timeline fence creates");
    TEST_ASSERT_EQ(agcCreateGpuLabel(device, &label_desc, &label), AGC_OK,
        "timeline label creates with a nonzero initial point");
    TEST_ASSERT_EQ(agcGetGpuLabelStatus(label, 3u), AGC_OK,
        "timeline initial point is already observed");
    TEST_ASSERT_EQ(agcGetGpuLabelStatus(label, 4u),
        AGC_ERROR_INVALID_STATE,
        "timeline status rejects a point that has not been scheduled");
    TEST_ASSERT_EQ(agcWaitGpuLabel(label, 3u,
        AGC_RUNTIME_INFINITE_TIMEOUT), AGC_ERROR_INVALID_ARGUMENT,
        "timeline wait rejects an unbounded deadline");
    TEST_ASSERT_EQ(agcWaitGpuLabel(label, 4u, UINT64_C(1000000)),
        AGC_ERROR_INVALID_STATE,
        "timeline wait rejects an unscheduled future point");
    TEST_ASSERT_EQ(agcWaitGpuLabel(label, 3u, UINT64_C(1000000)), AGC_OK,
        "timeline wait accepts an observed initial point");
    TEST_ASSERT_EQ(agcGetGpuLabelInfo(label, &info), AGC_OK,
        "timeline v2 diagnostics query succeeds");
    TEST_ASSERT_EQ(info.last_wait_value, 3u,
        "timeline diagnostics report the last bounded target");
    TEST_ASSERT_EQ(info.last_wait_result, AGC_OK,
        "timeline diagnostics report bounded wait success");
    TEST_ASSERT_EQ(info.timeout_count, 0u,
        "timeline diagnostics begin without timeouts");
    v1_info->struct_size = AGC_GPU_LABEL_INFO_V1_SIZE;
    v1_info->version = AGC_RUNTIME_STRUCTURE_VERSION_1;
    v1_storage.legacy.canary = UINT64_C(0xcafebabedeadbeef);
    TEST_ASSERT_EQ(agcGetGpuLabelInfo(label, v1_info), AGC_OK,
        "timeline diagnostics preserve the v1 prefix ABI");
    TEST_ASSERT_EQ(v1_info->scheduled_value, 3u,
        "timeline v1 diagnostics report the scheduled point");
    TEST_ASSERT_EQ(v1_storage.legacy.canary,
        UINT64_C(0xcafebabedeadbeef),
        "timeline v1 diagnostics do not write the v2 tail");

    TEST_ASSERT_EQ(agcBeginCommandBuffer(commands[0]), AGC_OK,
        "timeline ordered-signal command begins");
    TEST_ASSERT_EQ(agcCmdSignalGpuLabel(commands[0], label, 5u), AGC_OK,
        "timeline first increasing signal records");
    TEST_ASSERT_EQ(agcCmdSignalGpuLabel(commands[0], label, 5u),
        AGC_ERROR_INVALID_STATE,
        "timeline command rejects a repeated tentative point");
    TEST_ASSERT_EQ(agcCmdSignalGpuLabel(commands[0], label, 4u),
        AGC_ERROR_INVALID_STATE,
        "timeline command rejects a decreasing tentative point");
    TEST_ASSERT_EQ(agcCmdSignalGpuLabel(commands[0], label, 6u), AGC_OK,
        "timeline second increasing signal records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(commands[0]), AGC_OK,
        "timeline ordered-signal command ends");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &commands[0];
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "timeline ordered signals submit");
    TEST_ASSERT_EQ(agcGetGpuLabelStatus(label, 5u), AGC_OK,
        "timeline status treats an observed later value as completing five");
    TEST_ASSERT_EQ(agcWaitGpuLabel(label, 6u, UINT64_C(1000000)), AGC_OK,
        "timeline waits for the latest submitted point");
    info = (AgcGpuLabelInfo)AGC_GPU_LABEL_INFO_INIT;
    TEST_ASSERT_EQ(agcGetGpuLabelInfo(label, &info), AGC_OK,
        "timeline submitted diagnostics query succeeds");
    TEST_ASSERT_EQ(info.scheduled_value, 6u,
        "timeline submitted diagnostics report the final signal");
    TEST_ASSERT_EQ(info.observed_value, 6u,
        "timeline generic backend observes the final signal");
    TEST_ASSERT_EQ(info.last_wait_value, 6u,
        "timeline submitted diagnostics report the waited point");
    TEST_ASSERT_EQ(agcResetCommandBuffer(commands[0]), AGC_OK,
        "timeline ordered-signal command resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "timeline ordered-signal fence resets");

    TEST_ASSERT_EQ(agcBeginCommandBuffer(commands[0]), AGC_OK,
        "timeline batch first command begins");
    TEST_ASSERT_EQ(agcCmdSignalGpuLabel(commands[0], label, 8u), AGC_OK,
        "timeline batch first signal records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(commands[0]), AGC_OK,
        "timeline batch first command ends");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(commands[1]), AGC_OK,
        "timeline batch stale command begins");
    TEST_ASSERT_EQ(agcCmdSignalGpuLabel(commands[1], label, 7u), AGC_OK,
        "timeline independently-recorded stale signal records tentatively");
    TEST_ASSERT_EQ(agcEndCommandBuffer(commands[1]), AGC_OK,
        "timeline batch stale command ends");
    submit.command_buffer_count = 2u;
    submit.command_buffers = commands;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence),
        AGC_ERROR_INVALID_STATE,
        "timeline batch rejects decreasing cross-command signal order");
    info = (AgcGpuLabelInfo)AGC_GPU_LABEL_INFO_INIT;
    TEST_ASSERT_EQ(agcGetGpuLabelInfo(label, &info), AGC_OK,
        "timeline rejected-batch diagnostics query succeeds");
    TEST_ASSERT_EQ(info.scheduled_value, 6u,
        "timeline rejected batch publishes no tentative point");
    TEST_ASSERT_EQ(agcResetCommandBuffer(commands[1]), AGC_OK,
        "timeline stale batch command resets");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(commands[1]), AGC_OK,
        "timeline corrected batch command begins");
    TEST_ASSERT_EQ(agcCmdSignalGpuLabel(commands[1], label, 9u), AGC_OK,
        "timeline corrected batch signal records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(commands[1]), AGC_OK,
        "timeline corrected batch command ends");
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "timeline increasing cross-command batch submits");
    TEST_ASSERT_EQ(agcWaitGpuLabel(label, 9u, UINT64_C(1000000)), AGC_OK,
        "timeline corrected batch point completes");
    TEST_ASSERT_EQ(agcResetCommandBuffer(commands[1]), AGC_OK,
        "timeline corrected second command resets");
    TEST_ASSERT_EQ(agcResetCommandBuffer(commands[0]), AGC_OK,
        "timeline corrected first command resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "timeline corrected batch fence resets");

    TEST_ASSERT_EQ(agcBeginCommandBuffer(commands[0]), AGC_OK,
        "timeline stale-recording command begins");
    TEST_ASSERT_EQ(agcCmdSignalGpuLabel(commands[0], label, 10u), AGC_OK,
        "timeline stale candidate records before advancement");
    TEST_ASSERT_EQ(agcEndCommandBuffer(commands[0]), AGC_OK,
        "timeline stale-recording command ends");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(commands[1]), AGC_OK,
        "timeline advancing command begins");
    TEST_ASSERT_EQ(agcCmdSignalGpuLabel(commands[1], label, 11u), AGC_OK,
        "timeline advancing signal records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(commands[1]), AGC_OK,
        "timeline advancing command ends");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &commands[1];
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "timeline advancing command submits first");
    TEST_ASSERT_EQ(agcResetCommandBuffer(commands[1]), AGC_OK,
        "timeline advancing command resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "timeline advancing fence resets");
    submit.command_buffers = &commands[0];
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence),
        AGC_ERROR_INVALID_STATE,
        "timeline submit rejects a recording made stale by another submit");
    info = (AgcGpuLabelInfo)AGC_GPU_LABEL_INFO_INIT;
    TEST_ASSERT_EQ(agcGetGpuLabelInfo(label, &info), AGC_OK,
        "timeline stale-submit diagnostics query succeeds");
    TEST_ASSERT_EQ(info.scheduled_value, 11u,
        "timeline stale submit preserves the newer committed point");
    TEST_ASSERT_EQ(agcResetCommandBuffer(commands[0]), AGC_OK,
        "timeline stale-recording command resets");

    TEST_ASSERT_EQ(agcBeginCommandBuffer(commands[0]), AGC_OK,
        "timeline terminal command begins");
    TEST_ASSERT_EQ(agcCmdSignalGpuLabel(commands[0], label, UINT32_MAX),
        AGC_OK, "timeline terminal point records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(commands[0]), AGC_OK,
        "timeline terminal command ends");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &commands[0];
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "timeline terminal point submits");
    TEST_ASSERT_EQ(agcWaitGpuLabel(label, UINT32_MAX, UINT64_C(1000000)),
        AGC_OK, "timeline terminal point completes");
    TEST_ASSERT_EQ(agcResetCommandBuffer(commands[0]), AGC_OK,
        "timeline terminal command resets");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(commands[0]), AGC_OK,
        "timeline post-terminal command begins");
    TEST_ASSERT_EQ(agcCmdSignalGpuLabel(commands[0], label, UINT32_MAX),
        AGC_ERROR_INVALID_STATE,
        "timeline terminal point cannot repeat or wrap");
    TEST_ASSERT_EQ(agcResetCommandBuffer(commands[0]), AGC_OK,
        "timeline post-terminal command resets");

    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK,
        "timeline fence destroys");
    TEST_ASSERT_EQ(agcDestroyGpuLabel(label), AGC_OK,
        "timeline label destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(commands[1]), AGC_OK,
        "timeline second command destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(commands[0]), AGC_OK,
        "timeline first command destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK,
        "timeline queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "timeline device destroys");
}

static void test_runtime_submit_label_lists(void)
{
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueCompute);
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcGpuLabelDesc label_desc = AGC_GPU_LABEL_DESC_INIT;
    AgcGpuLabelInfo label_info = AGC_GPU_LABEL_INFO_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcSubmitInfo list_submit = AGC_SUBMIT_INFO_V2_INIT;
    AgcGpuLabelPoint waits[1] = { AGC_GPU_LABEL_POINT_INIT };
    AgcGpuLabelPoint signals[1] = { AGC_GPU_LABEL_POINT_INIT };
    AgcGpuLabelPoint rollback_waits[1] = { AGC_GPU_LABEL_POINT_INIT };
    AgcCommandBuffer producer = NULL;
    AgcCommandBuffer command = NULL;
    AgcCommandBuffer consumer = NULL;
    AgcCommandBuffer small = NULL;
    AgcFence fence = NULL;
    AgcGpuLabel source = NULL;
    AgcGpuLabel destination = NULL;
    const AgcCommandBufferSubmit *captured;
    const uint32_t *words;
    uint32_t owner = UINT32_MAX;

    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 32u;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &producer),
        AGC_OK, "submit-list producer command creates");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &command),
        AGC_OK, "submit-list command creates");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &consumer),
        AGC_OK, "submit-list consumer command creates");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "submit-list fence creates");
    TEST_ASSERT_EQ(agcCreateGpuLabel(device, &label_desc, &source), AGC_OK,
        "submit-list source label creates");
    TEST_ASSERT_EQ(agcCreateGpuLabel(device, &label_desc, &destination), AGC_OK,
        "submit-list destination label creates");

    TEST_ASSERT_EQ(agcBeginCommandBuffer(producer), AGC_OK,
        "submit-list producer begins");
    TEST_ASSERT_EQ(agcCmdSignalGpuLabel(producer, source, 1u), AGC_OK,
        "submit-list producer signal records");
    TEST_ASSERT_EQ(agcCmdSignalGpuLabel(producer, source, 2u), AGC_OK,
        "submit-list producer advances beyond the waited point");
    TEST_ASSERT_EQ(agcEndCommandBuffer(producer), AGC_OK,
        "submit-list producer ends");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &producer;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "submit-list source producer submits");
    TEST_ASSERT_EQ(agcResetCommandBuffer(producer), AGC_OK,
        "submit-list source producer resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "submit-list source producer fence resets");

    waits[0].label = source;
    waits[0].value = 1u;
    signals[0].label = destination;
    signals[0].value = 2u;
    list_submit.command_buffer_count = 1u;
    list_submit.command_buffers = &command;
    list_submit.wait_count = 1u;
    list_submit.waits = waits;
    list_submit.signal_count = 1u;
    list_submit.signals = signals;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "submit-list command begins");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command), AGC_OK,
        "submit-list command ends");
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &list_submit, fence), AGC_OK,
        "submit-level wait and signal submit atomically");
    captured = agcDriverDebugLastAcbSubmit(&owner);
    TEST_ASSERT(captured != NULL && owner != UINT32_MAX,
        "submit-list uses generic compute carrier");
    words = (const uint32_t *)(uintptr_t)captured->command_address;
    TEST_ASSERT_EQ(captured->dword_count, 17u,
        "submit-list emits one wait and one EOP signal");
    TEST_ASSERT_EQ(agcPm4Opcode(words[0]), AGC_PM4_OP_WAIT_REG_MEM,
        "submit-list wait is inserted before command body");
    TEST_ASSERT_EQ(words[1] & 7u, 5u,
        "submit-list wait uses reached-or-passed timeline comparison");
    TEST_ASSERT_EQ(agcPm4Opcode(words[7]), AGC_PM4_OP_RELEASE_MEM,
        "submit-list signal is appended after command body");
    TEST_ASSERT_EQ(agcGetGpuLabelInfo(destination, &label_info), AGC_OK,
        "submit-list destination label diagnostics query succeeds");
    TEST_ASSERT_EQ(label_info.scheduled_value, 2u,
        "submit-list signal commits its timeline value");
    TEST_ASSERT_EQ(agcDestroyGpuLabel(destination), AGC_ERROR_BUSY,
        "submit-list retains its signal label through command reset");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
        "submit-list command resets after completion");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "submit-list fence resets after completion");

    list_submit.command_buffers = &consumer;
    list_submit.wait_count = 1u;
    list_submit.waits = signals;
    list_submit.signal_count = 0u;
    list_submit.signals = NULL;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(consumer), AGC_OK,
        "submit-list consumer begins");
    TEST_ASSERT_EQ(agcEndCommandBuffer(consumer), AGC_OK,
        "submit-list consumer ends");
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &list_submit, fence), AGC_OK,
        "submit-list signal becomes a later submit dependency");
    TEST_ASSERT_EQ(agcResetCommandBuffer(consumer), AGC_OK,
        "submit-list consumer resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "submit-list consumer fence resets");

    TEST_ASSERT_EQ(agcBeginCommandBuffer(producer), AGC_OK,
        "submit-list rollback producer begins");
    TEST_ASSERT_EQ(agcCmdSignalGpuLabel(producer, source, 3u), AGC_OK,
        "submit-list rollback producer command records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(producer), AGC_OK,
        "submit-list rollback producer ends");
    list_submit.command_buffers = &producer;
    list_submit.wait_count = 1u;
    rollback_waits[0].label = destination;
    rollback_waits[0].value = 2u;
    list_submit.waits = rollback_waits;
    signals[0].value = 3u;
    list_submit.signal_count = 1u;
    list_submit.signals = signals;
    agcDriverDebugFailNextSubmit(AGC_ERROR_SUBMIT_FAILED);
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &list_submit, fence),
        AGC_ERROR_SUBMIT_FAILED,
        "submit-list driver failure rolls injected packets and label retains back");
    TEST_ASSERT_EQ(agcGetGpuLabelInfo(destination, &label_info), AGC_OK,
        "submit-list rollback destination diagnostics query succeeds");
    TEST_ASSERT_EQ(label_info.scheduled_value, 2u,
        "failed submit-list signal does not publish a new timeline point");
    submit.command_buffers = &producer;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "submit-list rollback restores the original command stream");
    captured = agcDriverDebugLastAcbSubmit(&owner);
    TEST_ASSERT_EQ(captured->dword_count, AGC_GFX1013_EOP_FENCE_DWORDS,
        "restored command has no submit-list prefix or tail");
    TEST_ASSERT_EQ(agcResetCommandBuffer(producer), AGC_OK,
        "submit-list rollback producer resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "submit-list rollback fence resets");

    command_desc.capacity_dwords = 8u;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &small),
        AGC_OK, "short submit-list command creates");
    list_submit.command_buffers = &small;
    list_submit.wait_count = 1u;
    list_submit.waits = waits;
    list_submit.signal_count = 1u;
    list_submit.signals = signals;
    waits[0].value = 2u;
    signals[0].value = 3u;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(small), AGC_OK,
        "short submit-list command begins");
    TEST_ASSERT_EQ(agcEndCommandBuffer(small), AGC_OK,
        "short submit-list command ends");
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &list_submit, fence),
        AGC_ERROR_COMMAND_SPACE_EXHAUSTED,
        "short submit-list rejects before command mutation");
    submit.command_buffers = &small;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "v1 submit still succeeds after rejected submit list");
    TEST_ASSERT_EQ(agcResetCommandBuffer(small), AGC_OK,
        "short submit-list command resets after v1 submit");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "short submit-list fence resets after v1 submit");

    TEST_ASSERT_EQ(agcDestroyGpuLabel(destination), AGC_OK,
        "submit-list destination label destroys");
    TEST_ASSERT_EQ(agcDestroyGpuLabel(source), AGC_OK,
        "submit-list source label destroys");
    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK,
        "submit-list fence destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(small), AGC_OK,
        "short submit-list command destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(consumer), AGC_OK,
        "submit-list consumer command destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command), AGC_OK,
        "submit-list command destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(producer), AGC_OK,
        "submit-list producer command destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK,
        "submit-list queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "submit-list device destroys");
}

static void test_runtime_image_transfer(void)
{
    const uint32_t input[] = {0x11223344u, 0x55667788u, 0x99aabbccu};
    uint32_t output[3] = {0u, 0u, 0u};
    AgcDevice device = create_device();
    AgcImageDesc desc = AGC_IMAGE_DESC_INIT;
    AgcImageLayout layout = AGC_IMAGE_LAYOUT_INIT;
    AgcImage image = NULL;

    desc.width = 8u;
    desc.height = 1u;
    desc.format = AGC_FORMAT_RGBA8_UNORM;
    desc.usage = AGC_IMAGE_USAGE_COLOR_TARGET_BIT;
    TEST_ASSERT_EQ(agcCreateImage(device, &desc, &image), AGC_OK,
        "non-transfer image creates");
    TEST_ASSERT_EQ(agcWriteImage(image, 0u, input, sizeof(input)),
        AGC_ERROR_INVALID_ARGUMENT,
        "write rejects missing transfer-dst usage");
    TEST_ASSERT_EQ(agcReadImage(image, 0u, output, sizeof(output)),
        AGC_ERROR_INVALID_ARGUMENT,
        "read rejects missing transfer-src usage");
    TEST_ASSERT_EQ(agcDestroyImage(image), AGC_OK,
        "non-transfer image destroys");

    desc.usage = AGC_IMAGE_USAGE_COLOR_TARGET_BIT |
        AGC_IMAGE_USAGE_TRANSFER_SRC_BIT | AGC_IMAGE_USAGE_TRANSFER_DST_BIT;
    TEST_ASSERT_EQ(agcCreateImage(device, &desc, &image), AGC_OK,
        "transfer image creates");
    TEST_ASSERT_EQ(agcWriteImage(image, 4u, input, sizeof(input)), AGC_OK,
        "image transfer write succeeds");
    TEST_ASSERT_EQ(agcReadImage(image, 4u, output, sizeof(output)), AGC_OK,
        "image transfer read succeeds");
    TEST_ASSERT(memcmp(input, output, sizeof(input)) == 0,
        "image transfer round trip preserves bytes");
    TEST_ASSERT_EQ(agcGetImageLayout(device, &desc, &layout), AGC_OK,
        "image transfer layout query succeeds");
    TEST_ASSERT_EQ(agcReadImage(image,
        layout.allocation_size - sizeof(output) + 1u, output,
        sizeof(output)),
        AGC_ERROR_INVALID_ARGUMENT, "image transfer bounds reject overflow");
    TEST_ASSERT_EQ(agcDestroyImage(image), AGC_OK, "transfer image destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "image transfer device destroys");
}

static void test_runtime_partial_resource_handoffs(void)
{
    AgcDevice device = create_device();
    AgcQueue compute_queue = create_queue(device, kAgcQueueCompute);
    AgcQueue graphics_queue = create_queue(device, kAgcQueueGraphics);
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcGpuLabelDesc label_desc = AGC_GPU_LABEL_DESC_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcResourceTransition setup[2] = {
        AGC_RESOURCE_TRANSITION_INIT, AGC_RESOURCE_TRANSITION_INIT };
    AgcResourceTransition releases[4] = {
        AGC_RESOURCE_TRANSITION_V2_INIT, AGC_RESOURCE_TRANSITION_V2_INIT,
        AGC_RESOURCE_TRANSITION_V2_INIT, AGC_RESOURCE_TRANSITION_V2_INIT };
    AgcResourceTransition acquires[4];
    AgcResourceTransition probe = AGC_RESOURCE_TRANSITION_INIT;
    AgcResourceStateInfo info = AGC_RESOURCE_STATE_INFO_INIT;
    AgcImageSubresourceRange image_ranges[2] = {
        { AGC_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u, 0u },
        { AGC_IMAGE_ASPECT_COLOR_BIT, 1u, 1u, 1u, 1u, 0u } };
    AgcBuffer buffer = NULL;
    AgcImage image = NULL;
    AgcCommandBuffer compute = NULL;
    AgcCommandBuffer graphics = NULL;
    AgcCommandBuffer competing = NULL;
    AgcCommandBuffer batch_commands[2];
    AgcFence fence = NULL;
    AgcGpuLabel labels[2] = { NULL, NULL };
    const AgcCommandBufferSubmit *captured;
    const uint32_t *words;
    uint32_t i;

    buffer_desc.size = 256u;
    buffer_desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT;
    image_desc.width = 8u;
    image_desc.height = 8u;
    image_desc.mip_levels = 2u;
    image_desc.array_layers = 2u;
    image_desc.format = AGC_FORMAT_RGBA8_UNORM;
    image_desc.usage = AGC_IMAGE_USAGE_STORAGE_BIT |
        AGC_IMAGE_USAGE_SAMPLED_BIT;
    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 512u;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &buffer), AGC_OK,
        "partial-handoff buffer creates");
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &image), AGC_OK,
        "partial-handoff image creates");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &compute),
        AGC_OK, "partial-handoff compute command creates");
    command_desc.queue_type = kAgcQueueGraphics;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &graphics),
        AGC_OK, "partial-handoff graphics command creates");
    command_desc.queue_type = kAgcQueueCompute;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &competing),
        AGC_OK, "partial-handoff competing command creates");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "partial-handoff fence creates");
    for (i = 0u; i < 2u; ++i) {
        TEST_ASSERT_EQ(agcCreateGpuLabel(device, &label_desc, &labels[i]),
            AGC_OK, "partial-handoff dependency label creates");
    }

    setup[0].resource_type = kAgcResourceTypeBuffer;
    setup[0].buffer = buffer;
    setup[0].buffer_size = buffer_desc.size;
    setup[0].before = kAgcResourceUsageUndefined;
    setup[0].after = kAgcResourceUsageShaderWrite;
    setup[0].before_owner = kAgcResourceOwnerHost;
    setup[0].after_owner = kAgcResourceOwnerCompute;
    setup[1].resource_type = kAgcResourceTypeImage;
    setup[1].image = image;
    setup[1].image_range.aspect_mask = AGC_IMAGE_ASPECT_COLOR_BIT;
    setup[1].image_range.mip_level_count = image_desc.mip_levels;
    setup[1].image_range.array_layer_count = image_desc.array_layers;
    setup[1].before = kAgcResourceUsageUndefined;
    setup[1].after = kAgcResourceUsageShaderWrite;
    setup[1].before_owner = kAgcResourceOwnerHost;
    setup[1].after_owner = kAgcResourceOwnerCompute;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(compute), AGC_OK,
        "partial-handoff setup begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(compute, 2u, setup), AGC_OK,
        "partial-handoff setup records both resources");
    TEST_ASSERT_EQ(agcEndCommandBuffer(compute), AGC_OK,
        "partial-handoff setup ends");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &compute;
    TEST_ASSERT_EQ(agcQueueSubmit(compute_queue, &submit, fence), AGC_OK,
        "partial-handoff setup submits");
    TEST_ASSERT_EQ(agcResetCommandBuffer(compute), AGC_OK,
        "partial-handoff setup command resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "partial-handoff setup fence resets");

    for (i = 0u; i < 4u; ++i) {
        releases[i].before = kAgcResourceUsageShaderWrite;
        releases[i].after = kAgcResourceUsageShaderRead;
        releases[i].before_owner = kAgcResourceOwnerCompute;
        releases[i].after_owner = kAgcResourceOwnerGraphics;
        releases[i].flags = AGC_RESOURCE_TRANSITION_RELEASE_BIT;
        releases[i].dependency_value = 1u;
    }
    releases[0].resource_type = kAgcResourceTypeBuffer;
    releases[0].buffer = buffer;
    releases[0].dependency_label = labels[0];
    releases[0].buffer_offset = 0u;
    releases[0].buffer_size = 64u;
    releases[1].resource_type = kAgcResourceTypeBuffer;
    releases[1].buffer = buffer;
    releases[1].dependency_label = labels[0];
    releases[1].dependency_value = 2u;
    releases[1].buffer_offset = 128u;
    releases[1].buffer_size = 64u;
    releases[2].resource_type = kAgcResourceTypeImage;
    releases[2].image = image;
    releases[2].dependency_label = labels[1];
    releases[2].image_range = image_ranges[0];
    releases[3].resource_type = kAgcResourceTypeImage;
    releases[3].image = image;
    releases[3].dependency_label = labels[1];
    releases[3].dependency_value = 2u;
    releases[3].image_range = image_ranges[1];

    acquires[0] = releases[0];
    acquires[1] = releases[0];
    acquires[1].dependency_label = labels[1];
    acquires[1].buffer_offset = 32u;
    acquires[1].buffer_size = 32u;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(compute), AGC_OK,
        "first independently recorded overlapping release begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(compute, 1u, &acquires[0]),
        AGC_OK, "first independently recorded release succeeds");
    TEST_ASSERT_EQ(agcEndCommandBuffer(compute), AGC_OK,
        "first independently recorded release ends");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(competing), AGC_OK,
        "second independently recorded overlapping release begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(competing, 1u, &acquires[1]),
        AGC_OK, "second independently recorded release succeeds alone");
    TEST_ASSERT_EQ(agcEndCommandBuffer(competing), AGC_OK,
        "second independently recorded release ends");
    batch_commands[0] = compute;
    batch_commands[1] = competing;
    submit.command_buffer_count = 2u;
    submit.command_buffers = batch_commands;
    TEST_ASSERT_EQ(agcQueueSubmit(compute_queue, &submit, fence),
        AGC_ERROR_INVALID_STATE,
        "batch rejects independently recorded overlapping releases");
    info = (AgcResourceStateInfo)AGC_RESOURCE_STATE_INFO_INIT;
    TEST_ASSERT_EQ(agcGetBufferRangeStateInfo(buffer, 0u, 64u, &info),
        AGC_OK, "rejected overlap batch preserves buffer diagnostics");
    TEST_ASSERT_EQ(info.flags, 0u,
        "rejected overlap batch publishes no pending transfer");
    TEST_ASSERT_EQ(agcResetCommandBuffer(competing), AGC_OK,
        "competing overlap command resets");
    TEST_ASSERT_EQ(agcResetCommandBuffer(compute), AGC_OK,
        "first overlap command resets");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &compute;

    TEST_ASSERT_EQ(agcBeginCommandBuffer(compute), AGC_OK,
        "disjoint partial releases begin");
    for (i = 0u; i < 4u; ++i) {
        TEST_ASSERT_EQ(agcCmdTransitionResources(compute, 1u, &releases[i]),
            AGC_OK,
            "multiple disjoint buffer and image releases record together");
    }
    TEST_ASSERT_EQ(agcEndCommandBuffer(compute), AGC_OK,
        "disjoint partial releases end");
    TEST_ASSERT_EQ(agcQueueSubmit(compute_queue, &submit, fence), AGC_OK,
        "disjoint partial releases submit");
    TEST_ASSERT_EQ(agcResetCommandBuffer(compute), AGC_OK,
        "disjoint partial release command resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "disjoint partial release fence resets");
    TEST_ASSERT_EQ(agcDestroyGpuLabel(labels[0]), AGC_ERROR_BUSY,
        "pending partial transfer retains its dependency label");

    TEST_ASSERT_EQ(agcGetBufferStateInfo(buffer, &info),
        AGC_ERROR_NOT_SUPPORTED,
        "whole buffer query rejects mixed pending-transfer coverage");
    info = (AgcResourceStateInfo)AGC_RESOURCE_STATE_INFO_INIT;
    TEST_ASSERT_EQ(agcGetBufferRangeStateInfo(buffer, 0u, 64u, &info),
        AGC_OK, "first pending buffer range is queryable");
    TEST_ASSERT_EQ(info.flags, AGC_RESOURCE_STATE_TRANSFER_PENDING_BIT,
        "first pending buffer range reports its transfer");
    TEST_ASSERT(info.transfer_label == labels[0],
        "first pending buffer range reports the exact dependency");
    info = (AgcResourceStateInfo)AGC_RESOURCE_STATE_INFO_INIT;
    TEST_ASSERT_EQ(agcGetBufferRangeStateInfo(buffer, 64u, 64u, &info),
        AGC_OK, "disjoint buffer range remains queryable");
    TEST_ASSERT_EQ(info.flags, 0u,
        "disjoint buffer range remains independently usable");
    info = (AgcResourceStateInfo)AGC_RESOURCE_STATE_INFO_INIT;
    TEST_ASSERT_EQ(agcGetImageStateInfo(image, &info),
        AGC_ERROR_NOT_SUPPORTED,
        "whole image query rejects mixed pending-transfer coverage");
    TEST_ASSERT_EQ(agcGetImageSubresourceStateInfo(image, &image_ranges[1],
        &info), AGC_OK, "second pending image range is queryable");
    TEST_ASSERT(info.transfer_label == labels[1],
        "second pending image range reports the exact dependency");

    probe = releases[0];
    probe.buffer_offset = 32u;
    probe.buffer_size = 64u;
    probe.dependency_value = 3u;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(compute), AGC_OK,
        "overlapping pending-range probe begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(compute, 1u, &probe),
        AGC_ERROR_INVALID_STATE,
        "normal transition cannot overlap a pending partial transfer");
    TEST_ASSERT_EQ(agcResetCommandBuffer(compute), AGC_OK,
        "overlapping pending-range probe resets");
    probe.buffer_offset = 64u;
    probe.buffer_size = 64u;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(compute), AGC_OK,
        "disjoint pending-range probe begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(compute, 1u, &probe), AGC_OK,
        "normal transition may use a disjoint pending-resource range");
    TEST_ASSERT_EQ(agcResetCommandBuffer(compute), AGC_OK,
        "disjoint pending-range probe resets without publication");

    for (i = 0u; i < 4u; ++i) {
        acquires[i] = releases[i];
        acquires[i].flags = AGC_RESOURCE_TRANSITION_ACQUIRE_BIT;
    }
    TEST_ASSERT_EQ(agcBeginCommandBuffer(graphics), AGC_OK,
        "partial acquire reservation probe begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(graphics, 1u, &acquires[0]),
        AGC_OK, "one exact partial acquire reserves its transfer");
    info = (AgcResourceStateInfo)AGC_RESOURCE_STATE_INFO_INIT;
    TEST_ASSERT_EQ(agcGetBufferRangeStateInfo(buffer, 0u, 64u, &info),
        AGC_OK, "reserved partial acquire is queryable");
    TEST_ASSERT_EQ(info.flags, AGC_RESOURCE_STATE_TRANSFER_PENDING_BIT |
        AGC_RESOURCE_STATE_ACQUIRE_RECORDED_BIT,
        "partial acquire reservation is reported independently");
    TEST_ASSERT_EQ(agcResetCommandBuffer(graphics), AGC_OK,
        "reset clears only the selected acquire reservation");
    info = (AgcResourceStateInfo)AGC_RESOURCE_STATE_INFO_INIT;
    TEST_ASSERT_EQ(agcGetBufferRangeStateInfo(buffer, 0u, 64u, &info),
        AGC_OK, "reset partial acquire remains pending");
    TEST_ASSERT_EQ(info.flags, AGC_RESOURCE_STATE_TRANSFER_PENDING_BIT,
        "reset removes the selected acquire-recorded diagnostic");

    TEST_ASSERT_EQ(agcBeginCommandBuffer(graphics), AGC_OK,
        "all exact partial acquires begin");
    for (i = 0u; i < 4u; ++i) {
        TEST_ASSERT_EQ(agcCmdTransitionResources(graphics, 1u, &acquires[i]),
            AGC_OK, "all exact disjoint partial acquires record together");
    }
    TEST_ASSERT_EQ(agcEndCommandBuffer(graphics), AGC_OK,
        "all exact partial acquires end");
    submit.command_buffers = &graphics;
    TEST_ASSERT_EQ(agcQueueSubmit(graphics_queue, &submit, fence), AGC_OK,
        "all exact partial acquires submit");
    captured = agcDriverDebugLastDcbSubmit();
    words = (const uint32_t *)(uintptr_t)captured->command_address;
    TEST_ASSERT_EQ(words[1] & 7u, 5u,
        "older shared-label acquire uses reached-or-passed comparison");
    TEST_ASSERT_EQ(agcResetCommandBuffer(graphics), AGC_OK,
        "all exact partial acquire command resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "all exact partial acquire fence resets");
    info = (AgcResourceStateInfo)AGC_RESOURCE_STATE_INFO_INIT;
    TEST_ASSERT_EQ(agcGetBufferRangeStateInfo(buffer, 0u, 64u, &info),
        AGC_OK, "acquired buffer range is queryable");
    TEST_ASSERT_EQ(info.usage, kAgcResourceUsageShaderRead,
        "acquired buffer range publishes destination usage");
    TEST_ASSERT_EQ(info.owner, kAgcResourceOwnerGraphics,
        "acquired buffer range publishes destination owner");
    TEST_ASSERT_EQ(info.flags, 0u,
        "acquired buffer range clears transfer diagnostics");
    info = (AgcResourceStateInfo)AGC_RESOURCE_STATE_INFO_INIT;
    TEST_ASSERT_EQ(agcGetImageSubresourceStateInfo(image, &image_ranges[0],
        &info), AGC_OK, "acquired image range is queryable");
    TEST_ASSERT_EQ(info.owner, kAgcResourceOwnerGraphics,
        "acquired image range publishes destination owner");

    for (i = 0u; i < 2u; ++i) {
        TEST_ASSERT_EQ(agcDestroyGpuLabel(labels[i]), AGC_OK,
            "partial-handoff dependency label destroys");
    }
    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK,
        "partial-handoff fence destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(competing), AGC_OK,
        "partial-handoff competing command destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(graphics), AGC_OK,
        "partial-handoff graphics command destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(compute), AGC_OK,
        "partial-handoff compute command destroys");
    TEST_ASSERT_EQ(agcDestroyImage(image), AGC_OK,
        "partial-handoff image destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(buffer), AGC_OK,
        "partial-handoff buffer destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(graphics_queue), AGC_OK,
        "partial-handoff graphics queue destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(compute_queue), AGC_OK,
        "partial-handoff compute queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "partial-handoff device destroys");
}

static void test_runtime_resource_transitions(void)
{
    AgcDevice device = create_device();
    AgcQueue compute_queue = create_queue(device, kAgcQueueCompute);
    AgcQueue graphics_queue = create_queue(device, kAgcQueueGraphics);
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcResourceTransition transition = AGC_RESOURCE_TRANSITION_INIT;
    AgcResourceTransition handoff = AGC_RESOURCE_TRANSITION_V2_INIT;
    AgcResourceTransition image_handoff = AGC_RESOURCE_TRANSITION_V2_INIT;
    AgcResourceTransition duplicate[2];
    AgcResourceTransition image_transitions[2] = {
        AGC_RESOURCE_TRANSITION_INIT,
        AGC_RESOURCE_TRANSITION_INIT,
    };
    AgcBuffer buffer = NULL;
    AgcImage image = NULL;
    AgcImage second_image = NULL;
    AgcImage handoff_image = NULL;
    AgcCommandBuffer compute_command = NULL;
    AgcCommandBuffer graphics_command = NULL;
    AgcFence fence = NULL;
    AgcGpuLabel label = NULL;
    AgcGpuLabel image_label = NULL;
    AgcGpuLabelDesc label_desc = AGC_GPU_LABEL_DESC_INIT;
    AgcResourceStateInfo state_info = AGC_RESOURCE_STATE_INFO_INIT;
    const AgcCommandBufferSubmit *captured;
    const uint32_t *words;
    uint32_t owner = UINT32_MAX;

    buffer_desc.size = 64u;
    buffer_desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT |
        AGC_BUFFER_USAGE_TRANSFER_SRC_BIT | AGC_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_desc.flags = AGC_BUFFER_CREATE_READBACK_BIT;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &buffer), AGC_OK,
        "transition buffer creates");
    TEST_ASSERT_EQ(agcGetBufferStateInfo(buffer, &state_info), AGC_OK,
        "new buffer state snapshot succeeds");
    TEST_ASSERT_EQ(state_info.resource_type, kAgcResourceTypeBuffer,
        "buffer state snapshot identifies resource type");
    TEST_ASSERT_EQ(state_info.usage, kAgcResourceUsageUndefined,
        "new buffer state is undefined");
    TEST_ASSERT_EQ(state_info.owner, kAgcResourceOwnerHost,
        "new buffer state is host-owned");
    TEST_ASSERT_EQ(state_info.flags, 0u,
        "new buffer state has no pending synchronization");
    state_info.reserved[0] = 1u;
    TEST_ASSERT_EQ(agcGetBufferStateInfo(buffer, &state_info),
        AGC_ERROR_INVALID_ARGUMENT,
        "buffer state snapshot rejects nonzero reserved input");
    state_info = (AgcResourceStateInfo)AGC_RESOURCE_STATE_INFO_INIT;
    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 64u;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
        &compute_command), AGC_OK, "transition compute command creates");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "transition fence creates");

    transition.resource_type = kAgcResourceTypeBuffer;
    transition.buffer = buffer;
    transition.buffer_size = buffer_desc.size;
    transition.before = kAgcResourceUsageUndefined;
    transition.after = kAgcResourceUsageShaderWrite;
    transition.before_owner = kAgcResourceOwnerHost;
    transition.after_owner = kAgcResourceOwnerCompute;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(compute_command), AGC_OK,
        "transition compute command begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(compute_command, 1u,
        &transition), AGC_OK, "undefined-to-compute transition records");
    TEST_ASSERT_EQ(agcGetBufferStateInfo(buffer, &state_info), AGC_OK,
        "recorded buffer state snapshot succeeds");
    TEST_ASSERT_EQ(state_info.usage, kAgcResourceUsageUndefined,
        "recording does not publish buffer state before submit");
    TEST_ASSERT_EQ(state_info.recorded_reference_count, 1u,
        "recorded buffer state exposes command retention");
    TEST_ASSERT_EQ(agcDestroyBuffer(buffer), AGC_ERROR_BUSY,
        "recorded transition retains its buffer");
    TEST_ASSERT_EQ(agcEndCommandBuffer(compute_command), AGC_OK,
        "transition compute command ends");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &compute_command;
    TEST_ASSERT_EQ(agcQueueSubmit(compute_queue, &submit, fence), AGC_OK,
        "undefined-to-compute transition submits");
    TEST_ASSERT_EQ(agcGetFenceStatus(fence), AGC_OK,
        "transition submit completes on host");
    TEST_ASSERT_EQ(agcGetBufferStateInfo(buffer, &state_info), AGC_OK,
        "submitted buffer state snapshot succeeds");
    TEST_ASSERT_EQ(state_info.usage, kAgcResourceUsageShaderWrite,
        "successful submit publishes buffer usage");
    TEST_ASSERT_EQ(state_info.owner, kAgcResourceOwnerCompute,
        "successful submit publishes buffer ownership");
    TEST_ASSERT_EQ(agcResetCommandBuffer(compute_command), AGC_OK,
        "submitted transition command resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "submitted transition fence resets");

    transition.before = kAgcResourceUsageShaderWrite;
    transition.after = kAgcResourceUsageHostRead;
    transition.before_owner = kAgcResourceOwnerCompute;
    transition.after_owner = kAgcResourceOwnerHost;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(compute_command), AGC_OK,
        "compute-to-host transition command begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(compute_command, 1u,
        &transition), AGC_OK, "compute-to-host transition records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(compute_command), AGC_OK,
        "compute-to-host transition command ends");
    TEST_ASSERT_EQ(agcQueueSubmit(compute_queue, &submit, fence), AGC_OK,
        "compute-to-host transition submits");
    captured = agcDriverDebugLastAcbSubmit(&owner);
    TEST_ASSERT(captured != NULL && owner != UINT32_MAX,
        "compute transition uses the qualified generic ACB carrier");
    words = (const uint32_t *)(uintptr_t)captured->command_address;
    TEST_ASSERT_EQ(agcPm4Opcode(words[0]), AGC_PM4_OP_RELEASE_MEM,
        "compute-to-host transition emits qualified release");
    TEST_ASSERT_EQ(agcGetFenceStatus(fence), AGC_OK,
        "compute-to-host transition completes on host");
    TEST_ASSERT_EQ(agcResetCommandBuffer(compute_command), AGC_OK,
        "compute-to-host command resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "compute-to-host fence resets");

    /* A v2 handoff must first submit the source-side release. The destination
     * then records the exact label wait and publishes state only on submit. */
    transition.before = kAgcResourceUsageHostRead;
    transition.after = kAgcResourceUsageShaderWrite;
    transition.before_owner = kAgcResourceOwnerHost;
    transition.after_owner = kAgcResourceOwnerCompute;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(compute_command), AGC_OK,
        "handoff setup command begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(compute_command, 1u,
        &transition), AGC_OK, "handoff setup records compute ownership");
    TEST_ASSERT_EQ(agcEndCommandBuffer(compute_command), AGC_OK,
        "handoff setup command ends");
    TEST_ASSERT_EQ(agcQueueSubmit(compute_queue, &submit, fence), AGC_OK,
        "handoff setup submits");
    TEST_ASSERT_EQ(agcResetCommandBuffer(compute_command), AGC_OK,
        "handoff setup command resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "handoff setup fence resets");
    TEST_ASSERT_EQ(agcCreateGpuLabel(device, &label_desc, &label), AGC_OK,
        "handoff dependency label creates");
    command_desc.queue_type = kAgcQueueGraphics;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
        &graphics_command), AGC_OK, "handoff graphics command creates");

    handoff.resource_type = kAgcResourceTypeBuffer;
    handoff.buffer = buffer;
    handoff.buffer_size = buffer_desc.size;
    handoff.before = kAgcResourceUsageShaderWrite;
    handoff.after = kAgcResourceUsageShaderRead;
    handoff.before_owner = kAgcResourceOwnerCompute;
    handoff.after_owner = kAgcResourceOwnerGraphics;
    handoff.flags = AGC_RESOURCE_TRANSITION_RELEASE_BIT;
    handoff.dependency_label = label;
    handoff.dependency_value = 1u;
    handoff.buffer_offset = 16u;
    handoff.buffer_size = 32u;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(compute_command), AGC_OK,
        "partial handoff probe command begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(compute_command, 1u, &handoff),
        AGC_OK, "partial cross-queue ownership transfer records");
    TEST_ASSERT_EQ(agcResetCommandBuffer(compute_command), AGC_OK,
        "unsubmitted partial handoff resets without publishing state");
    handoff.buffer_offset = 0u;
    handoff.buffer_size = buffer_desc.size;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(compute_command), AGC_OK,
        "handoff release command begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(compute_command, 1u, &handoff),
        AGC_OK, "handoff release records on source queue");
    TEST_ASSERT_EQ(agcEndCommandBuffer(compute_command), AGC_OK,
        "handoff release command ends");

    handoff.flags = AGC_RESOURCE_TRANSITION_ACQUIRE_BIT;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(graphics_command), AGC_OK,
        "premature handoff acquire command begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(graphics_command, 1u, &handoff),
        AGC_ERROR_INVALID_STATE,
        "handoff acquire rejects before source submit commits release");
    TEST_ASSERT_EQ(agcResetCommandBuffer(graphics_command), AGC_OK,
        "premature handoff acquire reset preserves resource state");
    handoff.flags = AGC_RESOURCE_TRANSITION_RELEASE_BIT;
    submit.command_buffers = &compute_command;
    TEST_ASSERT_EQ(agcQueueSubmit(compute_queue, &submit, fence), AGC_OK,
        "handoff release submits");
    TEST_ASSERT_EQ(agcGetBufferStateInfo(buffer, &state_info), AGC_OK,
        "released buffer state snapshot succeeds");
    TEST_ASSERT_EQ(state_info.usage, kAgcResourceUsageShaderWrite,
        "release preserves committed source usage");
    TEST_ASSERT_EQ(state_info.owner, kAgcResourceOwnerCompute,
        "release preserves committed source owner");
    TEST_ASSERT_EQ(state_info.flags,
        AGC_RESOURCE_STATE_TRANSFER_PENDING_BIT,
        "release snapshot exposes pending ownership transfer");
    TEST_ASSERT_EQ(state_info.transfer_usage, kAgcResourceUsageShaderRead,
        "release snapshot exposes destination usage");
    TEST_ASSERT_EQ(state_info.transfer_owner, kAgcResourceOwnerGraphics,
        "release snapshot exposes destination owner");
    TEST_ASSERT(state_info.transfer_label == label,
        "release snapshot exposes dependency label");
    TEST_ASSERT_EQ(state_info.transfer_value, 1u,
        "release snapshot exposes dependency point");
    TEST_ASSERT_EQ(state_info.recorded_reference_count, 1u,
        "submitted release remains retained until reset");
    TEST_ASSERT_EQ(agcResetCommandBuffer(compute_command), AGC_OK,
        "handoff release command resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "handoff release fence resets");
    TEST_ASSERT_EQ(agcDestroyBuffer(buffer), AGC_ERROR_BUSY,
        "pending handoff retains resource until destination acquire submits");

    handoff.flags = AGC_RESOURCE_TRANSITION_ACQUIRE_BIT;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(graphics_command), AGC_OK,
        "handoff acquire command begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(graphics_command, 1u, &handoff),
        AGC_OK, "handoff acquire records exact wait and invalidate");
    TEST_ASSERT_EQ(agcGetBufferStateInfo(buffer, &state_info), AGC_OK,
        "recorded acquire state snapshot succeeds");
    TEST_ASSERT_EQ(state_info.flags,
        AGC_RESOURCE_STATE_TRANSFER_PENDING_BIT |
            AGC_RESOURCE_STATE_ACQUIRE_RECORDED_BIT,
        "acquire snapshot distinguishes reservation from publication");
    TEST_ASSERT_EQ(state_info.recorded_reference_count, 1u,
        "recorded acquire retains its buffer");
    TEST_ASSERT_EQ(agcEndCommandBuffer(graphics_command), AGC_OK,
        "handoff acquire command ends");
    submit.command_buffers = &graphics_command;
    TEST_ASSERT_EQ(agcQueueSubmit(graphics_queue, &submit, fence), AGC_OK,
        "handoff acquire submits on destination queue");
    TEST_ASSERT_EQ(agcGetBufferStateInfo(buffer, &state_info), AGC_OK,
        "submitted acquire state snapshot succeeds");
    TEST_ASSERT_EQ(state_info.flags, 0u,
        "successful acquire clears pending transfer diagnostics");
    TEST_ASSERT_EQ(state_info.usage, kAgcResourceUsageShaderRead,
        "successful acquire publishes destination usage");
    TEST_ASSERT_EQ(state_info.owner, kAgcResourceOwnerGraphics,
        "successful acquire publishes destination owner");
    TEST_ASSERT_EQ(agcGetFenceStatus(fence), AGC_OK,
        "handoff acquire completion is observable on host");
    TEST_ASSERT_EQ(agcResetCommandBuffer(graphics_command), AGC_OK,
        "handoff acquire command resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "handoff acquire fence resets");
    TEST_ASSERT_EQ(agcDestroyGpuLabel(label), AGC_OK,
        "handoff dependency label destroys after both command resets");
    label = NULL;
    transition.before = kAgcResourceUsageShaderRead;
    transition.after = kAgcResourceUsageShaderWrite;
    transition.before_owner = kAgcResourceOwnerGraphics;
    transition.after_owner = kAgcResourceOwnerCompute;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(compute_command), AGC_OK,
        "post-handoff source mismatch command begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(compute_command, 1u,
        &transition), AGC_ERROR_NOT_SUPPORTED,
        "post-handoff state publishes to destination ownership only");
    TEST_ASSERT_EQ(agcResetCommandBuffer(compute_command), AGC_OK,
        "post-handoff source mismatch command resets");
    transition.before = kAgcResourceUsageShaderRead;
    transition.after = kAgcResourceUsageHostRead;
    transition.before_owner = kAgcResourceOwnerGraphics;
    transition.after_owner = kAgcResourceOwnerHost;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(graphics_command), AGC_OK,
        "post-handoff host transition command begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(graphics_command, 1u,
        &transition), AGC_OK, "post-handoff host transition records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(graphics_command), AGC_OK,
        "post-handoff host transition command ends");
    submit.command_buffers = &graphics_command;
    TEST_ASSERT_EQ(agcQueueSubmit(graphics_queue, &submit, fence), AGC_OK,
        "post-handoff host transition submits");
    TEST_ASSERT_EQ(agcResetCommandBuffer(graphics_command), AGC_OK,
        "post-handoff host transition command resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "post-handoff host transition fence resets");

    transition.before = kAgcResourceUsageHostRead;
    transition.after = kAgcResourceUsageUndefined;
    transition.before_owner = kAgcResourceOwnerHost;
    transition.after_owner = kAgcResourceOwnerHost;
    duplicate[0] = transition;
    duplicate[1] = transition;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(compute_command), AGC_OK,
        "duplicate transition command begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(compute_command, 2u, duplicate),
        AGC_ERROR_NOT_SUPPORTED,
        "duplicate whole-resource transitions reject atomically");
    TEST_ASSERT_EQ(agcResetCommandBuffer(compute_command), AGC_OK,
        "rejected duplicate transition command resets");

    transition.buffer_offset = 16u;
    transition.buffer_size = 32u;
    transition.before = kAgcResourceUsageHostRead;
    transition.after = kAgcResourceUsageCopySource;
    transition.before_owner = kAgcResourceOwnerHost;
    transition.after_owner = kAgcResourceOwnerCompute;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(compute_command), AGC_OK,
        "partial transition command begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(compute_command, 1u,
        &transition), AGC_OK,
        "partial buffer transition records");
    state_info = (AgcResourceStateInfo)AGC_RESOURCE_STATE_INFO_INIT;
    TEST_ASSERT_EQ(agcGetBufferStateInfo(buffer, &state_info), AGC_OK,
        "recorded partial transition leaves committed whole state uniform");
    TEST_ASSERT_EQ(state_info.usage, kAgcResourceUsageHostRead,
        "recorded partial transition does not publish before submit");
    TEST_ASSERT_EQ(agcEndCommandBuffer(compute_command), AGC_OK,
        "partial transition command ends");
    submit.command_buffers = &compute_command;
    TEST_ASSERT_EQ(agcQueueSubmit(compute_queue, &submit, fence), AGC_OK,
        "partial buffer transition submits");
    state_info = (AgcResourceStateInfo)AGC_RESOURCE_STATE_INFO_INIT;
    TEST_ASSERT_EQ(agcGetBufferStateInfo(buffer, &state_info),
        AGC_ERROR_NOT_SUPPORTED,
        "fragmented buffer rejects an ambiguous whole-state snapshot");
    TEST_ASSERT_EQ(agcGetBufferRangeStateInfo(buffer, 16u, 32u,
        &state_info), AGC_OK, "partial destination range is queryable");
    TEST_ASSERT_EQ(state_info.usage, kAgcResourceUsageCopySource,
        "partial destination range publishes exact usage");
    TEST_ASSERT_EQ(state_info.owner, kAgcResourceOwnerCompute,
        "partial destination range publishes exact owner");
    state_info = (AgcResourceStateInfo)AGC_RESOURCE_STATE_INFO_INIT;
    TEST_ASSERT_EQ(agcGetBufferRangeStateInfo(buffer, 0u, 16u,
        &state_info), AGC_OK, "left untouched range is queryable");
    TEST_ASSERT_EQ(state_info.usage, kAgcResourceUsageHostRead,
        "left untouched range preserves prior usage");
    TEST_ASSERT_EQ(state_info.owner, kAgcResourceOwnerHost,
        "left untouched range preserves prior owner");
    state_info = (AgcResourceStateInfo)AGC_RESOURCE_STATE_INFO_INIT;
    TEST_ASSERT_EQ(agcGetBufferRangeStateInfo(buffer, 8u, 16u,
        &state_info), AGC_ERROR_NOT_SUPPORTED,
        "mixed-state range query fails closed");
    TEST_ASSERT_EQ(agcResetCommandBuffer(compute_command), AGC_OK,
        "partial transition command resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "partial transition fence resets");

    transition.before = kAgcResourceUsageCopySource;
    transition.after = kAgcResourceUsageHostRead;
    transition.before_owner = kAgcResourceOwnerCompute;
    transition.after_owner = kAgcResourceOwnerHost;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(compute_command), AGC_OK,
        "partial merge command begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(compute_command, 1u,
        &transition), AGC_OK, "partial range records its prior state");
    TEST_ASSERT_EQ(agcEndCommandBuffer(compute_command), AGC_OK,
        "partial merge command ends");
    TEST_ASSERT_EQ(agcQueueSubmit(compute_queue, &submit, fence), AGC_OK,
        "partial merge transition submits");
    state_info = (AgcResourceStateInfo)AGC_RESOURCE_STATE_INFO_INIT;
    TEST_ASSERT_EQ(agcGetBufferStateInfo(buffer, &state_info), AGC_OK,
        "adjacent identical states merge to a whole-buffer snapshot");
    TEST_ASSERT_EQ(state_info.usage, kAgcResourceUsageHostRead,
        "merged whole-buffer usage matches original state");
    TEST_ASSERT_EQ(agcResetCommandBuffer(compute_command), AGC_OK,
        "partial merge command resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "partial merge fence resets");

    transition.buffer_offset = buffer_desc.size;
    transition.buffer_size = 1u;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(compute_command), AGC_OK,
        "invalid partial transition command begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(compute_command, 1u,
        &transition), AGC_ERROR_INVALID_ARGUMENT,
        "out-of-bounds partial transition fails closed");
    transition.buffer_offset = 0u;
    transition.buffer_size = buffer_desc.size;
    transition.before_owner = kAgcResourceOwnerGraphics;
    TEST_ASSERT_EQ(agcCmdTransitionResources(compute_command, 1u,
        &transition), AGC_ERROR_INVALID_STATE,
        "transition source owner must match committed state");
    TEST_ASSERT_EQ(agcResetCommandBuffer(compute_command), AGC_OK,
        "failed transition command resets without state commit");

    transition.buffer_offset = 0u;
    transition.buffer_size = buffer_desc.size;
    transition.before = kAgcResourceUsageHostRead;
    transition.after = kAgcResourceUsageUndefined;
    transition.before_owner = kAgcResourceOwnerHost;
    transition.after_owner = kAgcResourceOwnerHost;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(compute_command), AGC_OK,
        "discard transition command begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(compute_command, 1u,
        &transition), AGC_OK,
        "host-read-to-undefined discard transition records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(compute_command), AGC_OK,
        "discard transition command ends");
    submit.command_buffers = &compute_command;
    TEST_ASSERT_EQ(agcQueueSubmit(compute_queue, &submit, fence), AGC_OK,
        "discard transition submits and commits state");
    TEST_ASSERT_EQ(agcResetCommandBuffer(compute_command), AGC_OK,
        "discard transition command resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "discard transition fence resets");

    image_desc.width = 8u;
    image_desc.height = 8u;
    image_desc.format = AGC_FORMAT_RGBA8_UNORM;
    image_desc.usage = AGC_IMAGE_USAGE_COLOR_TARGET_BIT |
        AGC_IMAGE_USAGE_TRANSFER_SRC_BIT | AGC_IMAGE_USAGE_TRANSFER_DST_BIT;
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &image), AGC_OK,
        "transition image creates");
    state_info = (AgcResourceStateInfo)AGC_RESOURCE_STATE_INFO_INIT;
    TEST_ASSERT_EQ(agcGetImageStateInfo(image, &state_info), AGC_OK,
        "new image state snapshot succeeds");
    TEST_ASSERT_EQ(state_info.resource_type, kAgcResourceTypeImage,
        "image state snapshot identifies resource type");
    TEST_ASSERT_EQ(state_info.usage, kAgcResourceUsageUndefined,
        "new image state is undefined");
    TEST_ASSERT_EQ(state_info.owner, kAgcResourceOwnerHost,
        "new image state is host-owned");
    TEST_ASSERT_EQ(state_info.recorded_reference_count, 0u,
        "new image state has no command references");
    TEST_ASSERT_EQ(state_info.dependency_reference_count, 0u,
        "new image state has no dependent objects");
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &second_image), AGC_OK,
        "second transition image creates");
    image_transitions[0].resource_type = kAgcResourceTypeImage;
    image_transitions[0].image = image;
    image_transitions[0].before = kAgcResourceUsageUndefined;
    image_transitions[0].after = kAgcResourceUsageColorTarget;
    image_transitions[0].before_owner = kAgcResourceOwnerHost;
    image_transitions[0].after_owner = kAgcResourceOwnerGraphics;
    image_transitions[1] = image_transitions[0];
    image_transitions[1].image = second_image;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(graphics_command), AGC_OK,
        "image transition command begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(graphics_command, 2u,
        image_transitions), AGC_OK,
        "distinct undefined-to-color transitions record atomically");
    TEST_ASSERT_EQ(agcEndCommandBuffer(graphics_command), AGC_OK,
        "image transition command ends");
    submit.command_buffers = &graphics_command;
    TEST_ASSERT_EQ(agcQueueSubmit(graphics_queue, &submit, fence), AGC_OK,
        "undefined-to-color transition submits");
    TEST_ASSERT_EQ(agcGetImageStateInfo(image, &state_info), AGC_OK,
        "submitted image state snapshot succeeds");
    TEST_ASSERT_EQ(state_info.usage, kAgcResourceUsageColorTarget,
        "successful submit publishes image usage");
    TEST_ASSERT_EQ(state_info.owner, kAgcResourceOwnerGraphics,
        "successful submit publishes image ownership");
    TEST_ASSERT_EQ(state_info.recorded_reference_count, 1u,
        "submitted image remains retained until command reset");
    TEST_ASSERT_EQ(agcGetFenceStatus(fence), AGC_OK,
        "image transition completes on host");
    TEST_ASSERT_EQ(agcResetCommandBuffer(graphics_command), AGC_OK,
        "image transition command resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "image transition fence resets");

    image_transitions[0].before = kAgcResourceUsageColorTarget;
    image_transitions[0].after = kAgcResourceUsageHostRead;
    image_transitions[0].before_owner = kAgcResourceOwnerGraphics;
    image_transitions[0].after_owner = kAgcResourceOwnerHost;
    image_transitions[1] = image_transitions[0];
    image_transitions[1].image = second_image;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(graphics_command), AGC_OK,
        "color-to-host transition command begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(graphics_command, 2u,
        image_transitions), AGC_OK,
        "distinct color-to-host transitions record atomically");
    TEST_ASSERT_EQ(agcEndCommandBuffer(graphics_command), AGC_OK,
        "color-to-host transition command ends");
    TEST_ASSERT_EQ(agcQueueSubmit(graphics_queue, &submit, fence), AGC_OK,
        "color-to-host transition submits");
    captured = agcDriverDebugLastDcbSubmit();
    words = (const uint32_t *)(uintptr_t)captured->command_address;
    TEST_ASSERT_EQ(agcPm4Opcode(words[0]), AGC_PM4_OP_RELEASE_MEM,
        "color-to-host transition emits qualified release");
    TEST_ASSERT_EQ(captured->dword_count,
        2u * AGC_GFX1013_EOP_FENCE_DWORDS,
        "two color transitions emit two qualified releases");
    TEST_ASSERT_EQ(agcGetFenceStatus(fence), AGC_OK,
        "color-to-host transition completes on host");
    TEST_ASSERT_EQ(agcResetCommandBuffer(graphics_command), AGC_OK,
        "color-to-host command resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "color-to-host fence resets before image handoff");

    image_desc.usage = AGC_IMAGE_USAGE_COLOR_TARGET_BIT |
        AGC_IMAGE_USAGE_SAMPLED_BIT | AGC_IMAGE_USAGE_TRANSFER_SRC_BIT;
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &handoff_image), AGC_OK,
        "cross-queue handoff image creates");
    image_transitions[0] = (AgcResourceTransition)
        AGC_RESOURCE_TRANSITION_INIT;
    image_transitions[0].resource_type = kAgcResourceTypeImage;
    image_transitions[0].image = handoff_image;
    image_transitions[0].before = kAgcResourceUsageUndefined;
    image_transitions[0].after = kAgcResourceUsageColorTarget;
    image_transitions[0].before_owner = kAgcResourceOwnerHost;
    image_transitions[0].after_owner = kAgcResourceOwnerGraphics;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(graphics_command), AGC_OK,
        "image-handoff setup command begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(graphics_command, 1u,
        image_transitions), AGC_OK,
        "image-handoff setup records graphics ownership");
    TEST_ASSERT_EQ(agcEndCommandBuffer(graphics_command), AGC_OK,
        "image-handoff setup command ends");
    submit.command_buffers = &graphics_command;
    TEST_ASSERT_EQ(agcQueueSubmit(graphics_queue, &submit, fence), AGC_OK,
        "image-handoff setup submits");
    TEST_ASSERT_EQ(agcResetCommandBuffer(graphics_command), AGC_OK,
        "image-handoff setup command resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "image-handoff setup fence resets");
    TEST_ASSERT_EQ(agcCreateGpuLabel(device, &label_desc, &image_label), AGC_OK,
        "image-handoff dependency label creates");

    image_handoff.resource_type = kAgcResourceTypeImage;
    image_handoff.image = handoff_image;
    image_handoff.image_range.aspect_mask = AGC_IMAGE_ASPECT_COLOR_BIT;
    image_handoff.image_range.mip_level_count = image_desc.mip_levels;
    image_handoff.image_range.array_layer_count = image_desc.array_layers;
    image_handoff.before = kAgcResourceUsageColorTarget;
    image_handoff.after = kAgcResourceUsageShaderRead;
    image_handoff.before_owner = kAgcResourceOwnerGraphics;
    image_handoff.after_owner = kAgcResourceOwnerCompute;
    image_handoff.flags = AGC_RESOURCE_TRANSITION_RELEASE_BIT;
    image_handoff.dependency_label = image_label;
    image_handoff.dependency_value = 1u;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(graphics_command), AGC_OK,
        "image-handoff release command begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(graphics_command, 1u,
        &image_handoff), AGC_OK,
        "image-handoff release records color-write completion");
    TEST_ASSERT_EQ(agcEndCommandBuffer(graphics_command), AGC_OK,
        "image-handoff release command ends");
    TEST_ASSERT_EQ(agcQueueSubmit(graphics_queue, &submit, fence), AGC_OK,
        "image-handoff release submits on graphics");
    captured = agcDriverDebugLastDcbSubmit();
    words = (const uint32_t *)(uintptr_t)captured->command_address;
    TEST_ASSERT_EQ(agcPm4Opcode(words[0]), AGC_PM4_OP_RELEASE_MEM,
        "image-handoff release emits the qualified EOP signal");
    TEST_ASSERT_EQ(agcResetCommandBuffer(graphics_command), AGC_OK,
        "image-handoff release command resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "image-handoff release fence resets");
    TEST_ASSERT_EQ(agcDestroyImage(handoff_image), AGC_ERROR_BUSY,
        "pending image handoff prevents premature destruction");

    image_handoff.flags = AGC_RESOURCE_TRANSITION_ACQUIRE_BIT;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(compute_command), AGC_OK,
        "image-handoff acquire command begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(compute_command, 1u,
        &image_handoff), AGC_OK,
        "image-handoff acquire records wait and invalidate");
    TEST_ASSERT_EQ(agcEndCommandBuffer(compute_command), AGC_OK,
        "image-handoff acquire command ends");
    submit.command_buffers = &compute_command;
    TEST_ASSERT_EQ(agcQueueSubmit(compute_queue, &submit, fence), AGC_OK,
        "image-handoff acquire submits on compute");
    captured = agcDriverDebugLastAcbSubmit(&owner);
    words = (const uint32_t *)(uintptr_t)captured->command_address;
    TEST_ASSERT_EQ(agcPm4Opcode(words[0]), AGC_PM4_OP_WAIT_REG_MEM,
        "image-handoff acquire waits for the exact release value");
    TEST_ASSERT_EQ(agcPm4Opcode(words[7]), AGC_PM4_OP_ACQUIRE_MEM,
        "image-handoff acquire invalidates caches after its wait");
    TEST_ASSERT_EQ(agcResetCommandBuffer(compute_command), AGC_OK,
        "image-handoff acquire command resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "image-handoff acquire fence resets");
    TEST_ASSERT_EQ(agcDestroyGpuLabel(image_label), AGC_OK,
        "image-handoff dependency label destroys after command reset");
    image_label = NULL;

    image_transitions[0] = (AgcResourceTransition)
        AGC_RESOURCE_TRANSITION_INIT;
    image_transitions[0].resource_type = kAgcResourceTypeImage;
    image_transitions[0].image = handoff_image;
    image_transitions[0].before = kAgcResourceUsageShaderRead;
    image_transitions[0].after = kAgcResourceUsageHostRead;
    image_transitions[0].before_owner = kAgcResourceOwnerCompute;
    image_transitions[0].after_owner = kAgcResourceOwnerHost;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(compute_command), AGC_OK,
        "post-handoff image host transition begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(compute_command, 1u,
        image_transitions), AGC_OK,
        "post-handoff image state is owned by compute");
    TEST_ASSERT_EQ(agcEndCommandBuffer(compute_command), AGC_OK,
        "post-handoff image host transition ends");
    TEST_ASSERT_EQ(agcQueueSubmit(compute_queue, &submit, fence), AGC_OK,
        "post-handoff image host transition submits");
    TEST_ASSERT_EQ(agcResetCommandBuffer(compute_command), AGC_OK,
        "post-handoff image host transition resets");
    TEST_ASSERT_EQ(agcDestroyImage(handoff_image), AGC_OK,
        "acquired image destroys after final state submit");
    handoff_image = NULL;
    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK, "transition fence destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(graphics_command), AGC_OK,
        "transition graphics command destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(compute_command), AGC_OK,
        "transition compute command destroys");
    TEST_ASSERT_EQ(agcDestroyImage(second_image), AGC_OK,
        "second transition image destroys");
    TEST_ASSERT_EQ(agcDestroyImage(image), AGC_OK, "transition image destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(buffer), AGC_OK, "transition buffer destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(graphics_queue), AGC_OK,
        "transition graphics queue destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(compute_queue), AGC_OK,
        "transition compute queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "transition device destroys");
}

static void test_runtime_sampled_image_handoff(void)
{
    AgcDevice device = create_device();
    AgcQueue graphics_queue = create_queue(device, kAgcQueueGraphics);
    AgcQueue compute_queue = create_queue(device, kAgcQueueCompute);
    AgcShaderReflection requirements = AGC_SHADER_REFLECTION_INIT;
    AgcShaderDescriptorMapping mappings[2] = {
        {0u, 0u, AGC_SHADER_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,
         1u, 0u, 64u},
        {0u, 1u, AGC_SHADER_DESCRIPTOR_STORAGE_BUFFER,
         1u, 64u, 16u},
    };
    AgcComputePipelineDesc pipeline_desc = AGC_COMPUTE_PIPELINE_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
    AgcImageViewDesc view_desc = AGC_IMAGE_VIEW_DESC_INIT;
    AgcSamplerDesc sampler_desc = AGC_SAMPLER_DESC_INIT;
    AgcBufferDesc output_desc = AGC_BUFFER_DESC_INIT;
    AgcGpuLabelDesc label_desc = AGC_GPU_LABEL_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcResourceTransition setup = AGC_RESOURCE_TRANSITION_INIT;
    AgcResourceTransition handoff = AGC_RESOURCE_TRANSITION_V2_INIT;
    AgcResourceTransition output_transition = AGC_RESOURCE_TRANSITION_INIT;
    AgcDescriptorWrite writes[2] = {
        AGC_DESCRIPTOR_WRITE_INIT,
        AGC_DESCRIPTOR_WRITE_INIT,
    };
    AgcShader shader = NULL;
    AgcComputePipeline pipeline = NULL;
    AgcImage image = NULL;
    AgcImageView view = NULL;
    AgcSampler sampler = NULL;
    AgcBuffer output = NULL;
    AgcGpuLabel label = NULL;
    AgcCommandBuffer graphics_command = NULL;
    AgcCommandBuffer compute_command = NULL;
    AgcFence fence = NULL;
    const AgcCommandBufferSubmit *captured;
    const uint32_t *words;
    uint32_t owner = UINT32_MAX;

    requirements.local_size_x = 8u;
    requirements.local_size_y = 8u;
    requirements.local_size_z = 1u;
    requirements.descriptor_mapping_count = 2u;
    requirements.descriptor_mappings[0] = mappings[0];
    requirements.descriptor_mappings[1] = mappings[1];
    requirements.user_sgpr_count = 1u;
    requirements.user_sgprs[0] = (AgcShaderUserSgpr){
        AGC_SHADER_USER_SGPR_DESCRIPTOR_SET, 0u,
        AGC_REG_COMPUTE_USER_DATA_0, 1u};
    shader = create_shader_with_reflection(
        device, kAgcShaderStageCs, &requirements);
    pipeline_desc.shader = shader;
    pipeline_desc.local_size_x = 8u;
    pipeline_desc.local_size_y = 8u;
    pipeline_desc.descriptor_mapping_count = 2u;
    pipeline_desc.descriptor_mappings = mappings;
    TEST_ASSERT_EQ(agcCreateComputePipeline(device, &pipeline_desc,
        &pipeline), AGC_OK,
        "sampled-image consumer pipeline creates");

    image_desc.width = 8u;
    image_desc.height = 8u;
    image_desc.format = AGC_FORMAT_RGBA8_UNORM;
    image_desc.usage = AGC_IMAGE_USAGE_COLOR_TARGET_BIT |
        AGC_IMAGE_USAGE_SAMPLED_BIT;
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &image), AGC_OK,
        "sampled-image handoff target creates");
    view_desc.image = image;
    view_desc.format = image_desc.format;
    TEST_ASSERT_EQ(agcCreateImageView(device, &view_desc, &view), AGC_OK,
        "sampled-image handoff view creates");
    TEST_ASSERT_EQ(agcCreateSampler(device, &sampler_desc, &sampler), AGC_OK,
        "sampled-image handoff sampler creates");
    output_desc.size = 8u * 8u * sizeof(uint32_t);
    output_desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &output_desc, &output), AGC_OK,
        "sampled-image consumer output creates");
    TEST_ASSERT_EQ(agcCreateGpuLabel(device, &label_desc, &label), AGC_OK,
        "sampled-image handoff label creates");
    command_desc.capacity_dwords = 4096u;
    command_desc.queue_type = kAgcQueueGraphics;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
        &graphics_command), AGC_OK,
        "sampled-image graphics command creates");
    command_desc.queue_type = kAgcQueueCompute;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
        &compute_command), AGC_OK,
        "sampled-image compute command creates");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "sampled-image handoff fence creates");

    setup.resource_type = kAgcResourceTypeImage;
    setup.image = image;
    setup.before = kAgcResourceUsageUndefined;
    setup.after = kAgcResourceUsageColorTarget;
    setup.before_owner = kAgcResourceOwnerHost;
    setup.after_owner = kAgcResourceOwnerGraphics;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(graphics_command), AGC_OK,
        "sampled-image setup command begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(graphics_command, 1u, &setup),
        AGC_OK, "sampled-image setup records graphics ownership");
    TEST_ASSERT_EQ(agcEndCommandBuffer(graphics_command), AGC_OK,
        "sampled-image setup command ends");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &graphics_command;
    TEST_ASSERT_EQ(agcQueueSubmit(graphics_queue, &submit, fence), AGC_OK,
        "sampled-image setup submits");
    TEST_ASSERT_EQ(agcResetCommandBuffer(graphics_command), AGC_OK,
        "sampled-image setup command resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "sampled-image setup fence resets");

    handoff.resource_type = kAgcResourceTypeImage;
    handoff.image = image;
    handoff.image_range.aspect_mask = AGC_IMAGE_ASPECT_COLOR_BIT;
    handoff.image_range.mip_level_count = 1u;
    handoff.image_range.array_layer_count = 1u;
    handoff.before = kAgcResourceUsageColorTarget;
    handoff.after = kAgcResourceUsageShaderRead;
    handoff.before_owner = kAgcResourceOwnerGraphics;
    handoff.after_owner = kAgcResourceOwnerCompute;
    handoff.flags = AGC_RESOURCE_TRANSITION_RELEASE_BIT;
    handoff.dependency_label = label;
    handoff.dependency_value = 1u;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(graphics_command), AGC_OK,
        "sampled-image release command begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(graphics_command, 1u, &handoff),
        AGC_OK, "sampled-image release records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(graphics_command), AGC_OK,
        "sampled-image release command ends");
    TEST_ASSERT_EQ(agcQueueSubmit(graphics_queue, &submit, fence), AGC_OK,
        "sampled-image release submits");
    TEST_ASSERT_EQ(agcResetCommandBuffer(graphics_command), AGC_OK,
        "sampled-image release command resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "sampled-image release fence resets");

    writes[0].set = 0u;
    writes[0].binding = 0u;
    writes[0].type = AGC_SHADER_DESCRIPTOR_COMBINED_IMAGE_SAMPLER;
    writes[0].image_view = view;
    writes[0].sampler = sampler;
    writes[1].set = 0u;
    writes[1].binding = 1u;
    writes[1].type = AGC_SHADER_DESCRIPTOR_STORAGE_BUFFER;
    writes[1].buffer = output;
    writes[1].buffer_range = output_desc.size;
    output_transition.resource_type = kAgcResourceTypeBuffer;
    output_transition.buffer = output;
    output_transition.buffer_size = output_desc.size;
    output_transition.before = kAgcResourceUsageUndefined;
    output_transition.after = kAgcResourceUsageShaderWrite;
    output_transition.before_owner = kAgcResourceOwnerHost;
    output_transition.after_owner = kAgcResourceOwnerCompute;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(compute_command), AGC_OK,
        "sampled-image acquire command begins");
    TEST_ASSERT_EQ(agcCmdBindComputePipeline(compute_command, pipeline),
        AGC_OK, "sampled-image consumer pipeline binds");
    TEST_ASSERT_EQ(agcCmdTransitionResources(compute_command, 1u,
        &output_transition), AGC_OK,
        "sampled-image output transitions to shader write");
    TEST_ASSERT_EQ(agcCmdBindDescriptors(compute_command, 2u, writes),
        AGC_ERROR_INVALID_STATE,
        "sampled image rejects descriptor binding before acquire");
    handoff.flags = AGC_RESOURCE_TRANSITION_ACQUIRE_BIT;
    TEST_ASSERT_EQ(agcCmdTransitionResources(compute_command, 1u, &handoff),
        AGC_OK, "sampled-image acquire records exact dependency");
    TEST_ASSERT_EQ(agcCmdBindDescriptors(compute_command, 2u, writes), AGC_OK,
        "acquired sampled image binds to consumer shader");
    TEST_ASSERT_EQ(agcCmdDispatch(compute_command, 1u, 1u, 1u), AGC_OK,
        "sampled-image consumer dispatch records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(compute_command), AGC_OK,
        "sampled-image acquire command ends");
    submit.command_buffers = &compute_command;
    TEST_ASSERT_EQ(agcQueueSubmit(compute_queue, &submit, fence), AGC_OK,
        "sampled-image acquire and consumer submit");
    captured = agcDriverDebugLastAcbSubmit(&owner);
    words = (const uint32_t *)(uintptr_t)captured->command_address;
    TEST_ASSERT_EQ(agcPm4Opcode(words[0]), AGC_PM4_OP_WAIT_REG_MEM,
        "sampled-image consumer waits before descriptor use");
    TEST_ASSERT(runtime_has_opcode(words, captured->dword_count,
        AGC_PM4_OP_DISPATCH_DIRECT),
        "sampled-image consumer stream dispatches after acquire");
    TEST_ASSERT_EQ(agcResetCommandBuffer(compute_command), AGC_OK,
        "sampled-image consumer command resets");

    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK,
        "sampled-image handoff fence destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(compute_command), AGC_OK,
        "sampled-image compute command destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(graphics_command), AGC_OK,
        "sampled-image graphics command destroys");
    TEST_ASSERT_EQ(agcDestroyGpuLabel(label), AGC_OK,
        "sampled-image handoff label destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(output), AGC_OK,
        "sampled-image output destroys");
    TEST_ASSERT_EQ(agcDestroySampler(sampler), AGC_OK,
        "sampled-image sampler destroys");
    TEST_ASSERT_EQ(agcDestroyImageView(view), AGC_OK,
        "sampled-image view destroys");
    TEST_ASSERT_EQ(agcDestroyImage(image), AGC_OK,
        "sampled-image target destroys");
    TEST_ASSERT_EQ(agcDestroyComputePipeline(pipeline), AGC_OK,
        "sampled-image consumer pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyShader(shader), AGC_OK,
        "sampled-image consumer shader destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(compute_queue), AGC_OK,
        "sampled-image compute queue destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(graphics_queue), AGC_OK,
        "sampled-image graphics queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "sampled-image device destroys");
}

static void test_runtime_color_target_binding(void)
{
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueGraphics);
    AgcShaderReflection pixel_requirements = AGC_SHADER_REFLECTION_INIT;
    AgcColorBlendAttachmentState attachment =
        AGC_COLOR_BLEND_ATTACHMENT_STATE_INIT;
    AgcGraphicsPipelineDesc pipeline_desc = AGC_GRAPHICS_PIPELINE_DESC_INIT;
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcColorTargetBinding target = AGC_COLOR_TARGET_BINDING_INIT;
    AgcResourceTransition target_transition = AGC_RESOURCE_TRANSITION_INIT;
    AgcGraphicsPipeline pipeline = NULL;
    AgcShader vertex = create_shader(device, kAgcShaderStageVs);
    AgcShader pixel;
    AgcImage image = NULL;
    AgcImage incompatible_image = NULL;
    AgcBuffer index_buffer = NULL;
    AgcCommandBuffer command = NULL;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    const AgcCommandBufferSubmit *captured;
    const uint32_t *words;
    uint32_t value;

    pixel_requirements.color_export_count = 1u;
    pixel_requirements.color_exports[0] = (AgcShaderColorExport){
        0u, AGC_SHADER_COLOR_EXPORT_32_ABGR,
        AGC_SHADER_COMPONENT_FLOAT_OR_NORMALIZED, 0xfu, 0u};
    pixel = create_shader_with_reflection(
        device, kAgcShaderStagePs, &pixel_requirements);
    attachment.format = AGC_FORMAT_RGBA8_UNORM;
    pipeline_desc.vertex_shader = vertex;
    pipeline_desc.pixel_shader = pixel;
    pipeline_desc.color_attachment_count = 1u;
    pipeline_desc.color_attachments = &attachment;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &pipeline_desc, &pipeline),
        AGC_OK, "color-target graphics pipeline creates");

    image_desc.width = 64u;
    image_desc.height = 32u;
    image_desc.mip_levels = 2u;
    image_desc.array_layers = 2u;
    image_desc.format = AGC_FORMAT_RGBA8_UNORM;
    image_desc.usage = AGC_IMAGE_USAGE_COLOR_TARGET_BIT;
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &image), AGC_OK,
        "native RGBA8 color target creates");
    image_desc.format = AGC_FORMAT_RGBA16_FLOAT;
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &incompatible_image),
        AGC_OK, "incompatible color target creates for validation");
    buffer_desc.size = 64u;
    buffer_desc.usage = AGC_BUFFER_USAGE_INDEX_BIT;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &index_buffer), AGC_OK,
        "color-target index buffer creates");
    command_desc.capacity_dwords = 4096u;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &command),
        AGC_OK, "color-target command buffer creates");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "color-target command buffer begins");
    TEST_ASSERT_EQ(agcCmdBindGraphicsPipeline(command, pipeline), AGC_OK,
        "color-target graphics pipeline binds");
    TEST_ASSERT_EQ(runtime_transition_buffer_to_graphics_read(command,
        index_buffer, buffer_desc.size, kAgcResourceUsageUndefined, 0u),
        AGC_OK, "color-target index buffer transitions to graphics read");
    TEST_ASSERT_EQ(agcCmdBindIndexBuffer(command, index_buffer, 0u,
        kAgcIndexSize16), AGC_OK, "color-target index buffer binds");
    TEST_ASSERT_EQ(agcCmdDrawIndexed(command, 3u, 1u, 0u, 0, 0u),
        AGC_ERROR_RESOURCE_NOT_BOUND,
        "draw with reflected color exports requires color targets");

    target.image = incompatible_image;
    target.mip_level = 0u;
    target.array_layer = 1u;
    TEST_ASSERT_EQ(agcCmdBindColorTargets(command, 1u, &target),
        AGC_ERROR_VALIDATION_FAILED,
        "format-mismatched target fails before retention or packet emission");
    TEST_ASSERT_EQ(agcDestroyImage(incompatible_image), AGC_OK,
        "rejected color target remains destroyable");
    target.image = image;
    TEST_ASSERT_EQ(agcCmdBindColorTargets(command, 1u, &target),
        AGC_ERROR_INVALID_STATE,
        "untransitioned color target cannot bind on the graphics queue");
    target_transition.resource_type = kAgcResourceTypeImage;
    target_transition.image = image;
    target_transition.before = kAgcResourceUsageUndefined;
    target_transition.after = kAgcResourceUsageColorTarget;
    target_transition.before_owner = kAgcResourceOwnerHost;
    target_transition.after_owner = kAgcResourceOwnerGraphics;
    target_transition.image_range.mip_level_count = 2u;
    target_transition.image_range.array_layer_count = 2u;
    TEST_ASSERT_EQ(agcCmdTransitionResources(command, 1u,
        &target_transition), AGC_OK,
        "color target transitions to graphics ownership before binding");
    TEST_ASSERT_EQ(agcCmdBindColorTargets(command, 1u, &target), AGC_OK,
        "matching color target emits its native binding state");
    TEST_ASSERT_EQ(agcDestroyImage(image), AGC_ERROR_BUSY,
        "recorded color target remains retained by the command buffer");
    TEST_ASSERT_EQ(agcCmdBindColorTargets(command, 1u, &target),
        AGC_ERROR_NOT_SUPPORTED,
        "color targets cannot be replaced within one command buffer");
    TEST_ASSERT_EQ(agcCmdDrawIndexed(command, 3u, 1u, 0u, 0, 0u), AGC_OK,
        "draw records after its reflected target binds");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command), AGC_OK,
        "color-target command buffer becomes executable");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &command;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, NULL), AGC_OK,
        "color-target command buffer submits through the host carrier");
    captured = agcDriverDebugLastDcbSubmit();
    words = (const uint32_t *)(uintptr_t)captured->command_address;
    TEST_ASSERT(runtime_find_context_register(words, captured->dword_count,
        AGC_REG_CB_COLOR0_BASE, &value) && value != 0u,
        "color-target binding records CB_COLOR0_BASE");
    TEST_ASSERT(runtime_find_context_register(words, captured->dword_count,
        AGC_REG_CB_COLOR0_INFO, &value),
        "color-target binding records CB_COLOR0_INFO");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
        "reset releases the retained color target");
    TEST_ASSERT_EQ(agcDestroyImage(image), AGC_OK,
        "released color target destroys after reset");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command), AGC_OK,
        "color-target command buffer destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(index_buffer), AGC_OK,
        "color-target index buffer destroys");
    TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(pipeline), AGC_OK,
        "color-target graphics pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyShader(pixel), AGC_OK,
        "color-target pixel shader destroys");
    TEST_ASSERT_EQ(agcDestroyShader(vertex), AGC_OK,
        "color-target vertex shader destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK,
        "color-target graphics queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "color-target device destroys");
}

static void test_runtime_mrt_color_target_binding(void)
{
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueGraphics);
    AgcShader vertex = create_shader(device, kAgcShaderStageVs);
    AgcShaderReflection pixel_requirements = AGC_SHADER_REFLECTION_INIT;
    AgcGraphicsPipelineDesc pipeline_desc = AGC_GRAPHICS_PIPELINE_DESC_INIT;
    AgcColorBlendAttachmentState attachments[2] = {
        AGC_COLOR_BLEND_ATTACHMENT_STATE_INIT,
        AGC_COLOR_BLEND_ATTACHMENT_STATE_INIT,
    };
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcColorTargetBinding targets[2] = {
        AGC_COLOR_TARGET_BINDING_INIT,
        AGC_COLOR_TARGET_BINDING_INIT,
    };
    AgcResourceTransition target_transitions[2] = {
        AGC_RESOURCE_TRANSITION_INIT,
        AGC_RESOURCE_TRANSITION_INIT,
    };
    AgcGraphicsPipeline pipeline = NULL;
    AgcShader pixel = NULL;
    AgcImage first_image = NULL;
    AgcImage second_image = NULL;
    AgcImage mismatched_image = NULL;
    AgcBuffer index_buffer = NULL;
    AgcCommandBuffer command = NULL;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    const AgcCommandBufferSubmit *captured;
    const uint32_t *words;
    uint32_t value;

    pixel_requirements.color_export_count = 2u;
    pixel_requirements.color_exports[0] = (AgcShaderColorExport){
        0u, AGC_SHADER_COLOR_EXPORT_32_ABGR,
        AGC_SHADER_COMPONENT_FLOAT_OR_NORMALIZED, 0xfu, 0u};
    pixel_requirements.color_exports[1] = (AgcShaderColorExport){
        1u, AGC_SHADER_COLOR_EXPORT_32_ABGR,
        AGC_SHADER_COMPONENT_FLOAT_OR_NORMALIZED, 0xfu, 0u};
    pixel = create_shader_with_reflection(
        device, kAgcShaderStagePs, &pixel_requirements);
    attachments[0].format = AGC_FORMAT_RGBA8_UNORM;
    attachments[1].format = AGC_FORMAT_RGBA8_UNORM;
    pipeline_desc.vertex_shader = vertex;
    pipeline_desc.pixel_shader = pixel;
    pipeline_desc.color_attachment_count = 2u;
    pipeline_desc.color_attachments = attachments;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &pipeline_desc, &pipeline),
        AGC_OK, "two-target graphics pipeline creates");

    image_desc.width = 64u;
    image_desc.height = 32u;
    image_desc.format = AGC_FORMAT_RGBA8_UNORM;
    image_desc.usage = AGC_IMAGE_USAGE_COLOR_TARGET_BIT;
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &first_image), AGC_OK,
        "first native MRT target creates");
    image_desc.width = 32u;
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &mismatched_image),
        AGC_OK, "mismatched MRT target creates for validation");
    buffer_desc.size = 64u;
    buffer_desc.usage = AGC_BUFFER_USAGE_INDEX_BIT;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &index_buffer), AGC_OK,
        "MRT index buffer creates");
    command_desc.capacity_dwords = 4096u;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &command),
        AGC_OK, "MRT command buffer creates");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "MRT command buffer begins");
    TEST_ASSERT_EQ(agcCmdBindGraphicsPipeline(command, pipeline), AGC_OK,
        "MRT graphics pipeline binds");
    TEST_ASSERT_EQ(runtime_transition_buffer_to_graphics_read(command,
        index_buffer, buffer_desc.size, kAgcResourceUsageUndefined, 0u),
        AGC_OK, "MRT index buffer transitions to graphics read");
    TEST_ASSERT_EQ(agcCmdBindIndexBuffer(command, index_buffer, 0u,
        kAgcIndexSize16), AGC_OK, "MRT index buffer binds");
    targets[0].image = first_image;
    targets[1].image = mismatched_image;
    TEST_ASSERT_EQ(agcCmdBindColorTargets(command, 2u, targets),
        AGC_ERROR_VALIDATION_FAILED,
        "dimension-mismatched MRT bind rejects atomically");
    TEST_ASSERT_EQ(agcDestroyImage(first_image), AGC_OK,
        "rejected first MRT target remains unretained");
    first_image = NULL;
    TEST_ASSERT_EQ(agcDestroyImage(mismatched_image), AGC_OK,
        "rejected second MRT target remains unretained");
    mismatched_image = NULL;

    image_desc.width = 64u;
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &first_image), AGC_OK,
        "matching first MRT target creates");
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &second_image), AGC_OK,
        "matching second MRT target creates");
    targets[0].image = first_image;
    targets[1].image = second_image;
    TEST_ASSERT_EQ(agcCmdBindColorTargets(command, 1u, targets),
        AGC_ERROR_VALIDATION_FAILED,
        "MRT target count must match reflected exports");
    target_transitions[0].resource_type = kAgcResourceTypeImage;
    target_transitions[0].image = first_image;
    target_transitions[0].before = kAgcResourceUsageUndefined;
    target_transitions[0].after = kAgcResourceUsageColorTarget;
    target_transitions[0].before_owner = kAgcResourceOwnerHost;
    target_transitions[0].after_owner = kAgcResourceOwnerGraphics;
    target_transitions[1] = target_transitions[0];
    target_transitions[1].image = second_image;
    TEST_ASSERT_EQ(agcCmdTransitionResources(command, 2u,
        target_transitions), AGC_OK,
        "matching MRT targets transition to graphics ownership");
    TEST_ASSERT_EQ(agcCmdBindColorTargets(command, 2u, targets), AGC_OK,
        "matching MRT targets emit native binding state");
    TEST_ASSERT_EQ(agcDestroyImage(first_image), AGC_ERROR_BUSY,
        "first recorded MRT target remains retained");
    TEST_ASSERT_EQ(agcDestroyImage(second_image), AGC_ERROR_BUSY,
        "second recorded MRT target remains retained");
    TEST_ASSERT_EQ(agcCmdDrawIndexed(command, 3u, 1u, 0u, 0, 0u), AGC_OK,
        "MRT draw records after both targets bind");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command), AGC_OK,
        "MRT command buffer becomes executable");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &command;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, NULL), AGC_OK,
        "MRT command buffer submits through the host carrier");
    captured = agcDriverDebugLastDcbSubmit();
    words = (const uint32_t *)(uintptr_t)captured->command_address;
    TEST_ASSERT(runtime_find_context_register(words, captured->dword_count,
        AGC_REG_CB_COLOR0_BASE, &value) && value != 0u,
        "MRT binding records first color base");
    TEST_ASSERT(runtime_find_context_register(words, captured->dword_count,
        AGC_REG_CB_COLOR0_BASE + 15u, &value) && value != 0u,
        "MRT binding records second color base");
    TEST_ASSERT(runtime_find_context_register(words, captured->dword_count,
        AGC_REG_SPI_SHADER_COL_FORMAT, &value) && value == 0x99u,
        "MRT pipeline derives both shader color-format nibbles from reflection");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
        "MRT reset releases recorded targets");
    TEST_ASSERT_EQ(agcDestroyImage(second_image), AGC_OK,
        "released second MRT target destroys after reset");
    TEST_ASSERT_EQ(agcDestroyImage(first_image), AGC_OK,
        "released first MRT target destroys after reset");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command), AGC_OK,
        "MRT command buffer destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(index_buffer), AGC_OK,
        "MRT index buffer destroys");
    TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(pipeline), AGC_OK,
        "MRT graphics pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyShader(pixel), AGC_OK,
        "MRT pixel shader destroys");
    TEST_ASSERT_EQ(agcDestroyShader(vertex), AGC_OK,
        "MRT vertex shader destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK,
        "MRT graphics queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "MRT device destroys");
}

static void test_runtime_depth_stencil_target_binding(void)
{
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueGraphics);
    AgcShader vertex = create_shader(device, kAgcShaderStageVs);
    AgcShader pixel = create_shader(device, kAgcShaderStagePs);
    AgcGraphicsPipelineDesc pipeline_desc = AGC_GRAPHICS_PIPELINE_DESC_INIT;
    AgcDepthStencilPipelineState depth_stencil =
        AGC_DEPTH_STENCIL_PIPELINE_STATE_INIT;
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcDepthStencilTargetBinding target =
        AGC_DEPTH_STENCIL_TARGET_BINDING_INIT;
    AgcResourceTransition target_transition = AGC_RESOURCE_TRANSITION_INIT;
    AgcGraphicsPipeline pipeline = NULL;
    AgcImage image = NULL;
    AgcImage incompatible_image = NULL;
    AgcBuffer index_buffer = NULL;
    AgcCommandBuffer command = NULL;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    const AgcCommandBufferSubmit *captured;
    const uint32_t *words;
    uint32_t value;

    depth_stencil.format = AGC_FORMAT_D16_UNORM;
    depth_stencil.depth_test_enable = 1u;
    depth_stencil.depth_write_enable = 1u;
    pipeline_desc.vertex_shader = vertex;
    pipeline_desc.pixel_shader = pixel;
    pipeline_desc.depth_stencil = &depth_stencil;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &pipeline_desc, &pipeline),
        AGC_OK, "depth-target graphics pipeline creates");
    image_desc.width = 64u;
    image_desc.height = 64u;
    image_desc.format = AGC_FORMAT_D16_UNORM;
    image_desc.usage = AGC_IMAGE_USAGE_DEPTH_STENCIL_BIT;
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &image), AGC_OK,
        "native D16 depth target creates");
    image_desc.format = AGC_FORMAT_D32_FLOAT;
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &incompatible_image),
        AGC_OK, "incompatible depth target creates for validation");
    buffer_desc.size = 64u;
    buffer_desc.usage = AGC_BUFFER_USAGE_INDEX_BIT;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &index_buffer), AGC_OK,
        "depth-target index buffer creates");
    command_desc.capacity_dwords = 4096u;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &command),
        AGC_OK, "depth-target command buffer creates");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "depth-target command buffer begins");
    TEST_ASSERT_EQ(agcCmdBindGraphicsPipeline(command, pipeline), AGC_OK,
        "depth-target graphics pipeline binds");
    TEST_ASSERT_EQ(runtime_transition_buffer_to_graphics_read(command,
        index_buffer, buffer_desc.size, kAgcResourceUsageUndefined, 0u),
        AGC_OK, "depth-target index buffer transitions to graphics read");
    TEST_ASSERT_EQ(agcCmdBindIndexBuffer(command, index_buffer, 0u,
        kAgcIndexSize16), AGC_OK, "depth-target index buffer binds");
    TEST_ASSERT_EQ(agcCmdDrawIndexed(command, 3u, 1u, 0u, 0, 0u),
        AGC_ERROR_RESOURCE_NOT_BOUND,
        "depth-enabled draw requires a depth/stencil target");
    target.image = incompatible_image;
    TEST_ASSERT_EQ(agcCmdBindDepthStencilTarget(command, &target),
        AGC_ERROR_VALIDATION_FAILED,
        "format-mismatched depth target fails before retention or emission");
    TEST_ASSERT_EQ(agcDestroyImage(incompatible_image), AGC_OK,
        "rejected depth target remains destroyable");
    target.image = image;
    TEST_ASSERT_EQ(agcCmdBindDepthStencilTarget(command, &target),
        AGC_ERROR_INVALID_STATE,
        "untransitioned writable depth target cannot bind on graphics");
    target_transition.resource_type = kAgcResourceTypeImage;
    target_transition.image = image;
    target_transition.before = kAgcResourceUsageUndefined;
    target_transition.after = kAgcResourceUsageDepthStencilWrite;
    target_transition.before_owner = kAgcResourceOwnerHost;
    target_transition.after_owner = kAgcResourceOwnerGraphics;
    target_transition.image_range.aspect_mask = AGC_IMAGE_ASPECT_DEPTH_BIT;
    TEST_ASSERT_EQ(agcCmdTransitionResources(command, 1u,
        &target_transition), AGC_OK,
        "writable depth target transitions to graphics ownership");
    TEST_ASSERT_EQ(agcCmdBindDepthStencilTarget(command, &target), AGC_OK,
        "matching depth target emits native surface state");
    TEST_ASSERT_EQ(agcDestroyImage(image), AGC_ERROR_BUSY,
        "recorded depth target remains retained by the command buffer");
    TEST_ASSERT_EQ(agcCmdBindDepthStencilTarget(command, &target),
        AGC_ERROR_NOT_SUPPORTED,
        "depth target cannot be replaced within one command buffer");
    TEST_ASSERT_EQ(agcCmdDrawIndexed(command, 3u, 1u, 0u, 0, 0u), AGC_OK,
        "depth-enabled draw records after its target binds");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command), AGC_OK,
        "depth-target command buffer becomes executable");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &command;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, NULL), AGC_OK,
        "depth-target command buffer submits through the host carrier");
    captured = agcDriverDebugLastDcbSubmit();
    words = (const uint32_t *)(uintptr_t)captured->command_address;
    TEST_ASSERT(runtime_find_context_register(words, captured->dword_count,
        AGC_REG_DB_Z_READ_BASE, &value) && value != 0u,
        "depth-target binding records DB_Z_READ_BASE");
    TEST_ASSERT(runtime_find_context_register(words, captured->dword_count,
        AGC_REG_DB_Z_INFO, &value),
        "depth-target binding records DB_Z_INFO");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
        "reset releases the retained depth target");
    TEST_ASSERT_EQ(agcDestroyImage(image), AGC_OK,
        "released depth target destroys after reset");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command), AGC_OK,
        "depth-target command buffer destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(index_buffer), AGC_OK,
        "depth-target index buffer destroys");
    TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(pipeline), AGC_OK,
        "depth-target graphics pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyShader(pixel), AGC_OK,
        "depth-target pixel shader destroys");
    TEST_ASSERT_EQ(agcDestroyShader(vertex), AGC_OK,
        "depth-target vertex shader destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK,
        "depth-target graphics queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "depth-target device destroys");
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
    command_desc.capacity_dwords = 2277u;
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
    TEST_ASSERT_EQ(runtime_transition_buffer_to_graphics_read(command_buffer,
        index_buffer, buffer_desc.size, kAgcResourceUsageUndefined, 0u),
        AGC_OK, "small-buffer index transitions to graphics read");
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
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcViewport viewport = AGC_VIEWPORT_INIT;
    AgcScissor scissor = AGC_SCISSOR_INIT;
    AgcViewport viewport_array[2] = {
        AGC_VIEWPORT_INIT, AGC_VIEWPORT_INIT};
    AgcScissor scissor_array[2] = {
        AGC_SCISSOR_INIT, AGC_SCISSOR_INIT};
    AgcDepthBias depth_bias = AGC_DEPTH_BIAS_INIT;
    AgcDepthStencilTargetBinding depth_target =
        AGC_DEPTH_STENCIL_TARGET_BINDING_INIT;
    AgcResourceTransition depth_transition = AGC_RESOURCE_TRANSITION_INIT;
    AgcGraphicsPipeline pipeline = NULL;
    AgcBuffer index_buffer = NULL;
    AgcImage depth_image = NULL;
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
        AGC_DYNAMIC_STATE_DEPTH_BIAS_BIT |
        AGC_DYNAMIC_STATE_LINE_WIDTH_BIT;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &pipeline_desc,
        &pipeline), AGC_OK, "dynamic-state graphics pipeline creates");
    buffer_desc.size = 64u;
    buffer_desc.usage = AGC_BUFFER_USAGE_INDEX_BIT;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &index_buffer),
        AGC_OK, "dynamic-state index buffer creates");
    image_desc.width = 1280u;
    image_desc.height = 720u;
    image_desc.format = AGC_FORMAT_D32_FLOAT_S8_UINT;
    image_desc.usage = AGC_IMAGE_USAGE_DEPTH_STENCIL_BIT;
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &depth_image), AGC_OK,
        "dynamic-state depth/stencil image creates");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &command),
        AGC_OK, "dynamic-state command buffer creates");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "dynamic-state fence creates");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "dynamic-state command buffer begins");
    TEST_ASSERT_EQ(agcCmdBindGraphicsPipeline(command, pipeline), AGC_OK,
        "dynamic-state graphics pipeline binds");
    depth_target.image = depth_image;
    TEST_ASSERT_EQ(agcCmdBindDepthStencilTarget(command, &depth_target),
        AGC_ERROR_INVALID_STATE,
        "untransitioned read-only depth target cannot bind on graphics");
    depth_transition.resource_type = kAgcResourceTypeImage;
    depth_transition.image = depth_image;
    depth_transition.before = kAgcResourceUsageUndefined;
    depth_transition.after = kAgcResourceUsageDepthStencilRead;
    depth_transition.before_owner = kAgcResourceOwnerHost;
    depth_transition.after_owner = kAgcResourceOwnerGraphics;
    depth_transition.image_range.aspect_mask = AGC_IMAGE_ASPECT_DEPTH_BIT |
        AGC_IMAGE_ASPECT_STENCIL_BIT;
    TEST_ASSERT_EQ(agcCmdTransitionResources(command, 1u,
        &depth_transition), AGC_OK,
        "read-only depth target transitions to graphics ownership");
    TEST_ASSERT_EQ(agcCmdBindDepthStencilTarget(command, &depth_target),
        AGC_OK, "dynamic-state depth/stencil target binds");
    TEST_ASSERT_EQ(runtime_transition_buffer_to_graphics_read(command,
        index_buffer, buffer_desc.size, kAgcResourceUsageUndefined, 0u),
        AGC_OK, "dynamic-state index buffer transitions to graphics read");
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
    TEST_ASSERT_EQ(agcCmdSetViewportScissors(command, 0u,
        viewport_array, scissor_array), AGC_ERROR_INVALID_ARGUMENT,
        "viewport/scissor array rejects an empty update");
    viewport_array[0] = viewport;
    viewport_array[1] = viewport;
    viewport_array[1].x = 640.0f;
    viewport_array[1].width = 640.0f;
    scissor_array[0] = scissor;
    scissor_array[1] = scissor;
    scissor_array[1].x = 640;
    scissor_array[1].width = 640u;
    TEST_ASSERT_EQ(agcCmdSetViewportScissors(command, 2u,
        viewport_array, scissor_array), AGC_OK,
        "two viewport/scissor pairs record atomically");
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
    TEST_ASSERT_EQ(agcCmdSetLineWidth(command, 0.5f),
        AGC_ERROR_INVALID_ARGUMENT,
        "invalid dynamic line width does not satisfy required state");
    TEST_ASSERT_EQ(agcCmdSetLineWidth(command, 8.0f), AGC_OK,
        "valid dynamic line width records");
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
    TEST_ASSERT(runtime_find_context_register(words, captured->dword_count,
        AGC_REG_PA_SU_LINE_CNTL, &value),
        "dynamic line width emits its primitive-size register");
    TEST_ASSERT_EQ(value, 64u,
        "dynamic eight-pixel line width uses qualified 1/8-pixel units");
    TEST_ASSERT(runtime_find_context_register(words, captured->dword_count,
        AGC_REG_PA_SC_VPORT_SCISSOR_0_TL + 2u, &value),
        "viewport/scissor array emits its second viewport scissor");
    TEST_ASSERT_EQ(value, 640u,
        "second viewport scissor preserves its horizontal origin");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
        "dynamic-state command buffer resets");
    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK,
        "dynamic-state fence destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command), AGC_OK,
        "dynamic-state command buffer destroys");
    TEST_ASSERT_EQ(agcDestroyImage(depth_image), AGC_OK,
        "dynamic-state depth/stencil image destroys");
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
    AgcShader alpha_ps;
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
        AGC_ERROR_VALIDATION_FAILED,
        "alpha-to-one requires matching shader reflection");
    TEST_ASSERT(pipeline == NULL,
        "alpha-to-one reflection mismatch leaves pipeline output null");
    ps_requirements.flags |= AGC_SHADER_REFLECTION_ALPHA_TO_ONE_BIT;
    alpha_ps = create_shader_with_reflection(
        device, kAgcShaderStagePs, &ps_requirements);
    desc.pixel_shader = alpha_ps;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &desc, &pipeline),
        AGC_OK, "reflected alpha-to-one creates pipeline");
    TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(pipeline), AGC_OK,
        "alpha-to-one pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyShader(alpha_ps), AGC_OK,
        "alpha-to-one pixel shader destroys");
    pipeline = NULL;
    desc.pixel_shader = ps;
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
    uint32_t compatible_attachment_mask;
} PipelineCompatibilityCase;

static void test_runtime_graphics_pipeline_compatibility_matrix(void)
{
    enum {
        kRuntimeFormatRgba8Unorm = 0,
        kRuntimeFormatBgra8Unorm,
        kRuntimeFormatRgba8Srgb,
        kRuntimeFormatRgba16Float,
        kRuntimeFormatRgba32Float,
        kRuntimeFormatRgba16Uint,
        kRuntimeFormatRgba32Uint,
        kRuntimeFormatRgba16Sint,
        kRuntimeFormatRgba32Sint,
    };
    static const AgcFormat attachment_formats[] = {
        AGC_FORMAT_RGBA8_UNORM,
        AGC_FORMAT_BGRA8_UNORM,
        AGC_FORMAT_RGBA8_SRGB,
        AGC_FORMAT_RGBA16_FLOAT,
        AGC_FORMAT_RGBA32_FLOAT,
        AGC_FORMAT_RGBA16_UINT,
        AGC_FORMAT_RGBA32_UINT,
        AGC_FORMAT_RGBA16_SINT,
        AGC_FORMAT_RGBA32_SINT,
    };
    static const PipelineCompatibilityCase cases[] = {
        {AGC_SHADER_COLOR_EXPORT_FP16_ABGR,
         AGC_SHADER_COMPONENT_FLOAT_OR_NORMALIZED,
         (1u << kRuntimeFormatRgba8Unorm) |
         (1u << kRuntimeFormatBgra8Unorm) |
         (1u << kRuntimeFormatRgba8Srgb) |
         (1u << kRuntimeFormatRgba16Float)},
        {AGC_SHADER_COLOR_EXPORT_32_ABGR,
         AGC_SHADER_COMPONENT_FLOAT_OR_NORMALIZED,
         (1u << kRuntimeFormatRgba8Unorm) |
         (1u << kRuntimeFormatBgra8Unorm) |
         (1u << kRuntimeFormatRgba8Srgb) |
         (1u << kRuntimeFormatRgba16Float) |
         (1u << kRuntimeFormatRgba32Float)},
        {AGC_SHADER_COLOR_EXPORT_UINT16_ABGR,
         AGC_SHADER_COMPONENT_UINT, 1u << kRuntimeFormatRgba16Uint},
        {AGC_SHADER_COLOR_EXPORT_32_ABGR,
         AGC_SHADER_COMPONENT_UINT, 1u << kRuntimeFormatRgba32Uint},
        {AGC_SHADER_COLOR_EXPORT_SINT16_ABGR,
         AGC_SHADER_COMPONENT_SINT, 1u << kRuntimeFormatRgba16Sint},
        {AGC_SHADER_COLOR_EXPORT_32_ABGR,
         AGC_SHADER_COMPONENT_SINT, 1u << kRuntimeFormatRgba32Sint},
        {AGC_SHADER_COLOR_EXPORT_DEFAULT,
         AGC_SHADER_COMPONENT_FLOAT_OR_NORMALIZED, 0u},
        {AGC_SHADER_COLOR_EXPORT_32_R,
         AGC_SHADER_COMPONENT_FLOAT_OR_NORMALIZED, 0u},
        {AGC_SHADER_COLOR_EXPORT_32_GR,
         AGC_SHADER_COMPONENT_FLOAT_OR_NORMALIZED, 0u},
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
        desc.vertex_shader = vs;
        desc.pixel_shader = ps;
        desc.color_attachment_count = 1u;
        desc.color_attachments = &attachment;
        for (j = 0u; j < sizeof(attachment_formats) /
             sizeof(attachment_formats[0]); ++j) {
            attachment.format = attachment_formats[j];
            pipeline = NULL;
            if ((cases[i].compatible_attachment_mask & (1u << j)) != 0u) {
                TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &desc,
                    &pipeline), AGC_OK,
                    "listed export/attachment pair creates pipeline");
                TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(pipeline), AGC_OK,
                    "compatible graphics pipeline destroys");
            } else {
                TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &desc,
                    &pipeline), AGC_ERROR_VALIDATION_FAILED,
                    "unsupported export/attachment pair fails closed");
                TEST_ASSERT(pipeline == NULL,
                    "rejected attachment pair leaves pipeline output null");
            }
        }

        if (cases[i].component_class !=
            AGC_SHADER_COMPONENT_FLOAT_OR_NORMALIZED &&
            cases[i].compatible_attachment_mask != 0u) {
            for (j = 0u; j < sizeof(attachment_formats) /
                 sizeof(attachment_formats[0]); ++j) {
                if ((cases[i].compatible_attachment_mask & (1u << j)) != 0u) {
                    attachment.format = attachment_formats[j];
                    break;
                }
            }
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
        attachments[0].format = AGC_FORMAT_RGBA8_UNORM;
        desc.logic_operation_enable = 1u;
        desc.logic_operation = AGC_LOGIC_OPERATION_XOR;
        TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &desc, &pipeline),
            AGC_OK, "logic operation creates a native pipeline");
        TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(pipeline), AGC_OK,
            "logic-operation pipeline destroys");
        pipeline = NULL;
        desc.logic_operation = AGC_LOGIC_OPERATION_COUNT;
        TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &desc, &pipeline),
            AGC_ERROR_INVALID_ARGUMENT,
            "invalid logic operation fails before pipeline allocation");
        TEST_ASSERT_EQ(agcDestroyShader(ps), AGC_OK,
            "export-count pixel shader destroys");
    }

    {
        AgcShaderReflection requirements = AGC_SHADER_REFLECTION_INIT;
        AgcGraphicsPipelineDesc desc = AGC_GRAPHICS_PIPELINE_DESC_INIT;
        AgcColorBlendAttachmentState attachment =
            AGC_COLOR_BLEND_ATTACHMENT_STATE_INIT;
        AgcShader ps;
        AgcGraphicsPipeline pipeline = NULL;

        requirements.color_export_count = 1u;
        requirements.color_exports[0] = (AgcShaderColorExport){
            0u, AGC_SHADER_COLOR_EXPORT_FP16_ABGR,
            AGC_SHADER_COMPONENT_FLOAT_OR_NORMALIZED, 0xfu, 0u};
        requirements.flags = AGC_SHADER_REFLECTION_DUAL_SOURCE_EXPORT_BIT;
        ps = create_shader_with_reflection(
            device, kAgcShaderStagePs, &requirements);
        attachment.format = AGC_FORMAT_RGBA8_UNORM;
        desc.vertex_shader = vs;
        desc.pixel_shader = ps;
        desc.color_attachment_count = 1u;
        desc.color_attachments = &attachment;
        TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &desc, &pipeline),
            AGC_ERROR_NOT_SUPPORTED,
            "dual-source reflection fails without a native secondary export contract");
        TEST_ASSERT(pipeline == NULL,
            "dual-source reflection leaves pipeline output null");
        TEST_ASSERT_EQ(agcDestroyShader(ps), AGC_OK,
            "dual-source pixel shader destroys");

        requirements.flags = 0u;
        ps = create_shader_with_reflection(
            device, kAgcShaderStagePs, &requirements);
        desc.pixel_shader = ps;
        attachment.blend_enable = 1u;
        attachment.source_color_factor = AGC_BLEND_FACTOR_SRC1_COLOR;
        TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &desc, &pipeline),
            AGC_ERROR_NOT_SUPPORTED,
            "SRC1 blend factor fails without a dual-source shader contract");
        TEST_ASSERT(pipeline == NULL,
            "SRC1 blend rejection leaves pipeline output null");
        TEST_ASSERT_EQ(agcDestroyShader(ps), AGC_OK,
            "SRC1 pixel shader destroys");
    }

    {
        AgcGraphicsPipelineDesc desc = AGC_GRAPHICS_PIPELINE_DESC_INIT;
        AgcRasterizationState rasterization = AGC_RASTERIZATION_STATE_INIT;
        AgcDepthStencilPipelineState depth_stencil =
            AGC_DEPTH_STENCIL_PIPELINE_STATE_INIT;
        AgcDepthBias static_depth_bias = AGC_DEPTH_BIAS_INIT;
        AgcShader ps = create_shader(device, kAgcShaderStagePs);
        AgcGraphicsPipeline pipeline = NULL;

        desc.vertex_shader = vs;
        desc.pixel_shader = ps;
        desc.rasterization = &rasterization;
        rasterization.polygon_mode = AGC_POLYGON_MODE_LINE;
        TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &desc, &pipeline),
            AGC_OK, "line polygon mode creates a native pipeline");
        TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(pipeline), AGC_OK,
            "line polygon-mode pipeline destroys");
        pipeline = NULL;
        rasterization.polygon_mode = AGC_POLYGON_MODE_POINT;
        TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &desc, &pipeline),
            AGC_OK, "point polygon mode creates a native pipeline");
        TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(pipeline), AGC_OK,
            "point polygon-mode pipeline destroys");
        pipeline = NULL;
        rasterization = (AgcRasterizationState)AGC_RASTERIZATION_STATE_INIT;
        rasterization.depth_clamp_enable = 1u;
        TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &desc, &pipeline),
            AGC_OK, "depth clamp creates a native pipeline");
        TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(pipeline), AGC_OK,
            "depth-clamp pipeline destroys");
        pipeline = NULL;
        rasterization = (AgcRasterizationState)AGC_RASTERIZATION_STATE_INIT;
        rasterization.rasterizer_discard_enable = 1u;
        TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &desc, &pipeline),
            AGC_OK, "rasterizer discard creates a native pipeline");
        TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(pipeline), AGC_OK,
            "rasterizer-discard pipeline destroys");
        pipeline = NULL;
        rasterization = (AgcRasterizationState)AGC_RASTERIZATION_STATE_INIT;
        rasterization.line_width = 2.0f;
        TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &desc, &pipeline),
            AGC_OK, "wide line creates a native pipeline");
        TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(pipeline), AGC_OK,
            "wide-line pipeline destroys");
        pipeline = NULL;
        rasterization = (AgcRasterizationState)AGC_RASTERIZATION_STATE_INIT;
        rasterization.depth_bias_enable = 1u;
        depth_stencil.format = AGC_FORMAT_D32_FLOAT;
        static_depth_bias.constant_factor = 2.0f;
        static_depth_bias.clamp = 0.25f;
        static_depth_bias.slope_factor = -1.5f;
        desc.depth_stencil = &depth_stencil;
        desc.static_depth_bias = &static_depth_bias;
        TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &desc, &pipeline),
            AGC_OK, "static depth bias creates a native pipeline");
        TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(pipeline), AGC_OK,
            "static-depth-bias pipeline destroys");
        pipeline = NULL;
        desc.static_depth_bias = NULL;
        TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &desc, &pipeline),
            AGC_ERROR_VALIDATION_FAILED,
            "enabled depth bias without static or dynamic state fails closed");
        TEST_ASSERT_EQ(agcDestroyShader(ps), AGC_OK,
            "unqualified polygon-mode pixel shader destroys");
    }

    TEST_ASSERT_EQ(agcDestroyShader(vs), AGC_OK,
        "matrix vertex shader destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "graphics compatibility matrix device destroys");
}

static void test_runtime_pipeline_switching(void)
{
    AgcDevice device = create_device();
    AgcShader vs = create_shader(device, kAgcShaderStageVs);
    AgcShader ps = create_shader(device, kAgcShaderStagePs);
    AgcShaderReflection cs_requirements = AGC_SHADER_REFLECTION_INIT;
    AgcShader cs;
    AgcGraphicsPipelineDesc graphics_desc = AGC_GRAPHICS_PIPELINE_DESC_INIT;
    AgcComputePipelineDesc compute_desc = AGC_COMPUTE_PIPELINE_DESC_INIT;
    AgcRasterizationState line_raster = AGC_RASTERIZATION_STATE_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcGraphicsPipeline graphics_a = NULL;
    AgcGraphicsPipeline graphics_b = NULL;
    AgcComputePipeline compute_a = NULL;
    AgcComputePipeline compute_b = NULL;
    AgcCommandBuffer graphics_command = NULL;
    AgcCommandBuffer compute_command = NULL;

    cs_requirements.local_size_x = 1u;
    cs_requirements.local_size_y = 1u;
    cs_requirements.local_size_z = 1u;
    cs = create_shader_with_reflection(
        device, kAgcShaderStageCs, &cs_requirements);
    graphics_desc.vertex_shader = vs;
    graphics_desc.pixel_shader = ps;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &graphics_desc,
        &graphics_a), AGC_OK, "first switchable graphics pipeline creates");
    line_raster.polygon_mode = AGC_POLYGON_MODE_LINE;
    graphics_desc.rasterization = &line_raster;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &graphics_desc,
        &graphics_b), AGC_OK, "second switchable graphics pipeline creates");
    compute_desc.shader = cs;
    compute_desc.local_size_x = 1u;
    compute_desc.local_size_y = 1u;
    compute_desc.local_size_z = 1u;
    TEST_ASSERT_EQ(agcCreateComputePipeline(device, &compute_desc,
        &compute_a), AGC_OK, "first switchable compute pipeline creates");
    TEST_ASSERT_EQ(agcCreateComputePipeline(device, &compute_desc,
        &compute_b), AGC_OK, "second switchable compute pipeline creates");

    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
        &graphics_command), AGC_OK, "graphics switch command creates");
    command_desc.queue_type = kAgcQueueCompute;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
        &compute_command), AGC_OK, "compute switch command creates");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(graphics_command), AGC_OK,
        "graphics switch command begins");
    TEST_ASSERT_EQ(agcCmdBindGraphicsPipeline(graphics_command, graphics_a),
        AGC_OK, "graphics command binds first pipeline");
    TEST_ASSERT_EQ(agcCmdBindGraphicsPipeline(graphics_command, graphics_b),
        AGC_OK, "graphics command switches to second pipeline");
    TEST_ASSERT_EQ(agcCmdBindGraphicsPipeline(graphics_command, graphics_a),
        AGC_OK, "graphics command switches back to first pipeline");
    TEST_ASSERT_EQ(agcEndCommandBuffer(graphics_command), AGC_OK,
        "graphics switch command ends");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(compute_command), AGC_OK,
        "compute switch command begins");
    TEST_ASSERT_EQ(agcCmdBindComputePipeline(compute_command, compute_a),
        AGC_OK, "compute command binds first pipeline");
    TEST_ASSERT_EQ(agcCmdBindComputePipeline(compute_command, compute_b),
        AGC_OK, "compute command switches to second pipeline");
    TEST_ASSERT_EQ(agcCmdBindComputePipeline(compute_command, compute_a),
        AGC_OK, "compute command switches back to first pipeline");
    TEST_ASSERT_EQ(agcEndCommandBuffer(compute_command), AGC_OK,
        "compute switch command ends");
    TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(graphics_a), AGC_ERROR_BUSY,
        "recorded first graphics pipeline remains retained");
    TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(graphics_b), AGC_ERROR_BUSY,
        "recorded second graphics pipeline remains retained");
    TEST_ASSERT_EQ(agcDestroyComputePipeline(compute_a), AGC_ERROR_BUSY,
        "recorded first compute pipeline remains retained");
    TEST_ASSERT_EQ(agcDestroyComputePipeline(compute_b), AGC_ERROR_BUSY,
        "recorded second compute pipeline remains retained");
    TEST_ASSERT_EQ(agcResetCommandBuffer(graphics_command), AGC_OK,
        "graphics switch command releases both pipelines");
    TEST_ASSERT_EQ(agcResetCommandBuffer(compute_command), AGC_OK,
        "compute switch command releases both pipelines");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(compute_command), AGC_OK,
        "compute switch command destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(graphics_command), AGC_OK,
        "graphics switch command destroys");
    TEST_ASSERT_EQ(agcDestroyComputePipeline(compute_b), AGC_OK,
        "second compute pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyComputePipeline(compute_a), AGC_OK,
        "first compute pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(graphics_b), AGC_OK,
        "second graphics pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(graphics_a), AGC_OK,
        "first graphics pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyShader(cs), AGC_OK,
        "switching compute shader destroys");
    TEST_ASSERT_EQ(agcDestroyShader(ps), AGC_OK,
        "switching pixel shader destroys");
    TEST_ASSERT_EQ(agcDestroyShader(vs), AGC_OK,
        "switching vertex shader destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "pipeline-switching device destroys");
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
        TEST_ASSERT_EQ(runtime_transition_buffer_to_graphics_read(command,
            index_buffer, buffer_desc.size, kAgcResourceUsageUndefined, 0u),
            AGC_OK, "reflected index buffer transitions to graphics read");
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
        TEST_ASSERT_EQ(runtime_transition_buffer_to_graphics_read(command,
            vertex_buffer, buffer_desc.size, kAgcResourceUsageUndefined, 0u),
            AGC_OK, "reflected vertex buffer transitions to graphics read");
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
        AgcResourceTransition transition = AGC_RESOURCE_TRANSITION_INIT;
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
        transition.resource_type = kAgcResourceTypeBuffer;
        transition.buffer = storage;
        transition.buffer_size = buffer_desc.size;
        transition.before = kAgcResourceUsageUndefined;
        transition.after = kAgcResourceUsageShaderWrite;
        transition.before_owner = kAgcResourceOwnerHost;
        transition.after_owner = kAgcResourceOwnerCompute;
        TEST_ASSERT_EQ(agcCmdTransitionResources(command, 1u, &transition),
            AGC_OK, "reflected storage write state records");
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
    AgcResourceTransition transition = AGC_RESOURCE_TRANSITION_INIT;
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
    command_desc.capacity_dwords = 4096u;
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
    transition.resource_type = kAgcResourceTypeBuffer;
    transition.buffer = storage;
    transition.buffer_size = buffer_desc.size;
    transition.before = kAgcResourceUsageUndefined;
    transition.after = kAgcResourceUsageShaderWrite;
    transition.before_owner = kAgcResourceOwnerHost;
    transition.after_owner = kAgcResourceOwnerCompute;
    TEST_ASSERT_EQ(agcCmdTransitionResources(command, 1u, &transition), AGC_OK,
        "indirect descriptor storage write state records");
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

static void test_runtime_primitive_restart_pipeline(void)
{
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueGraphics);
    AgcShader vertex = create_shader(device, kAgcShaderStageVs);
    AgcShader pixel = create_shader(device, kAgcShaderStagePs);
    AgcGraphicsPipelineDesc pipeline_desc = AGC_GRAPHICS_PIPELINE_DESC_INIT;
    AgcGraphicsPipeline pipeline = NULL;
    AgcGraphicsPipeline invalid = NULL;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcBuffer index_buffer = NULL;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcCommandBuffer command = NULL;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    const AgcCommandBufferSubmit *captured;
    const uint32_t *words;
    uint32_t value = 0u;

    pipeline_desc.vertex_shader = vertex;
    pipeline_desc.pixel_shader = pixel;
    pipeline_desc.primitive_topology =
        AGC_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    pipeline_desc.primitive_restart_enable = 1u;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &pipeline_desc,
        &pipeline), AGC_OK,
        "strip primitive-restart pipeline creates");
    pipeline_desc.primitive_topology =
        AGC_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &pipeline_desc,
        &invalid), AGC_ERROR_VALIDATION_FAILED,
        "list primitive restart fails closed without list-restart support");
    TEST_ASSERT(invalid == NULL,
        "rejected primitive-restart pipeline output remains null");

    buffer_desc.size = 64u;
    buffer_desc.usage = AGC_BUFFER_USAGE_INDEX_BIT;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &index_buffer),
        AGC_OK, "primitive-restart index buffer creates");
    command_desc.capacity_dwords = 4096u;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &command),
        AGC_OK, "primitive-restart command buffer creates");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "primitive-restart command begins");
    TEST_ASSERT_EQ(agcCmdBindGraphicsPipeline(command, pipeline), AGC_OK,
        "primitive-restart pipeline binds");
    TEST_ASSERT_EQ(runtime_transition_buffer_to_graphics_read(command,
        index_buffer, buffer_desc.size, kAgcResourceUsageUndefined, 0u),
        AGC_OK, "primitive-restart index buffer transitions");
    TEST_ASSERT_EQ(agcCmdBindIndexBuffer(command, index_buffer, 0u,
        kAgcIndexSize16), AGC_OK, "16-bit restart index buffer binds");
    TEST_ASSERT_EQ(agcCmdDrawIndexed(command, 4u, 1u, 0u, 0, 0u),
        AGC_OK, "16-bit primitive-restart draw records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command), AGC_OK,
        "primitive-restart command ends");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &command;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, NULL), AGC_OK,
        "primitive-restart command submits");
    captured = agcDriverDebugLastDcbSubmit();
    words = (const uint32_t *)(uintptr_t)captured->command_address;
    TEST_ASSERT(runtime_find_context_register(words, captured->dword_count,
        AGC_REG_GE_MULTI_PRIM_IB_RESET_EN, &value) && value == 1u,
        "pipeline bind enables primitive restart");
    TEST_ASSERT(runtime_find_context_register(words, captured->dword_count,
        AGC_REG_VGT_MULTI_PRIM_IB_RESET_INDX, &value) &&
        value == UINT16_MAX,
        "16-bit indexed draw programs the fixed restart index");

    TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
        "primitive-restart command resets");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command), AGC_OK,
        "primitive-restart command destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(index_buffer), AGC_OK,
        "primitive-restart index buffer destroys");
    TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(pipeline), AGC_OK,
        "primitive-restart pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyShader(pixel), AGC_OK,
        "primitive-restart pixel shader destroys");
    TEST_ASSERT_EQ(agcDestroyShader(vertex), AGC_OK,
        "primitive-restart vertex shader destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK,
        "primitive-restart queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "primitive-restart device destroys");
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
    pipeline_desc.primitive_topology = AGC_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
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
    command_desc.capacity_dwords = 4096u;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &command),
        AGC_OK, "geometry command buffer creates");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "geometry command buffer begins");
    TEST_ASSERT_EQ(agcCmdBindGraphicsPipeline(command, pipeline), AGC_OK,
        "geometry pipeline bind records cached state");
    vertex_binding.buffer = vertex_buffer;
    vertex_binding.stride = 16u;
    TEST_ASSERT_EQ(runtime_transition_buffer_to_graphics_read(command,
        vertex_buffer, buffer_desc.size, kAgcResourceUsageUndefined, 0u),
        AGC_OK, "geometry vertex buffer transitions to graphics read");
    TEST_ASSERT_EQ(agcCmdBindVertexBuffers(command, 1u, &vertex_binding),
        AGC_OK, "geometry front-stage vertex table binds");
    TEST_ASSERT_EQ(runtime_transition_buffer_to_graphics_read(command,
        index_buffer, buffer_desc.size, kAgcResourceUsageUndefined, 0u),
        AGC_OK, "geometry index buffer transitions to graphics read");
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
    pipeline_desc.primitive_topology = AGC_PRIMITIVE_TOPOLOGY_LINE_LIST;
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
    TEST_ASSERT_EQ(runtime_transition_buffer_to_graphics_read(command,
        vertex_buffer, buffer_desc.size, kAgcResourceUsageUndefined, 0u),
        AGC_OK, "line geometry vertex buffer transitions to graphics read");
    TEST_ASSERT_EQ(agcCmdBindVertexBuffers(command, 1u, &vertex_binding),
        AGC_OK, "line geometry vertex table binds");
    TEST_ASSERT_EQ(runtime_transition_buffer_to_graphics_read(command,
        index_buffer, buffer_desc.size, kAgcResourceUsageUndefined, 0u),
        AGC_OK, "line geometry index buffer transitions to graphics read");
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
    pipeline_desc.primitive_topology = AGC_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
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

    {
        static const struct {
            AgcShaderPrimitiveTopology input;
            AgcShaderPrimitiveTopology output;
            uint32_t vertices_in;
            uint32_t vertices_out;
            AgcPrimitiveTopology topology;
        } supported_topologies[] = {
            {AGC_SHADER_PRIMITIVE_POINTS, AGC_SHADER_PRIMITIVE_POINTS,
             1u, 1u, AGC_PRIMITIVE_TOPOLOGY_POINT_LIST},
            {AGC_SHADER_PRIMITIVE_LINES_ADJACENCY,
             AGC_SHADER_PRIMITIVE_LINE_STRIP, 4u, 4u,
             AGC_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY},
            {AGC_SHADER_PRIMITIVE_TRIANGLES_ADJACENCY,
             AGC_SHADER_PRIMITIVE_TRIANGLE_STRIP, 6u, 6u,
             AGC_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY},
            {AGC_SHADER_PRIMITIVE_TRIANGLES, AGC_SHADER_PRIMITIVE_POINTS,
             3u, 1u, AGC_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST},
        };
        uint32_t i;

        gs_requirements.geometry_invocations = 1u;
        for (i = 0u; i < sizeof(supported_topologies) /
             sizeof(supported_topologies[0]); ++i) {
            gs_requirements.geometry_input_primitive =
                supported_topologies[i].input;
            gs_requirements.geometry_output_primitive =
                supported_topologies[i].output;
            gs_requirements.geometry_vertices_in =
                supported_topologies[i].vertices_in;
            gs_requirements.geometry_vertices_out =
                supported_topologies[i].vertices_out;
            geometry = create_ngg_shader_bundle(
                device, kAgcShaderStageGs, &gs_requirements);
            pixel = create_shader_with_reflection(
                device, kAgcShaderStagePs, &ps_requirements);
            pipeline_desc.geometry_shader = geometry;
            pipeline_desc.pixel_shader = pixel;
            pipeline_desc.primitive_topology =
                supported_topologies[i].topology;
            pipeline = NULL;
            TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &pipeline_desc,
                &pipeline), AGC_OK,
                "qualified geometry topology creates a native pipeline");
            TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(pipeline), AGC_OK,
                "qualified geometry topology pipeline destroys");
            TEST_ASSERT_EQ(agcDestroyShader(pixel), AGC_OK,
                "qualified geometry pixel shader destroys");
            TEST_ASSERT_EQ(agcDestroyShader(geometry), AGC_OK,
                "qualified geometry bundle destroys");
        }
    }

    gs_requirements.geometry_input_primitive =
        AGC_SHADER_PRIMITIVE_TRIANGLES;
    gs_requirements.geometry_output_primitive =
        AGC_SHADER_PRIMITIVE_TRIANGLE_STRIP;
    pipeline_desc.primitive_topology = AGC_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    gs_requirements.geometry_vertices_in = 3u;
    gs_requirements.geometry_vertices_out = 3u;
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
    pipeline_desc.primitive_topology = AGC_PRIMITIVE_TOPOLOGY_PATCH_LIST;
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
    command_desc.capacity_dwords = 4096u;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &command),
        AGC_OK, "tessellation command buffer creates");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "tessellation command buffer begins");
    TEST_ASSERT_EQ(agcCmdBindGraphicsPipeline(command, tess_pipeline), AGC_OK,
        "tessellation pipeline binds its cached ring and shader state");
    vertex_binding.buffer = vertex_buffer;
    vertex_binding.stride = 16u;
    TEST_ASSERT_EQ(runtime_transition_buffer_to_graphics_read(command,
        vertex_buffer, buffer_desc.size, kAgcResourceUsageUndefined, 0u),
        AGC_OK, "tessellation vertex buffer transitions to graphics read");
    TEST_ASSERT_EQ(agcCmdBindVertexBuffers(command, 1u, &vertex_binding),
        AGC_OK, "tessellation VS-front vertex table binds");
    TEST_ASSERT_EQ(agcCmdPushConstants(command, 1u << kAgcShaderStageHs,
        0u, sizeof(push_value), &push_value), AGC_OK,
        "tessellation-control push constants bind");
    TEST_ASSERT_EQ(runtime_transition_buffer_to_graphics_read(command,
        index_buffer, buffer_desc.size, kAgcResourceUsageUndefined, 0u),
        AGC_OK, "tessellation index buffer transitions to graphics read");
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
    TEST_ASSERT_EQ(runtime_transition_buffer_to_graphics_read(command,
        vertex_buffer, buffer_desc.size, kAgcResourceUsageShaderRead, 0u),
        AGC_OK, "combined tessellation vertex retains graphics-read state");
    TEST_ASSERT_EQ(agcCmdBindVertexBuffers(command, 1u, &vertex_binding),
        AGC_OK, "combined tessellation VS-front vertex table binds");
    TEST_ASSERT_EQ(agcCmdPushConstants(command, 1u << kAgcShaderStageHs,
        0u, sizeof(push_value), &push_value), AGC_OK,
        "combined tessellation-control push constants bind");
    TEST_ASSERT_EQ(runtime_transition_buffer_to_graphics_read(command,
        index_buffer, buffer_desc.size, kAgcResourceUsageShaderRead, 0u),
        AGC_OK, "combined tessellation index retains graphics-read state");
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
    TEST_ASSERT_EQ(layout.alignment, 256u,
        "linear image binding requires only qualified 256-byte alignment");

    desc = (AgcImageDesc)AGC_IMAGE_DESC_INIT;
    layout = (AgcImageLayout)AGC_IMAGE_LAYOUT_INIT;
    desc.width = 64u;
    desc.height = 32u;
    desc.format = AGC_FORMAT_D16_UNORM;
    desc.usage = AGC_IMAGE_USAGE_DEPTH_STENCIL_BIT |
        AGC_IMAGE_USAGE_TRANSFER_SRC_BIT;
    desc.tiling = AGC_IMAGE_TILING_LINEAR;
    TEST_ASSERT_EQ(agcGetImageLayout(device, &desc, &layout), AGC_OK,
        "linear depth layout remains distinct from optimal depth tiling");
    TEST_ASSERT_EQ(layout.alignment, 256u,
        "linear depth layout is flexible-memory alignable");
    TEST_ASSERT_EQ(layout.bytes_per_block, 2u,
        "linear D16 layout preserves its element size");

    {
        const uint32_t formats[] = { AGC_FORMAT_R8_UNORM,
            AGC_FORMAT_RG8_UNORM, AGC_FORMAT_RGB10A2_UNORM,
            AGC_FORMAT_R16_FLOAT, AGC_FORMAT_RG16_FLOAT,
            AGC_FORMAT_R32_FLOAT, AGC_FORMAT_RG32_FLOAT,
            AGC_FORMAT_R11G11B10_FLOAT, AGC_FORMAT_BGRA8_SRGB };
        const uint32_t bytes[] = { 1u, 2u, 4u, 2u, 4u, 4u, 8u, 4u, 4u };
        uint32_t i;
        for (i = 0u; i < sizeof(formats) / sizeof(formats[0]); ++i) {
            desc = (AgcImageDesc)AGC_IMAGE_DESC_INIT;
            layout = (AgcImageLayout)AGC_IMAGE_LAYOUT_INIT;
            desc.width = 8u;
            desc.height = 8u;
            desc.format = formats[i];
            desc.usage = AGC_IMAGE_USAGE_COLOR_TARGET_BIT;
            TEST_ASSERT_EQ(agcGetImageLayout(device, &desc, &layout), AGC_OK,
                "Vulkan color format has a native image layout");
            TEST_ASSERT_EQ(layout.bytes_per_block, bytes[i],
                "native Vulkan color format exposes the exact element size");
        }
    }

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
    command_desc.capacity_dwords = 512u;
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
    AgcResourceStateInfo state_info = AGC_RESOURCE_STATE_INFO_INIT;

    pipeline_desc.shader = shader;
    pipeline_desc.local_size_x = 64u;
    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 512u;
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
    TEST_ASSERT_EQ(agcGetBufferStateInfo(buffer, &state_info), AGC_OK,
        "deferred buffer state remains queryable before collection");
    TEST_ASSERT_EQ(state_info.flags, AGC_RESOURCE_STATE_DEFERRED_BIT,
        "deferred buffer state exposes retirement status");
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
    state_info = (AgcResourceStateInfo)AGC_RESOURCE_STATE_INFO_INIT;
    TEST_ASSERT_EQ(agcGetImageStateInfo(image, &state_info), AGC_OK,
        "deferred image state remains queryable before collection");
    TEST_ASSERT_EQ(state_info.flags, AGC_RESOURCE_STATE_DEFERRED_BIT,
        "deferred image state exposes retirement status");
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

static void test_runtime_batch_deferred_retirement_stress(void)
{
    enum { kCycles = 32u };
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueCompute);
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcGpuLabelDesc label_desc = AGC_GPU_LABEL_DESC_INIT;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcCommandBuffer commands[2] = {NULL, NULL};
    AgcFence fence = NULL;
    AgcGpuLabel body_labels[2] = {NULL, NULL};
    AgcMemoryStats baseline = AGC_MEMORY_STATS_INIT;
    uint32_t cycle;

    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 256u;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
        &commands[0]), AGC_OK, "stress first batch command creates");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
        &commands[1]), AGC_OK, "stress second batch command creates");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "stress batch fence creates");
    TEST_ASSERT_EQ(agcCreateGpuLabel(device, &label_desc, &body_labels[0]),
        AGC_OK, "stress first body label creates");
    TEST_ASSERT_EQ(agcCreateGpuLabel(device, &label_desc, &body_labels[1]),
        AGC_OK, "stress second body label creates");
    TEST_ASSERT_EQ(agcGetMemoryStats(device, &baseline), AGC_OK,
        "stress baseline memory statistics query succeeds");
    submit.command_buffer_count = 2u;
    submit.command_buffers = commands;
    buffer_desc.size = 4096u;
    buffer_desc.usage = AGC_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_desc.flags = AGC_BUFFER_CREATE_READBACK_BIT;
    image_desc.width = 64u;
    image_desc.height = 64u;
    image_desc.format = AGC_FORMAT_RGBA8_UNORM;
    image_desc.usage = AGC_IMAGE_USAGE_TRANSFER_SRC_BIT |
        AGC_IMAGE_USAGE_TRANSFER_DST_BIT;

    for (cycle = 0u; cycle < kCycles; ++cycle) {
        AgcResourceTransition first[2] = {
            AGC_RESOURCE_TRANSITION_INIT,
            AGC_RESOURCE_TRANSITION_INIT,
        };
        AgcResourceTransition second[2] = {
            AGC_RESOURCE_TRANSITION_V2_INIT,
            AGC_RESOURCE_TRANSITION_V2_INIT,
        };
        AgcMemoryStats pending = AGC_MEMORY_STATS_INIT;
        AgcMemoryStats collected = AGC_MEMORY_STATS_INIT;
        AgcBuffer buffer = NULL;
        AgcImage image = NULL;

        TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &buffer), AGC_OK,
            "stress batch buffer creates");
        TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &image), AGC_OK,
            "stress batch image creates");
        first[0].resource_type = kAgcResourceTypeBuffer;
        first[0].buffer = buffer;
        first[0].buffer_size = buffer_desc.size;
        first[0].before = kAgcResourceUsageUndefined;
        first[0].after = kAgcResourceUsageCopyDestination;
        first[0].before_owner = kAgcResourceOwnerHost;
        first[0].after_owner = kAgcResourceOwnerCompute;
        first[1].resource_type = kAgcResourceTypeImage;
        first[1].image = image;
        first[1].image_range =
            (AgcImageSubresourceRange)AGC_IMAGE_SUBRESOURCE_RANGE_INIT;
        first[1].before = kAgcResourceUsageUndefined;
        first[1].after = kAgcResourceUsageCopyDestination;
        first[1].before_owner = kAgcResourceOwnerHost;
        first[1].after_owner = kAgcResourceOwnerCompute;
        second[0].resource_type = kAgcResourceTypeBuffer;
        second[0].buffer = buffer;
        second[0].buffer_size = buffer_desc.size;
        second[0].before = kAgcResourceUsageCopyDestination;
        second[0].after = kAgcResourceUsageHostRead;
        second[0].before_owner = kAgcResourceOwnerCompute;
        second[0].after_owner = kAgcResourceOwnerHost;
        second[0].flags = AGC_RESOURCE_TRANSITION_BATCH_DEPENDENCY_BIT;
        second[1].resource_type = kAgcResourceTypeImage;
        second[1].image = image;
        second[1].image_range =
            (AgcImageSubresourceRange)AGC_IMAGE_SUBRESOURCE_RANGE_INIT;
        second[1].before = kAgcResourceUsageCopyDestination;
        second[1].after = kAgcResourceUsageHostRead;
        second[1].before_owner = kAgcResourceOwnerCompute;
        second[1].after_owner = kAgcResourceOwnerHost;
        second[1].flags = AGC_RESOURCE_TRANSITION_BATCH_DEPENDENCY_BIT;

        TEST_ASSERT_EQ(agcBeginCommandBuffer(commands[0]), AGC_OK,
            "stress first batch command begins");
        TEST_ASSERT_EQ(agcCmdTransitionResources(commands[0], 2u, first),
            AGC_OK, "stress first batch transitions record");
        TEST_ASSERT_EQ(agcCmdSignalGpuLabel(commands[0], body_labels[0],
            cycle + 1u), AGC_OK, "stress first batch body signal records");
        TEST_ASSERT_EQ(agcEndCommandBuffer(commands[0]), AGC_OK,
            "stress first batch command ends");
        TEST_ASSERT_EQ(agcBeginCommandBuffer(commands[1]), AGC_OK,
            "stress second batch command begins");
        TEST_ASSERT_EQ(agcCmdTransitionResources(commands[1], 2u, second),
            AGC_OK, "stress dependent batch transitions record");
        TEST_ASSERT_EQ(agcCmdSignalGpuLabel(commands[1], body_labels[1],
            cycle + 1u), AGC_OK, "stress second batch body signal records");
        TEST_ASSERT_EQ(agcEndCommandBuffer(commands[1]), AGC_OK,
            "stress second batch command ends");
        TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
            "stress two-command batch submits");
        TEST_ASSERT_EQ(agcDestroyBufferDeferred(buffer, fence), AGC_OK,
            "submitted batch buffer enters deferred retirement");
        TEST_ASSERT_EQ(agcDestroyImageDeferred(image, fence), AGC_OK,
            "submitted batch image enters deferred retirement");
        TEST_ASSERT_EQ(agcGetMemoryStats(device, &pending), AGC_OK,
            "stress pending memory statistics query succeeds");
        TEST_ASSERT_EQ(pending.deferred_free_count, 2u,
            "both submitted resources remain queued before command reset");
        TEST_ASSERT_EQ(agcWaitFence(fence, UINT64_C(200000000)), AGC_OK,
            "stress batch fence completes within finite timeout");
        TEST_ASSERT_EQ(agcCollectDeferredFrees(device), AGC_ERROR_BUSY,
            "completed fence cannot bypass live command references");
        TEST_ASSERT_EQ(agcResetCommandBuffer(commands[1]), AGC_OK,
            "stress second batch command releases resources");
        TEST_ASSERT_EQ(agcResetCommandBuffer(commands[0]), AGC_OK,
            "stress first batch command releases resources");
        TEST_ASSERT_EQ(agcCollectDeferredFrees(device), AGC_OK,
            "stress retired batch resources collect after reset");
        TEST_ASSERT_EQ(agcGetMemoryStats(device, &collected), AGC_OK,
            "stress collected memory statistics query succeeds");
        TEST_ASSERT_EQ(collected.deferred_free_count, 0u,
            "stress deferred queue returns to baseline each cycle");
        TEST_ASSERT_EQ(collected.live_allocation_count,
            baseline.live_allocation_count,
            "stress live allocation count returns to baseline each cycle");
        TEST_ASSERT_EQ(collected.live_bytes, baseline.live_bytes,
            "stress live bytes return to baseline each cycle");
        TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
            "stress batch fence resets for next cycle");
    }

    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK,
        "stress batch fence destroys");
    TEST_ASSERT_EQ(agcDestroyGpuLabel(body_labels[1]), AGC_OK,
        "stress second body label destroys");
    TEST_ASSERT_EQ(agcDestroyGpuLabel(body_labels[0]), AGC_OK,
        "stress first body label destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(commands[1]), AGC_OK,
        "stress second batch command destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(commands[0]), AGC_OK,
        "stress first batch command destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK,
        "stress batch queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "stress batch device destroys without leaks");
}

static void test_runtime_present_chain(void)
{
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueGraphics);
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
    AgcPresentChainDesc present_desc = AGC_PRESENT_CHAIN_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcResourceTransition transition = AGC_RESOURCE_TRANSITION_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcImage images[2] = {NULL, NULL};
    AgcPresentChain present_chain = NULL;
    AgcCommandBuffer command = NULL;
    AgcFence fence = NULL;

    image_desc.width = 1920u;
    image_desc.height = 1080u;
    image_desc.format = AGC_FORMAT_BGRA8_SRGB;
    image_desc.usage = AGC_IMAGE_USAGE_SCANOUT_BIT |
        AGC_IMAGE_USAGE_COLOR_TARGET_BIT;
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &images[0]), AGC_OK,
        "first present image creates");
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &images[1]), AGC_OK,
        "second present image creates");
    present_desc.image_count = 2u;
    present_desc.images = images;
    TEST_ASSERT_EQ(agcCreatePresentChain(device, &present_desc,
        &present_chain), AGC_OK, "present chain registers opaque image");
    TEST_ASSERT_EQ(agcDestroyImage(images[0]), AGC_ERROR_BUSY,
        "present chain retains registered image");

    command_desc.queue_type = kAgcQueueGraphics;
    command_desc.capacity_dwords = 256u;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &command),
        AGC_OK, "present transition command creates");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "present readiness fence creates");
    TEST_ASSERT_EQ(agcPresent(present_chain, 0u, 1u, fence,
        UINT64_C(1000000)), AGC_ERROR_INVALID_STATE,
        "present rejects image before scanout transition");

    transition.resource_type = kAgcResourceTypeImage;
    transition.image = images[0];
    transition.image_range =
        (AgcImageSubresourceRange)AGC_IMAGE_SUBRESOURCE_RANGE_INIT;
    transition.before = kAgcResourceUsageUndefined;
    transition.after = kAgcResourceUsageVideoOutScanout;
    transition.before_owner = kAgcResourceOwnerHost;
    transition.after_owner = kAgcResourceOwnerGraphics;
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "initial present command begins");
    TEST_ASSERT_EQ(agcCmdTransitionResources(command, 1u, &transition), AGC_OK,
        "undefined-to-scanout transition records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command), AGC_OK,
        "initial present command ends");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &command;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "initial scanout transition submits");
    TEST_ASSERT_EQ(agcPresent(present_chain, 0u, 1u, fence,
        AGC_RUNTIME_INFINITE_TIMEOUT), AGC_ERROR_INVALID_ARGUMENT,
        "present rejects an unbounded wait");
    TEST_ASSERT_EQ(agcPresent(present_chain, 0u, 1u, fence, 0u),
        AGC_ERROR_TIMEOUT, "present rejects a zero wait budget");
    TEST_ASSERT_EQ(agcPresent(present_chain, 0u, 1u, fence,
        UINT64_C(1000000)), AGC_OK,
        "scanout-state image presents after readiness fence");

    TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
        "initial present command resets");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "present readiness fence resets");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "present-to-render command begins");
    transition.before = kAgcResourceUsageVideoOutScanout;
    transition.after = kAgcResourceUsageColorTarget;
    transition.before_owner = kAgcResourceOwnerGraphics;
    transition.after_owner = kAgcResourceOwnerGraphics;
    TEST_ASSERT_EQ(agcCmdTransitionResources(command, 1u, &transition), AGC_OK,
        "scanout-to-color-target transition records");
    transition.before = kAgcResourceUsageColorTarget;
    transition.after = kAgcResourceUsageVideoOutScanout;
    TEST_ASSERT_EQ(agcCmdTransitionResources(command, 1u, &transition), AGC_OK,
        "color-target-to-scanout transition records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command), AGC_OK,
        "present-to-render command ends");
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "present-to-render transition chain submits");
    TEST_ASSERT_EQ(agcPresent(present_chain, 0u, 2u, fence,
        UINT64_C(1000000)), AGC_OK,
        "rendered image returns to bounded presentation");

    TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
        "present-to-render command resets");
    TEST_ASSERT_EQ(agcDestroyImageDeferred(images[0], fence), AGC_OK,
        "present-owned image enters deferred retirement");
    TEST_ASSERT_EQ(agcPresent(present_chain, 0u, 3u, fence,
        UINT64_C(1000000)), AGC_ERROR_INVALID_STATE,
        "retiring present image rejects new flips");
    TEST_ASSERT_EQ(agcDestroyPresentChain(present_chain), AGC_OK,
        "present chain releases registered image");
    TEST_ASSERT_EQ(agcCollectDeferredFrees(device), AGC_OK,
        "present image collects after dependency release");
    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK,
        "present readiness fence destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command), AGC_OK,
        "present command destroys");
    TEST_ASSERT_EQ(agcDestroyImage(images[1]), AGC_OK,
        "released second present image destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK,
        "present queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "present device destroys");
}

static void test_runtime_explicit_placed_memory(void)
{
    AgcDevice device = create_device();
    AgcMemoryStats baseline = AGC_MEMORY_STATS_INIT;
    AgcMemoryStats final = AGC_MEMORY_STATS_INIT;
    AgcMemoryDesc memory_desc = AGC_MEMORY_DESC_INIT;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
    AgcImageLayout image_layout = AGC_IMAGE_LAYOUT_INIT;
    AgcMemory flexible = NULL;
    AgcMemory garlic = NULL;
    AgcBuffer first = NULL;
    AgcBuffer second = NULL;
    AgcBuffer invalid_buffer = NULL;
    AgcImage image = NULL;
    uint8_t pattern[64];
    uint8_t readback[64];
    uint8_t *mapped = NULL;
    uint32_t i;

    TEST_ASSERT_EQ(agcGetMemoryStats(device, &baseline), AGC_OK,
        "placed-memory test records its allocator baseline");
    memory_desc.size = 4096u;
    memory_desc.heap = AGC_MEMORY_HEAP_FLEXIBLE;
    memory_desc.alignment = 256u;
    TEST_ASSERT_EQ(agcCreateMemory(device, &memory_desc, &flexible), AGC_OK,
        "explicit flexible memory creates");
    TEST_ASSERT_EQ(agcMapMemory(flexible, 0u, memory_desc.size,
        (void **)&mapped), AGC_OK, "explicit memory maps an in-range span");
    for (i = 0u; i < sizeof(pattern); ++i)
        pattern[i] = (uint8_t)(i * 3u + 1u);
    memcpy(mapped + 512u, pattern, sizeof(pattern));
    TEST_ASSERT_EQ(agcFlushMemory(flexible, 512u, sizeof(pattern)), AGC_OK,
        "explicit mapped bytes flush");
    TEST_ASSERT_EQ(agcInvalidateMemory(flexible, 512u, sizeof(pattern)),
        AGC_OK, "explicit mapped bytes invalidate");
    TEST_ASSERT_EQ(agcUnmapMemory(flexible), AGC_OK,
        "explicit memory unmaps");

    buffer_desc.size = 512u;
    buffer_desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT |
        AGC_BUFFER_USAGE_TRANSFER_SRC_BIT |
        AGC_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_desc.flags = AGC_BUFFER_CREATE_UPLOAD_BIT;
    TEST_ASSERT_EQ(agcCreatePlacedBuffer(device, &buffer_desc, flexible,
        256u, &first), AGC_OK, "first placed buffer binds at an offset");
    TEST_ASSERT_EQ(agcCreatePlacedBuffer(device, &buffer_desc, flexible,
        512u, &second), AGC_OK,
        "second placed buffer may alias the first buffer");
    TEST_ASSERT_EQ(agcMapMemory(flexible, 0u, memory_desc.size,
        (void **)&mapped), AGC_OK,
        "explicit memory remaps for alias verification");
    TEST_ASSERT(memcmp(mapped + 512u, pattern, sizeof(pattern)) == 0,
        "placed buffer storage aliases mapped explicit memory");
    TEST_ASSERT_EQ(agcWriteBuffer(second, 0u, pattern, sizeof(pattern)),
        AGC_OK, "placed buffer writes through its binding offset");
    TEST_ASSERT_EQ(agcUnmapMemory(flexible), AGC_OK,
        "explicit memory unmaps after alias verification");
    TEST_ASSERT_EQ(agcCreatePlacedBuffer(device, &buffer_desc, flexible,
        1u, &invalid_buffer), AGC_ERROR_INVALID_ARGUMENT,
        "placed buffer rejects a misaligned offset");
    TEST_ASSERT_EQ(agcCreatePlacedBuffer(device, &buffer_desc, flexible,
        3840u, &invalid_buffer), AGC_ERROR_INVALID_ARGUMENT,
        "placed buffer rejects an out-of-range binding");
    TEST_ASSERT_EQ(agcDestroyMemory(flexible), AGC_OK,
        "destroying memory releases its handle before placed resources");
    TEST_ASSERT_EQ(agcMapMemory(flexible, 0u, 1u, (void **)&mapped),
        AGC_ERROR_INVALID_ARGUMENT, "released memory cannot be mapped again");
    TEST_ASSERT_EQ(agcWriteBuffer(first, 256u, pattern, sizeof(pattern)),
        AGC_OK, "placed resources retain storage after memory release");
    TEST_ASSERT_EQ(agcDestroyBuffer(second), AGC_OK,
        "second placed buffer releases its reference");
    TEST_ASSERT_EQ(agcDestroyBuffer(first), AGC_OK,
        "last placed buffer retires released memory storage");

    image_desc.width = 8u;
    image_desc.height = 8u;
    image_desc.depth = 1u;
    image_desc.mip_levels = 1u;
    image_desc.array_layers = 1u;
    image_desc.format = AGC_FORMAT_RGBA8_UNORM;
    image_desc.sample_count = 1u;
    image_desc.usage = AGC_IMAGE_USAGE_TRANSFER_SRC_BIT |
        AGC_IMAGE_USAGE_TRANSFER_DST_BIT;
    TEST_ASSERT_EQ(agcGetImageLayout(device, &image_desc, &image_layout),
        AGC_OK, "placed image queries its required layout");
    memory_desc.size = image_layout.alignment + image_layout.allocation_size;
    memory_desc.heap = AGC_MEMORY_HEAP_GARLIC;
    memory_desc.alignment = image_layout.alignment;
    TEST_ASSERT_EQ(agcCreateMemory(device, &memory_desc, &garlic), AGC_OK,
        "explicit garlic memory creates for an image");
    TEST_ASSERT_EQ(agcCreatePlacedImage(device, &image_desc, garlic,
        image_layout.alignment, &image), AGC_OK,
        "placed image binds with queried alignment and size");
    TEST_ASSERT_EQ(agcDestroyMemory(garlic), AGC_OK,
        "placed image retains released explicit memory");
    TEST_ASSERT_EQ(agcWriteImage(image, 0u, pattern, sizeof(pattern)), AGC_OK,
        "placed image upload honors its memory offset");
    memset(readback, 0, sizeof(readback));
    TEST_ASSERT_EQ(agcReadImage(image, 0u, readback, sizeof(readback)), AGC_OK,
        "placed image readback honors its memory offset");
    TEST_ASSERT(memcmp(readback, pattern, sizeof(pattern)) == 0,
        "placed image round-trips through explicit memory");
    TEST_ASSERT_EQ(agcDestroyImage(image), AGC_OK,
        "last placed image releases explicit memory storage");
    TEST_ASSERT_EQ(agcGetMemoryStats(device, &final), AGC_OK,
        "placed-memory test records final allocator state");
    TEST_ASSERT_EQ(final.live_allocation_count, baseline.live_allocation_count,
        "placed-memory allocation count returns exactly to baseline");
    TEST_ASSERT_EQ(final.live_bytes, baseline.live_bytes,
        "placed-memory live bytes return exactly to baseline");
    TEST_ASSERT_EQ(final.deferred_free_count, baseline.deferred_free_count,
        "placed-memory test leaves no deferred frees");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "placed-memory device destroys cleanly");
}

static void test_runtime_extended_image_view_and_sampler(void)
{
    AgcDevice device = create_device();
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
    AgcImageViewDesc view_desc = AGC_IMAGE_VIEW_DESC_INIT;
    AgcSamplerDesc sampler_desc = AGC_SAMPLER_DESC_INIT;
    AgcAllocationInfo image_info = AGC_ALLOCATION_INFO_INIT;
    AgcAllocationInfo view_info = AGC_ALLOCATION_INFO_INIT;
    AgcAllocationInfo sampler_info = AGC_ALLOCATION_INFO_INIT;
    AgcGfx1013Image2DState image_state = {0};
    AgcGfx1013ImageDescriptor expected_view;
    AgcSamplerDescriptor expected_sampler;
    AgcImage image = NULL;
    AgcImageView view = NULL;
    AgcSampler sampler = NULL;

    image_desc.width = 32u;
    image_desc.height = 16u;
    image_desc.format = AGC_FORMAT_RGBA8_UNORM;
    image_desc.usage = AGC_IMAGE_USAGE_SAMPLED_BIT;
    image_desc.tiling = AGC_IMAGE_TILING_LINEAR;
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &image), AGC_OK,
        "extended-view source image creates");
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_IMAGE,
        image, &image_info), AGC_OK, "source image allocation is queryable");
    view_desc.image = image;
    view_desc.format = AGC_GFX1013_IMAGE_FORMAT_RGBA8_UNORM;
    view_desc.swizzle_r = AGC_COMPONENT_SWIZZLE_B;
    view_desc.swizzle_b = AGC_COMPONENT_SWIZZLE_R;
    TEST_ASSERT_EQ(agcCreateImageView(device, &view_desc, &view), AGC_OK,
        "v2 image view accepts explicit component swizzles");
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device,
        AGC_OBJECT_TYPE_IMAGE_VIEW, view, &view_info), AGC_OK,
        "swizzled image-view descriptor allocation is queryable");
    image_state.address = image_info.gpu_address;
    image_state.width = image_desc.width;
    image_state.height = image_desc.height;
    image_state.format = AGC_GFX1013_IMAGE_FORMAT_RGBA8_UNORM;
    image_state.image_type = AGC_GFX1013_IMAGE_TYPE_2D;
    image_state.dst_sel_x = 6u;
    image_state.dst_sel_y = 5u;
    image_state.dst_sel_z = 4u;
    image_state.dst_sel_w = 7u;
    image_state.sample_count = 1u;
    image_state.mip_level_count = 1u;
    TEST_ASSERT_EQ(agcGfx1013Image2DDescriptorEncode(&expected_view,
        &image_state), AGC_OK, "expected swizzled view descriptor encodes");
    TEST_ASSERT(memcmp(view_info.cpu_address, &expected_view,
        sizeof(expected_view)) == 0,
        "native image-view backing contains the exact swizzled descriptor");

    sampler_desc.min_filter = AGC_FILTER_LINEAR;
    sampler_desc.mag_filter = AGC_FILTER_NEAREST;
    sampler_desc.address_u = AGC_ADDRESS_MODE_MIRRORED_REPEAT;
    sampler_desc.address_v = AGC_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampler_desc.address_w = AGC_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
    sampler_desc.mip_filter = AGC_MIP_FILTER_LINEAR;
    sampler_desc.anisotropy_enable = 1u;
    sampler_desc.max_anisotropy = 8u;
    sampler_desc.compare_enable = 1u;
    sampler_desc.compare_operation = AGC_COMPARE_OPERATION_LESS_OR_EQUAL;
    sampler_desc.border_color = AGC_SAMPLER_BORDER_CUSTOM;
    sampler_desc.custom_border_color_index = 7u;
    sampler_desc.min_lod = 1.0f;
    sampler_desc.max_lod = 5.0f;
    sampler_desc.lod_bias = 0.5f;
    TEST_ASSERT_EQ(agcCreateSampler(device, &sampler_desc, &sampler), AGC_OK,
        "v2 sampler accepts mip, anisotropy, compare, wrap, and custom border state");
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_SAMPLER,
        sampler, &sampler_info), AGC_OK,
        "extended sampler descriptor allocation is queryable");
    agcSamplerDescriptorInit(&expected_sampler);
    agcSamplerDescriptorSetFilterMode(&expected_sampler,
        kAgcFilterAnisoLinear, kAgcFilterAnisoPoint, kAgcMipFilterLinear);
    agcSamplerDescriptorSetClampMode(&expected_sampler, kAgcClampMirror,
        kAgcClampBorder, kAgcClampMirrorOnce);
    agcSamplerDescriptorSetLod(&expected_sampler, 1.0f, 5.0f, 0.5f);
    agcSamplerDescriptorSetMaxAnisotropy(&expected_sampler, 8u);
    agcSamplerDescriptorSetCompareFunc(&expected_sampler,
        AGC_COMPARE_OPERATION_LESS_OR_EQUAL);
    TEST_ASSERT_EQ(agcSamplerDescriptorSetCustomBorderColor(&expected_sampler,
        7u), AGC_OK, "expected custom-border sampler descriptor encodes");
    TEST_ASSERT(memcmp(sampler_info.cpu_address, &expected_sampler,
        sizeof(expected_sampler)) == 0,
        "native sampler backing matches the complete normalized descriptor");

    TEST_ASSERT_EQ(agcDestroySampler(sampler), AGC_OK,
        "extended sampler destroys");
    TEST_ASSERT_EQ(agcDestroyImageView(view), AGC_OK,
        "extended image view destroys");
    TEST_ASSERT_EQ(agcDestroyImage(image), AGC_OK,
        "extended-view source image destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "extended resource device destroys");
}

static void test_runtime_occlusion_query_commands(void)
{
    AgcDevice device = create_device();
    AgcOcclusionQueryLayout layout = AGC_OCCLUSION_QUERY_LAYOUT_INIT;
    AgcOcclusionQueryResult query_result = AGC_OCCLUSION_QUERY_RESULT_INIT;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcAllocationInfo buffer_info = AGC_ALLOCATION_INFO_INIT;
    AgcResourceStateInfo state = AGC_RESOURCE_STATE_INFO_INIT;
    AgcBuffer query_buffer = NULL;
    AgcBuffer wrong_buffer = NULL;
    AgcCommandBuffer command = NULL;
    uint8_t *records;
    uint32_t rb;

    TEST_ASSERT_EQ(agcGetOcclusionQueryLayout(device, &layout), AGC_OK,
        "occlusion query layout is queryable without backend constants");
    TEST_ASSERT(layout.record_size >=
        AGC_GFX1013_OCCLUSION_QUERY_STRIDE + sizeof(uint32_t),
        "occlusion query record includes snapshots and availability");
    TEST_ASSERT_EQ(layout.alignment, 8u,
        "occlusion query records retain snapshot alignment");

    buffer_desc.size = layout.record_size * 2u;
    buffer_desc.usage = AGC_BUFFER_USAGE_QUERY_BIT;
    buffer_desc.flags = AGC_BUFFER_CREATE_UPLOAD_BIT |
        AGC_BUFFER_CREATE_READBACK_BIT;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &query_buffer),
        AGC_OK, "typed query buffer creates with host reset/readback access");
    TEST_ASSERT_EQ(agcResetOcclusionQueryResults(query_buffer, 0u, 2u),
        AGC_OK, "host query reset clears complete opaque records");
    TEST_ASSERT_EQ(agcGetBufferStateInfo(query_buffer, &state), AGC_OK,
        "host-reset query buffer state is queryable");
    TEST_ASSERT_EQ(state.usage, kAgcResourceUsageHostWrite,
        "host query reset records HostWrite state");
    TEST_ASSERT_EQ(agcGetOcclusionQueryResult(query_buffer,
        layout.record_size, 0u, &query_result), AGC_ERROR_BUSY,
        "zero-time query poll reports an unavailable result without waiting");

    buffer_desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT;
    buffer_desc.flags = 0u;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &wrong_buffer),
        AGC_OK, "non-query control buffer creates");
    command_desc.queue_type = kAgcQueueGraphics;
    command_desc.capacity_dwords = 4096u;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &command),
        AGC_OK, "query graphics command buffer creates");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command), AGC_OK,
        "query graphics command buffer begins");
    TEST_ASSERT_EQ(agcCmdBeginOcclusionQuery(command, wrong_buffer, 0u, 0u),
        AGC_ERROR_INVALID_ARGUMENT,
        "query begin rejects a buffer without typed query usage");
    TEST_ASSERT_EQ(agcCmdResetOcclusionQueries(command, query_buffer, 0u, 2u),
        AGC_OK, "GPU query reset acquires and clears two records");
    state = (AgcResourceStateInfo)AGC_RESOURCE_STATE_INFO_INIT;
    TEST_ASSERT_EQ(agcGetCommandBufferRangeStateInfo(command, query_buffer,
        layout.record_size, layout.record_size, &state), AGC_OK,
        "command-local range query observes recorded transitions");
    TEST_ASSERT_EQ(state.usage, kAgcResourceUsageQueryWrite,
        "command-local range state reports the effective query usage");
    TEST_ASSERT_EQ(agcCmdBeginOcclusionQuery(command, query_buffer,
        layout.record_size, 1u), AGC_OK,
        "precise occlusion query begins through a typed buffer offset");
    TEST_ASSERT_EQ(agcCmdEndOcclusionQuery(command, query_buffer,
        layout.record_size), AGC_OK,
        "occlusion query end records snapshot and EOP availability");
    TEST_ASSERT_EQ(agcCmdBeginOcclusionQuery(command, query_buffer,
        layout.record_size * 2u, 0u), AGC_ERROR_INVALID_ARGUMENT,
        "query begin rejects an out-of-bounds opaque record");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command), AGC_OK,
        "query graphics command buffer ends");
    TEST_ASSERT_EQ(agcDestroyBuffer(query_buffer), AGC_ERROR_BUSY,
        "recorded query storage is retained until command recycling");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command), AGC_OK,
        "query command recycling releases retained storage");

    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_BUFFER,
        query_buffer, &buffer_info), AGC_OK,
        "query backing is inspectable for generic result execution");
    records = (uint8_t *)buffer_info.cpu_address + layout.record_size;
    for (rb = 0u; rb < AGC_GFX1013_OCCLUSION_QUERY_MAX_RBS; ++rb) {
        uint64_t begin = (UINT64_C(1) << 63u) | (10u + rb);
        uint64_t end = (UINT64_C(1) << 63u) | (20u + rb);
        memcpy(records + rb * 16u, &begin, sizeof(begin));
        memcpy(records + rb * 16u + 8u, &end, sizeof(end));
    }
    {
        const uint32_t available = 1u;
        memcpy(records + AGC_GFX1013_OCCLUSION_QUERY_STRIDE,
            &available, sizeof(available));
    }
    query_result = (AgcOcclusionQueryResult)
        AGC_OCCLUSION_QUERY_RESULT_INIT;
    TEST_ASSERT_EQ(agcGetOcclusionQueryResult(query_buffer,
        layout.record_size, 0u, &query_result), AGC_OK,
        "available query record reduces without a CPU-side backend layout");
    TEST_ASSERT_EQ(query_result.available, 1u,
        "available query result reports availability");
    TEST_ASSERT_EQ(query_result.value,
        10u * AGC_GFX1013_OCCLUSION_QUERY_MAX_RBS,
        "query result reduces all active render-backend counters");
    TEST_ASSERT_EQ(agcGetBufferRangeStateInfo(query_buffer,
        layout.record_size, layout.record_size, &state), AGC_OK,
        "read query record state is queryable");
    TEST_ASSERT_EQ(state.usage, kAgcResourceUsageHostRead,
        "successful query result read records HostRead state");

    TEST_ASSERT_EQ(agcDestroyBuffer(wrong_buffer), AGC_OK,
        "non-query control buffer destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(query_buffer), AGC_OK,
        "recycled query storage destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command), AGC_OK,
        "query command buffer destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "query device destroys cleanly");
}

void test_suite_runtime(void)
{
    TEST_SUITE("Firmware-neutral Native Runtime");
    TEST_RUN(test_runtime_descriptor_and_info_contract);
    TEST_RUN(test_runtime_multiple_logical_devices);
    TEST_RUN(test_runtime_optional_debug_callback);
    TEST_RUN(test_runtime_invalid_program_diagnostic_matrix);
    TEST_RUN(test_runtime_capture_v1_stream);
    TEST_RUN(test_runtime_capture_shader_bytes_opt_in);
    TEST_RUN(test_runtime_shader_reflection_contract);
    TEST_RUN(test_runtime_graphics_pipeline_compatibility_matrix);
    TEST_RUN(test_runtime_pipeline_switching);
    TEST_RUN(test_runtime_pipeline_layout_and_stage_validation);
    TEST_RUN(test_runtime_indirect_descriptor_set_table);
    TEST_RUN(test_runtime_primitive_restart_pipeline);
    TEST_RUN(test_runtime_geometry_pipeline_bundle);
    TEST_RUN(test_runtime_tessellation_pipeline_bundles);
    TEST_RUN(test_runtime_allocation_callbacks);
    TEST_RUN(test_runtime_validation_is_allocation_free);
    TEST_RUN(test_runtime_allocation_failure_rollback);
    TEST_RUN(test_runtime_all_object_lifecycle);
    TEST_RUN(test_runtime_fence_and_command_states);
    TEST_RUN(test_runtime_compute_submission);
    TEST_RUN(test_runtime_compute_on_graphics_queue);
    TEST_RUN(test_runtime_empty_submission_eop_diagnostic);
    TEST_RUN(test_runtime_compiler_reflection_sidecar);
    TEST_RUN(test_runtime_compiler_graphics_sidecar);
    TEST_RUN(test_runtime_indexed_graphics_submission);
    TEST_RUN(test_runtime_multi_graphics_submission);
    TEST_RUN(test_runtime_multi_compute_submission);
    TEST_RUN(test_runtime_batch_transition_chain);
    TEST_RUN(test_runtime_fence_driven_command_reuse);
    TEST_RUN(test_runtime_copy_buffer_submission);
    TEST_RUN(test_runtime_buffer_range_fragmentation);
    TEST_RUN(test_runtime_image_subresource_states);
TEST_RUN(test_runtime_copy_image_submission);
TEST_RUN(test_runtime_image_region_and_buffer_copies);
    TEST_RUN(test_runtime_compute_copy_shader_batch);
    TEST_RUN(test_runtime_gpu_labels);
    TEST_RUN(test_runtime_gpu_label_timeline_waits);
    TEST_RUN(test_runtime_submit_label_lists);
    TEST_RUN(test_runtime_image_transfer);
    TEST_RUN(test_runtime_partial_resource_handoffs);
    TEST_RUN(test_runtime_resource_transitions);
    TEST_RUN(test_runtime_sampled_image_handoff);
    TEST_RUN(test_runtime_color_target_binding);
    TEST_RUN(test_runtime_mrt_color_target_binding);
    TEST_RUN(test_runtime_depth_stencil_target_binding);
    TEST_RUN(test_runtime_command_space_atomic_failure);
    TEST_RUN(test_runtime_dynamic_graphics_state);
    TEST_RUN(test_runtime_depth_stencil_pipeline_state);
    TEST_RUN(test_runtime_multisample_pipeline_state);
    TEST_RUN(test_runtime_ps5_image_layouts);
    TEST_RUN(test_runtime_all_backing_categories);
    TEST_RUN(test_runtime_heap_staging_and_stats);
    TEST_RUN(test_runtime_explicit_placed_memory);
    TEST_RUN(test_runtime_extended_image_view_and_sampler);
    TEST_RUN(test_runtime_occlusion_query_commands);
    TEST_RUN(test_runtime_fence_deferred_free);
    TEST_RUN(test_runtime_batch_deferred_retirement_stress);
    TEST_RUN(test_runtime_present_chain);
}
