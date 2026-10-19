#include "SSTProtocol.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
#include <fstream>
#include <iterator>
#include <cstring>

static void put(std::vector<uint8_t> &b,size_t off,uint32_t v) {for(unsigned i=0;i<4;i++)b.at(off+i)=uint8_t(v>>(i*8));}
int main(int argc,char **argv) {
    assert(argc==2);
    std::ifstream file(argv[1],std::ios::binary);
    std::vector<uint8_t> fw((std::istreambuf_iterator<char>(file)),{});
    assert(fw.size()==525056 && SST::le32(fw.data())==0x6e614d58 && SST::le32(fw.data()+4)==768);
    const uint8_t signature[]={108,0,0,0,0,0,0,0x70};
    size_t found=0;
    for(size_t i=768;i+108<=fw.size();i++)if(!std::memcmp(fw.data()+i,signature,8)){assert(!found);found=i;}
    assert(found);
    std::vector<uint8_t> ready(fw.begin()+found,fw.begin()+found+108);
    SST::Mailboxes mb;
    assert(SST::ready(ready.data(),ready.size(),0x100000,mb,fw.data()+24,400));
    assert(mb.fromManifest && mb.up==0x81000 && mb.down==0xa0000 && mb.upSize==4096 && mb.downSize==8192);
    for(size_t n=0;n<108;n++)assert(!SST::ready(ready.data(),n,0x100000,mb,fw.data()+24,400));
    for(size_t n=0;n<400;n++)assert(!SST::ready(ready.data(),108,0x100000,mb,fw.data()+24,n));
    for(size_t off:{0u,4u,28u,30u,32u,64u,68u}) {
        auto bad=ready;bad[off]^=1;assert(!SST::ready(bad.data(),bad.size(),0x100000,mb,fw.data()+24,400));
    }
    auto extended=ready;extended.insert(extended.end(),fw.begin()+24,fw.begin()+424);
    assert(SST::ready(extended.data(),extended.size(),0x100000,mb,nullptr,0) && !mb.fromManifest);
    auto bad=extended;put(bad,108+12,17);assert(!SST::ready(bad.data(),bad.size(),0x100000,mb,nullptr,0));
    bad=extended;put(bad,108+16+2*24+8,0);put(bad,108+16+2*24+20,0x1000);assert(!SST::ready(bad.data(),bad.size(),0x100000,mb,nullptr,0));
    bad=extended;put(bad,108+16+24+8,4);assert(!SST::ready(bad.data(),bad.size(),0x100000,mb,nullptr,0));
    assert(!SST::ready(ready.data(),ready.size(),0x90000,mb,fw.data()+24,400));
    SST::Stream empty={0x040000,0,0,0,0,0};assert(SST::unused(empty));
    SST::Stream changed=empty;
    for(unsigned bit=0;bit<24;bit++)if((1u<<bit)&0xf0001f) {changed=empty;changed.ctl|=1u<<bit;assert(!SST::unused(changed));}
    changed=empty;changed.lo=0x1000;assert(!SST::unused(changed));
    changed=empty;changed.cbl=4096;assert(!SST::unused(changed));
    changed=empty;changed.fmt=0x41;assert(!SST::unused(changed));
    for(unsigned index=0;index<16;index++) {
        assert(SST::outputTag(index,7,9)==(index>=7?index-6:0));
        assert(SST::ownedIndex(index)==(index==5 || index==6 || index==8));
    }
    SST::BDL entries[256]{};unsigned count=0;uint32_t total=0;
    assert(SST::append(entries,256,count,0x123450000ULL,524288,total));
    assert(count==128 && total==524288);
    for(unsigned i=0;i<count;i++){uint64_t addr=entries[i].lo|(uint64_t(entries[i].hi)<<32);assert(addr==0x123450000ULL+i*4096 && entries[i].length==4096 && entries[i].ioc==0);}
    count=0;total=0;assert(!SST::append(entries,127,count,0x123450000ULL,524288,total));
    count=0;total=0;assert(!SST::append(entries,256,count,0x1001,4096,total));
    count=0;total=0;assert(!SST::append(entries,256,count,0x1000,4095,total));
    count=0;total=0;assert(!SST::append(entries,256,count,0xfffffffffffff000ULL,4096,total));
    for(uint32_t request:{0x30100000u,0x30200000u,0x30010000u,0x60010000u}) {
        assert(SST::reply(request,0x10000000,12,8192));
        assert(SST::reply(request,request,20,8192));
        assert(!SST::reply(request,request,12,8192));
        assert(!SST::reply(request,request^0x10000,20,8192));
    }
    assert(SST::reply(0x80010000,0x10000000,12,8192));
    assert(!SST::reply(0x80010000,0x80010000,20,8192));
    assert(!SST::reply(0x60010000,0x10000000,8196,8192));
    puts("PASS: actual signed ADL FW_READY/XMan windows; all truncated lengths; ABI/source/hash/overlap rejection; SD0/SD7 ownership exclusion; mapped BDL complete coverage/no truncation");
}
