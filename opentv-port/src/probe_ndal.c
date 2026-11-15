#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef void* (*fn_void)(void);
typedef int (*fn_int0)(void);
typedef int (*fn_int1)(int);
typedef int (*fn_int2)(int,int);
typedef int (*fn_int3)(int,int,int);

static void try_sym(void *h, const char *name) {
    void *p = dlsym(h, name);
    if (p) printf("  OK   %s @ %p\n", name, p);
    else   printf("  MISS %s (%s)\n", name, dlerror());
}

int main(int argc, char **argv) {
    const char *libs[] = {
        argc > 1 ? argv[1] : "/vendor/lib/libNDAL_EX.so",
        "/mtd_apexe/board/lib/libNDAL_EX.so",
        "libNDAL_EX.so",
        NULL
    };
    void *h = NULL;
    for (int i=0; libs[i]; i++) {
        if (!libs[i]) continue;
        h = dlopen(libs[i], RTLD_NOW|RTLD_LOCAL);
        if (h) { printf("[probe] dlopen ok: %s\n", libs[i]); break; }
        printf("[probe] dlopen fail %s: %s\n", libs[i], dlerror());
    }
    if (!h) {
        h = dlopen("/vendor/lib/libNDAL3.so", RTLD_NOW|RTLD_LOCAL);
        if (h) printf("[probe] dlopen ok: /vendor/lib/libNDAL3.so\n");
        else { printf("[probe] dlopen fail NDAL3: %s\n", dlerror()); return 1; }
    }

    const char *syms[] = {
        "NDAL_VideoExt_STBC_FreshWDT",
        "NDAL_VideoExt_STBC_StopWDT",
        "NDAL_VideoExt_STBC_SetWDT",
        "NDAL_VideoExt_STBC_GetMcuWatchDogTimeout",
        "NDAL_VideoExt_STBC_ResetByHWWDT",
        "NDOSD_SetPlaneOrder",
        "NDOSD_GetPlaneOrder",
        "NDOSD_EnableOSDEngine",
        "NDOSD_SetTransparency",
        "NDOSD_SetUpdateEnable",
        "NDOSD_SetDispWin",
        "NDOSD_SetPhyBufferAddress",
        "NDOSD_SetStride",
        "NDOSD_SetColorMode",
        "NDOSD_EnableProtectionArea",
        "NDAL_GfxExt_SetTransparency",
        "NDAL_GfxExt_SetPreAlpha",
        "_Z27NDAL_GfxExt_SetTransparency22_Nd_Gfxex_OSD_PLANE_IDj",
        "_Z23NDAL_GfxExt_SetPreAlpha22_Nd_Gfxex_OSD_PLANE_IDb",
        "_Z21NDOSD_SetTransparencyhj",
        "_Z21NDOSD_EnableOSDEngineb",
        "GPU_Plane_Setting",
        NULL
    };
    printf("[probe] resolving symbols:\n");
    for (int i=0; syms[i]; i++) try_sym(h, syms[i]);

    /* optional: try calling FreshWDT if requested and symbol found */
    if (argc > 2 && strcmp(argv[2], "--kick-once") == 0) {
        fn_int0 kick = (fn_int0)dlsym(h, "NDAL_VideoExt_STBC_FreshWDT");
        if (kick) {
            printf("[probe] calling NDAL_VideoExt_STBC_FreshWDT()...\n");
            int r = kick();
            printf("[probe] FreshWDT returned %d\n", r);
        } else {
            printf("[probe] FreshWDT symbol not found, not calling\n");
        }
    }

    printf("[probe] done\n");
    return 0;
}
