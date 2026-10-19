// SPDX-License-Identifier: BSD-3-Clause
// Razer ADL probe's bounds/ownership rules, shared by kernel and host tests.
#pragma once
#include <stdint.h>
#include <stddef.h>
namespace SST {
inline uint16_t le16(const uint8_t *p) { return uint16_t(p[0] | (uint16_t(p[1]) << 8)); }
inline uint32_t le32(const uint8_t *p) { return le16(p) | (uint32_t(le16(p+2)) << 16); }
struct Stream {
    uint32_t ctl, cbl, lo, hi;
    uint16_t lvi, fmt;
};
inline bool equal(const Stream &a,const Stream &b) {
    return a.ctl==b.ctl && a.cbl==b.cbl && a.lo==b.lo && a.hi==b.hi && a.lvi==b.lvi && a.fmt==b.fmt;
}
inline bool unused(const Stream &s) {
    return !(s.ctl & 0xf0001f) && !s.cbl && !s.lo && !s.hi && !s.lvi && !s.fmt;
}
inline uint32_t outputTag(unsigned index,unsigned inputs,unsigned outputs) {
    return index>=inputs && index<inputs+outputs ? index-inputs+1 : 0;
}
inline bool ownedIndex(unsigned i) { return i==5 || i==6 || i==8; }
inline bool range(uint32_t off,uint32_t size,uint32_t bar) {
    return off>=0x80000 && size && off<=bar && size<=bar-off;
}
struct Mailboxes { uint32_t up=0,upSize=0,down=0,downSize=0,abi=0,sourceHash=0; bool fromManifest=false; };
inline bool window(const uint8_t *p,size_t bytes,uint32_t bar,Mailboxes &m) {
    if(bytes<16) return false;
    uint32_t size=le32(p),count=le32(p+12);
    if(le32(p+4)!=0x70000000 || le32(p+8)!=1 || !count || count>16 || size>bytes || size<16+count*24 || size>400) return false;
    for(unsigned i=0;i<count;i++) {
        const uint8_t *w=p+16+i*24;
        uint32_t kind=le32(w+4),id=le32(w+8),len=le32(w+16),rel=le32(w+20);
        if((le32(w)!=0 && le32(w)!=24) || id>3 || rel>=0x20000 || len>0x20000-rel) return false;
        uint32_t addr=0x80000+id*0x20000+rel;
        if(!range(addr,len,bar)) return false;
        if(kind==0) { if(m.down) return false;m.down=addr;m.downSize=len; }
        if(kind==1) { if(m.up) return false;m.up=addr;m.upSize=len; }
    }
    return true;
}
// Bootstrap FW_READY is at the Linux TGL mailbox location 0x81000. Runtime
// mailbox addresses MUST be advertised by firmware; no CML fallback is used.
inline bool ready(const uint8_t *p,size_t bytes,uint32_t bar,Mailboxes &out,const uint8_t *manifest,size_t manifestBytes) {
    if(bytes<108 || le32(p)!=108 || le32(p+4)!=0x70000000) return false;
    if(le16(p+28)!=2 || le16(p+30)!=2 || le16(p+32)!=0) return false;
    Mailboxes m; m.abi=le32(p+64);m.sourceHash=le32(p+68);
    if(m.abi!=0x03016001 || m.sourceHash!=0x0f80a51b) return false; // signed ADL IPC3 3.22.1
    bool windows=false;
    for(size_t off=108,n=0;off+12<=bytes && n<16;n++) {
        uint32_t size=le32(p+off),cmd=le32(p+off+4),type=le32(p+off+8);
        if(cmd!=0x70000000) break; // no legacy runtime extension; use firmware XMan
        if(size<12 || size>bytes-off || (size&3) || cmd!=0x70000000) return false;
        if(type==1) {
            if(windows || !window(p+off,size,bar,m)) return false;
            windows=true;
        }
        off+=size;
    }
    if(!windows) {
        if(!manifest || !window(manifest,manifestBytes,bar,m)) return false;
        m.fromManifest=true;
    }
    if(!m.up || !m.down || m.downSize<216 || m.upSize<108 || (m.down&3) || (m.up&3)) return false;
    if(m.up<m.down+m.downSize && m.down<m.up+m.upSize) return false;
    out=m;return true;
}
struct BDL { uint32_t lo,hi,length,ioc; } __attribute__((packed));
inline bool reply(uint32_t request,uint32_t response,uint32_t size,uint32_t capacity) {
    if(size<12 || size>capacity || (size&3))return false;
    // SOF ipc3/handler.c: generic acknowledgements use GLB_REPLY; successful
    // component/buffer/pipeline creation and PCM_PARAMS echo the request.
    if(response==0x10000000)return true;
    bool echoes=request==0x30100000 || request==0x30200000 || request==0x30010000 || request==0x60010000;
    return echoes && response==request && size>=16;
}
inline bool append(BDL *bdl,unsigned capacity,unsigned &count,uint64_t address,uint64_t length,uint32_t &total) {
    if(!address || !length || (address&127) || (length&127) || address+length<address || length>0xffffffffu-total) return false;
    // Split long mapped segments at a page boundary. Never silently truncate.
    while(length) {
        if(count>=capacity) return false;
        uint32_t chunk=uint32_t(4096-(address&4095));if(length<chunk)chunk=uint32_t(length);
        bdl[count++]={uint32_t(address),uint32_t(address>>32),chunk,0};
        total+=chunk;address+=chunk;length-=chunk;
    }
    return true;
}
}
