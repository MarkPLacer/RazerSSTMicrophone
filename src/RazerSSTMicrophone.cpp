// SPDX-License-Identifier: BSD-3-Clause
// Exact Razer ADL/cAVS2.5 on-demand internal microphone driver.
// Register sequencing adapted from Intel Linux SOF (BSD alternative) and
// LatSOFAudio. Copyright(c) 2018 Intel Corporation; Copyright(c) 2026
// DexterSLamb and Shubham Banekar. See LICENSE.donor and LICENSE.sof.
// No global HDA reset, codec verbs, direct PCI writes or stream borrowing.
// SD0/SD7 stay read-only; all IRQs remain masked. Shared PCI power is requested
// through IOKit PM, not by writing its PMCSR. No raw audio is saved.
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IODMACommand.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSString.h>
#include <libkern/c++/OSBoolean.h>
#include <libkern/OSAtomic.h>
#include "SSTProtocol.hpp"
#include "AudioPCM.hpp"
#include "RazerMicAudio.hpp"
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOMessage.h>
#include <IOKit/pwr_mgt/RootDomain.h>

extern "C" const unsigned char razer_sof_fw[],razer_sof_fw_end[],razer_dmic_dai[];
static constexpr UInt32 BUSY=0x80000000u,PP=0x804,SPIB=0x704;
static UInt8 r8(volatile UInt8 *b,UInt32 o){return b[o];}
static UInt16 r16(volatile UInt8 *b,UInt32 o){return *reinterpret_cast<volatile UInt16 *>(b+o);}
static UInt32 r32(volatile UInt8 *b,UInt32 o){return *reinterpret_cast<volatile UInt32 *>(b+o);}
static void w8(volatile UInt8 *b,UInt32 o,UInt8 v){b[o]=v;OSSynchronizeIO();}
static void w16(volatile UInt8 *b,UInt32 o,UInt16 v){*reinterpret_cast<volatile UInt16 *>(b+o)=v;OSSynchronizeIO();}
static void w32(volatile UInt8 *b,UInt32 o,UInt32 v){*reinterpret_cast<volatile UInt32 *>(b+o)=v;OSSynchronizeIO();}
static void bits(volatile UInt8 *b,UInt32 o,UInt32 m,UInt32 v){w32(b,o,(r32(b,o)&~m)|(v&m));}
static void number(OSDictionary *d,const char *k,UInt64 v){auto *n=OSNumber::withNumber(v,64);if(n){d->setObject(k,n);n->release();}}
struct DMA {
    IOBufferMemoryDescriptor *memory=nullptr;
    IODMACommand *command=nullptr;
    UInt32 size=0;
    bool prepared=false;
    UInt64 bus=0;
    void *bytes(){return memory?memory->getBytesNoCopy():nullptr;}
    void release() {
        if(command){command->clearMemoryDescriptor();command->release();command=nullptr;}
        if(memory){if(prepared)memory->complete();memory->release();memory=nullptr;}
        prepared=false;bus=0;size=0;
    }
    bool allocate(UInt32 amount,UInt64 alignment=4096,unsigned addressBits=64) {
        size=amount;
        memory=IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
            kIOMemoryPhysicallyContiguous|kIODirectionInOut,amount,
            (~(alignment-1)) & (addressBits==32?0xffffffffULL:~0ULL));
        if(!memory || memory->prepare()!=kIOReturnSuccess){release();return false;}
        prepared=true;bzero(bytes(),amount);
        // Called only from start(), before entering the workloop gate.
        command=IODMACommand::withSpecification(kIODMACommandOutputHost64,UInt8(addressBits),amount,
            IODMACommand::kMapped,amount,128);
        if(!command || command->setMemoryDescriptor(memory)!=kIOReturnSuccess){release();return false;}
        UInt64 offset=0;UInt32 count=1;IODMACommand::Segment64 segment;
        if(command->gen64IOVMSegments(&offset,&segment,&count)!=kIOReturnSuccess || count!=1 ||
            segment.fLength!=amount || offset!=amount || !segment.fIOVMAddr || (segment.fIOVMAddr&(alignment-1))) {
            release();return false;
        }
        bus=segment.fIOVMAddr;return true;
    }
};
struct Channel {
    UInt32 index=0,offset=0,tag=0,spibValue=0;
    bool claimed=false,ppBefore=false,spibBefore=false,dmaMayRun=false;
    SST::Stream before{};
    DMA data,bdl;
    UInt32 entries=0;
};
struct PCMParams {
    UInt32 size,cmd,id,flags,reserved[2],paramsSize;
    struct { UInt32 size,address,pages,bytes,reserved[3]; } buffer;
    UInt32 direction,frameFormat,bufferFormat,rate;
    UInt16 tag,channels,validBytes,containerBytes;
    UInt32 periodBytes;
    UInt16 noPosition;UInt8 continuous,reserved0;
    SInt16 extLength;UInt8 reserved1[2];UInt16 chmap[8];
} __attribute__((packed));
static_assert(sizeof(PCMParams)==108,"IPC3 PCM ABI");

