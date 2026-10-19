// SPDX-License-Identifier: BSD-3-Clause
// Adapted IOAudioFamily publication and timestamps from LatSOFKernelAudio.
OSDefineMetaClassAndStructors(RazerMicAudioDevice,IOAudioDevice)
OSDefineMetaClassAndStructors(RazerMicAudioEngine,IOAudioEngine)
bool RazerMicAudioDevice::initWithOwner(RazerSSTMicrophone *owner) {
    if(!IOAudioDevice::init(nullptr))return false;
    owner_=owner;setFamilyManagePower(false);return owner_!=nullptr;
}
IOWorkLoop *RazerMicAudioDevice::getWorkLoop() const {
    return owner_?owner_->getWorkLoop():IOAudioDevice::getWorkLoop();
}
bool RazerMicAudioDevice::initHardware(IOService *provider) {
    if(!owner_ || !IOAudioDevice::initHardware(provider))return false;
    setDeviceName("Razer Internal Microphone");setDeviceShortName("Razer Mic");
    setManufacturerName("Razer Blade 17");setDeviceModelName("Blade 17 PDM1 Stereo Microphone");
    setDeviceTransportType(kIOAudioDeviceTransportTypeBuiltIn);
    setDeviceCanBeDefault(kIOAudioDeviceCanBeDefaultInput);
    engine_=new RazerMicAudioEngine;
    if(!engine_)return false;
    if(!engine_->initWithOwner(owner_)){engine_->release();engine_=nullptr;return false;}
    if(activateAudioEngine(engine_)!=kIOReturnSuccess){engine_->release();engine_=nullptr;return false;}
    engine_->release();return true;
}
void RazerMicAudioDevice::stop(IOService *provider) {
    if(owner_)owner_->audioStopping(this);
    engine_=nullptr;IOAudioDevice::stop(provider);
}
bool RazerMicAudioEngine::initWithOwner(RazerSSTMicrophone *owner) {
    if(!IOAudioEngine::init(nullptr))return false;
    owner_=owner;filter_=new RazerPCM::Filter;return owner_ && filter_;
}
bool RazerMicAudioEngine::initHardware(IOService *provider) {
    if(!owner_ || !filter_ || !IOAudioEngine::initHardware(provider))return false;
    auto *ring=owner_->captureBuffer();if(!ring)return false;
    setDescription("Razer PDM1 internal microphone capture");
    IOAudioSampleRate rate={RazerPCM::Rate,0};setSampleRate(&rate);
    setNumSampleFramesPerBuffer(RazerPCM::Frames);setSampleOffset(480);
    setInputSampleLatency(240);setClockDomain();
    auto *stream=new IOAudioStream;if(!stream)return false;
    if(!stream->initWithAudioEngine(this,kIOAudioStreamDirectionInput,1)){stream->release();return false;}
    IOAudioStreamFormat format{};format.fNumChannels=2;
    format.fSampleFormat=kIOAudioStreamSampleFormatLinearPCM;
    format.fNumericRepresentation=kIOAudioStreamNumericRepresentationSignedInt;
    format.fBitDepth=32;format.fBitWidth=32;format.fAlignment=kIOAudioStreamAlignmentHighByte;
    format.fByteOrder=kIOAudioStreamByteOrderLittleEndian;format.fIsMixable=true;
    stream->addAvailableFormat(&format,&rate,&rate);
    IOReturn result=stream->setFormat(&format);
    stream->setTerminalType(INPUT_MICROPHONE);stream->setSampleBuffer(ring->getBytesNoCopy(),RazerPCM::Bytes);
    if(result==kIOReturnSuccess)result=addAudioStream(stream);
    stream->release();if(result!=kIOReturnSuccess)return false;
    auto *gain=IOAudioLevelControl::createVolumeControl(30,0,40,0,40<<16,kIOAudioControlChannelIDAll,nullptr,0,kIOAudioControlUsageInput);
    if(!gain)return false;
    gain->setValueChangeHandler((IOAudioControl::IntValueChangeHandler)gainChanged,this);
    result=addDefaultAudioControl(gain);gain->release();if(result!=kIOReturnSuccess)return false;
    auto *mute=IOAudioToggleControl::createMuteControl(false,kIOAudioControlChannelIDAll,nullptr,1,kIOAudioControlUsageInput);
    if(!mute)return false;
    mute->setValueChangeHandler((IOAudioControl::IntValueChangeHandler)muteChanged,this);
    result=addDefaultAudioControl(mute);mute->release();if(result!=kIOReturnSuccess)return false;
    auto *source=IOAudioSelectorControl::createInputSelector(kIOAudioSelectorControlSelectionValueInternalMicrophone,kIOAudioControlChannelIDAll);
    if(!source)return false;
    source->addAvailableSelection(kIOAudioSelectorControlSelectionValueInternalMicrophone,"Internal Microphone");
    result=addDefaultAudioControl(source);source->release();if(result!=kIOReturnSuccess)return false;
    timer_=IOTimerEventSource::timerEventSource(this,tick);
    if(!timer_)return false;
    if(!getWorkLoop() || getWorkLoop()->addEventSource(timer_)!=kIOReturnSuccess){timer_->release();timer_=nullptr;return false;}
    return true;
}
void RazerMicAudioEngine::stop(IOService *provider) {
    if(timer_){timer_->cancelTimeout();if(getWorkLoop())getWorkLoop()->removeEventSource(timer_);timer_->release();timer_=nullptr;}
    IOAudioEngine::stop(provider);
}
void RazerMicAudioEngine::free() {
    if(filter_){delete filter_;filter_=nullptr;}
    IOAudioEngine::free();
}
IOReturn RazerMicAudioEngine::gainChanged(OSObject *target,IOAudioControl *,SInt32,SInt32 value) {
    auto *self=OSDynamicCast(RazerMicAudioEngine,target);if(!self || value<0 || value>40)return kIOReturnBadArgument;
    __atomic_store_n(&self->gainDB_,value,__ATOMIC_RELAXED);return kIOReturnSuccess;
}
IOReturn RazerMicAudioEngine::muteChanged(OSObject *target,IOAudioControl *,SInt32,SInt32 value) {
    auto *self=OSDynamicCast(RazerMicAudioEngine,target);if(!self || (value!=0 && value!=1))return kIOReturnBadArgument;
    __atomic_store_n(&self->muted_,value,__ATOMIC_RELAXED);return kIOReturnSuccess;
}
void RazerMicAudioEngine::stamp(bool increment,AbsoluteTime when,UInt32 framesAgo) {
    UInt64 ns=0;absolutetime_to_nanoseconds(when,&ns);
    UInt64 ago=UInt64(framesAgo)*1000000000ULL/RazerPCM::Rate;
    AbsoluteTime at;nanoseconds_to_absolutetime(ns>ago?ns-ago:0,&at);takeTimeStamp(increment,&at);
}
bool RazerMicAudioEngine::edgePosition(UInt32 &position,AbsoluteTime &when) {
    UInt32 previous=0;if(!owner_ || !owner_->readPosition(previous))return false;
    AbsoluteTime start;clock_get_uptime(&start);UInt64 startNS=0;absolutetime_to_nanoseconds(start,&startNS);
    for(;;){
        AbsoluteTime before,after;UInt64 a=0,b=0;UInt32 current;
        clock_get_uptime(&before);if(!owner_->readPosition(current))return false;clock_get_uptime(&after);
        absolutetime_to_nanoseconds(before,&a);absolutetime_to_nanoseconds(after,&b);
        if(b-a<=20000 && current!=previous){position=current;when=after;return true;}
        if(b-startNS>=2000000)break;IODelay(5);
    }
    return false;
}
IOReturn RazerMicAudioEngine::performAudioEngineStart() {
    if(!owner_)return kIOReturnNotReady;
    IOReturn result=owner_->beginCapture();if(result!=kIOReturnSuccess)return result;
    filter_->reset();running_=true;
    UInt32 pos=0;AbsoluteTime at;clock_get_uptime(&at);
    if(!edgePosition(pos,at)){owner_->readPosition(pos);clockMisses_++;}
    stamp(false,at,pos);lastFrame_=pos;absolutetime_to_nanoseconds(at,&lastTickNS_);
    lastProgressNS_=lastTickNS_;
    timer_->setTimeoutMS(20);setProperty("CaptureSessionActive",true);return kIOReturnSuccess;
}
IOReturn RazerMicAudioEngine::performAudioEngineStop() {
    running_=false;if(timer_)timer_->cancelTimeout();
    if(owner_)owner_->endCapture();
    setProperty("CaptureSessionActive",false);setProperty("ClockEdgeMisses",clockMisses_,32);return kIOReturnSuccess;
}
UInt32 RazerMicAudioEngine::getCurrentSampleFrame() {
    UInt32 position=0;return owner_ && owner_->readPosition(position)?position:0;
}
void RazerMicAudioEngine::tick(OSObject *target,IOTimerEventSource *timer) {
    auto *self=OSDynamicCast(RazerMicAudioEngine,target);if(!self || !self->running_)return;
    AbsoluteTime at;clock_get_uptime(&at);UInt32 pos=0;
    if(!self->owner_->readPosition(pos)){self->setProperty("ClockFailure","Capture position unavailable");self->stopAudioEngine();return;}
    UInt64 ns=0;absolutetime_to_nanoseconds(at,&ns);
    if(pos!=self->lastFrame_)self->lastProgressNS_=ns;
    if(ns-self->lastProgressNS_>100000000ULL){self->setProperty("ClockFailure","Capture DMA stopped advancing");self->stopAudioEngine();return;}
    UInt32 wraps=0;
    if(!RazerPCM::wrapsSince(self->lastFrame_,pos,ns-self->lastTickNS_,wraps)){self->setProperty("ClockFailure","Scheduling gap exceeded bounded capture timeline");self->stopAudioEngine();return;}
    if(wraps){
        UInt32 edge=pos;AbsoluteTime edgeTime=at;
        if(self->edgePosition(edge,edgeTime)){pos=edge;at=edgeTime;}
        else self->clockMisses_++;
        for(SInt64 i=0;i<wraps;i++)self->stamp(true,at,pos+UInt32(wraps-i-1)*RazerPCM::Frames);
    }
    self->lastFrame_=pos;absolutetime_to_nanoseconds(at,&self->lastTickNS_);timer->setTimeoutMS(20);
}
IOReturn RazerMicAudioEngine::performFormatChange(IOAudioStream *,const IOAudioStreamFormat *format,const IOAudioSampleRate *rate) {
    if(rate && (rate->whole!=RazerPCM::Rate || rate->fraction))return kIOReturnUnsupported;
    if(format && (format->fNumChannels!=2 || format->fBitDepth!=32 || format->fBitWidth!=32 ||
       format->fSampleFormat!=kIOAudioStreamSampleFormatLinearPCM ||
       format->fNumericRepresentation!=kIOAudioStreamNumericRepresentationSignedInt ||
       format->fByteOrder!=kIOAudioStreamByteOrderLittleEndian || !format->fIsMixable))return kIOReturnUnsupported;
    return kIOReturnSuccess;
}
IOReturn RazerMicAudioEngine::convertInputSamples(const void *source,void *dest,UInt32 first,UInt32 count,const IOAudioStreamFormat *format,IOAudioStream *) {
    if(!filter_ || !format || format->fNumChannels!=2 || format->fBitWidth!=32 || first>=RazerPCM::Frames || count>RazerPCM::Frames)return kIOReturnBadArgument;
    UInt32 loop=0;AbsoluteTime timestamp;getLoopCountAndTimeStamp(&loop,&timestamp);
    return filter_->convert(static_cast<const int32_t *>(source),static_cast<float *>(dest),first,count,
        RazerPCM::gainForDB(UInt32(__atomic_load_n(&gainDB_,__ATOMIC_RELAXED))),__atomic_load_n(&muted_,__ATOMIC_RELAXED)!=0,loop)?kIOReturnSuccess:kIOReturnBadArgument;
}
void RazerMicAudioEngine::prepareForSleep() {
    resumeWanted_=numActiveUserClients>0 && getState()==kIOAudioEngineRunning;
    if(getState()==kIOAudioEngineRunning)pauseAudioEngine();
}
bool RazerMicAudioEngine::resumeIfNeeded() {
    if(!resumeWanted_ || numActiveUserClients==0){resumeWanted_=false;return true;}
    if(getState()==kIOAudioEngineRunning){resumeWanted_=false;return true;}
    if(getState()==kIOAudioEnginePaused)IOAudioEngine::resumeAudioEngine();
    IOReturn result=startAudioEngine();
    bool running=result==kIOReturnSuccess && getState()==kIOAudioEngineRunning;
    if(running)resumeWanted_=false;return running;
}
