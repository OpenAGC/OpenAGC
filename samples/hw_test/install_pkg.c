/*
 * install_pkg.c — Install a .pkg file on PS5 via sceAppInstUtil
 *
 * Usage: deploy this ELF, it installs /data/<pkg> and exits.
 * The pkg path is hardcoded — change PKG_PATH before building.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

/* sceAppInstUtil ABI (from elf-arsenal) */
typedef char hb_content_id_t[0x30];

typedef struct {
    hb_content_id_t content_id;
    int             content_type;
    int             content_platform;
} hb_pkg_info_t;

#define HB_NUM_LANGUAGES 30
#define HB_NUM_IDS       64
typedef char hb_playgo_scenario_id_t[3];
typedef char hb_language_t[8];

typedef struct {
    hb_language_t            languages[HB_NUM_LANGUAGES];
    hb_playgo_scenario_id_t  playgo_scenario_ids[HB_NUM_IDS];
    hb_content_id_t          content_ids[HB_NUM_IDS];
    long                     unknown[810];
} hb_playgo_info_t;

typedef struct {
    const char *uri;
    const char *ex_uri;
    const char *playgo_scenario_id;
    const char *content_id;
    const char *content_name;
    const char *icon_url;
} hb_meta_info_t;

extern int sceAppInstUtilInitialize(void);
extern int sceAppInstUtilAppInstallPkg(const char *path,
                                       hb_pkg_info_t *pkg_info);
extern int sceAppInstUtilInstallByPackage(hb_meta_info_t *meta,
                                          hb_pkg_info_t *pkg_info,
                                          hb_playgo_info_t *play);

int sceKernelUsleep(unsigned int microseconds);

/* GPU credential bypass — needed for elevated privileges */
#include "gpu_credentials.h"

#define PKG_PATH "/data/UP9000-AGCT00001_00-OPENAGC000000000-A0100-V0100.pkg"

static void rewrite_path(const char *in, char *out, size_t out_size) {
    if (strncmp(in, "/data/", 6) == 0) {
        snprintf(out, out_size, "/user%s", in);
        return;
    }
    strncpy(out, in, out_size - 1);
    out[out_size - 1] = '\0';
}

int main(void) {
    printf("=== PKG Installer ===\n");

    /* Set GPU credentials for elevated privileges */
    printf("[1] Setting GPU credentials...\n");
    int cred_err = set_gpu_credentials();
    if (cred_err != 0) {
        printf("    WARNING: credential bypass failed (0x%x)\n", cred_err);
    } else {
        printf("    GPU credentials set\n");
    }

    /* Initialize sceAppInstUtil */
    printf("[2] Initializing sceAppInstUtil...\n");
    int rc = sceAppInstUtilInitialize();
    printf("    sceAppInstUtilInitialize: 0x%08x\n", (unsigned)rc);
    if (rc != 0) {
        printf("    FATAL: initialization failed\n");
        return 1;
    }

    /* Rewrite /data/ → /user/data/ for sandbox visibility */
    char sdk_path[256];
    rewrite_path(PKG_PATH, sdk_path, sizeof(sdk_path));
    printf("[3] Installing: %s\n", sdk_path);

    /* Try sceAppInstUtilAppInstallPkg first (local file) */
    hb_pkg_info_t pkg_info = {0};
    rc = sceAppInstUtilAppInstallPkg(sdk_path, &pkg_info);
    printf("    sceAppInstUtilAppInstallPkg: 0x%08x\n", (unsigned)rc);

    if (rc == 0) {
        printf("    SUCCESS! Content ID: %s\n", pkg_info.content_id);
        printf("=== PKG installed! Check home screen. ===\n");
        return 0;
    }

    /* Fallback: InstallByPackage with file:// URI */
    printf("[4] Trying InstallByPackage fallback...\n");
    char file_uri[512];
    snprintf(file_uri, sizeof(file_uri), "file://%s", sdk_path);

    hb_meta_info_t meta = {0};
    hb_playgo_info_t play = {0};
    meta.uri = file_uri;
    meta.ex_uri = "";
    meta.playgo_scenario_id = "";
    meta.content_id = "";
    meta.content_name = "openagc hw_test";
    meta.icon_url = "";

    rc = sceAppInstUtilInstallByPackage(&meta, &pkg_info, &play);
    printf("    sceAppInstUtilInstallByPackage: 0x%08x\n", (unsigned)rc);

    if (rc == 0) {
        printf("    SUCCESS! Content ID: %s\n", pkg_info.content_id);
        printf("=== PKG installed! Check home screen. ===\n");
        return 0;
    }

    printf("=== Installation failed ===\n");
    return 1;
}
