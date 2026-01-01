/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "../nativefiledialog.h"
#include <QStandardPaths>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <Cocoa/Cocoa.h>
#include <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

// Delegate class to handle file type popup changes
@interface FileTypeAccessoryController : NSObject
@property (strong) NSSavePanel* panel;
@property (strong) NSPopUpButton* popup;
@property (strong) NSArray<NSArray<NSString*>*>* filterExtensions;
- (void)filterChanged:(id)sender;
@end

// Helper function to convert file extensions to UTTypes
static NSArray<UTType*>* extensionsToContentTypes(NSArray<NSString*>* extensions) API_AVAILABLE(macos(11.0)) {
    if (!extensions || [extensions count] == 0) {
        return nil;
    }
    
    NSMutableArray<UTType*>* types = [NSMutableArray array];
    for (NSString* ext in extensions) {
        if ([ext isEqualToString:@"*"]) {
            return nil;  // All files
        }
        UTType* type = [UTType typeWithFilenameExtension:ext];
        if (type) {
            [types addObject:type];
        }
    }
    return [types count] > 0 ? types : nil;
}

// Helper function to set allowed file types on a panel (handles API availability)
static void setAllowedExtensions(NSSavePanel* panel, NSArray<NSString*>* extensions) {
    if (@available(macOS 11.0, *)) {
        NSArray<UTType*>* types = extensionsToContentTypes(extensions);
        [panel setAllowedContentTypes:(types ? types : @[])];
    } else {
        // Fallback for older macOS versions
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [panel setAllowedFileTypes:extensions];
#pragma clang diagnostic pop
    }
}

@implementation FileTypeAccessoryController
- (void)filterChanged:(id)sender {
    NSInteger idx = [self.popup indexOfSelectedItem];
    if (idx >= 0 && idx < (NSInteger)[self.filterExtensions count]) {
        NSArray<NSString*>* exts = self.filterExtensions[idx];
        if ([exts count] == 0 || ([exts count] == 1 && [exts[0] isEqualToString:@"*"])) {
            // "All files" - allow everything
            setAllowedExtensions(self.panel, nil);
        } else {
            setAllowedExtensions(self.panel, exts);
        }
        // Force panel to refresh file list
        [self.panel validateVisibleColumns];
    }
}
@end