class RazerSSTMicrophone:public IOService {
    OSDeclareDefaultStructors(RazerSSTMicrophone)
    IOWorkLoop *loop_=nullptr;
    IOTimerEventSource *timer_=nullptr;
    IOCommandGate *gate_=nullptr;
    IONotifier *powerNotifier_=nullptr;
    RazerMicAudioDevice *audio_=nullptr;
    bool pmStarted_=false,asleep_=false,pinned_=false,gateAdded_=false,timerAdded_=false;
    UInt32 sessions_=0;
    UInt32 resumeTries_=0,pmWaits_=0;
    volatile SInt32 stopping_=0;
    IOPCIDevice *pci_=nullptr;
    IOMemoryMap *hmap_=nullptr,*dmap_=nullptr;
    volatile UInt8 *h_=nullptr,*d_=nullptr;
    UInt8 pm_=0,guardband_=0;
    bool attempted_=false,powered_=false,guardSaved_=false,sspSaved_=false,ipcHealthy_=false;
    volatile UInt32 armed_=0;
    bool fwReady_=false,pcmConfigured_=false,captureStarted_=false;
    UInt32 sspBefore_[3]={},adspBefore_=0,ipcBefore_=0,interruptBefore_=0;
    SST::Stream ownerBefore_[2]{};
    Channel loader_,icc_,capture_;
    SST::Mailboxes mailbox_;
    OSArray *steps_=nullptr;
    UInt32 ipcNumber_=0;
    static void entry(OSObject *,IOTimerEventSource *);
    bool allocateDMA(Channel &,UInt32,UInt32);
    bool setup();
    bool online();
    bool wait(volatile UInt8 *,UInt32,UInt32,UInt32,UInt32);
    SST::Stream stream(UInt32);
    void control(UInt32,UInt32);
    bool claim(Channel &,UInt32,UInt32,UInt32,UInt16,bool);
    bool reset(Channel &);
    bool stopDMA(Channel &);
    bool restore(Channel &);
    bool boot();
    bool topology();
    bool capture();
    bool bindPCI();
    bool initializePowerManagement();
    bool hardwareStart();
    void hardwareStop();
    bool position(UInt32 &);
    static IOReturn startAction(OSObject *,void *,void *,void *,void *);
    static IOReturn stopAction(OSObject *,void *,void *,void *,void *);
    static IOReturn positionAction(OSObject *,void *,void *,void *,void *);
    static IOReturn audioStoppingAction(OSObject *,void *,void *,void *,void *);
    static IOReturn powerAction(OSObject *,void *,void *,void *,void *);
    static IOReturn powerInterest(void *,void *,UInt32,IOService *,void *,vm_size_t);
    bool ipc(const void *,UInt32,const char *);
    void ack();
    void cleanup();
    bool fail(const char *s){setProperty("Failure",s);IOLog("RazerSSTMicrophone: %s\n",s);return false;}
    void phase(const char *s){setProperty("Phase",s);IOLog("RazerSSTMicrophone: %s\n",s);}
public:
    IOWorkLoop *getWorkLoop() const override {return loop_;}
    IOReturn beginCapture();
    IOReturn endCapture();
    bool readPosition(UInt32 &frame);
    IOBufferMemoryDescriptor *captureBuffer(){return capture_.data.memory;}
    void audioStopping(RazerMicAudioDevice *);
    IOReturn setPowerState(unsigned long,IOService *) override;
    unsigned long initialPowerStateForDomainState(IOPMPowerFlags) override {return 0;}
    bool start(IOService *) override;
    void stop(IOService *) override;
    IOReturn setProperties(OSObject *) override;
};
OSDefineMetaClassAndStructors(RazerSSTMicrophone,IOService)

