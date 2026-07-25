/*
 * openagc — register name and bitfield tests
 */

#include "test.h"
#include "agc_registers.h"

static void test_register_offsets_cx(void) {
    /* Depth buffer */
    TEST_ASSERT_EQ(AGC_REG_DB_Z_INFO, 0x010u, "DB_Z_INFO offset");
    TEST_ASSERT_EQ(AGC_REG_DB_DEPTH_CONTROL, 0x200u, "DB_DEPTH_CONTROL offset");
    TEST_ASSERT_EQ(AGC_REG_DB_SHADER_CONTROL, 0x203u, "DB_SHADER_CONTROL offset");
    TEST_ASSERT_EQ(AGC_REG_DB_STENCIL_CONTROL, 0x10Bu, "DB_STENCIL_CONTROL offset");

    /* Color target */
    TEST_ASSERT_EQ(AGC_REG_CB_COLOR0_BASE, 0x318u, "CB_COLOR0_BASE offset");
    TEST_ASSERT_EQ(AGC_REG_CB_COLOR7_BASE, 0x381u, "CB_COLOR7_BASE offset");
    TEST_ASSERT_EQ(AGC_REG_CB_TARGET_MASK, 0x08Eu, "CB_TARGET_MASK offset");
    TEST_ASSERT_EQ(AGC_REG_CB_BLEND0_CONTROL, 0x1E0u, "CB_BLEND0_CONTROL offset");
    TEST_ASSERT_EQ(AGC_REG_CB_COLOR_CONTROL, 0x202u, "CB_COLOR_CONTROL offset");

    /* Viewport */
    TEST_ASSERT_EQ(AGC_REG_PA_CL_VPORT_XSCALE, 0x10Fu, "PA_CL_VPORT_XSCALE offset");
    TEST_ASSERT_EQ(AGC_REG_PA_CL_VPORT_ZOFFSET, 0x114u, "PA_CL_VPORT_ZOFFSET offset");
    TEST_ASSERT_EQ(AGC_REG_PA_CL_VPORT_XSCALE_15, 0x169u, "PA_CL_VPORT_XSCALE_15 offset");

    /* Rasterizer */
    TEST_ASSERT_EQ(AGC_REG_PA_SU_SC_MODE_CNTL, 0x205u, "PA_SU_SC_MODE_CNTL offset");
    TEST_ASSERT_EQ(AGC_REG_PA_SC_WINDOW_SCISSOR_TL, 0x081u, "PA_SC_WINDOW_SCISSOR_TL offset");
    TEST_ASSERT_EQ(AGC_REG_PA_SC_BINNER_CNTL_0, 0x311u, "PA_SC_BINNER_CNTL_0 offset");

    /* SPI */
    TEST_ASSERT_EQ(AGC_REG_SPI_PS_INPUT_ENA, 0x1B3u, "SPI_PS_INPUT_ENA offset");
    TEST_ASSERT_EQ(AGC_REG_SPI_TMPRING_SIZE, 0x1BAu, "SPI_TMPRING_SIZE offset");
}

static void test_register_offsets_sh(void) {
    /* Pixel shader */
    TEST_ASSERT_EQ(AGC_REG_SPI_SHADER_PGM_LO_PS, 0x008u, "SPI_SHADER_PGM_LO_PS offset");
    TEST_ASSERT_EQ(AGC_REG_SPI_SHADER_PGM_HI_PS, 0x009u, "SPI_SHADER_PGM_HI_PS offset");
    TEST_ASSERT_EQ(AGC_REG_SPI_SHADER_PGM_RSRC1_PS, 0x00Au, "SPI_SHADER_PGM_RSRC1_PS offset");
    TEST_ASSERT_EQ(AGC_REG_SPI_SHADER_USER_DATA_PS_0, 0x00Cu, "SPI_SHADER_USER_DATA_PS_0 offset");

    /* Export shader */
    TEST_ASSERT_EQ(AGC_REG_SPI_SHADER_PGM_LO_ES, 0x0C8u, "SPI_SHADER_PGM_LO_ES offset");
    TEST_ASSERT_EQ(AGC_REG_SPI_SHADER_PGM_HI_ES, 0x0C9u, "SPI_SHADER_PGM_HI_ES offset");

    /* Geometry shader */
    TEST_ASSERT_EQ(AGC_REG_SPI_SHADER_PGM_LO_GS, 0x088u, "SPI_SHADER_PGM_LO_GS offset");
    TEST_ASSERT_EQ(AGC_REG_SPI_SHADER_PGM_HI_GS, 0x089u, "SPI_SHADER_PGM_HI_GS offset");

    /* Hull shader */
    TEST_ASSERT_EQ(AGC_REG_SPI_SHADER_PGM_LO_HS, 0x108u, "SPI_SHADER_PGM_LO_HS offset");
    TEST_ASSERT_EQ(AGC_REG_SPI_SHADER_PGM_HI_HS, 0x109u, "SPI_SHADER_PGM_HI_HS offset");

    /* Compute */
    TEST_ASSERT_EQ(AGC_REG_COMPUTE_PGM_LO, 0x20Cu, "COMPUTE_PGM_LO offset");
    TEST_ASSERT_EQ(AGC_REG_COMPUTE_PGM_HI, 0x20Du, "COMPUTE_PGM_HI offset");
    TEST_ASSERT_EQ(AGC_REG_COMPUTE_PGM_RSRC1, 0x212u, "COMPUTE_PGM_RSRC1 offset");
    TEST_ASSERT_EQ(AGC_REG_COMPUTE_USER_DATA_0, 0x240u, "COMPUTE_USER_DATA_0 offset");
}

