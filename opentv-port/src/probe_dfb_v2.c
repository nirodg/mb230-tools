#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef int DFBResult;
typedef DFBResult (*DFBInitFn)(int *argc, char **argv[]);
typedef DFBResult (*DFBCreateFn)(void **interface);
typedef const char* (*DFBErrorStringFn)(DFBResult result);

int main(int argc, char **argv){
    int hold = (argc>1 && strcmp(argv[1],"--hold")==0) ? atoi(argc>2?argv[2]:"30") : 0;
    setenv("DFBARGS", "system=fbdev,wm=sawman,no-cursor,module-dir=/vendor/lib/dfb/directfb-1.7-1", 0);
    void *h = dlopen("/vendor/lib/dfb/libdirectfb-1.7.so.1", RTLD_NOW|RTLD_GLOBAL);
    if(!h){ printf("dlopen: %s\n", dlerror()); return 2; }
    DFBInitFn pInit = (DFBInitFn)dlsym(h, "DirectFBInit");
    DFBCreateFn pCreate = (DFBCreateFn)dlsym(h, "DirectFBCreate");
    DFBErrorStringFn pErr = (DFBErrorStringFn)dlsym(h, "DirectFBErrorString");
    int ac=argc; char **av=argv;
    DFBResult r = pInit ? pInit(&ac,&av) : -1;
    printf("init=%d %s\n", r, pErr?pErr(r):"?");
    void *dfb=NULL;
    r = pCreate ? pCreate(&dfb) : -1;
    printf("create=%d %s dfb=%p pid=%d\n", r, pErr?pErr(r):"?", dfb, getpid());
    if(dfb){
        unsigned long *p = (unsigned long*)dfb;
        printf("dfb[0..15]:\n");
        for(int i=0;i<16;i++) printf("  [%02d] 0x%08lx\n", i, p[i]);
        /* IDirectFB in DirectFB usually: p[0]=priv, p[1]=AddRef, p[2]=Release, p[3]=SetCooperativeLevel? */
        if(hold>0){ printf("holding %ds for inspection...\n", hold); fflush(stdout); sleep(hold); }
    }
    printf("done\n");
    return 0;
}