bool RazerSSTMicrophone::start(IOService *p) {
    if(!IOService::start(p))return false;
    loop_=IOWorkLoop::workLoop();timer_=IOTimerEventSource::timerEventSource(this,entry);
    gate_=IOCommandGate::commandGate(this);steps_=OSArray::withCapacity(32);
    bool memory=allocateDMA(icc_,5,4096) && allocateDMA(loader_,8,524288) && allocateDMA(capture_,6,65536);
    if(memory){memcpy(loader_.data.bytes(),razer_sof_fw+768,524288);memory=loader_.data.command->synchronize(kIODirectionOut)==kIOReturnSuccess;}
    if(!loop_ || !timer_ || !gate_ || !steps_ || !memory || !bindPCI()){
        stop(p);return false;
    }
    gateAdded_=loop_->addEventSource(gate_)==kIOReturnSuccess;
    timerAdded_=loop_->addEventSource(timer_)==kIOReturnSuccess;
    if(!gateAdded_ || !timerAdded_){stop(p);return false;}
    setProperty("Version","0.3.2-ExactADL-CoreAudioInput");
    setProperty("PublishesAudioInput",false);setProperty("SavesAudio",false);
    setProperty("MicrophoneCaptureWorking",false);setProperty("HardwareWritesStarted",false);
    setProperty("CaptureActive",false);setProperty("PMRequestsOnly",true);
    phase("WaitingForRootPublication");registerService();return true;
}
bool RazerSSTMicrophone::initializePowerManagement() {
    PMinit();pmStarted_=true;
    static IOPMPowerState states[2]={
        {1,0,0,0,0,0,0,0,0,0,0,0},
        {1,IOPMDeviceUsable,0,IOPMPowerOn,0,0,0,0,0,0,0,0}
    };
    // IOAudioDevice below does not join this power plane: its software
    // publication must not pin the shared controller in D0 while idle.
    registerPowerDriver(this,states,2);pci_->joinPMtree(this);
    changePowerStateTo(0);changePowerStateToPriv(0);
    auto *root=getPMRootDomain();
    if(root)powerNotifier_=root->registerInterest(gIOGeneralInterest,powerInterest,this,nullptr);
    if(!powerNotifier_)return fail("Root PM notifications unavailable");
    return true;
}
IOReturn RazerSSTMicrophone::setProperties(OSObject *value) {
    if(IOUserClient::clientHasPrivilege(current_task(),kIOClientPrivilegeAdministrator)!=kIOReturnSuccess)return kIOReturnNotPrivileged;
    auto *dictionary=OSDynamicCast(OSDictionary,value);
    if(!dictionary || dictionary->getCount()!=1 || dictionary->getObject("PublishMicrophone")!=kOSBooleanTrue)return kIOReturnBadArgument;
    if(OSCompareAndSwap(0,1,&armed_)){timer_->setTimeoutMS(1);return kIOReturnSuccess;}
    return kIOReturnBusy;
}
void RazerSSTMicrophone::stop(IOService *p) {
    OSIncrementAtomic(&stopping_);
    if(powerNotifier_){powerNotifier_->remove();powerNotifier_=nullptr;}
    if(timer_){timer_->cancelTimeout();if(timerAdded_ && loop_)loop_->removeEventSource(timer_);timerAdded_=false;}
    if(audio_){auto *audio=audio_;audio_=nullptr;audio->terminate(kIOServiceSynchronous);audio->release();}
    if(gateAdded_ && gate_)gate_->runAction(stopAction);
    if(pmStarted_){PMstop();pmStarted_=false;}
    if(gateAdded_ && gate_ && loop_)loop_->removeEventSource(gate_);gateAdded_=false;
    Channel *channels[]={&capture_,&loader_,&icc_};
    for(Channel *c:channels)if(!c->claimed){c->bdl.release();c->data.release();}
    OSSafeReleaseNULL(timer_);OSSafeReleaseNULL(gate_);OSSafeReleaseNULL(loop_);
    OSSafeReleaseNULL(steps_);OSSafeReleaseNULL(pci_);IOService::stop(p);
}
void RazerSSTMicrophone::entry(OSObject *o,IOTimerEventSource *) {
    auto *self=OSDynamicCast(RazerSSTMicrophone,o);if(!self || OSAddAtomic(0,&self->stopping_))return;
    if(self->armed_ && !self->audio_){
        // Publication is armed only AFTER restoring the stable on-disk KC.
        // No power-plane changes occur in start() or during early boot.
        if(!self->pmStarted_){
            if(!self->initializePowerManagement())return;
            self->phase("WaitingForInitialPMIdle");self->timer_->setTimeoutMS(100);return;
        }
        if(!self->getProperty("PMActualState")){
            if(++self->pmWaits_<30)self->timer_->setTimeoutMS(100);
            else self->fail("Initial PM idle acknowledgement missing");
            return;
        }
        auto *device=new RazerMicAudioDevice;
        if(!device || !device->initWithOwner(self)){OSSafeReleaseNULL(device);self->fail("Audio device allocation failed");return;}
        if(!device->attach(self)){device->release();self->fail("Audio device attach failed");return;}
        if(!device->start(self)){device->detach(self);device->release();self->fail("Audio device start failed");return;}
        self->audio_=device;device->registerService();
        self->setProperty("PublishesAudioInput",true);self->phase("CoreAudioInputPublishedIdle");
    }
    if(self->audio_ && !self->asleep_ && self->resumeTries_){
        auto *engine=self->audio_->engine();
        if(engine && !engine->resumeIfNeeded() && --self->resumeTries_)self->timer_->setTimeoutMS(500);
        else self->resumeTries_=0;
    }
}
void RazerSSTMicrophone::audioStopping(RazerMicAudioDevice *device) {
    // Wait for in-flight gated resume readers, then clear their pointer
    // before IOAudioDevice::stop can deactivate/free the engine.
    if(gate_)gate_->runAction(audioStoppingAction,device);
}
IOReturn RazerSSTMicrophone::audioStoppingAction(OSObject *o,void *value,void *,void *,void *) {
    auto *self=OSDynamicCast(RazerSSTMicrophone,o);
    auto *device=static_cast<RazerMicAudioDevice *>(value);
    if(!self || !device)return kIOReturnBadArgument;
    device->clearEngine();
    if(device==self->audio_){self->setProperty("PublishesAudioInput",false);self->resumeTries_=0;}
    return kIOReturnSuccess;
}
IOReturn RazerSSTMicrophone::startAction(OSObject *o,void *,void *,void *,void *) {
    auto *self=OSDynamicCast(RazerSSTMicrophone,o);
    return self && self->hardwareStart()?kIOReturnSuccess:kIOReturnNotReady;
}
IOReturn RazerSSTMicrophone::stopAction(OSObject *o,void *,void *,void *,void *) {
    auto *self=OSDynamicCast(RazerSSTMicrophone,o);if(self)self->hardwareStop();return kIOReturnSuccess;
}
IOReturn RazerSSTMicrophone::positionAction(OSObject *o,void *p,void *,void *,void *) {
    auto *self=OSDynamicCast(RazerSSTMicrophone,o);
    return self && p && self->position(*static_cast<UInt32 *>(p))?kIOReturnSuccess:kIOReturnNotReady;
}
IOReturn RazerSSTMicrophone::beginCapture() {
    if(!gate_ || !pmStarted_ || OSAddAtomic(0,&stopping_))return kIOReturnNotReady;
    changePowerStateToPriv(1);
    IOReturn result=gate_->runAction(startAction);
    if(result!=kIOReturnSuccess)changePowerStateToPriv(0);
    return result;
}
IOReturn RazerSSTMicrophone::endCapture() {
    IOReturn result=gate_?gate_->runAction(stopAction):kIOReturnNotReady;
    if(pmStarted_)changePowerStateToPriv(0);return result;
}
bool RazerSSTMicrophone::readPosition(UInt32 &frame) {
    return gate_ && gate_->runAction(positionAction,&frame)==kIOReturnSuccess;
}
IOReturn RazerSSTMicrophone::setPowerState(unsigned long state,IOService *) {
    // D0 requests may complete while beginCapture is waiting under our gate;
    // the power-up acknowledgement does not need MMIO or that gate.
    if(state==0 && gate_)gate_->runAction(stopAction);
    setProperty("PMActualState",state,32);return IOPMAckImplied;
}
IOReturn RazerSSTMicrophone::powerInterest(void *target,void *,UInt32 type,IOService *,void *,vm_size_t) {
    auto *self=static_cast<RazerSSTMicrophone *>(target);
    if(self && self->gate_ && !OSAddAtomic(0,&self->stopping_))self->gate_->runAction(powerAction,reinterpret_cast<void *>(uintptr_t(type)));
    return kIOReturnSuccess;
}
IOReturn RazerSSTMicrophone::powerAction(OSObject *o,void *value,void *,void *,void *) {
    auto *self=OSDynamicCast(RazerSSTMicrophone,o);if(!self)return kIOReturnBadArgument;
    UInt32 type=UInt32(uintptr_t(value));
    if(type==kIOMessageSystemWillSleep || type==kIOMessageSystemWillPowerOff || type==kIOMessageSystemWillRestart){
        self->asleep_=true;self->resumeTries_=0;
        if(self->timer_)self->timer_->cancelTimeout();
        if(self->audio_ && self->audio_->engine())self->audio_->engine()->prepareForSleep();
        self->hardwareStop();if(self->pmStarted_)self->changePowerStateToPriv(0);
    }else if(type==kIOMessageSystemHasPoweredOn){
        self->asleep_=false;self->resumeTries_=6;if(self->timer_)self->timer_->setTimeoutMS(500);
    }
    return kIOReturnSuccess;
}

