// SPDX-License-Identifier: BSD-3-Clause
// IOAudioFamily layer adapted from LatSOFKernelAudio. See LICENSE.donor.
#pragma once
#include <IOKit/audio/IOAudioDevice.h>
#include <IOKit/audio/IOAudioEngine.h>
#include <IOKit/audio/IOAudioStream.h>
#include <IOKit/audio/IOAudioLevelControl.h>
#include <IOKit/audio/IOAudioToggleControl.h>
#include <IOKit/audio/IOAudioSelectorControl.h>
class RazerSSTMicrophone;
class RazerMicAudioEngine;
class RazerMicAudioDevice:public IOAudioDevice {
    OSDeclareDefaultStructors(RazerMicAudioDevice)
    RazerSSTMicrophone *owner_=nullptr;
    RazerMicAudioEngine *engine_=nullptr;
public:
    bool initWithOwner(RazerSSTMicrophone *);
    bool initHardware(IOService *) override;
    void stop(IOService *) override;
    IOWorkLoop *getWorkLoop() const override;
    RazerMicAudioEngine *engine(){return engine_;}
    void clearEngine(){engine_=nullptr;}
};
class RazerMicAudioEngine:public IOAudioEngine {
    OSDeclareDefaultStructors(RazerMicAudioEngine)
    RazerSSTMicrophone *owner_=nullptr;
    IOTimerEventSource *timer_=nullptr;
    RazerPCM::Filter *filter_=nullptr;
    volatile SInt32 gainDB_=30,muted_=0;
    bool running_=false,resumeWanted_=false;
    UInt32 lastFrame_=0,clockMisses_=0;
    UInt64 lastTickNS_=0,lastProgressNS_=0;
    void stamp(bool,AbsoluteTime,UInt32);
    bool edgePosition(UInt32 &,AbsoluteTime &);
    static void tick(OSObject *,IOTimerEventSource *);
    static IOReturn gainChanged(OSObject *,IOAudioControl *,SInt32,SInt32);
    static IOReturn muteChanged(OSObject *,IOAudioControl *,SInt32,SInt32);
public:
    bool initWithOwner(RazerSSTMicrophone *);
    bool initHardware(IOService *) override;
    void stop(IOService *) override;
    void free() override;
    IOReturn performAudioEngineStart() override;
    IOReturn performAudioEngineStop() override;
    UInt32 getCurrentSampleFrame() override;
    OSString *getGlobalUniqueID() override {return OSString::withCString("RazerSSTMicrophone:8086-51C8:1A58-3005:PDM1");}
    IOReturn performFormatChange(IOAudioStream *,const IOAudioStreamFormat *,const IOAudioSampleRate *) override;
    IOReturn convertInputSamples(const void *,void *,UInt32,UInt32,const IOAudioStreamFormat *,IOAudioStream *) override;
    void prepareForSleep();
    bool resumeIfNeeded();
};
