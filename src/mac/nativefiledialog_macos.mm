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

namespace {
QString convertQtFilterToMacOS(const QString &qtFilter)
{
    if (qtFilter.isEmpty()) {
        return QString();
    }
    
    // Qt filter format: "Description (*.ext1 *.ext2);;Another (*.ext3)"
    // macOS format: comma-separated extensions without dots
    QString macFilter;
    QStringList filters = qtFilter.split(";;");
    
    for (const QString &filter : filters) {
        // Skip "All files (*)" entries as they don't contain specific extensions
        QString lowerFilter = filter.toLower();
        if (lowerFilter.contains("all files") && lowerFilter.contains("(*)")) {
            continue;
        }
        
        QString extensions = filter.section('(', 1, 1).section(')', 0, 0);
        QStringList extList = extensions.split(" ", Qt::SkipEmptyParts);
        
        for (QString ext : extList) {
            if (ext.startsWith("*.")) {
                ext = ext.mid(2); // Remove "*."
                if (!ext.isEmpty()) {
                    if (!macFilter.isEmpty()) {
                        macFilter += ",";
                    }
                    macFilter += ext;
                }
            }
        }
    }
    
    return macFilter;
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
        
        if (saveDialog) {
            NSSavePanel *panel = [NSSavePanel savePanel];
            [panel setTitle:nsTitle];
            
            if (nsInitialDir) {
                NSURL *dirUrl = [NSURL fileURLWithPath:nsInitialDir];
                [panel setDirectoryURL:dirUrl];
            }
            
            // Set file type filters - macOS will automatically add "All Files" option to the dropdown
            if (!filter.isEmpty()) {
                QString macFilter = convertQtFilterToMacOS(filter);
                if (!macFilter.isEmpty()) {
                    NSArray *fileTypes = [macFilter.toNSString() componentsSeparatedByString:@","];
                    [panel setAllowedFileTypes:fileTypes];
                    // macOS automatically provides "All Files" option in dropdown when allowedFileTypes is set
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
            
            // Set file type filters - macOS will automatically add "All Files" option to the dropdown
            if (!filter.isEmpty()) {
                QString macFilter = convertQtFilterToMacOS(filter);
                if (!macFilter.isEmpty()) {
                    NSArray *fileTypes = [macFilter.toNSString() componentsSeparatedByString:@","];
                    [panel setAllowedFileTypes:fileTypes];
                    // macOS automatically provides "All Files" option in dropdown when allowedFileTypes is set
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