static void test_register_offsets_uc(void) {
    TEST_ASSERT_EQ(AGC_REG_VGT_PRIMITIVE_TYPE, 0x242u, "VGT_PRIMITIVE_TYPE offset");
    TEST_ASSERT_EQ(AGC_REG_VGT_INDEX_TYPE, 0x243u, "VGT_INDEX_TYPE offset");
    TEST_ASSERT_EQ(AGC_REG_GE_CNTL, 0x25Bu, "GE_CNTL offset");
    TEST_ASSERT_EQ(AGC_REG_GDS_OA_ADDRESS, 0x41Fu, "GDS_OA_ADDRESS offset");
}

static void test_bitfield_macros(void) {
    /* DB_Z_INFO: set FORMAT=2 (16-bit depth), TILE_MODE_INDEX=5 */
    uint32_t z_info = 0;
    z_info |= AGC_REG_SET(DB_Z_INFO, FORMAT, 2u);
    z_info |= AGC_REG_SET(DB_Z_INFO, TILE_MODE_INDEX, 5u);
    TEST_ASSERT_EQ(AGC_REG_GET(z_info, DB_Z_INFO, FORMAT), 2u, "DB_Z_INFO.FORMAT extract");
    TEST_ASSERT_EQ(AGC_REG_GET(z_info, DB_Z_INFO, TILE_MODE_INDEX), 5u, "DB_Z_INFO.TILE_MODE_INDEX extract");

    /* CB_BLEND0_CONTROL: set ENABLE=1, COLOR_SRCBLEND=2 */
    uint32_t blend = 0;
    blend |= AGC_REG_SET(CB_BLEND0_CONTROL, ENABLE, 1u);
    blend |= AGC_REG_SET(CB_BLEND0_CONTROL, COLOR_SRCBLEND, 2u);
    blend |= AGC_REG_SET(CB_BLEND0_CONTROL, COLOR_COMB_FCN, 1u);
    TEST_ASSERT_EQ(AGC_REG_GET(blend, CB_BLEND0_CONTROL, ENABLE), 1u, "CB_BLEND0_CONTROL.ENABLE extract");
    TEST_ASSERT_EQ(AGC_REG_GET(blend, CB_BLEND0_CONTROL, COLOR_SRCBLEND), 2u, "CB_BLEND0_CONTROL.COLOR_SRCBLEND extract");
    TEST_ASSERT_EQ(AGC_REG_GET(blend, CB_BLEND0_CONTROL, COLOR_COMB_FCN), 1u, "CB_BLEND0_CONTROL.COLOR_COMB_FCN extract");

    /* PA_SU_SC_MODE_CNTL: set CULL_FRONT=1, CULL_BACK=0, FACE=1 */
    uint32_t mode = 0;
    mode |= AGC_REG_SET(PA_SU_SC_MODE_CNTL, CULL_FRONT, 1u);
    mode |= AGC_REG_SET(PA_SU_SC_MODE_CNTL, CULL_BACK, 0u);
    mode |= AGC_REG_SET(PA_SU_SC_MODE_CNTL, FACE, 1u);
    TEST_ASSERT_EQ(AGC_REG_GET(mode, PA_SU_SC_MODE_CNTL, CULL_FRONT), 1u, "PA_SU_SC_MODE_CNTL.CULL_FRONT extract");
    TEST_ASSERT_EQ(AGC_REG_GET(mode, PA_SU_SC_MODE_CNTL, CULL_BACK), 0u, "PA_SU_SC_MODE_CNTL.CULL_BACK extract");
    TEST_ASSERT_EQ(AGC_REG_GET(mode, PA_SU_SC_MODE_CNTL, FACE), 1u, "PA_SU_SC_MODE_CNTL.FACE extract");

    /* VGT_PRIMITIVE_TYPE: set PRIM_TYPE=1 (point list) */
    uint32_t prim = AGC_REG_SET(VGT_PRIMITIVE_TYPE, PRIM_TYPE, 1u);
    TEST_ASSERT_EQ(AGC_REG_GET(prim, VGT_PRIMITIVE_TYPE, PRIM_TYPE), 1u, "VGT_PRIMITIVE_TYPE.PRIM_TYPE extract");
}

static void test_cb_color_slot_stride(void) {
    /* CB_COLOR0_BASE = 0x318, CB_COLOR7_BASE = 0x381
     * Stride between BASE registers = (0x381 - 0x318) / 7 = 105 / 7 = 15 */
    uint32_t diff = AGC_REG_CB_COLOR7_BASE - AGC_REG_CB_COLOR0_BASE;
    TEST_ASSERT_EQ(diff, 105u, "CB_COLOR7_BASE - CB_COLOR0_BASE = 7 * 15");
}

static void test_sentinel_registers(void) {
    /* Fake register codes used for AGC-custom NOP-wrapped commands */
    TEST_ASSERT_EQ(AGC_REG_CX_NOP, 0x800003FFu, "CX_NOP sentinel");
    TEST_ASSERT_EQ(AGC_REG_SH_NOP, 0x800002FFu, "SH_NOP sentinel");
    TEST_ASSERT_EQ(AGC_REG_UC_NOP, 0x80003FFFu, "UC_NOP sentinel");

    /* FSR registers (AGC-custom) */
    TEST_ASSERT_EQ(AGC_REG_FSR_ENABLE, 0x800003FEu, "FSR_ENABLE offset");
    TEST_ASSERT_EQ(AGC_REG_FSR_RECURSIONS0, 0x800003FCu, "FSR_RECURSIONS0 offset");
}

void test_suite_registers(void) {
    printf("  register names and bitfields ...\n");
    test_register_offsets_cx();
    test_register_offsets_sh();
    test_register_offsets_uc();
    test_bitfield_macros();
    test_cb_color_slot_stride();
    test_sentinel_registers();
}
