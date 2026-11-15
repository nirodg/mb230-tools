#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
static void try_sym(void *h, const char *name){ void *p=dlsym(h,name); if(p) printf("  OK   %s @ %p\n",name,p); else printf("  MISS %s\n",name); }
int main(int argc,char**argv){
  const char*path=argc>1?argv[1]:"/vendor/lib/libNDAL_EX.so";
  void*h=dlopen(path,RTLD_LAZY|RTLD_LOCAL);
  if(!h){printf("dlopen fail: %s\n",dlerror());return 1;}
  printf("dlopen ok %s @ %p\n",path,h);
  const char*syms[]={
    "NDAL_VideoExt_STBC_FreshWDT","NDAL_VideoExt_STBC_SetWDT","NDAL_VideoExt_STBC_StopWDT","NDAL_VideoExt_STBC_GetMcuWatchDogTimeout",
    "NDOSD_SetPlaneOrder","NDOSD_GetPlaneOrder","NDOSD_EnableOSDEngine","_Z21NDOSD_EnableOSDEngineb",
    "NDOSD_SetTransparency","_Z21NDOSD_SetTransparencyhj",
    "NDOSD_SetUpdateEnable","NDOSD_SetDispWin","NDOSD_SetPhyBufferAddress","NDOSD_SetStride","NDOSD_SetColorMode",
    "NDOSD_EnableProtectionArea","NDOSD_GetPhyBufferAddress","NDOSD_GetStride","NDOSD_GetDispWin","NDOSD_GetColorMode","NDOSD_GetCurrentBufferOrder","NDOSD_SetForceBufferOrder",
    "NDOSD_ReleaseCMABuffer","NDOSD_GetStrLogoPhyAddrFromCMABuffer",
    "_Z27NDAL_GfxExt_SetTransparency22_Nd_Gfxex_OSD_PLANE_IDj","_Z23NDAL_GfxExt_SetPreAlpha22_Nd_Gfxex_OSD_PLANE_IDb",
    "_Z24NDAL_GfxExt_SET_CROP_WIN22_Nd_Gfxex_OSD_PLANE_IDP8_ST_RECTS1_","_Z29NDAL_GfxExt_SetOSD1_AUTHORITY22_Nd_Gfxex_OSD_PLANE_ID",
    NULL};
  for(int i=0;syms[i];i++) try_sym(h,syms[i]);
  printf("done\n"); return 0;
}
