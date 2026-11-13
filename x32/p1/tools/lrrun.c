/* lrrun: headless libretro runner.
   usage: lrrun core.so rom frames [options via env]
   env: SHOTS=f1,f2,..  SHOTDIR=dir  INPUT=file ("frame hexmask" lines; bits: B=0x01 Y? ... we use RETRO ids)
        SYSDIR=dir  OPT_key=value (core options) RAMDUMP=frame:file  SAVESTATE/LOADSTATE=file */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dlfcn.h>
#include "libretro.h"
static void *h; static unsigned fmt=RETRO_PIXEL_FORMAT_0RGB1555;
static const void*lastfb; static unsigned lw,lh; static size_t lpitch;
static uint16_t joy; static char sysdir[512]=".";
#include <stdarg.h>
static void logf_(enum retro_log_level l,const char*f,...){ if(!getenv("LRLOG")) return; va_list a; va_start(a,f); vfprintf(stderr,f,a); va_end(a); }
static bool env(unsigned cmd,void*data){
  switch(cmd){
  case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: fmt=*(unsigned*)data; return true;
  case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char**)data=sysdir; return true;
  case RETRO_ENVIRONMENT_GET_VARIABLE: { struct retro_variable*v=data; char k[256]; snprintf(k,sizeof k,"OPT_%s",v->key); v->value=getenv(k); return v->value!=NULL; }
  case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool*)data=false; return true;
  case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool*)data=true; return true;
  case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((struct retro_log_callback*)data)->log=logf_; return true;
  default: return false;
  }
}
static void vid(const void*d,unsigned w,unsigned hh,size_t p){ if(d){lastfb=d;lw=w;lh=hh;lpitch=p;} }
static void aud(int16_t l,int16_t r){}
static FILE*wav; static size_t wavn;
static size_t audb(const int16_t*d,size_t f){ if(wav){ fwrite(d,4,f,wav); wavn+=f; } return f; }
static void poll(void){}
static unsigned joy2; static int inf2[4096]; static unsigned inm2[4096]; static int nin2, ii2;
static int16_t st(unsigned port,unsigned dev,unsigned idx,unsigned id){ if(port<=1 && dev==RETRO_DEVICE_JOYPAD){ unsigned j=port?joy2:joy; if(id==RETRO_DEVICE_ID_JOYPAD_MASK) return j; return (j>>id)&1; } return 0; }
static void shot(const char*fn){
  FILE*f=fopen(fn,"wb"); if(!f||!lastfb) return; fprintf(f,"P6\n%u %u\n255\n",lw,lh);
  for(unsigned y=0;y<lh;y++){ const uint8_t*row=(const uint8_t*)lastfb+y*lpitch;
    for(unsigned x=0;x<lw;x++){ uint8_t rgb[3];
      if(fmt==RETRO_PIXEL_FORMAT_XRGB8888){ uint32_t p=((const uint32_t*)row)[x]; rgb[0]=p>>16;rgb[1]=p>>8;rgb[2]=p; }
      else if(fmt==RETRO_PIXEL_FORMAT_RGB565){ uint16_t p=((const uint16_t*)row)[x]; rgb[0]=((p>>11)&31)*255/31; rgb[1]=((p>>5)&63)*255/63; rgb[2]=(p&31)*255/31; }
      else { uint16_t p=((const uint16_t*)row)[x]; rgb[0]=((p>>10)&31)*255/31; rgb[1]=((p>>5)&31)*255/31; rgb[2]=(p&31)*255/31; }
      fwrite(rgb,1,3,f); } }
  fclose(f);
}
#define SYM(n) n##_t n=(n##_t)dlsym(h,#n)
typedef void(*retro_init_t)(void); typedef bool(*retro_load_game_t)(const struct retro_game_info*); typedef void(*retro_run_t)(void);
typedef void(*retro_set_environment_t)(retro_environment_t); typedef void(*retro_set_video_refresh_t)(retro_video_refresh_t);
typedef void(*retro_set_audio_sample_t)(retro_audio_sample_t); typedef void(*retro_set_audio_sample_batch_t)(retro_audio_sample_batch_t);
typedef void(*retro_set_input_poll_t)(retro_input_poll_t); typedef void(*retro_set_input_state_t)(retro_input_state_t);
typedef void*(*retro_get_memory_data_t)(unsigned); typedef size_t(*retro_get_memory_size_t)(unsigned);
typedef size_t(*retro_serialize_size_t)(void); typedef bool(*retro_serialize_t)(void*,size_t); typedef bool(*retro_unserialize_t)(const void*,size_t);
typedef void(*retro_set_controller_port_device_t)(unsigned,unsigned);
int main(int argc,char**argv){
  if(argc<4){ fprintf(stderr,"usage\n"); return 1; }
  if(getenv("SYSDIR")) snprintf(sysdir,sizeof sysdir,"%s",getenv("SYSDIR"));
  h=dlopen(argv[1],RTLD_NOW); if(!h){ fprintf(stderr,"%s\n",dlerror()); return 1; }
  SYM(retro_set_environment); SYM(retro_set_video_refresh); SYM(retro_set_audio_sample); SYM(retro_set_audio_sample_batch);
  SYM(retro_set_input_poll); SYM(retro_set_input_state); SYM(retro_init); SYM(retro_load_game); SYM(retro_run);
  SYM(retro_get_memory_data); SYM(retro_get_memory_size); SYM(retro_serialize_size); SYM(retro_serialize); SYM(retro_unserialize); SYM(retro_set_controller_port_device);
  retro_set_environment(env); retro_init(); retro_set_video_refresh(vid); retro_set_audio_sample(aud); retro_set_audio_sample_batch(audb);
  retro_set_input_poll(poll); retro_set_input_state(st);
  struct retro_game_info gi={0}; gi.path=argv[2];
  FILE*f=fopen(argv[2],"rb"); fseek(f,0,SEEK_END); gi.size=ftell(f); fseek(f,0,SEEK_SET); void*buf=malloc(gi.size); fread(buf,1,gi.size,f); fclose(f); gi.data=buf;
  if(!retro_load_game(&gi)){ fprintf(stderr,"load failed\n"); return 1; }
  retro_set_controller_port_device(0,getenv("PADDEV")?strtoul(getenv("PADDEV"),0,0):RETRO_DEVICE_JOYPAD);
  retro_set_controller_port_device(1,RETRO_DEVICE_JOYPAD);
  if(getenv("INPUT2")){ FILE*g=fopen(getenv("INPUT2"),"r"); int a; unsigned m; while(g && fscanf(g,"%d %x",&a,&m)==2){ inf2[nin2]=a; inm2[nin2]=m; nin2++; } if(g) fclose(g); }
  if(getenv("WAV")) { wav=fopen(getenv("WAV"),"wb"); }
  if(getenv("LOADSTATE")){ size_t n=retro_serialize_size(); void*s=malloc(n); FILE*g=fopen(getenv("LOADSTATE"),"rb"); fread(s,1,n,g); fclose(g); retro_unserialize(s,n); }
  int frames=atoi(argv[3]);
  /* input script */
  int nin=0; static int inf[200000]; static uint16_t inm[200000];
  if(getenv("INPUT")){ FILE*g=fopen(getenv("INPUT"),"r"); int a; unsigned m; while(g && fscanf(g,"%d %x",&a,&m)==2){ inf[nin]=a; inm[nin]=m; nin++; } if(g) fclose(g); }
  const char*shots=getenv("SHOTS"); const char*sd=getenv("SHOTDIR")?getenv("SHOTDIR"):".";
  int ii=0;
  for(int fr=0;fr<frames;fr++){
    while(ii<nin && inf[ii]<=fr){ joy=inm[ii]; ii++; }
    while(ii2<nin2 && inf2[ii2]<=fr){ joy2=inm2[ii2]; ii2++; }
    if(getenv("POKE")){ static int pf=-1; if(pf<0) pf=getenv("POKEFROM")?atoi(getenv("POKEFROM")):0;
      if(fr>=pf){ unsigned char*ram=retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM); char b[256]; strncpy(b,getenv("POKE"),255); b[255]=0;
        for(char*t=strtok(b,",");t;t=strtok(0,",")){ unsigned a,v; if(sscanf(t,"%x=%x",&a,&v)==2) ram[(a&0xffff)^1]=v; } } }
    if(getenv("DBGSTALL")) fprintf(stderr,"FRAME %d\n",fr); fflush(stderr); retro_run();
    if(getenv("SDRAMEVERY") && fr%atoi(getenv("SDRAMEVERY"))==0 && fr>=(getenv("SDRAMFROM")?atoi(getenv("SDRAMFROM")):0)){ static unsigned char*m; if(!m){ void**pp=(void**)dlsym(h,"Pico32xMem"); m=pp?(unsigned char*)*pp:0; }
      char fn[600]; snprintf(fn,sizeof fn,"%s/sdr_%06d.bin",sd,fr); FILE*g=fopen(fn,"wb"); if(m) fwrite(m,1,0x9000,g); fclose(g); }
    if(getenv("FBLOG")){ static int a=-1,b; static unsigned char*m; static unsigned short*px; if(a<0) sscanf(getenv("FBLOG"),"%d-%d",&a,&b); if(!m){ void**pp=(void**)dlsym(h,"Pico32xMem"); m=pp?(unsigned char*)*pp:0; px=(unsigned short*)dlsym(h,"Pico32x"); }
      if(fr==a) fprintf(stderr,"FBLOG m=%p px=%p fbcr=%x\n",(void*)m,(void*)px,px?px[0x25]:0); if(fr>=a && fr<=b && m && px){ int fs=px[0x20+5]&1; unsigned short*d=(unsigned short*)(m+0x80000+fs*0x20000); unsigned long long hh=1469598103934665603ULL;
        for(int y=0;y<224;y++){ unsigned o=d[y]; for(int x=0;x<160;x++) hh=(hh^d[(o+x)&0xffff])*1099511628211ULL; }
        unsigned short*sw=(unsigned short*)(m+0x76F0+fs*4); unsigned id=((unsigned)sw[0]<<16)|sw[1];
        printf("FB %d %d %u %016llx\n",fr,fs,id,hh);
        if(getenv("FBSHOT")){ char k[32],bb[8192]; snprintf(k,sizeof k,",%d,",fr); snprintf(bb,sizeof bb,",%s,",getenv("FBSHOT")); if(strstr(bb,k)){ char fn[600]; snprintf(fn,sizeof fn,"%s/fb_%06d.pgm",sd,fr); FILE*g=fopen(fn,"wb"); fprintf(g,"P5\n320 224\n255\n");
          for(int y=0;y<224;y++){ unsigned o=d[y]; for(int x=0;x<160;x++){ unsigned short w=d[(o+x)&0xffff]; fputc(w>>8,g); fputc(w&255,g);} } fwrite(m+0xD2C00,1,512,g); fclose(g);} } } }
    if(getenv("FHASH") && lastfb && fr>=atoi(getenv("FHASH"))){ unsigned long long h=1469598103934665603ULL; for(unsigned y=0;y<lh;y++){ const unsigned char*r=(const unsigned char*)lastfb+y*lpitch; for(unsigned x=0;x<lw*2;x++) h=(h^r[x])*1099511628211ULL; } printf("FH %d %016llx\n",fr,h); }
    if(getenv("FCOUNT")){ static int a=-1,b; static unsigned long long prevh; static int ch; if(a<0) sscanf(getenv("FCOUNT"),"%d-%d",&a,&b);
      if(fr>=a && fr<=b && lastfb){ unsigned long long h=1469598103934665603ULL; for(unsigned y=0;y<lh;y++){ const unsigned char*r=(const unsigned char*)lastfb+y*lpitch; for(unsigned x=0;x<lw*2;x++) h=(h^r[x])*1099511628211ULL; } if(h!=prevh) ch++; prevh=h; }
      if(fr==b) fprintf(stderr,"FCOUNT %d %d\n",ch,b-a+1); }
    if(shots){ char k[32],b[8192]; snprintf(k,sizeof k,",%d,",fr); snprintf(b,sizeof b,",%s,",shots); if(strstr(b,k)){ char fn[600]; snprintf(fn,sizeof fn,"%s/shot_%06d.ppm",sd,fr); shot(fn);} }
    if(getenv("SHOTEVERY") && fr%atoi(getenv("SHOTEVERY"))==0){ char fn[600]; snprintf(fn,sizeof fn,"%s/shot_%06d.ppm",sd,fr); shot(fn);
      if(getenv("RAMEVERY")){ snprintf(fn,sizeof fn,"%s/ram_%06d.bin",sd,fr); FILE*g=fopen(fn,"wb"); fwrite(retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM),1,0x10000,g); fclose(g);} }
  }
  { char fn[600]; snprintf(fn,sizeof fn,"%s/last.ppm",sd); shot(fn); }
  if(getenv("RAMDUMP")){ FILE*g=fopen(getenv("RAMDUMP"),"wb"); fwrite(retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM),1,retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM),g); fclose(g); }
  if(getenv("VDPDUMP")){ char*b=(char*)retro_run-0x4f3000; FILE*g=fopen(getenv("VDPDUMP"),"wb");
    fwrite(b+0x2c2ff80,1,0x10000,g); fwrite(b+0x2c2ff00,1,128,g); fwrite(b+0x2c2fe60,1,32,g); fwrite(b+0x2c2fe80,1,128,g); fclose(g); }
  if(getenv("FBDUMP")){ void**pp=(void**)dlsym(h,"Pico32xMem"); unsigned char*m=pp?(unsigned char*)*pp:0; unsigned short*px=(unsigned short*)dlsym(h,"Pico32x"); FILE*g=fopen(getenv("FBDUMP"),"wb"); if(m){ fwrite(m+0x80000,1,0x40000,g); fwrite(px,1,0x80,g);} fclose(g); }
  if(getenv("MDMEMDUMP")){ unsigned char*m=(unsigned char*)dlsym(h,"PicoMem"); FILE*g=fopen(getenv("MDMEMDUMP"),"wb"); if(m) fwrite(m,1,0x22180,g); fclose(g); }
  if(getenv("SDRAMDUMP")){ void**pp=(void**)dlsym(h,"Pico32xMem"); unsigned char*m=pp?(unsigned char*)*pp:0; FILE*g=fopen(getenv("SDRAMDUMP"),"wb"); if(m) fwrite(m,1,0x40000,g); fclose(g); fprintf(stderr,"sdram %p\n",(void*)m); }
  if(getenv("VRAMDUMP")){ FILE*g=fopen(getenv("VRAMDUMP"),"wb"); void*p=retro_get_memory_data(RETRO_MEMORY_VIDEO_RAM); size_t n=retro_get_memory_size(RETRO_MEMORY_VIDEO_RAM); if(p) fwrite(p,1,n,g); fclose(g); fprintf(stderr,"vram %zu\n",n); }
  if(getenv("SAVESTATE")){ size_t n=retro_serialize_size(); void*s=malloc(n); retro_serialize(s,n); FILE*g=fopen(getenv("SAVESTATE"),"wb"); fwrite(s,1,n,g); fclose(g); }
  if(wav){ fclose(wav); fprintf(stderr,"audio frames %zu\n",wavn); }
  fprintf(stderr,"ran %d frames %ux%u\n",frames,lw,lh);
  return 0;
}
