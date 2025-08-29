/*
 * SPDX-License-Identifier: Apache-2.0
 */

#import <Foundation/Foundation.h>
#import <CoreLocation/CoreLocation.h>
#include "location_helper.h"

@interface RPIImagerLocationDelegate : NSObject<CLLocationManagerDelegate>
@property (atomic, assign) BOOL finished;
@property (atomic, assign) BOOL authorized;
@end

@implementation RPIImagerLocationDelegate
- (instancetype)init { if (self = [super init]) { _finished = NO; _authorized = NO; } return self; }
- (void)locationManagerDidChangeAuthorization:(CLLocationManager *)manager API_AVAILABLE(macos(11.0)) {
    CLAuthorizationStatus st = manager.authorizationStatus;
    BOOL isAuth = (st == kCLAuthorizationStatusAuthorized);
#ifdef kCLAuthorizationStatusAuthorizedAlways
    isAuth = isAuth || (st == kCLAuthorizationStatusAuthorizedAlways);
#endif
    if (isAuth) _authorized = YES;
    _finished = YES;
}
- (void)locationManager:(CLLocationManager *)manager didChangeAuthorizationStatus:(CLAuthorizationStatus)status {
    BOOL isAuth = (status == kCLAuthorizationStatusAuthorized);
#ifdef kCLAuthorizationStatusAuthorizedAlways
    isAuth = isAuth || (status == kCLAuthorizationStatusAuthorizedAlways);
#endif
    if (isAuth) _authorized = YES;
    _finished = YES;
}
@end

int rpiimager_request_location_permission()
{
    @autoreleasepool {
        if (![CLLocationManager locationServicesEnabled]) {
            return 0;
        }
        CLAuthorizationStatus status;
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 120000
        status = [CLLocationManager authorizationStatus];
#else
        status = kCLAuthorizationStatusNotDetermined; // Will be updated via delegate callbacks
#endif
        if (status == kCLAuthorizationStatusAuthorized)
            return 1;
#ifdef kCLAuthorizationStatusAuthorizedAlways
        if (status == kCLAuthorizationStatusAuthorizedAlways)
            return 1;
#endif

        CLLocationManager *mgr = [[CLLocationManager alloc] init];
        RPIImagerLocationDelegate *delegate = [[RPIImagerLocationDelegate alloc] init];
        mgr.delegate = delegate;
        if (status == kCLAuthorizationStatusNotDetermined) {
            [mgr requestWhenInUseAuthorization];
        } else if (status == kCLAuthorizationStatusDenied || status == kCLAuthorizationStatusRestricted) {
            return 0;
        }

        NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:5.0];
        while (!delegate.finished && [deadline timeIntervalSinceNow] > 0) {
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
        }
        return delegate.authorized ? 1 : 0;
    }
}


