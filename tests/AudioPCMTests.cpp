#include "AudioPCM.hpp"
#include <assert.h>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>
int main(){
    using namespace RazerPCM;
    Filter filter;
    std::vector<int32_t> input(Frames*Channels);
    std::vector<float> a(Frames*Channels),b(Frames*Channels);
    for(uint32_t f=0;f<Frames;f++){
        input[f*2]=int32_t(2000000.0*std::sin(f*2.0*3.141592653589793*1000/Rate)+1000000);
        input[f*2+1]=-input[f*2];
    }
    assert(filter.convert(input.data(),a.data(),0,512,gainForDB(30),false,0));
    assert(filter.convert(input.data(),b.data(),0,512,gainForDB(30),false,0));
    for(unsigned i=0;i<1024;i++)assert(a[i]==b[i]);
    assert(filter.convert(input.data(),b.data(),0,512,gainForDB(30),true,0));
    for(unsigned i=0;i<1024;i++)assert(b[i]==0.0f);
    assert(filter.convert(input.data(),b.data(),0,512,gainForDB(30),false,0));
    for(unsigned i=0;i<1024;i++)assert(a[i]==b[i]);
    assert(filter.convert(input.data(),b.data(),0,512,gainForDB(0),false,0));
    for(unsigned i=0;i<1024;i++)assert(std::fabs(a[i]-b[i]*gainForDB(30))<1e-6f);
    assert(filter.convert(input.data(),a.data(),Frames-127,512,gainForDB(40),false,1));
    for(unsigned i=0;i<1024;i++){assert(std::isfinite(a[i]));assert(a[i]>=-1.0f && a[i]<=1.0f);}
    assert(!filter.convert(nullptr,a.data(),0,1,1,false,0));
    assert(!filter.convert(input.data(),nullptr,0,1,1,false,0));
    assert(!filter.convert(input.data(),a.data(),Frames,1,1,false,0));
    assert(!filter.convert(input.data(),a.data(),0,Frames+1,1,false,0));
    assert(!filter.convert(input.data(),a.data(),0,1,std::numeric_limits<float>::quiet_NaN(),false,0));
    assert(!filter.convert(input.data(),a.data(),0,1,101.0f,false,0));
    // Constant PDM DC must decay instead of consuming microphone headroom.
    filter.reset();for(auto &v:input)v=2000000;
    for(uint32_t j=0;j<16;j++)assert(filter.convert(input.data(),a.data(),j*512,512,gainForDB(30),false,2));
    for(unsigned i=0;i<1024;i++)assert(std::fabs(a[i])<1e-8f);
    // Full-scale alternating impulses are safely bounded after gain.
    filter.reset();for(unsigned i=0;i<input.size();i++)input[i]=(i%4<2)?INT32_MAX:INT32_MIN;
    assert(filter.convert(input.data(),a.data(),0,Frames,gainForDB(40),false,3));
    for(float value:a){assert(std::isfinite(value));assert(value>=-1.0f && value<=1.0f);}
    uint32_t wraps=999;
    assert(wrapsSince(100,1060,20000000,wraps) && wraps==0);
    assert(wrapsSince(7800,568,20000000,wraps) && wraps==1);
    assert(wrapsSince(100,2916,400000000,wraps) && wraps==2);
    assert(!wrapsSince(Frames,0,1000000,wraps));
    assert(!wrapsSince(0,0,1600000000ULL,wraps));
    puts("PASS: stereo/DC/gain/mute/clipping/ring bounds/duplicate clients/clock wraps (ASan+UBSan)");
}
