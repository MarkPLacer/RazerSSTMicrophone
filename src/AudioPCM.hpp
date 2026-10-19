// SPDX-License-Identifier: BSD-3-Clause
// Input conditioning derived from LatSOFKernelAudio (see LICENSE.donor).
#pragma once
#include <stdint.h>
namespace RazerPCM {
constexpr uint32_t Frames=8192,Channels=2,Bytes=Frames*Channels*4,Rate=48000;
struct Filter {
    float x[2]{},y[2]{};
    uint32_t next=0xffffffffu;
    uint32_t cachedFirst=0,cachedCount=0,cachedLoop=0;
    bool cached=false;
    float cache[Frames*Channels]{};
    void reset(){x[0]=x[1]=y[0]=y[1]=0.0f;next=0xffffffffu;cached=false;}
    bool convert(const int32_t *source,float *dest,uint32_t first,uint32_t count,
                 float gain,bool muted,uint32_t loop) {
        if(!source || !dest || first>=Frames || count>Frames || !(gain>=1.0f && gain<=100.0f))return false;
        if(cached && first==cachedFirst && count==cachedCount && loop==cachedLoop){
            for(uint32_t i=0;i<count*Channels;i++){
                float value=muted?0.0f:cache[i]*gain;
                if(value>1.0f)value=1.0f;if(value< -1.0f)value= -1.0f;dest[i]=value;
            }
            return true;
        }
        if(next==0xffffffffu)next=first;
        const bool advance=first==next;
        float xx[2]={x[0],x[1]},yy[2]={y[0],y[1]};
        for(uint32_t f=0;f<count;f++)for(uint32_t ch=0;ch<Channels;ch++){
            const float input=float(source[((first+f)%Frames)*Channels+ch])/2147483648.0f;
            float output=input-xx[ch]+0.98822f*yy[ch];
            xx[ch]=input;if(output<1e-15f && output>-1e-15f)output=0.0f;yy[ch]=output;
            cache[f*Channels+ch]=output;
            output=muted?0.0f:output*gain;
            if(output>1.0f)output=1.0f;if(output< -1.0f)output= -1.0f;
            dest[f*Channels+ch]=output;
        }
        if(advance){x[0]=xx[0];x[1]=xx[1];y[0]=yy[0];y[1]=yy[1];next=(first+count)%Frames;}
        cached=true;cachedFirst=first;cachedCount=count;cachedLoop=loop;return true;
    }
};
// Control values are dB. No transcendental calls occur in the realtime path.
inline float gainForDB(uint32_t db){
    static const float gain[41]={1.0f,1.122018f,1.258925f,1.412538f,1.584893f,1.778279f,1.995262f,2.238721f,2.511886f,2.818383f,3.162278f,3.548134f,3.981072f,4.466836f,5.011872f,5.623413f,6.309573f,7.079458f,7.943282f,8.912509f,10.0f,11.220185f,12.589254f,14.125375f,15.848932f,17.782794f,19.952623f,22.387211f,25.118864f,28.183829f,31.622777f,35.481339f,39.810717f,44.668359f,50.118723f,56.234133f,63.095734f,70.794578f,79.432823f,89.125094f,100.0f};
    return gain[db<=40?db:40];
}
inline bool wrapsSince(uint32_t previous,uint32_t current,uint64_t elapsedNS,uint32_t &wraps){
    if(previous>=Frames || current>=Frames || elapsedNS>1500000000ULL)return false;
    const int64_t frames=int64_t(elapsedNS*Rate/1000000000ULL);
    const int64_t delta=int64_t(current)-previous;
    const int64_t count=(frames-delta+Frames/2)/Frames;
    if(count<0 || count>8)return false;
    wraps=uint32_t(count);return true;
}
}
