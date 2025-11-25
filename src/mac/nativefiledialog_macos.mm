/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "../nativefiledialog.h"
#include <QStandardPaths>
#include <QDebug>
#include <QTimer>
#include <QEventLoop>
#include <QCoreApplication>
#include <QWindow>
#include <Cocoa/Cocoa.h>

// Delegate class to handle file type popup changes
@interface FileTypeAccessoryController : NSObject
@property (strong) NSSavePanel* panel;
@property (strong) NSPopUpButton* popup;
@property (strong) NSArray<NSArray<NSString*>*>* filterExtensions;
- (void)filterChanged:(id)sender;
@end

@implementation FileTypeAccessoryController
- (void)filterChanged:(id)sender {
    NSInteger idx = [self.popup indexOfSelectedItem];
    if (idx >= 0 && idx < (NSInteger)[self.filterExtensions count]) {
        NSArray<NSString*>* exts = self.filterExtensions[idx];
        if ([exts count] == 0 || ([exts count] == 1 && [exts[0] isEqualToString:@"*"])) {
            // "All files" - allow everything
            [self.panel setAllowedFileTypes:nil];
        } else {
            [self.panel setAllowedFileTypes:exts];
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
    // Defer dialog presentation to avoid interfering with Qt QML object destruction
    QString result;
    QEventLoop loop;
    
    // Process events once to allow QML cleanup, then show dialog with minimal delay
    QCoreApplication::processEvents();
    
    // Parent window parameter is unused on macOS
    // Native panels always run as application-modal dialogs
    Q_UNUSED(parentWindow);
    
    // Use a very short timer to minimize delay while still avoiding Qt conflicts
    QTimer::singleShot(1, [&]() {
        NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
        
        NSString *nsTitle = title.isEmpty() ? nil : title.toNSString();
        
        // Convert initial directory
        NSString *nsInitialDir = nil;
        if (!initialDir.isEmpty()) {
            nsInitialDir = initialDir.toNSString();
        } else {
            QString homeDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
            nsInitialDir = homeDir.toNSString();
        }
        
        NSString *modalResult = nil;
        
        // Parse filters once for either dialog type
        NSArray<NSDictionary*>* parsedFilters = parseQtFilters(filter);
        FileTypeAccessoryController* controller = [[FileTypeAccessoryController alloc] init];
        
        if (saveDialog) {
            NSSavePanel *panel = [NSSavePanel savePanel];
            [panel setTitle:nsTitle];
            
            if (nsInitialDir) {
                NSURL *dirUrl = [NSURL fileURLWithPath:nsInitialDir];
                [panel setDirectoryURL:dirUrl];
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
                    [panel setAllowedFileTypes:exts];
                }
            }
            
            // Simply run as application modal dialog
            // Sheet modals require async handling which complicates Qt integration
            NSInteger buttonPressed = [panel runModal];
            
            if (buttonPressed == NSModalResponseOK) {
                NSURL *url = [panel URL];
                modalResult = [url path];
            }
        } else {
            NSOpenPanel *panel = [NSOpenPanel openPanel];
            [panel setTitle:nsTitle];
            [panel setCanChooseFiles:YES];
            [panel setCanChooseDirectories:NO];
            [panel setAllowsMultipleSelection:NO];
            
            if (nsInitialDir) {
                NSURL *dirUrl = [NSURL fileURLWithPath:nsInitialDir];
                [panel setDirectoryURL:dirUrl];
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
                    [panel setAllowedFileTypes:exts];
                }
            }
            
            // Simply run as application modal dialog
            // Sheet modals require async handling which complicates Qt integration
            NSInteger buttonPressed = [panel runModal];
            
            if (buttonPressed == NSModalResponseOK) {
                NSArray *urls = [panel URLs];
                if ([urls count] > 0) {
                    NSURL *url = [urls objectAtIndex:0];
                    modalResult = [url path];
                }
            }
        }
        
        result = modalResult ? QString::fromNSString(modalResult) : QString();
        [pool release];
        
        // Exit the event loop
        loop.quit();
    });
    
    // Wait for the dialog to complete
    loop.exec();
    
    return result;
}

bool NativeFileDialog::areNativeDialogsAvailablePlatform()
{
    // Native dialogs are always available on macOS
    return true;
}
