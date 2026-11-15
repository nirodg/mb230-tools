#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef int DFBResult;
typedef unsigned int DFBDisplayLayerID;
typedef DFBResult (*DFBInitFn)(int *argc, char **argv[]);
typedef DFBResult (*DFBCreateFn)(void **interface);
typedef const char* (*DFBErrorStringFn)(DFBResult result);
typedef DFBResult (*GetLayerFn)(void*, DFBDisplayLayerID, void**);
typedef DFBResult (*LayerGetSurfaceFn)(void*, void**);
typedef DFBResult (*LayerSetCoopFn)(void*, int);

static DFBErrorStringFn pErr;

static void dump(const char *name, void *obj, int n){
    unsigned long *p=(unsigned long*)obj;
    printf("%s=%p\n", name, obj);
    for(int i=0;i<n;i++) printf("  %s[%02d]=0x%08lx\n", name, i, p[i]);
    fflush(stdout);
}

int main(int argc,char**argv){
    setenv("DFBARGS","system=fbdev,wm=sawman,no-cursor,module-dir=/vendor/lib/dfb/directfb-1.7-1",0);
    void *h=dlopen("/vendor/lib/dfb/libdirectfb-1.7.so.1",RTLD_NOW|RTLD_GLOBAL);
    if(!h){printf("dlopen %s\n",dlerror()); return 2;}
    DFBInitFn pInit=dlsym(h,"DirectFBInit");
    DFBCreateFn pCreate=dlsym(h,"DirectFBCreate");
    pErr=dlsym(h,"DirectFBErrorString");
    int ac=1; char *avv[]={"probe",NULL}; char **av=avv;
    DFBResult r=pInit(&ac,&av); printf("init=%d\n",r);
    void *dfb=NULL; r=pCreate(&dfb); printf("create=%d dfb=%p\n",r,dfb);
    if(r||!dfb) return 1;

    GetLayerFn getLayer=(GetLayerFn)(((unsigned long*)dfb)[14]);
    void *layer=NULL; r=getLayer(dfb,0,&layer);
    printf("GetDisplayLayer(0)=%d %s layer=%p\n",r,pErr?pErr(r):"?",layer);
    if(r||!layer) return 1;

    unsigned long *lv=(unsigned long*)layer;
    /* layer[11] = SetCooperativeLevel (0x4b3575f5) per our mapping? slot 11 in dump was index 11 -> SetCooperativeLevel+0x5c? wait: in our vtable dump indices started at 03 for first element printed as layer[03] etc. */
    /* From mapping: [11] 0x4b3575f5 -> IDirectFBDisplayLayer_SetCooperativeLevel (exact addr match 0x4b3575f5) */
    LayerSetCoopFn layerSetCoop=(LayerSetCoopFn)lv[11];
    /* DLSCL_SHARED=1, DLSCL_EXCLUSIVE=2, DLSCL_ADMINISTRATIVE=3 in DirectFB 1.x */
    if(layerSetCoop){ r=layerSetCoop(layer,1); printf("layer SetCooperativeLevel(SHARED=1)=%d %s\n",r,pErr?pErr(r):"?"); }

    /* [09] = GetSurface 0x4b35b125 */
    LayerGetSurfaceFn getSurf=(LayerGetSurfaceFn)lv[9];
    void *surf=NULL;
    if(getSurf){
        r=getSurf(layer,&surf);
        printf("layer GetSurface=%d %s surf=%p\n",r,pErr?pErr(r):"?",surf);
        if(r==0 && surf) dump("surf", surf, 48);
    } else printf("no GetSurface ptr\n");

    printf("holding 2s\n"); fflush(stdout); sleep(2);
    printf("done\n");
    return 0;
}
