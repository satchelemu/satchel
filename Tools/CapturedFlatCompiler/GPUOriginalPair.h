/* Original public-encoding fixture, copied from reviewed captured-facade test. */
static uint8_t vs[1104],fs[1104];
static void w32(uint8_t*p,uint32_t x){memcpy(p,&x,4);}
static void w64(uint8_t*p,uint64_t x){memcpy(p,&x,8);}
static uint64_t r64(const uint8_t*p){uint64_t x;memcpy(&x,p,8);return x;}
static void original(unsigned location,unsigned rotate){
 memset(vs,0,sizeof vs);memset(fs,0,sizeof fs);
 w32(vs,0x60461);w32(fs,0x61462);w32(vs+16,0xff000);w32(fs+16,0xff000);
 w32(vs+24,255);w32(vs+52,0xf0000000);w32(vs+56,15u<<(location*4));
 w32(fs+24+(location/4)*4,0x55u<<((location%4)*8));w32(fs+72,15);
 const uint64_t v[]={0,0xefd9ff800807ff00ULL,0xeff1ff800707ff00ULL,0xefd9ff800907ff04ULL,0,0xeff1ff800807ff04ULL,0xe30000000007000fULL};
 for(unsigned i=0;i<7;i++)w64(vs+80+i*8,v[i]);
 w64(vs+120,r64(vs+120)+((uint64_t)location*16<<20));
 for(unsigned i=0;i<4;i++){unsigned slot=i<3?i+1:5;uint64_t off=0x80+location*16+((i+rotate)%4)*4;w64(fs+80+slot*8,0xe003ff800ff7ff00ULL|(off<<28)|i);}
 w64(fs+128,0xe30000000007000fULL);
}
