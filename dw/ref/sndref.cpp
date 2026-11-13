// sndref: CPS1 (dynwarj) sound board renderer: Z80 + YM2151 (ymfm) + OKI MSM6295.
// usage: sndref CMD SECONDS out.wav [ymlog.txt]
//   env ROMDIR, LATCH2 (hex, default ff), RATE (output, default 44100)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <vector>
#include "ymfm_opm.h"
extern "C" {
#include "z80.h"
}
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;
static u8 zrom[0x10000], zram[0x800], oki[0x40000];
static int zbank; static u8 latch1=0xff, latch2=0xff;
static z80 zc;
static FILE* ymlogf; static double now_s; static u8 ymaddr;

// ---------------- YM2151 via ymfm ----------------
struct YmIf : public ymfm::ymfm_interface {
    double timer_left[2]={-1,-1}; bool irq=false;
    void ymfm_set_timer(uint32_t tnum, int32_t duration) override { timer_left[tnum] = duration < 0 ? -1 : duration; }
    void ymfm_update_irq(bool asserted) override { irq = asserted; }
    void advance(double clocks, ymfm::ym2151 &chip){
        for(int t=0;t<2;t++) if(timer_left[t]>=0){ timer_left[t]-=clocks; if(timer_left[t]<=0){ timer_left[t]=-1; m_engine->engine_timer_expired(t);} }
    }
};
static YmIf yif; static ymfm::ym2151 *ym;

// ---------------- OKI MSM6295 ----------------
static const int okisteps[49]={16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552};
static const int idxadj[8]={-1,-1,-1,-1,2,4,6,8};
static const int okivol[16]={0x20,0x16,0x10,0x0b,0x08,0x06,0x04,0x03,0x02,0,0,0,0,0,0,0};
struct OkiVoice { bool playing; u32 addr, end; int nib; int sig, step; int vol; };
static OkiVoice ov[4]; static int oki_cmd=-1;
static void oki_w(u8 d){
    if(oki_cmd>=0){
        int ch=d>>4;
        for(int i=0;i<4;i++) if(ch&(1<<i)){
            if(ov[i].playing) continue;
            const u8*e=&oki[oki_cmd*8];
            u32 s=((e[0]<<16)|(e[1]<<8)|e[2])&0x3ffff, t=((e[3]<<16)|(e[4]<<8)|e[5])&0x3ffff;
            if(s<t){ ov[i].playing=true; ov[i].addr=s; ov[i].end=t; ov[i].nib=0; ov[i].sig=0; ov[i].step=0; ov[i].vol=okivol[d&15]; }
        }
        oki_cmd=-1; return;
    }
    if(d&0x80){ oki_cmd=d&0x7f; return; }
    for(int i=0;i<4;i++) if(d&(8<<i)) ov[i].playing=false;
}
static u8 oki_status(){ u8 s=0xf0; for(int i=0;i<4;i++) if(ov[i].playing) s|=1<<i; return s; }
static int oki_sample(){
    int out=0;
    for(int i=0;i<4;i++){ OkiVoice&v=ov[i]; if(!v.playing) continue;
        u8 b=oki[v.addr]; int n = v.nib ? (b&15) : (b>>4);
        int s=okisteps[v.step]; int diff=((2*(n&7)+1)*s)/8; if(n&8) diff=-diff;
        v.sig+=diff; if(v.sig>2047) v.sig=2047; if(v.sig<-2048) v.sig=-2048;
        v.step+=idxadj[n&7]; if(v.step<0) v.step=0; if(v.step>48) v.step=48;
        if(v.nib){ v.addr++; if(v.addr>v.end) v.playing=false; } v.nib^=1;
        out+=v.sig*v.vol/2;
    }
    return out;
}

// ---------------- Z80 bus ----------------
static u8 zr(void*,u16 a){
    if(a<0x8000) return zrom[a];
    if(a<0xc000) return zrom[0x8000+zbank*0x4000+(a-0x8000)];
    if(a>=0xd000&&a<0xd800) return zram[a-0xd000];
    if(a==0xf001||a==0xf000) return ym->read(1);
    if(a==0xf002) return oki_status();
    if(a==0xf008) return latch1;
    if(a==0xf00a) return latch2;
    return 0xff;
}
static void zw(void*,u16 a,u8 d){
    if(a>=0xd000&&a<0xd800){ zram[a-0xd000]=d; return; }
    if(a==0xf000){ ymaddr=d; ym->write(0,d); return; }
    if(a==0xf001){ if(ymlogf) fprintf(ymlogf,"%.6f %02X %02X\n",now_s,ymaddr,d); ym->write(1,d); return; }
    if(a==0xf002){ if(getenv("OKILOG")) fprintf(stderr,"oki %.4f %02X\n",now_s,d); oki_w(d); return; }
    if(a==0xf004){ zbank=d&1; return; }
}
static u8 zin(z80*,u8){return 0xff;} static void zout(z80*,u8,u8){}

