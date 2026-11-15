#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static void try_sym(void *h, const char *name) {
    void *p = dlsym(h, name);
    if (p) printf("  OK   %s @ %p\n", name, p);
    else   printf("  MISS %s (%s)\n", name, dlerror());
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/vendor/lib/libNDAL_EX.so";
    int mode = RTLD_LAZY|RTLD_LOCAL;
    void *h = dlopen(path, mode);
    if (!h) {
        printf("[probe2] dlopen(%s,LAZY) fail: %s\n", path, dlerror());
        /* try RTLD_NOW|RTLD_GLOBAL as fallback */
        h = dlopen(path, RTLD_NOW|RTLD_GLOBAL);
        if (!h) { printf("[probe2] dlopen NOW|GLOBAL fail: %s\n", dlerror()); return 1; }
        printf("[probe2] dlopen NOW|GLOBAL ok\n");
    } else {
        printf("[probe2] dlopen(%s,LAZY) ok @ %p\n", path, h);
    }
    const char *syms[] = {
        "NDAL_VideoExt_STBC_FreshWDT",
        "NDAL_VideoExt_STBC_StopWDT",
        "NDAL_VideoExt_STBC_SetWDT",
        "NDAL_VideoExt_STBC_GetMcuWatchDogTimeout",
        "NDOSD_SetPlaneOrder",
        "NDOSD_GetPlaneOrder",
        "NDOSD_EnableOSDEngine",
        "NDOSD_SetTransparency",
        "NDOSD_SetUpdateEnable",
        "NDOSD_SetDispWin",
        "NDOSD_SetPhyBufferAddress",
        "NDOSD_SetStride",
        "NDOSD_SetColorMode",
        "_Z27NDAL_GfxExt_SetTransparency22_Nd_Gfxex_OSD_PLANE_IDj",
        "_Z23NDAL_GfxExt_SetPreAlpha22_Nd_Gfxex_OSD_PLANE_IDb",
        "GPU_Plane_Setting",
        NULL
    };
    printf("[probe2] resolving symbols:\n");
    for (int i=0; syms[i]; i++) try_sym(h, syms[i]);
    printf("[probe2] done\n");
    return 0;
}
