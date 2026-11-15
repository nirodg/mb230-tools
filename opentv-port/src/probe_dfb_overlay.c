#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef int DFBResult;
typedef unsigned int DFBDisplayLayerID; /* DLID_PRIMARY = 0 */
typedef unsigned int DFBSurfaceCapabilities;
typedef unsigned int DFBCooperativeLevel; /* DFSCL_FULLSCREEN=1, DFSCL_EXCLUSIVE=2, DFSCL_NORMAL=3? values vary; will try carefully */

typedef DFBResult (*DFBInitFn)(int *argc, char **argv[]);
typedef DFBResult (*DFBCreateFn)(void **interface);
typedef const char* (*DFBErrorStringFn)(DFBResult result);

typedef DFBResult (*DFB_SetCoopFn)(void *thiz, int level);
typedef DFBResult (*DFB_GetDisplayLayerFn)(void *thiz, DFBDisplayLayerID id, void **ret_interface);
typedef DFBResult (*DFB_CreateSurfaceFn)(void *thiz, void *desc, void **ret_interface); /* desc is DFBSurfaceDescription* */

typedef DFBResult (*Layer_GetSurfaceFn)(void *thiz, void **ret_interface);
typedef DFBResult (*Layer_SetCoopFn)(void *thiz, int level);
typedef DFBResult (*Layer_GetScreenFn)(void *thiz, void **ret_interface);

static void dump(const char *name, void *obj, int n){
    unsigned long *p=(unsigned long*)obj;
    printf("%s=%p\n", name, obj);
    for(int i=0;i<n;i++) printf("  %s[%02d]=0x%08lx\n", name, i, p[i]);
}

int main(int argc,char**argv){
    setenv("DFBARGS","system=fbdev,wm=sawman,no-cursor,module-dir=/vendor/lib/dfb/directfb-1.7-1",0);
    void *h=dlopen("/vendor/lib/dfb/libdirectfb-1.7.so.1",RTLD_NOW|RTLD_GLOBAL);
    if(!h){printf("dlopen %s\n",dlerror()); return 2;}
    DFBInitFn pInit=dlsym(h,"DirectFBInit");
    DFBCreateFn pCreate=dlsym(h,"DirectFBCreate");
    DFBErrorStringFn pErr=dlsym(h,"DirectFBErrorString");
    int ac=1; char *avv[]={"probe",NULL}; char **av=avv;
    DFBResult r=pInit(&ac,&av);
    printf("init=%d %s\n",r,pErr?pErr(r):"?");
    void *dfb=NULL; r=pCreate(&dfb);
    printf("create=%d %s dfb=%p\n",r,pErr?pErr(r):"?",dfb);
    if(!dfb||r!=0) return 1;

    unsigned long *dv=(unsigned long*)dfb;
    /* known: [3]AddRef [4]Release [5]SetCooperativeLevel [14]GetDisplayLayer [9]CreateSurface [31]GetSurface? wait [31] was GetSurface in our map */
    DFB_SetCoopFn dfbSetCoop = (DFB_SetCoopFn)(dv[5]);
    DFB_GetDisplayLayerFn dfbGetLayer = (DFB_GetDisplayLayerFn)(dv[14]);
    DFB_CreateSurfaceFn dfbCreateSurface = (DFB_CreateSurfaceFn)(dv[9]);

    /* try NORMAL cooperative level (commonly 3 in DirectFB; DFSCL_NORMAL=3, FULLSCREEN=2, EXCLUSIVE=1? or different - be safe) */
    /* DFSCL_FULLSCREEN=1, DFSCL_EXCLUSIVE=2 in some versions; better to not force exclusive. Try 3 (NORMAL) */
    if(dfbSetCoop){ r=dfbSetCoop(dfb, 3); printf("dfb SetCooperativeLevel(3/NORMAL?)=%d %s\n",r,pErr?pErr(r):"?"); }

    void *layer=NULL;
    if(dfbGetLayer){
        r=dfbGetLayer(dfb, 0 /*DLID_PRIMARY*/, &layer);
        printf("GetDisplayLayer(0)=%d %s layer=%p\n",r,pErr?pErr(r):"?",layer);
        if(r==0 && layer) dump("layer", layer, 40);
    }

    /* if we got layer, try to get its surface */
    if(layer){
        unsigned long *lv=(unsigned long*)layer;
        /* IDirectFBDisplayLayer vtable unknown; dump and try common positions */
        /* Try calling lv[?] as GetSurface(IDirectFBDisplayLayer*, IDirectFBSurface**) - need to guess */
        /* Common DirectFB 1.7 layer: AddRef,Release,GetID,GetDescription,GetSourceDescriptions,GetCurrentOutputField,GetScreen,SetCooperativeLevel,GetConfiguration,SetConfiguration,...,GetSurface */
        /* safer: just dump for now and also try to enumerate by printing more info */
    }

    printf("holding 2s\n"); fflush(stdout); sleep(2);
    printf("done\n");
    return 0;
}
