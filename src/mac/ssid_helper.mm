/*
 * SPDX-License-Identifier: Apache-2.0
 */

#import <Foundation/Foundation.h>
#import <CoreWLAN/CoreWLAN.h>
#include <stdlib.h>
#include "ssid_helper.h"

const char *rpiimager_current_ssid_cstr()
{
    @autoreleasepool {
        // CWWiFiClient default client
        CWWiFiClient *client = [CWWiFiClient sharedWiFiClient];
        if (!client) { NSLog(@"rpi-imager CoreWLAN: no sharedWiFiClient"); return NULL; }

        // Try default interface first (may be nil on some systems)
        CWInterface *iface = [client interface];
        if (iface) {
            NSString *ssid = [iface ssid];
            if (ssid && [ssid length] > 0) {
                NSLog(@"rpi-imager CoreWLAN default interface %@ SSID: %@", iface.interfaceName, ssid);
                const char *utf8 = [ssid UTF8String];
                if (!utf8) return NULL;
                size_t len = strlen(utf8);
                char *dup = (char *)malloc(len + 1);
                if (!dup) return NULL;
                memcpy(dup, utf8, len + 1);
                return dup;
            } else {
                NSLog(@"rpi-imager CoreWLAN default interface %@ has no SSID", iface.interfaceName);
            }
        } else {
            NSLog(@"rpi-imager CoreWLAN: default interface is nil, enumerating interfaces");
        }

        // Enumerate all Wi-Fi interfaces as fallback
        NSArray<CWInterface*> *ifaces = [client interfaces];
        if (!ifaces || [ifaces count] == 0) {
            NSLog(@"rpi-imager CoreWLAN: no interfaces found");
            return NULL;
        }

        for (CWInterface *it in ifaces) {
            NSString *ssid = [it ssid];
            NSLog(@"rpi-imager CoreWLAN interface %@ power:%@ active:%@ ssid:%@",
                  it.interfaceName,
                  it.powerOn ? @"on" : @"off",
                  it.serviceActive ? @"yes" : @"no",
                  ssid ? ssid : @"<none>");
            if (ssid && [ssid length] > 0) {
                const char *utf8 = [ssid UTF8String];
                if (!utf8) continue;
                size_t len = strlen(utf8);
                char *dup = (char *)malloc(len + 1);
                if (!dup) return NULL;
                memcpy(dup, utf8, len + 1);
                return dup;
            }
        }

        // Final attempt: use networksetup -getairportnetwork <iface>
        for (CWInterface *it in ifaces) {
            @try {
                NSTask *task = [[NSTask alloc] init];
                task.launchPath = @"/usr/sbin/networksetup";
                task.arguments = @[@"-getairportnetwork", it.interfaceName ?: @"en0"];
                NSPipe *pipe = [NSPipe pipe];
                task.standardOutput = pipe;
                task.standardError = [NSPipe pipe];
#if __MAC_OS_X_VERSION_MIN_REQUIRED >= 101300
                [task setExecutableURL:[NSURL fileURLWithPath:task.launchPath]];
                [task launchAndReturnError:nil];
#else
                [task launch];
#endif
                [task waitUntilExit];
                NSData *data = [[pipe fileHandleForReading] readDataToEndOfFile];
                NSString *outStr = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
                if (outStr.length > 0) {
                    // Expected: "Current Wi-Fi Network: <SSID>" or "You are not associated ..."
                    NSRange range = [outStr rangeOfString:@"Current Wi-Fi Network: "];
                    if (range.location != NSNotFound) {
                        NSString *ssid = [outStr substringFromIndex:range.location + range.length];
                        ssid = [ssid stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
                        if (ssid.length > 0) {
                            NSLog(@"rpi-imager networksetup %@ -> %@", it.interfaceName, ssid);
                            const char *utf8 = [ssid UTF8String];
                            if (!utf8) continue;
                            size_t len = strlen(utf8);
                            char *dup = (char *)malloc(len + 1);
                            if (!dup) return NULL;
                            memcpy(dup, utf8, len + 1);
                            return dup;
                        }
                    } else {
                        NSLog(@"rpi-imager networksetup %@ -> %@", it.interfaceName, outStr);
                    }
                }
            } @catch (NSException *ex) {
                NSLog(@"rpi-imager networksetup exception: %@", ex);
            }
        }

        // Nothing found
        return NULL;
    }
}