namespace {
// Parse Qt filter format into structured filter info
// Qt format: "Description (*.ext1 *.ext2);;Another (*.ext3)"
NSArray<NSDictionary*>* parseQtFilters(const QString &qtFilter)
{
    NSMutableArray<NSDictionary*>* result = [NSMutableArray array];
    
    if (qtFilter.isEmpty()) {
        return result;
    }
    
    QStringList filters = qtFilter.split(";;");
    
    for (const QString &filter : filters) {
        QString trimmed = filter.trimmed();
        if (trimmed.isEmpty()) continue;
        
        // Extract display name (everything before the parenthesis, or the whole string)
        QString displayName = trimmed.section('(', 0, 0).trimmed();
        if (displayName.isEmpty()) {
            displayName = trimmed;
        }
        
        // Extract extensions from parentheses
        NSMutableArray<NSString*>* extensions = [NSMutableArray array];
        QString extensionsStr = trimmed.section('(', 1, 1).section(')', 0, 0);
        QStringList extList = extensionsStr.split(" ", Qt::SkipEmptyParts);
        
        bool isAllFiles = false;
        for (QString ext : extList) {
            if (ext == "*" || ext == "*.*") {
                isAllFiles = true;
                break;
            }
            if (ext.startsWith("*.")) {
                ext = ext.mid(2); // Remove "*."
                if (!ext.isEmpty()) {
                    [extensions addObject:ext.toNSString()];
                }
            }
        }
        
        if (isAllFiles) {
            [extensions removeAllObjects];
            [extensions addObject:@"*"];
        }
        
        NSDictionary* filterDict = @{
            @"displayName": displayName.toNSString(),
            @"extensions": [extensions copy]
        };
        [result addObject:filterDict];
    }
    
    return result;
}

// Create accessory view with file type popup
NSView* createAccessoryView(NSArray<NSDictionary*>* filters, 
                            FileTypeAccessoryController* controller,
                            NSSavePanel* panel)
{
    if ([filters count] <= 1) {
        return nil;  // No need for popup with 0 or 1 filter
    }
    
    // Create container view
    NSView* accessoryView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 300, 32)];
    
    // Create label
    NSTextField* label = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 6, 80, 20)];
    QString labelText = QCoreApplication::translate("NativeFileDialog", "File type:");
    [label setStringValue:labelText.toNSString()];
    [label setBezeled:NO];
    [label setDrawsBackground:NO];
    [label setEditable:NO];
    [label setSelectable:NO];
    [label setAlignment:NSTextAlignmentRight];
    [accessoryView addSubview:label];
    
    // Create popup button
    NSPopUpButton* popup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(85, 2, 210, 26) pullsDown:NO];
    
    NSMutableArray<NSArray<NSString*>*>* allExtensions = [NSMutableArray array];
    
    for (NSDictionary* filter in filters) {
        NSString* displayName = filter[@"displayName"];
        NSArray<NSString*>* exts = filter[@"extensions"];
        
        [popup addItemWithTitle:displayName];
        [allExtensions addObject:exts];
    }
    
    controller.panel = panel;
    controller.popup = popup;
    controller.filterExtensions = [allExtensions copy];
    
    [popup setTarget:controller];
    [popup setAction:@selector(filterChanged:)];
    
    [accessoryView addSubview:popup];
    
    // Apply initial filter
    [controller filterChanged:nil];
    
    return accessoryView;
}

// Get extensions for first filter (to apply when no accessory view)
NSArray<NSString*>* getFirstFilterExtensions(NSArray<NSDictionary*>* filters)
{
    if ([filters count] == 0) {
        return nil;
    }
    
    NSArray<NSString*>* exts = filters[0][@"extensions"];
    if ([exts count] == 0 || ([exts count] == 1 && [exts[0] isEqualToString:@"*"])) {
        return nil;  // All files
    }
    return exts;
}
} // anonymous namespace

