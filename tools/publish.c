// SPDX-License-Identifier: BSD-3-Clause
// Root-only publication of an already loaded service. Integrators must stage
// the driver and provide their own reviewed startup delay; this is not an installer.
// No raw MMIO user client is provided.
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <unistd.h>
#include <stdio.h>
int main(void) {
    if(geteuid()!=0){fputs("Root is required.\n",stderr);return 1;}
    io_service_t service=IOServiceGetMatchingService(kIOMainPortDefault,IOServiceMatching("RazerSSTMicrophone"));
    if(!service){fputs("Probe service absent.\n",stderr);return 2;}
    const void *keys[]={CFSTR("PublishMicrophone")},*values[]={kCFBooleanTrue};
    CFDictionaryRef dictionary=CFDictionaryCreate(kCFAllocatorDefault,keys,values,1,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    kern_return_t result=IORegistryEntrySetCFProperties(service,dictionary);
    CFRelease(dictionary);IOObjectRelease(service);
    printf("Root microphone publication: 0x%08x\n",result);
    return result==KERN_SUCCESS?0:3;
}
