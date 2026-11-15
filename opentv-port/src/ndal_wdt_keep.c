#define _GNU_SOURCE
#include <dlfcn.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/*
 * ndal_wdt_keep: experimental MB230/NDAL STBC WDT kicker via libNDAL_EX.so.
 *
 * Evidence: libNDAL_EX.so exports NDAL_VideoExt_STBC_FreshWDT/SetWDT/StopWDT/
 * GetMcuWatchDogTimeout (resolved on-device via dlopen RTLD_LAZY). aurora calls
 * FreshWDT from an initialized NDAL/platform context.
 *
 * IMPORTANT: calling NDAL functions from an uninitialized/foreign process
 * context may be unsafe (observed: a test that queried GetMcuWatchDogTimeout and
 * attempted kicks was followed by TV reboot; causality not fully proven but
 * suspected). Default behavior is dlsym-only (no calls). Use --kick to actually
 * call FreshWDT, and --query-timeout to call GetMcuWatchDogTimeout, ONLY when
 * you understand/accept the risk (prefer running with proper NDAL init/context
 * or as a validated keeper). --dry-run implies no calls.
 */

static volatile sig_atomic_t g_run = 1;
static void on_sig(int s){ (void)s; g_run = 0; }

typedef int (*ndal_kick_fn)(void);
typedef int (*ndal_get_to_fn)(void);

static void log_line(const char *msg){
    const char *paths[] = {"/tmp/otv_ndal_wdt.log", "/conf/opentv-port/otv_ndal_wdt.log", NULL};
    long ts = (long)time(NULL);
    for(int i=0; paths[i]; i++){
        FILE *f=fopen(paths[i],"a");
        if(f){ fprintf(f,"[%ld] %s\n", ts, msg); fclose(f); }
    }
    fprintf(stderr,"[ndal-wdt] %s\n", msg);
}

int main(int argc,char**argv){
    int interval_ms = 1000;
    int max_kicks = -1;
    int do_kick = 0;
    int query_timeout = 0;
    int dry_run = 0;
    const char *libpath = "/vendor/lib/libNDAL_EX.so";
    for(int i=1;i<argc;i++){
        if(strcmp(argv[i],"--interval-ms")==0 && i+1<argc){ interval_ms=atoi(argv[++i]); if(interval_ms<50) interval_ms=50; }
        else if(strcmp(argv[i],"--once")==0){ do_kick=1; max_kicks=1; }
        else if(strcmp(argv[i],"--max-kicks")==0 && i+1<argc){ max_kicks=atoi(argv[++i]); do_kick=1; }
        else if(strcmp(argv[i],"--kick")==0){ do_kick=1; }
        else if(strcmp(argv[i],"--query-timeout")==0){ query_timeout=1; }
        else if(strcmp(argv[i],"--dry-run")==0 || strcmp(argv[i],"--dlsym-only")==0){ dry_run=1; do_kick=0; query_timeout=0; }
        else if(strcmp(argv[i],"--lib")==0 && i+1<argc){ libpath=argv[++i]; }
        else if(strcmp(argv[i],"--help")==0 || strcmp(argv[i],"-h")==0){
            printf("Usage: %s [--kick] [--interval-ms N] [--once|--max-kicks N] [--query-timeout] [--dry-run] [--lib PATH]\n", argv[0]);
            printf("  Default is dlsym-only (no NDAL calls). --kick enables FreshWDT calls (EXPERIMENTAL/RISKY outside initialized context).\n");
            return 0;
        }
    }
    signal(SIGINT,on_sig); signal(SIGTERM,on_sig);

    char buf[320];
    snprintf(buf,sizeof(buf),"starting ndal_wdt_keep interval_ms=%d do_kick=%d query_timeout=%d dry_run=%d lib=%s max_kicks=%d",
             interval_ms, do_kick, query_timeout, dry_run, libpath, max_kicks);
    log_line(buf);

    void *h = dlopen(libpath, RTLD_LAZY|RTLD_LOCAL);
    if(!h){ snprintf(buf,sizeof(buf),"dlopen failed: %s", dlerror()); log_line(buf); return 2; }
    snprintf(buf,sizeof(buf),"dlopen ok handle=%p", h); log_line(buf);

    ndal_kick_fn kick = (ndal_kick_fn)dlsym(h, "NDAL_VideoExt_STBC_FreshWDT");
    ndal_get_to_fn get_to = (ndal_get_to_fn)dlsym(h, "NDAL_VideoExt_STBC_GetMcuWatchDogTimeout");
    snprintf(buf,sizeof(buf),"FreshWDT=%p GetMcuWatchDogTimeout=%p", (void*)kick, (void*)get_to); log_line(buf);
    if(!kick && do_kick){ log_line("FreshWDT symbol missing; cannot kick"); dlclose(h); return 3; }

    if(query_timeout && !dry_run){
        if(get_to){
            log_line("calling GetMcuWatchDogTimeout() (EXPERIMENTAL)");
            int to = get_to();
            snprintf(buf,sizeof(buf),"GetMcuWatchDogTimeout() -> %d", to); log_line(buf);
        } else log_line("GetMcuWatchDogTimeout symbol missing");
    }

    if(!do_kick || dry_run){
        log_line("dlsym-only/dry-run: no WDT kicks performed");
        dlclose(h); return 0;
    }

    log_line("WDT kick loop enabled (EXPERIMENTAL: ensure proper NDAL context/liveness plan)");
    long kicks=0, errs=0;
    while(g_run){
        if(max_kicks>=0 && kicks>=max_kicks) break;
        int r = kick ? kick() : -1;
        if(r==0) kicks++; else errs++;
        if((kicks<=5) || (kicks%30==0) || r!=0){
            snprintf(buf,sizeof(buf),"kick#%ld ret=%d total_err=%ld", kicks, r, errs); log_line(buf);
        }
        if(max_kicks>=0 && kicks>=max_kicks) break;
        usleep(interval_ms * 1000);
    }
    snprintf(buf,sizeof(buf),"stopping kicks=%ld errs=%ld", kicks, errs); log_line(buf);
    dlclose(h);
    return 0;
}