QString NativeFileDialog::getFileNameNative(const QString &title,
                                           const QString &initialDir, const QString &filter,
                                           bool saveDialog, void *parentWindow)
{
    // Parent window parameter is unused on macOS
    // Native panels always run as application-modal dialogs
    Q_UNUSED(parentWindow);
    
    // Initialize timing info
    TimingInfo timing;
    timing.isSaveDialog = saveDialog;
    
    QElapsedTimer totalTimer;
    totalTimer.start();
    
    @autoreleasepool {
        QElapsedTimer timer;
        timer.start();
        
        NSString *nsTitle = title.isEmpty() ? nil : title.toNSString();
        
        // Parse initial directory and filename using string operations only (no filesystem access)
        // This avoids slow stat() calls on iCloud-synced directories
        QString dirPath;
        QString filename;
        
        if (!initialDir.isEmpty()) {
            int lastSlash = initialDir.lastIndexOf('/');
            if (lastSlash > 0) {
                QString lastComponent = initialDir.mid(lastSlash + 1);
                // For save dialogs, assume last component with an extension is a filename
                // For open dialogs, treat the whole path as a directory
                if (saveDialog && lastComponent.contains('.')) {
                    dirPath = initialDir.left(lastSlash);
                    filename = lastComponent;
                } else {
                    dirPath = initialDir;
                }
            } else {
                dirPath = initialDir;
            }
        }
        
        if (dirPath.isEmpty()) {
            dirPath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
        }
        
        NSString *nsInitialDir = dirPath.toNSString();
        NSString *nsInitialFilename = filename.isEmpty() ? nil : filename.toNSString();
        
        timing.pathParsingMs = timer.elapsed();
        timer.restart();
        
        // Parse filters once for either dialog type
        NSArray<NSDictionary*>* parsedFilters = parseQtFilters(filter);
        FileTypeAccessoryController* controller = [[FileTypeAccessoryController alloc] init];
        
        timing.filterParsingMs = timer.elapsed();
        timer.restart();
        
        if (saveDialog) {
            NSSavePanel *panel = [NSSavePanel savePanel];
            timing.panelCreationMs = timer.elapsed();
            timer.restart();
            
            [panel setTitle:nsTitle];
            
            NSURL *dirUrl = [NSURL fileURLWithPath:nsInitialDir];
            [panel setDirectoryURL:dirUrl];
            timing.setDirectoryMs = timer.elapsed();
            timer.restart();
            
            if (nsInitialFilename) {
                [panel setNameFieldStringValue:nsInitialFilename];
            }
            
            // Set up file type filter UI
            if ([parsedFilters count] > 1) {
                // Multiple filters: show popup menu for selection
                NSView* accessory = createAccessoryView(parsedFilters, controller, panel);
                if (accessory) {
                    [panel setAccessoryView:accessory];
                }
            } else if ([parsedFilters count] == 1) {
                // Single filter: just apply it without UI
                NSArray<NSString*>* exts = getFirstFilterExtensions(parsedFilters);
                if (exts) {
                    setAllowedExtensions(panel, exts);
                }
            }
            
            timing.panelSetupMs = timer.elapsed();
            timing.totalBeforeShowMs = totalTimer.elapsed();
            timer.restart();
            
            // Run as application modal dialog - this blocks until dismissed
            // No QEventLoop needed since runModal handles its own run loop
            NSInteger buttonPressed = [panel runModal];
            
            timing.userInteractionMs = timer.elapsed();
            
            // Store timing info for later retrieval
            s_lastTimingInfo = timing;
            
            if (buttonPressed == NSModalResponseOK) {
                NSURL *url = [panel URL];
                return QString::fromNSString([url path]);
            }
        } else {
            NSOpenPanel *panel = [NSOpenPanel openPanel];
            timing.panelCreationMs = timer.elapsed();
            timer.restart();
            
            [panel setTitle:nsTitle];
            [panel setCanChooseFiles:YES];
            [panel setCanChooseDirectories:NO];
            [panel setAllowsMultipleSelection:NO];
            
            NSURL *dirUrl = [NSURL fileURLWithPath:nsInitialDir];
            [panel setDirectoryURL:dirUrl];
            timing.setDirectoryMs = timer.elapsed();
            timer.restart();
            
            // Set up file type filter UI
            if ([parsedFilters count] > 1) {
                // Multiple filters: show popup menu for selection
                NSView* accessory = createAccessoryView(parsedFilters, controller, panel);
                if (accessory) {
                    [panel setAccessoryView:accessory];
                }
            } else if ([parsedFilters count] == 1) {
                // Single filter: just apply it without UI
                NSArray<NSString*>* exts = getFirstFilterExtensions(parsedFilters);
                if (exts) {
                    setAllowedExtensions(panel, exts);
                }
            }
            
            timing.panelSetupMs = timer.elapsed();
            timing.totalBeforeShowMs = totalTimer.elapsed();
            timer.restart();
            
            // Run as application modal dialog - this blocks until dismissed
            // No QEventLoop needed since runModal handles its own run loop
            NSInteger buttonPressed = [panel runModal];
            
            timing.userInteractionMs = timer.elapsed();
            
            // Store timing info for later retrieval
            s_lastTimingInfo = timing;
            
            if (buttonPressed == NSModalResponseOK) {
                NSArray *urls = [panel URLs];
                if ([urls count] > 0) {
                    NSURL *url = [urls objectAtIndex:0];
                    return QString::fromNSString([url path]);
                }
            }
        }
        
        return QString();
    }
}

bool NativeFileDialog::areNativeDialogsAvailablePlatform()
{
    // Native dialogs are always available on macOS
    return true;
}