static void wav_write(const char*fn,const std::vector<int16_t>&s,int rate){
    FILE*f=fopen(fn,"wb"); u32 n=s.size()*2;
    auto w32=[&](u32 v){fwrite(&v,4,1,f);}; auto w16=[&](u16 v){fwrite(&v,2,1,f);};
    fwrite("RIFF",1,4,f); w32(36+n); fwrite("WAVEfmt ",1,8,f); w32(16); w16(1); w16(2); w32(rate); w32(rate*4); w16(4); w16(16);
    fwrite("data",1,4,f); w32(n); fwrite(s.data(),2,s.size(),f); fclose(f);
}
int main(int argc,char**argv){
    if(argc<4){ fprintf(stderr,"usage: sndref CMD SECONDS out.wav [ymlog]\n"); return 1; }
    const char*R=getenv("ROMDIR")?getenv("ROMDIR"):".";
    char fn[512]; FILE*f;
    snprintf(fn,sizeof fn,"%s/z80.bin",R); f=fopen(fn,"rb"); fread(zrom,1,0x10000,f); fclose(f);
    snprintf(fn,sizeof fn,"%s/oki.bin",R); f=fopen(fn,"rb"); fread(oki,1,0x40000,f); fclose(f);
    int cmd=strtol(argv[1],0,16); double secs=atof(argv[2]);
    if(argc>4) ymlogf=fopen(argv[4],"w");
    if(getenv("LATCH2")) latch2=strtol(getenv("LATCH2"),0,16);
    int rate=getenv("RATE")?atoi(getenv("RATE")):44100;
    ym=new ymfm::ym2151(yif); ym->reset();
    z80_init(&zc); zc.read_byte=zr; zc.write_byte=zw; zc.port_in=zin; zc.port_out=zout;
    const double YMCLK=3579545.0, ZCLK=3579545.0, OKIRATE=1000000.0/132;
    const double ymrate=YMCLK/64;               // native output rate of OPM
    std::vector<int16_t> out; out.reserve((size_t)(secs*rate*2+16));
    double t=0, dt=1.0/ymrate, zacc=0, okiacc=0; int okiv=0;
    double settle=getenv("SETTLE")?atof(getenv("SETTLE")):1.0;
    double nextout=0; double prevL=0,prevR=0;
    bool sent=false;
    ymfm::ym2151::output_data od;
    while(t<secs+settle){
        now_s=t-settle;
        { static int ph=0;
          if(ph==0 && t>=0.10){ latch1=0xF0; ph=1; } else if(ph==1 && t>=0.14){ latch1=0xFF; ph=2; }
          else if(ph==2 && t>=0.20){ latch1=0xF7; ph=3; } else if(ph==3 && t>=0.24){ latch1=0xFF; ph=4; }
          else if(ph==4 && t>=settle){ latch1=cmd; sent=true; ph=5; } else if(ph==5 && t>=settle+0.034){ latch1=0xFF; ph=6; }
          /* SEQ="t:cc,t:cc,..." more commands (seconds after the first) */
          static double sq_t[256]; static int sq_c[256], sq_n=-1, sq_i=0; static double sq_off=-1;
          if(sq_n<0){ sq_n=0; const char*e=getenv("SEQ"); while(e&&*e&&sq_n<256){ double tt; int cc,n; if(sscanf(e,"%lf:%x%n",&tt,&cc,&n)<2) break; sq_t[sq_n]=tt; sq_c[sq_n++]=cc; e+=n; if(*e==',') e++; } }
          if(sq_off>=0 && t>=sq_off){ latch1=0xFF; sq_off=-1; }
          if(ph>=6 && sq_i<sq_n && t>=settle+sq_t[sq_i]){ latch1=sq_c[sq_i++]; sq_off=t+0.034; } }
        // run Z80 for one OPM sample period
        zacc+=ZCLK*dt; unsigned long tgt=zc.cyc+(unsigned long)zacc; zacc-=(unsigned long)zacc;
        while(zc.cyc<tgt){ if(yif.irq) z80_gen_int(&zc,0xff); else zc.int_pending=0; z80_step(&zc); }
        yif.advance(64,*ym);
        ym->generate(&od,1);
        okiacc+=OKIRATE*dt; while(okiacc>=1){ okiacc-=1; okiv=oki_sample(); }
        static double gy=getenv("GYM")?atof(getenv("GYM")):0.35, go=getenv("GOKI")?atof(getenv("GOKI")):0.9;
        double L=od.data[0]*gy+okiv*go, Rr=od.data[1]*gy+okiv*go;
        // resample to 'rate' by linear interpolation
        while(t>=settle && nextout<=t-settle){
            double a=(nextout-(t-settle-dt))/dt; if(a<0)a=0; if(a>1)a=1;
            double l=prevL+(L-prevL)*a, r=prevR+(Rr-prevR)*a;
            auto cl=[](double v){ v=std::lround(v); if(v>32767)v=32767; if(v<-32768)v=-32768; return (int16_t)v; };
            out.push_back(cl(l)); out.push_back(cl(r)); nextout+=1.0/rate;
        }
        prevL=L; prevR=Rr; t+=dt;
    }
    wav_write(argv[3],out,rate);
    if(ymlogf) fclose(ymlogf);
    double rms=0; for(auto v:out) rms+=(double)v*v; rms=sqrt(rms/std::max<size_t>(1,out.size()));
    fprintf(stderr,"cmd %02X %.1fs rms %.0f samples %zu\n",cmd,secs,rms,out.size()/2);
    return 0;
}
