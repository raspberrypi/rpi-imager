/*
 * SPDX-License-Identifier: Apache-2.0
 */

#import <Foundation/Foundation.h>
#import <CoreLocation/CoreLocation.h>
#include "location_helper.h"

/* Persistent delegate that can invoke a callback when permission changes */
@interface RPIImagerLocationDelegate : NSObject<CLLocationManagerDelegate>
@property (atomic, assign) BOOL finished;
@property (atomic, assign) BOOL authorized;
@property (atomic, assign) rpiimager_location_callback callback;
@property (atomic, assign) void *callbackContext;
@property (atomic, assign) BOOL callbackInvoked;
@end

@implementation RPIImagerLocationDelegate
- (instancetype)init {
    if (self = [super init]) {
        _finished = NO;
        _authorized = NO;
        _callback = NULL;
        _callbackContext = NULL;
        _callbackInvoked = NO;
    }
    return self;
}

- (void)handleAuthorizationStatus:(CLAuthorizationStatus)status {
    BOOL isAuth = (status == kCLAuthorizationStatusAuthorized);
#ifdef kCLAuthorizationStatusAuthorizedAlways
    isAuth = isAuth || (status == kCLAuthorizationStatusAuthorizedAlways);
#endif
    
    // Only set authorized if actually authorized (don't clear if already set)
    if (isAuth) _authorized = YES;
    _finished = YES;
    
    // Invoke callback if permission was granted and we have a callback
    // Only invoke once, and only when authorized (to handle late permission grants)
    if (isAuth && _callback && !_callbackInvoked) {
        _callbackInvoked = YES;
        NSLog(@"rpi-imager location: Permission granted, invoking callback");
        _callback(1, _callbackContext);
    }
}

- (void)locationManagerDidChangeAuthorization:(CLLocationManager *)manager API_AVAILABLE(macos(11.0)) {
    CLAuthorizationStatus st = manager.authorizationStatus;
    NSLog(@"rpi-imager location: Authorization changed to %d", (int)st);
    [self handleAuthorizationStatus:st];
}

- (void)locationManager:(CLLocationManager *)manager didChangeAuthorizationStatus:(CLAuthorizationStatus)status {
    NSLog(@"rpi-imager location: Authorization status changed to %d", (int)status);
    [self handleAuthorizationStatus:status];
}
@end

/* Global persistent manager and delegate to receive callbacks after initial timeout */
static CLLocationManager *g_persistentManager = nil;
static RPIImagerLocationDelegate *g_persistentDelegate = nil;

static BOOL isAuthorizedStatus(CLAuthorizationStatus status) {
    if (status == kCLAuthorizationStatusAuthorized) return YES;
#ifdef kCLAuthorizationStatusAuthorizedAlways
    if (status == kCLAuthorizationStatusAuthorizedAlways) return YES;
#endif
    return NO;
}

int rpiimager_check_location_permission()
{
    @autoreleasepool {
        if (![CLLocationManager locationServicesEnabled]) {
            return 0;
        }
        CLAuthorizationStatus status;
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 120000
        status = [CLLocationManager authorizationStatus];
#else
        status = kCLAuthorizationStatusNotDetermined;
#endif
        return isAuthorizedStatus(status) ? 1 : 0;
    }
}

int rpiimager_request_location_permission_async(rpiimager_location_callback callback, void *context)
{
    @autoreleasepool {
        if (![CLLocationManager locationServicesEnabled]) {
            return 0;
        }
        
        CLAuthorizationStatus status;
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 120000
        status = [CLLocationManager authorizationStatus];
#else
        status = kCLAuthorizationStatusNotDetermined;
#endif
        
        // Already authorized
        if (isAuthorizedStatus(status)) {
            return 1;
        }
        
        // Denied or restricted - no point in asking
        if (status == kCLAuthorizationStatusDenied || status == kCLAuthorizationStatusRestricted) {
            return 0;
        }
        
        // Not determined - need to request permission
        // Create persistent manager/delegate if not already created
        if (!g_persistentManager) {
            g_persistentDelegate = [[RPIImagerLocationDelegate alloc] init];
            g_persistentManager = [[CLLocationManager alloc] init];
            g_persistentManager.delegate = g_persistentDelegate;
        }
        
        // Set callback (can be updated for subsequent calls)
        g_persistentDelegate.callback = callback;
        g_persistentDelegate.callbackContext = context;
        g_persistentDelegate.callbackInvoked = NO;
        g_persistentDelegate.finished = NO;
        g_persistentDelegate.authorized = NO;
        
        // Request permission
        NSLog(@"rpi-imager location: Requesting when-in-use authorization");
        [g_persistentManager requestWhenInUseAuthorization];
        
        // Wait up to 5 seconds for immediate response
        NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:5.0];
        while (!g_persistentDelegate.finished && [deadline timeIntervalSinceNow] > 0) {
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
        }
        
        if (g_persistentDelegate.authorized) {
            NSLog(@"rpi-imager location: Permission granted within timeout");
            return 1;
        }
        
        // Not authorized yet - but the persistent manager will continue to listen
        // and will invoke the callback if permission is granted later
        NSLog(@"rpi-imager location: Permission not granted within timeout, will callback if granted later");
        return 0;
    }
}

int rpiimager_request_location_permission()
{
    return rpiimager_request_location_permission_async(NULL, NULL);
}


