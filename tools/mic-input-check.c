// SPDX-License-Identifier: BSD-3-Clause
// Standard CoreAudio input client. Keeps only aggregate statistics in RAM;
// no audio file, output playback, routing or volume change.
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef struct {
    AudioStreamBasicDescription format;
    unsigned long long callbacks,frames,changes[2],nonzero[2],invalid;
    double sumSquares[2],peak[2],previous[2];
    double bandCoefficient,bandS1[2],bandS2[2],bandEnergy[2];
    unsigned bandFrames[2];
    unsigned long long bandBlocks[2],clipped[2];
} Stats;
static OSStatus input(AudioObjectID device,const AudioTimeStamp *now,const AudioBufferList *buffers,
                      const AudioTimeStamp *at,AudioBufferList *out,const AudioTimeStamp *outAt,void *context){
    (void)device;(void)now;(void)at;(void)out;(void)outAt;
    Stats *stats=context;stats->callbacks++;
    if(!buffers || !buffers->mNumberBuffers || buffers->mNumberBuffers>2)return noErr;
    UInt32 frames=0,totalChannels=0;
    for(UInt32 b=0;b<buffers->mNumberBuffers;b++){
        const AudioBuffer *buffer=&buffers->mBuffers[b];
        if(!buffer->mData || !buffer->mNumberChannels || buffer->mNumberChannels>2){stats->invalid++;continue;}
        UInt32 count=buffer->mDataByteSize/(4*buffer->mNumberChannels);if(count>frames)frames=count;
        for(UInt32 f=0;f<count;f++)for(UInt32 c=0;c<buffer->mNumberChannels;c++){
            UInt32 channel=totalChannels+c;if(channel>1)continue;
            UInt32 offset=f*buffer->mNumberChannels+c;
            double value=(stats->format.mFormatFlags&kAudioFormatFlagIsFloat)?((const float *)buffer->mData)[offset]:((const SInt32 *)buffer->mData)[offset]/2147483648.0;
            if(!isfinite(value) || fabs(value)>1.0001){stats->invalid++;continue;}
            if(value!=0.0)stats->nonzero[channel]++;
            if(value!=stats->previous[channel])stats->changes[channel]++;
            stats->previous[channel]=value;stats->sumSquares[channel]+=value*value;
            if(fabs(value)>stats->peak[channel])stats->peak[channel]=fabs(value);
            if(fabs(value)>=0.999999)stats->clipped[channel]++;
            // Bounded 1024-frame Goertzel windows measure a known test tone.
            // Only aggregate band energy survives; no microphone samples are saved.
            double next=value+stats->bandCoefficient*stats->bandS1[channel]-stats->bandS2[channel];
            stats->bandS2[channel]=stats->bandS1[channel];stats->bandS1[channel]=next;
            if(++stats->bandFrames[channel]==1024){
                double a=stats->bandS1[channel],b=stats->bandS2[channel];
                stats->bandEnergy[channel]+=fmax(0.0,2*(a*a+b*b-stats->bandCoefficient*a*b)/(1024.0*1024.0));
                stats->bandBlocks[channel]++;stats->bandFrames[channel]=0;stats->bandS1[channel]=stats->bandS2[channel]=0;
            }
        }
        totalChannels+=buffer->mNumberChannels;
    }
    stats->frames+=frames;return noErr;
}
static AudioDeviceID find(void){
    AudioObjectPropertyAddress address={kAudioHardwarePropertyDevices,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    UInt32 bytes=0;if(AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,&address,0,NULL,&bytes))return 0;
    AudioDeviceID *devices=malloc(bytes);if(!devices)return 0;
    if(AudioObjectGetPropertyData(kAudioObjectSystemObject,&address,0,NULL,&bytes,devices)){free(devices);return 0;}
    AudioDeviceID found=0;
    for(UInt32 n=0;n<bytes/sizeof(*devices);n++){
        CFStringRef uid=NULL;UInt32 size=sizeof uid;
        address=(AudioObjectPropertyAddress){kAudioDevicePropertyDeviceUID,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
        if(!AudioObjectGetPropertyData(devices[n],&address,0,NULL,&size,&uid) && uid){
            if(CFEqual(uid,CFSTR("RazerSSTMicrophone:8086-51C8:1A58-3005:PDM1")))found=devices[n];CFRelease(uid);
        }
    }
    free(devices);return found;
}
int main(void){
    AudioDeviceID device=0;for(unsigned i=0;i<150 && !device;i++){device=find();if(!device)usleep(100000);}
    if(!device){puts("{\"device_found\":false,\"audio_saved\":false}");return 2;}
    AudioObjectPropertyAddress address={kAudioDevicePropertyStreamFormat,kAudioObjectPropertyScopeInput,kAudioObjectPropertyElementMain};
    AudioStreamBasicDescription format={0};UInt32 size=sizeof format;
    OSStatus status=AudioObjectGetPropertyData(device,&address,0,NULL,&size,&format);
    if(status || format.mFormatID!=kAudioFormatLinearPCM || format.mBitsPerChannel!=32 || !(format.mFormatFlags&(kAudioFormatFlagIsFloat|kAudioFormatFlagIsSignedInteger)) || format.mChannelsPerFrame!=2 || format.mSampleRate!=48000.0 || (format.mFormatFlags&kAudioFormatFlagIsBigEndian)){
        printf("{\"device_found\":true,\"unsupported_format\":true,\"status\":%d,\"rate\":%.0f,\"channels\":%u,\"bits\":%u,\"audio_saved\":false}\n",status,format.mSampleRate,(unsigned)format.mChannelsPerFrame,(unsigned)format.mBitsPerChannel);return 3;
    }
    int good=1;printf("{\"device_found\":true,\"audio_saved\":false,\"cycles\":[");fflush(stdout);
    for(unsigned cycle=0;cycle<3;cycle++){
        Stats stats={0};stats.format=format;stats.bandCoefficient=2*cos(2*3.14159265358979323846*1200/48000);AudioDeviceIOProcID proc=NULL;
        fprintf(stderr,"cycle%u create begin\n",cycle+1);fflush(stderr);
        status=AudioDeviceCreateIOProcID(device,input,&stats,&proc);
        fprintf(stderr,"cycle%u create end status=%d\n",cycle+1,status);fflush(stderr);
        if(!status){fprintf(stderr,"cycle%u start begin\n",cycle+1);fflush(stderr);status=AudioDeviceStart(device,proc);fprintf(stderr,"cycle%u start end status=%d\n",cycle+1,status);fflush(stderr);}
        if(!status)usleep(4000000);
        if(proc){fprintf(stderr,"cycle%u stop begin\n",cycle+1);fflush(stderr);AudioDeviceStop(device,proc);fprintf(stderr,"cycle%u destroy begin\n",cycle+1);fflush(stderr);AudioDeviceDestroyIOProcID(device,proc);fprintf(stderr,"cycle%u destroy end\n",cycle+1);fflush(stderr);}
        int valid=!status && stats.frames>100000 && !stats.invalid && stats.changes[0]>1000 && stats.changes[1]>1000;
        if(!valid)good=0;
        printf("%s{\"number\":%u,\"status\":%d,\"callbacks\":%llu,\"frames\":%llu,\"invalid\":%llu,\"changing_stereo_input\":%s,\"channel0_changes\":%llu,\"channel1_changes\":%llu,\"channel0_peak\":%.8f,\"channel1_peak\":%.8f,\"channel0_rms\":%.8f,\"channel1_rms\":%.8f,\"channel0_clipped\":%llu,\"channel1_clipped\":%llu,\"channel0_1200hz_rms\":%.8f,\"channel1_1200hz_rms\":%.8f}",cycle?",":"",cycle+1,status,stats.callbacks,stats.frames,stats.invalid,valid?"true":"false",stats.changes[0],stats.changes[1],stats.peak[0],stats.peak[1],sqrt(stats.sumSquares[0]/(stats.frames?stats.frames:1)),sqrt(stats.sumSquares[1]/(stats.frames?stats.frames:1)),stats.clipped[0],stats.clipped[1],sqrt(stats.bandEnergy[0]/(stats.bandBlocks[0]?stats.bandBlocks[0]:1)),sqrt(stats.bandEnergy[1]/(stats.bandBlocks[1]?stats.bandBlocks[1]:1)));fflush(stdout);
        usleep(1500000);
    }
    printf("],\"all_three_cycles_passed\":%s}\n",good?"true":"false");return good?0:4;
}
