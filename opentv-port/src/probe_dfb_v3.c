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

static void dump_maps(void){
    FILE *f=fopen("/proc/self/maps","r"); if(!f) return;
    char line[512];
    while(fgets(line,sizeof(line),f)){
        if(strstr(line,"libdirectfb")) fputs(line, stdout);
    }
    fclose(f);
}

int main(int argc, char **argv){
    setenv("DFBARGS", "system=fbdev,wm=sawman,no-cursor,module-dir=/vendor/lib/dfb/directfb-1.7-1", 0);
    void *h = dlopen("/vendor/lib/dfb/libdirectfb-1.7.so.1", RTLD_NOW|RTLD_GLOBAL);
    if(!h){ printf("dlopen: %s\n", dlerror()); return 2; }
    DFBInitFn pInit = (DFBInitFn)dlsym(h, "DirectFBInit");
    DFBCreateFn pCreate = (DFBCreateFn)dlsym(h, "DirectFBCreate");
    DFBErrorStringFn pErr = (DFBErrorStringFn)dlsym(h, "DirectFBErrorString");
    printf("syms: Init=%p Create=%p\n",(void*)pInit,(void*)pCreate);
    int ac=1; char *avv[]={"probe",NULL}; char **av=avv;
    DFBResult r = pInit ? pInit(&ac,&av) : -1;
    printf("init=%d %s\n", r, pErr?pErr(r):"?");
    void *dfb=NULL;
    r = pCreate ? pCreate(&dfb) : -1;
    printf("create=%d dfb=%p pid=%d\n", r, dfb, getpid());
    dump_maps();
    if(dfb){
        unsigned long *p = (unsigned long*)dfb;
        for(int i=0;i<32;i++) printf("dfb[%02d]=0x%08lx\n", i, p[i]);
        fflush(stdout); sleep(1);
    }
    printf("done\n");
    return 0;
}
