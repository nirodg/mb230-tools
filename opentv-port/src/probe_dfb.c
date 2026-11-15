#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Minimal DirectFB probe - tries to join existing fusion/directfb session.
 * No headers: we only call DirectFBInit/DirectFBCreate/ErrorString by symbol.
 */
typedef int DFBResult;
typedef DFBResult (*DFBInitFn)(int *argc, char **argv[]);
typedef DFBResult (*DFBCreateFn)(void **interface);
typedef const char* (*DFBErrorStringFn)(DFBResult result);
typedef DFBResult (*DFBSetOptionFn)(const char *name, const char *value);

static void logm(const char *m){ printf("[probe-dfb] %s\n", m); fflush(stdout); }

int main(int argc, char **argv){
    /* try to help loader find modules/config as aurora's mp profile expects */
    setenv("DFBARGS", "system=fbdev,wm=sawman,no-cursor,module-dir=/vendor/lib/dfb/directfb-1.7-1", 0);
    const char *paths[] = {
        "/vendor/lib/dfb/libdirectfb-1.7.so.1",
        "/mtd_apexe/board/lib/dfb/libdirectfb-1.7.so.1",
        "libdirectfb-1.7.so.1",
        NULL
    };
    void *h = NULL;
    for(int i=0; paths[i]; i++){
        h = dlopen(paths[i], RTLD_NOW|RTLD_GLOBAL);
        if(h){ char b[256]; snprintf(b,sizeof(b),"dlopen ok: %s (%p)", paths[i], h); logm(b); break; }
        char b[512]; snprintf(b,sizeof(b),"dlopen fail %s: %s", paths[i], dlerror()); logm(b);
    }
    if(!h) return 2;

    DFBInitFn pInit = (DFBInitFn)dlsym(h, "DirectFBInit");
    DFBCreateFn pCreate = (DFBCreateFn)dlsym(h, "DirectFBCreate");
    DFBErrorStringFn pErr = (DFBErrorStringFn)dlsym(h, "DirectFBErrorString");
    DFBSetOptionFn pOpt = (DFBSetOptionFn)dlsym(h, "DirectFBSetOption");
    char b[256];
    snprintf(b,sizeof(b),"DirectFBInit=%p DirectFBCreate=%p DirectFBErrorString=%p DirectFBSetOption=%p", (void*)pInit,(void*)pCreate,(void*)pErr,(void*)pOpt);
    logm(b);

    int ac = argc; char **av = argv;
    DFBResult r = 0;
    if(pOpt){
        /* hints to join existing session */
        pOpt("system","fbdev");
        pOpt("wm","sawman");
    }
    if(pInit){
        r = pInit(&ac, &av);
        snprintf(b,sizeof(b),"DirectFBInit() -> %d (%s)", r, pErr?pErr(r):"?");
        logm(b);
    } else {
        logm("DirectFBInit symbol missing");
    }

    void *dfb = NULL;
    if(pCreate){
        r = pCreate(&dfb);
        snprintf(b,sizeof(b),"DirectFBCreate() -> %d (%s) dfb=%p", r, pErr?pErr(r):"?", dfb);
        logm(b);
    } else {
        logm("DirectFBCreate symbol missing");
    }

    if(dfb){
        logm("got IDirectFB interface (vtable ops need headers/ABI to proceed)");
        /* do not attempt vtable calls without known layout; leak/release intentionally skipped in probe */
    }

    logm("done");
    return 0;
}
