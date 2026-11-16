#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

typedef int DFBResult;
typedef DFBResult (*DFBInitFn)(int *argc, char **argv[]);
typedef DFBResult (*DFBCreateFn)(void **interface);
typedef const char* (*DFBErrorStringFn)(DFBResult result);
typedef DFBResult (*GetLayerFn)(void*, unsigned int, void**);
typedef DFBResult (*CreateWindowFn)(void*, void*, void**);
typedef DFBResult (*WinGetSizeFn)(void*, int*, int*);
typedef DFBResult (*WinGetPosFn)(void*, int*, int*);
typedef DFBResult (*WinReleaseFn)(void*);
typedef DFBResult (*WinGetSurfaceFn)(void*, void**);
typedef DFBResult (*WinRaiseFn)(void*);
typedef DFBResult (*WinSetOpacityFn)(void*, uint8_t);
typedef DFBResult (*SurfSetColorFn)(void*, uint8_t,uint8_t,uint8_t,uint8_t);
typedef DFBResult (*SurfFillRectFn)(void*, int,int,int,int);
typedef DFBResult (*SurfFlipFn)(void*, void*, unsigned int);
typedef DFBResult (*SurfGetSizeFn)(void*, int*, int*);
typedef DFBResult (*SurfReleaseFn)(void*);

static DFBErrorStringFn pErr;
static const char *E(DFBResult r){ return pErr?pErr(r):"?"; }

