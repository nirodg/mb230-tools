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
typedef DFBResult (*LayerSetCoopFn)(void*, int);
typedef DFBResult (*LayerGetSurfaceFn)(void*, void**);
typedef DFBResult (*GetSizeFn)(void*, int*, int*);
typedef DFBResult (*SetColorFn)(void*, unsigned char, unsigned char, unsigned char, unsigned char);
typedef DFBResult (*FillRectFn)(void*, int, int, int, int);
typedef DFBResult (*ClearFn)(void*, unsigned char, unsigned char, unsigned char, unsigned char);
typedef DFBResult (*FlipFn)(void*, void*, unsigned int);

static const char *E(DFBErrorStringFn e, DFBResult r){ return e?e(r):"?"; }

int main(int argc,char**argv){
    int hold = (argc>1)?atoi(argv[1]):5;
    setenv("DFBARGS","system=fbdev,wm=sawman,no-cursor,module-dir=/vendor/lib/dfb/directfb-1.7-1",0);
    void *lib=dlopen("/vendor/lib/dfb/libdirectfb-1.7.so.1",RTLD_NOW|RTLD_GLOBAL);
    if(!lib){printf("dlopen %s\n",dlerror()); return 2;}
    DFBInitFn pInit=dlsym(lib,"DirectFBInit");
    DFBCreateFn pCreate=dlsym(lib,"DirectFBCreate");
    DFBErrorStringFn pErr=dlsym(lib,"DirectFBErrorString");
    int ac=1; char *avv[]={"probe",NULL}; char **av=avv;
    DFBResult r=pInit(&ac,&av); printf("init=%d\n",r);
    void *dfb=NULL; r=pCreate(&dfb); printf("create=%d dfb=%p\n",r,dfb);
    if(r||!dfb) return 1;

    GetLayerFn getLayer=(GetLayerFn)(((unsigned long*)dfb)[14]);
    void *layer=NULL; r=getLayer(dfb,0,&layer);
    printf("GetDisplayLayer(0)=%d %s layer=%p\n",r,E(pErr,r),layer);
    if(r||!layer) return 1;

    unsigned long *lv=(unsigned long*)layer;
    LayerSetCoopFn layerSetCoop=(LayerSetCoopFn)lv[11];
    if(layerSetCoop){ r=layerSetCoop(layer,1); printf("layer coop(shared)=%d %s\n",r,E(pErr,r)); }

    LayerGetSurfaceFn getSurf=(LayerGetSurfaceFn)lv[9];
    void *surf=NULL; r=getSurf(layer,&surf);
    printf("GetSurface=%d %s surf=%p\n",r,E(pErr,r),surf);
    if(r||!surf) return 1;

    unsigned long *sv=(unsigned long*)surf;
    /* exact symbol matches: [07] GetSize, [20] Layer_Flip, [23] Clear, [26] SetColor, [42] FillRectangle */
    GetSizeFn getSize=(GetSizeFn)sv[7];
    FlipFn layerFlip=(FlipFn)sv[20];
    ClearFn clearFn=(ClearFn)sv[23];
    SetColorFn setColor=(SetColorFn)sv[26];
    FillRectFn fillRect=(FillRectFn)sv[42];

    int w=0,hh=0;
    if(getSize){ r=getSize(surf,&w,&hh); printf("GetSize=%d %s %dx%d\n",r,E(pErr,r),w,hh); }

    printf("drawing red bar...\n"); fflush(stdout);
    if(setColor){ r=setColor(surf, 255, 0, 0, 200); printf("SetColor=%d %s\n",r,E(pErr,r)); }
    if(fillRect && w>0 && hh>0){ r=fillRect(surf, 0, 0, w, (hh>120?hh/6:60)); printf("FillRect=%d %s\n",r,E(pErr,r)); }
    if(layerFlip){ r=layerFlip(surf, NULL, 0); printf("Flip=%d %s\n",r,E(pErr,r)); }

    printf("VISIBLE? holding %ds\n", hold); fflush(stdout); sleep(hold);

    printf("clearing...\n"); fflush(stdout);
    if(clearFn){ r=clearFn(surf, 0,0,0,0); printf("Clear=%d %s\n",r,E(pErr,r)); }
    if(layerFlip){ r=layerFlip(surf, NULL, 0); printf("Flip2=%d %s\n",r,E(pErr,r)); }

    printf("done\n");
    return 0;
}