bool RazerSSTMicrophone::online() {
    return pci_ && pm_ && (pci_->configRead16(pm_+4)&3)==0 &&
        (pci_->configRead16(4)&6)==6 && h_ && r16(h_,0)==0x9701 && (r32(h_,8)&1);
}
bool RazerSSTMicrophone::wait(volatile UInt8 *base,UInt32 off,UInt32 mask,UInt32 value,UInt32 ms) {
    for(UInt32 i=0;i<ms;i++) {
        if(!online())return false;
        UInt32 v=r32(base,off);
        if(v==0xffffffffu)return false;
        if((v&mask)==value)return true;
        IOSleep(1);
    }
    return false;
}
SST::Stream RazerSSTMicrophone::stream(UInt32 index) {
    UInt32 s=0x80+index*32;
    return {r32(h_,s)&0xffffff,r32(h_,s+8),r32(h_,s+24),r32(h_,s+28),r16(h_,s+12),r16(h_,s+18)};
}
void RazerSSTMicrophone::control(UInt32 index,UInt32 v) {
    if(!SST::ownedIndex(index))return;
    UInt32 s=0x80+index*32;
    // CTL is 24-bit. A dword write would accidentally acknowledge SDSTS.
    w8(h_,s,UInt8(v));w8(h_,s+1,UInt8(v>>8));w8(h_,s+2,UInt8(v>>16));
}
bool RazerSSTMicrophone::bindPCI() {
    auto *matching=serviceMatching("IOPCIDevice");auto *it=matching?getMatchingServices(matching):nullptr;
    OSSafeReleaseNULL(matching);
    if(it){while(OSObject *o=it->getNextObject()) {
        auto *p=OSDynamicCast(IOPCIDevice,o);
        if(p && p->configRead16(0)==0x8086 && p->configRead16(2)==0x51c8 &&
            p->configRead16(0x2c)==0x1a58 && p->configRead16(0x2e)==0x3005 &&
            p->getBusNumber()==0 && p->getDeviceNumber()==31 && p->getFunctionNumber()==3){pci_=p;p->retain();break;}
    }it->release();}
    if(!pci_)return fail("Exact PCI device absent");
    bool visited[256]={};UInt8 off=pci_->configRead8(0x34)&0xfc;
    for(unsigned i=0;i<48 && off>=0x40;i++) {
        if(visited[off])break;visited[off]=true;
        if(pci_->configRead8(off)==1 && off<=0xf8){pm_=off;break;}
        off=pci_->configRead8(off+1)&0xfc;
    }
    if(!pm_)return fail("No valid PCI power capability");
    return true;
}
bool RazerSSTMicrophone::setup() {
    if(!pci_ || !pm_)return fail("PCI service missing");
    // Only the normal IOKit PM parent may bring the shared function into D0.
    // Unlike the probe, real capture must work without active speaker output.
    for(unsigned i=0;i<200;i++) {
        if(OSAddAtomic(0,&stopping_) || asleep_)return false;
        if(getPowerState()==1 && (pci_->configRead16(pm_+4)&3)==0 && (pci_->configRead16(4)&6)==6)break;
        IOSleep(10);
    }
    if(getPowerState()!=1 || (pci_->configRead16(pm_+4)&3)!=0)return fail("PM parent did not complete D0 transition");
    hmap_=pci_->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0);
    dmap_=pci_->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress4);
    if(!hmap_ || !dmap_ || hmap_->getLength()!=0x4000 || dmap_->getLength()!=0x100000)return fail("Unexpected BAR size");
    h_=reinterpret_cast<volatile UInt8 *>(hmap_->getVirtualAddress());d_=reinterpret_cast<volatile UInt8 *>(dmap_->getVirtualAddress());
    for(unsigned i=0;i<50 && !online();i++)IOSleep(10);
    if(!online())return fail("PM parent did not restore HDA decoding");
    setProperty("PMStateAtCaptureStart",getPowerState(),32);
    if(((r32(h_,0x800)>>16)&0xfff)!=3 || ((r32(h_,0x700)>>16)&0xfff)!=4 || !(r32(h_,PP)&(1u<<30)))return fail("Unexpected PP/SPIB capabilities");
    adspBefore_=r32(d_,4);ipcBefore_=r32(d_,0xe8);interruptBefore_=r32(d_,8);
    setProperty("HDAVSEM2Before",r32(h_,0x1030),32);setProperty("HDAINTCTLBefore",r32(h_,0x20),32);
    setProperty("PCIPMCSRBefore",pci_->configRead16(pm_+4),16);setProperty("DSPADSPCSBefore",adspBefore_,32);
    if(adspBefore_!=0x00000f0f || ipcBefore_ || interruptBefore_ || r32(d_,0xd0) || r32(d_,0xd4) || r32(d_,0xc0) || r32(d_,0xc4))return fail("DSP is not idle; refusing to take ownership");
    ownerBefore_[0]=stream(0);ownerBefore_[1]=stream(7);
    setProperty("AppleHDAOutputActiveAtCaptureStart",(ownerBefore_[1].ctl&2)!=0);
    // Two samples, including the tag and interrupt bits, immediately before
    // claiming. Only SD5/SD6/SD8 can ever pass control()'s index allowlist.
    const unsigned indices[]={5,6,8};
    for(unsigned i:indices) {
        SST::Stream a=stream(i);IOSleep(2);SST::Stream b=stream(i);
        if(!SST::equal(a,b) || !SST::unused(b) || (r32(h_,PP)&(1u<<i)) || (r32(h_,SPIB)&(1u<<i)) || (r32(h_,0x20)&(1u<<i)))return fail("An assigned free stream is now owned");
    }
    return true;
}
bool RazerSSTMicrophone::reset(Channel &c) {
    if(!c.claimed || !online())return false;
    control(c.index,(r32(h_,c.offset)&0xffffff)&~0x1eu);
    if(!wait(h_,c.offset,2,0,50))return false;
    control(c.index,(r32(h_,c.offset)&0xffffff)|1);
    if(!wait(h_,c.offset,1,1,50))return false;
    control(c.index,(r32(h_,c.offset)&0xffffff)&~1u);
    if(!wait(h_,c.offset,1,0,50))return false;
    w8(h_,c.offset+3,0x1c);return true;
}
bool RazerSSTMicrophone::allocateDMA(Channel &c,UInt32 index,UInt32 bytes) {
    c.index=index;c.offset=0x80+index*32;
    if(!SST::ownedIndex(index) || !c.data.allocate(bytes,4096,index==6?32:64) || !c.bdl.allocate(4096))return fail("DMA allocation/mapping refused");
    UInt32 count=0,total=0;
    if(!SST::append(static_cast<SST::BDL *>(c.bdl.bytes()),256,count,c.data.bus,bytes,total) || total!=bytes)return fail("DMA BDL coverage invalid");
    c.entries=count;
    if(c.data.command->synchronize(kIODirectionOut)!=kIOReturnSuccess || c.bdl.command->synchronize(kIODirectionOut)!=kIOReturnSuccess)return fail("DMA synchronization failed");
    return true;
}
bool RazerSSTMicrophone::claim(Channel &c,UInt32 index,UInt32 tag,UInt32 bytes,UInt16 format,bool spib) {
    if(c.index!=index || !c.data.memory || !c.bdl.memory || c.data.size!=bytes)return fail("Preallocated DMA mismatch");
    c.tag=tag;
    if(!online())return fail("PCI left D0 before stream claim");
    auto a=stream(index);IODelay(50);auto b=stream(index);
    if(!SST::equal(a,b) || !SST::unused(b) || (r32(h_,PP)&(1u<<index)) || (r32(h_,SPIB)&(1u<<index)) || (r32(h_,0x20)&(1u<<index)))return fail("Stream became owned before claim");
    c.before=b;c.ppBefore=false;c.spibBefore=false;c.spibValue=r32(h_,0x708+index*8);c.claimed=true;
    setProperty("HardwareWritesStarted",true);
    bits(h_,PP,1u<<index,1u<<index);
    if(!reset(c))return fail("Owned stream reset failed");
    w32(h_,c.offset+24,UInt32(c.bdl.bus));w32(h_,c.offset+28,UInt32(c.bdl.bus>>32));
    w32(h_,c.offset+8,bytes);w16(h_,c.offset+12,UInt16(c.entries-1));
    w16(h_,c.offset+18,format);
    control(index,((r32(h_,c.offset)&0x0fffff)|(tag<<20))&~0x1fu);
    if(spib){w32(h_,0x708+index*8,bytes);bits(h_,SPIB,1u<<index,1u<<index);}
    return true;
}
bool RazerSSTMicrophone::stopDMA(Channel &c) {
    if(!c.claimed)return true;
    if(!online())return false;
    control(c.index,(r32(h_,c.offset)&0xffffff)&~0x1eu);
    if(!wait(h_,c.offset,2,0,50))return false;
    c.dmaMayRun=false;w8(h_,c.offset+3,0x1c);return true;
}
bool RazerSSTMicrophone::restore(Channel &c) {
    if(!c.claimed)return true;
    if(!stopDMA(c) || !reset(c)) {
        // Hardware may still dereference the BDL. Intentionally retain BOTH
        // descriptors and their prepared mappings until reboot, never UAF.
        setProperty("DMAMemoryRetainedForSafety",true);return false;
    }
    w32(h_,c.offset+24,c.before.lo);w32(h_,c.offset+28,c.before.hi);
    w32(h_,c.offset+8,c.before.cbl);w16(h_,c.offset+12,c.before.lvi);w16(h_,c.offset+18,c.before.fmt);
    w32(h_,0x708+c.index*8,c.spibValue);
    bits(h_,SPIB,1u<<c.index,c.spibBefore?(1u<<c.index):0);
    bits(h_,PP,1u<<c.index,c.ppBefore?(1u<<c.index):0);
    control(c.index,c.before.ctl);
    bool same=SST::equal(c.before,stream(c.index));
    c.claimed=false;return same;
}
void RazerSSTMicrophone::ack() {
    if(r32(d_,0xc0)&BUSY){w32(d_,0xc0,r32(d_,0xc0)|BUSY);w32(d_,0xc4,BUSY);}
}
bool RazerSSTMicrophone::boot() {
    phase("PreparingFreeDMAChannels");
    if(!claim(icc_,5,0,4096,0,false))return false;
    guardband_=r8(h_,0x1048)&0x3f;guardSaved_=true;
    w8(h_,0x1048,(r8(h_,0x1048)&~0x3f)|(95&0x3f));
    icc_.dmaMayRun=true;control(icc_.index,(r32(h_,icc_.offset)&0xffffff)|2);
    if(!wait(h_,icc_.offset,2,2,50))return fail("ICCMAX stream did not start");
    if(!claim(loader_,8,SST::outputTag(8,7,9),524288,0x40,true))return false;
    phase("PoweringOnlyHostManagedCore0");
    powered_=true;bits(d_,4,0x10000,0x10000);
    if(!wait(d_,4,0x01000000,0x01000000,50))return fail("Core0 power-up timeout");
    for(unsigned i=0;i<3;i++){sspBefore_[i]=r32(d_,0x10004+i*0x1000);if(sspBefore_[i]==0xffffffffu)return fail("SSP does not decode after core power-up");}
    sspSaved_=true;
    for(unsigned i=0;i<3;i++)bits(d_,0x10004+i*0x1000,0x03000000,0x03000000);
    phase("ROMHandshake");w32(d_,0xd0,BUSY|0x01000000|0x4000|((loader_.tag-1)<<9));
    bits(d_,4,1,0);if(!wait(d_,4,1,0,50))return fail("Core0 reset release failed");
    bits(d_,4,0x100,0);
    if(!wait(d_,0xd4,BUSY,BUSY,500))return fail("ROM did not acknowledge IPC");
    w32(d_,0xd4,r32(d_,0xd4)|BUSY);
    if(!wait(d_,0x80000,0xffffff,1,300))return fail("ROM INIT_DONE timeout");
    setProperty("ROMInitialized",true);phase("DownloadingIntelSignedADLFirmware");
    loader_.dmaMayRun=true;control(loader_.index,(r32(h_,loader_.offset)&0xffffff)|2);
    bool entered=wait(d_,0x80000,0xffffff,5,3000);
    setProperty("FirmwareEntered",entered);
    bool loaderRestored=restore(loader_),iccRestored=restore(icc_);
    if(online() && guardSaved_){w8(h_,0x1048,(r8(h_,0x1048)&~0x3f)|guardband_);guardSaved_=false;}
    if(!entered || !loaderRestored || !iccRestored)return fail("Firmware download or DMA cleanup failed");
    phase("WaitingForFirmwareReady");
    if(!wait(d_,0xc0,BUSY,BUSY,2000))return fail("No FW_READY notification");
    UInt8 record[4096];for(UInt32 i=0;i<sizeof(record);i+=4){UInt32 v=r32(d_,0x81000+i);memcpy(record+i,&v,4);}
    auto *raw=OSData::withBytes(record,256);if(raw){setProperty("FWReadyRecord",raw);raw->release();}
    if(!SST::ready(record,sizeof(record),UInt32(dmap_->getLength()),mailbox_,razer_sof_fw+24,400))return fail("Unexpected FW_READY identity or mailbox bounds");
    setProperty("FirmwareABI",mailbox_.abi,32);setProperty("FirmwareSourceHash",mailbox_.sourceHash,32);
    setProperty("HostMailbox",mailbox_.down,32);setProperty("DSPMailbox",mailbox_.up,32);
    setProperty("MailboxFromFirmwareManifest",mailbox_.fromManifest);
    setProperty("FirmwareVersion","2.2.0 / IPC3 ABI3.22.1 / tag57864");
    ack();fwReady_=true;ipcHealthy_=true;setProperty("FirmwareReady",true);return true;
}
bool RazerSSTMicrophone::ipc(const void *msg,UInt32 size,const char *name) {
    auto *s=static_cast<const UInt8 *>(msg);
    if(!ipcHealthy_ || !online() || size<8 || size>mailbox_.downSize || size%4 || SST::le32(s)!=size)return false;
    if(!wait(d_,0xd0,BUSY,0,500)){ipcHealthy_=false;return fail("Previous host IPC still busy");}
    // A pending notification must be acknowledged before issuing another IPC.
    ack();if(r32(d_,0xd4)&BUSY)w32(d_,0xd4,r32(d_,0xd4)|BUSY);
    for(UInt32 i=0;i<size;i+=4)w32(d_,mailbox_.down+i,SST::le32(s+i));
    w32(d_,0xd0,BUSY);
    bool done=wait(d_,0xd4,BUSY,BUSY,500);
    UInt32 replySize=0,replyCommand=0;SInt32 error=0x7fffffff;
    if(done){replySize=r32(d_,mailbox_.down);replyCommand=r32(d_,mailbox_.down+4);error=static_cast<SInt32>(r32(d_,mailbox_.down+8));w32(d_,0xd4,r32(d_,0xd4)|BUSY);}
    auto *row=OSDictionary::withCapacity(7);
    if(row){auto *label=OSString::withCString(name);if(label){row->setObject("Step",label);label->release();}number(row,"Number",++ipcNumber_);number(row,"Command",SST::le32(s+4));number(row,"ReplySize",replySize);number(row,"ReplyCommand",replyCommand);number(row,"Error",UInt32(error));row->setObject("Done",done?kOSBooleanTrue:kOSBooleanFalse);steps_->setObject(row);row->release();auto *copy=OSArray::withArray(steps_);if(copy){setProperty("IPCSteps",copy);copy->release();}}
    if(!done){ipcHealthy_=false;return fail("Runtime IPC timeout");}
    if(!SST::reply(SST::le32(s+4),replyCommand,replySize,mailbox_.downSize))return fail("Malformed runtime IPC reply");
    if(error)return fail(name);
    return true;
}
bool RazerSSTMicrophone::topology() {
    phase("ConfiguringPDM1MicrophonePipeline");
    UInt32 scheduler[]={48,0x30100000,30,3,35,0,1000,0,5000,0,0,1};
    UInt32 buffer[]={44,0x30200000,31,12,3,0,0,9600,0x71,0,0};
    UInt32 host[19]={};host[0]=76;host[1]=0x30010000;host[2]=35;host[3]=1;host[4]=3;host[7]=36;host[9]=2;host[10]=2;host[12]=2;host[16]=1;
    UInt32 dai[20]={};dai[0]=80;dai[1]=0x30010000;dai[2]=34;dai[3]=2;dai[4]=3;dai[7]=36;dai[10]=2;dai[12]=2;dai[16]=1;dai[18]=2;
    UInt32 conn1[]={16,0x30030000,34,31},conn2[]={16,0x30030000,31,35},complete[]={12,0x30130000,30};
    if(!ipc(scheduler,sizeof(scheduler),"PipelineCreate") || !ipc(buffer,sizeof(buffer),"BufferCreate") ||
       !ipc(host,sizeof(host),"HostCreate") || !ipc(dai,sizeof(dai),"DMICCreate") ||
       !ipc(razer_dmic_dai,216,"DAIConfigPDM1") || !ipc(conn1,16,"ConnectDMICBuffer") ||
       !ipc(conn2,16,"ConnectBufferHost") || !ipc(complete,12,"PipelineComplete"))return false;
    setProperty("MicrophonePipelineConfigured",true);return true;
}
bool RazerSSTMicrophone::capture() {
    phase("StartingCoreAudioCapture");
    if(!claim(capture_,6,7,65536,0x41,false))return false;
    PCMParams p{};p.size=sizeof(p);p.cmd=0x60010000;p.id=35;p.paramsSize=84;
    p.buffer.address=UInt32(capture_.data.bus);p.buffer.pages=16;p.buffer.bytes=65536;
    p.direction=1;p.frameFormat=2;p.rate=48000;p.tag=7;p.channels=2;p.validBytes=4;p.containerBytes=4;p.periodBytes=16384;p.noPosition=1;
    for(unsigned i=0;i<8;i++)p.chmap[i]=i<2?UInt16(i):0xffff;
    if(!ipc(&p,sizeof(p),"CapturePCMParams"))return false;pcmConfigured_=true;
    capture_.dmaMayRun=true;control(6,(r32(h_,capture_.offset)&0xffffff)|2);
    if(!wait(h_,capture_.offset,2,2,50))return fail("Capture DMA did not start");
    UInt32 trigger[]={12,0x60040000,35};
    if(!ipc(trigger,12,"CaptureTriggerStart"))return false;captureStarted_=true;
    setProperty("CaptureActive",true);return true;
}
bool RazerSSTMicrophone::position(UInt32 &frame) {
    if(!captureStarted_ || !online() || !(r32(h_,capture_.offset)&2))return false;
    UInt32 pos=r32(h_,0x1084+6*32);
    if(pos==0xffffffffu || pos>=65536 || (r8(h_,capture_.offset+3)&0x18))return false;
    frame=(pos/8)%8192;return true;
}
bool RazerSSTMicrophone::hardwareStart() {
    if(captureStarted_)return true;
    if(asleep_ || pinned_ || OSAddAtomic(0,&stopping_))return false;
    removeProperty("Failure");steps_->flushCollection();ipcNumber_=0;
    fwReady_=pcmConfigured_=captureStarted_=ipcHealthy_=powered_=guardSaved_=sspSaved_=false;
    bzero(capture_.data.bytes(),65536);
    // synchronize accepts exactly one copy direction, never InOut. The
    // CPU has just cleared the ring, so publish those zeros towards DMA.
    IOReturn sync=capture_.data.command->synchronize(kIODirectionOut);
    setProperty("CaptureRingSyncStatus",UInt32(sync),32);
    if(sync!=kIOReturnSuccess)return fail("Capture ring sync failed");
    bool ok=setup();
    if(ok){attempted_=true;ok=boot() && topology() && capture();}
    if(!ok){hardwareStop();return false;}
    setProperty("CaptureSessions",++sessions_,32);setProperty("DormantAfterStop",false);
    phase("CoreAudioCaptureRunning");return true;
}
void RazerSSTMicrophone::hardwareStop() {
    if(hmap_ || dmap_ || attempted_)cleanup();
    captureStarted_=pcmConfigured_=fwReady_=ipcHealthy_=powered_=false;attempted_=false;
    setProperty("CaptureActive",false);
}
void RazerSSTMicrophone::cleanup() {
    if(attempted_ && online()) {
        if(fwReady_ && ipcHealthy_) {
            if(captureStarted_){UInt32 stop[]={12,0x60050000,35};ipc(stop,12,"CaptureTriggerStop");}
            if(pcmConfigured_){UInt32 freePCM[]={12,0x60030000,35};ipc(freePCM,12,"CapturePCMFree");}
        }
        // Before releasing any BDL, stop its host DMA. Core0 reset only;
        // mask1 is the ADL host-managed mask, never the donor's 0xf.
        bool capStopped=stopDMA(capture_),loaderStopped=stopDMA(loader_),iccStopped=stopDMA(icc_);
        bool stopped=capStopped && loaderStopped && iccStopped;
        setProperty("OwnedDMAsStopped",stopped);
        if(powered_) {
            bits(d_,4,0x100,0x100);bits(d_,4,1,1);wait(d_,4,1,1,50);
            if(sspSaved_)for(unsigned i=0;i<3;i++)bits(d_,0x10004+i*0x1000,0x03000000,sspBefore_[i]);
            bits(d_,4,0x10000,0);setProperty("Core0PoweredDown",wait(d_,4,0x01000000,0,50));
        }
        if(guardSaved_){w8(h_,0x1048,(r8(h_,0x1048)&~0x3f)|guardband_);guardSaved_=false;}
        bool capRestored=restore(capture_),loadRestored=restore(loader_),iccRestored=restore(icc_);
        setProperty("OwnedStreamsRestored",capRestored && loadRestored && iccRestored);
        setProperty("AppleHDAInputDescriptorUnchanged",SST::equal(ownerBefore_[0],stream(0)));
        setProperty("AppleHDAOutputDescriptorUnchanged",SST::equal(ownerBefore_[1],stream(7)));
        setProperty("DSPADSPCSAfter",r32(d_,4),32);setProperty("PPControlAfter",r32(h_,PP),32);
        setProperty("InterruptsStillMasked",r32(d_,8)==interruptBefore_ && r32(d_,0xe8)==ipcBefore_);
    } else {
        // On D3/cancellation, no MMIO is safe. Unclaimed allocations can be
        // released; claimed DMA memory must stay pinned until reboot.
        Channel *channels[]={&capture_,&loader_,&icc_};
        for(Channel *c:channels)if(c->claimed)setProperty("DMAMemoryRetainedForSafety",true);
    }
    OSSafeReleaseNULL(hmap_);OSSafeReleaseNULL(dmap_);h_=nullptr;d_=nullptr;
    pinned_=capture_.claimed || loader_.claimed || icc_.claimed;
    setProperty("DormantAfterStop",true);phase("CoreAudioInputIdle");
}

#include "RazerMicAudio.inc"