int main(int argc,char**argv){
    int variant=(argc>1)?atoi(argv[1]):6;
    int draw=(argc>2)?atoi(argv[2]):1;
    setenv("DFBARGS","system=fbdev,wm=sawman,no-cursor,no-sighandler,module-dir=/vendor/lib/dfb/directfb-1.7-1",0);
    void *lib=dlopen("/vendor/lib/dfb/libdirectfb-1.7.so.1",RTLD_NOW|RTLD_GLOBAL);
    if(!lib){ printf("dlopen %s\n", dlerror()); return 2; }
    DFBInitFn pInit=dlsym(lib,"DirectFBInit");
    DFBCreateFn pCreate=dlsym(lib,"DirectFBCreate");
    pErr=dlsym(lib,"DirectFBErrorString");
    int ac=1; char *avv[]={"probe",NULL}; char **av=avv;
    pInit(&ac,&av);
    void *dfb=NULL; DFBResult r=pCreate(&dfb);
    if(r||!dfb) return 1;
    GetLayerFn getLayer=(GetLayerFn)(((unsigned long*)dfb)[14]);
    void *layer=NULL; r=getLayer(dfb,0,&layer);
    if(r||!layer) return 1;
    unsigned long *lv=(unsigned long*)layer;
    CreateWindowFn createWindow=(CreateWindowFn)lv[33];

    uint32_t d[64]; memset(d,0,sizeof(d));
    if(variant==3){
        d[0]=0x0001|0x0002|0x0004|0x0008; /* posx,posy,w,h (guess) */
        d[1]=150; d[2]=150; d[3]=300; d[4]=200;
        printf("v3\n");
    } else if(variant==5){
        /* old working-create layout from probes: gave 150x150@100,100 (wrong for this lib) */
        d[0]=0x0f; d[2]=150; d[3]=150; d[4]=400; d[5]=250;
        printf("v5 old-layout\n");
    } else {
        /* v6: layout inferred from IDirectFBDisplayLayer_CreateWindow @0x4b358244
           in vendor libdirectfb-1.7.so.1 (sha256 87de74...5901), decomp/log format:
             flags=d[0], caps=d[1], width=d[2], height=d[3], pixelformat=d[4],
             posx=d[5], posy=d[6], surface_caps=d[7], stacking=d[10]
           Observed flag-bit tests (decomp): 0 CAPS,1 WIDTH,2 HEIGHT,3 PIXELFORMAT,4 POSX,5 POSY,6 SURFACE_CAPS,9 STACKING.
           Enum values (caps/pixelformat/surface_caps/stacking) are build-specific guesses; adjust if needed. */
        unsigned int flags = (1u<<0)|(1u<<1)|(1u<<2)|(1u<<3)|(1u<<4)|(1u<<5)|(1u<<6)|(1u<<9);
        d[0]=flags;
        d[1]=0x00000001; /* caps guess */
        d[2]=500; d[3]=300;
        d[4]=0; /* pixelformat guess */
        d[5]=100; d[6]=100;
        d[7]=0x00000000; /* surface_caps */
        d[10]=2; /* stacking: 2=UPPER (validated on-device, persists across IR/channel) */
        if(argc>3) d[1]=(uint32_t)strtoul(argv[3],NULL,0);
        if(argc>4) d[4]=(uint32_t)strtoul(argv[4],NULL,0);
        if(argc>5) d[7]=(uint32_t)strtoul(argv[5],NULL,0);
        if(argc>6) d[10]=(uint32_t)strtoul(argv[6],NULL,0);
        printf("v6 inferred-layout flags=0x%x caps=0x%x %ux%u pf=%u pos=%u,%u scaps=0x%x stacking=%u\n",d[0],d[1],d[2],d[3],d[4],d[5],d[6],d[7],d[10]);
    }

    void *win=NULL; r=createWindow(layer,d,&win);
    printf("CreateWindow=%d %s win=%p\n",r,E(r),win);
    printf("desc-echo flags=0x%x caps=0x%x w=%u h=%u pf=%u x=%u y=%u scaps=0x%x stacking=%u\n",d[0],d[1],d[2],d[3],d[4],d[5],d[6],d[7],d[10]);
    if(r||!win) return 1;
    unsigned long *wv=(unsigned long*)win;
    WinGetSizeFn getSize=(WinGetSizeFn)wv[7];
    WinGetPosFn getPos=(WinGetPosFn)wv[6];
    int w=0,h=0,x=0,y=0;
    if(getSize){ r=getSize(win,&w,&h); printf("WinGetSize=%d %s %dx%d\n",r,E(r),w,h); }
    if(getPos){ r=getPos(win,&x,&y); printf("WinGetPos=%d %s %d,%d\n",r,E(r),x,y); }

    if(draw){
        WinGetSurfaceFn getSurf=(WinGetSurfaceFn)wv[10];
        WinRaiseFn raiseToTop=(WinRaiseFn)wv[33];
        WinSetOpacityFn setOpacity=(WinSetOpacityFn)wv[22];
        void *surf=NULL; r=getSurf?getSurf(win,&surf):-1;
        printf("GetSurface=%d %s surf=%p\n",r,E(r),surf);
        if(setOpacity){ r=setOpacity(win,255); printf("SetOpacity=%d %s\n",r,E(r)); }
        if(raiseToTop){ r=raiseToTop(win); printf("RaiseToTop=%d %s\n",r,E(r)); }
        if(surf){
            unsigned long *sv=(unsigned long*)surf;
            SurfGetSizeFn sgs=(SurfGetSizeFn)sv[7];
            SurfSetColorFn sc=(SurfSetColorFn)sv[26];
            SurfFillRectFn fr=(SurfFillRectFn)sv[42];
            SurfFlipFn fl=(SurfFlipFn)sv[20];
            int sw=0,sh=0; if(sgs) sgs(surf,&sw,&sh);
            printf("SurfSize=%dx%d\n",sw,sh);
            if(sc) sc(surf,0,255,0,255);
            if(fr) fr(surf,0,0,sw>0?sw:100,sh>0?sh:100);
            if(fl){ r=fl(surf,NULL,0); printf("Flip=%d %s\n",r,E(r)); }
            printf("VISIBLE? holding 5s\n"); fflush(stdout); sleep(5);
            SurfReleaseFn srel=(SurfReleaseFn)sv[4]; if(srel) srel(surf);
        }
    }
    WinReleaseFn wrel=(WinReleaseFn)wv[4]; if(wrel) wrel(win);
    printf("clean exit\n");
    return 0;
}
