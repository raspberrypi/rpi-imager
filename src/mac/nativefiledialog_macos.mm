/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "../nativefiledialog.h"
#include <QStandardPaths>
#include <QDebug>
#include <QMacAutoReleasePool>
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
        QString extensions = filter.section('(', 1, 1).section(')', 0, 0);
        QStringList extList = extensions.split(" ", Qt::SkipEmptyParts);
        
        for (QString ext : extList) {
            ext = ext.replace("*.", "");
            if (!macFilter.isEmpty()) {
                macFilter += ",";
            }
            macFilter += ext;
        }
    }
    
    return macFilter;
}
} // anonymous namespace

QString NativeFileDialog::getFileNameNative(QWidget *parent, const QString &title,
                                           const QString &initialDir, const QString &filter,
                                           bool saveDialog)
{
    Q_UNUSED(parent)
    
    QMacAutoReleasePool pool;
    
    NSString *nsTitle = title.isEmpty() ? nil : title.toNSString();
    
    // Convert initial directory
    NSString *nsInitialDir = nil;
    if (!initialDir.isEmpty()) {
        nsInitialDir = initialDir.toNSString();
    } else {
        QString homeDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
        nsInitialDir = homeDir.toNSString();
    }
    
    NSString *result = nil;
    
    if (saveDialog) {
        NSSavePanel *panel = [NSSavePanel savePanel];
        [panel setTitle:nsTitle];
        
        if (nsInitialDir) {
            NSURL *dirUrl = [NSURL fileURLWithPath:nsInitialDir];
            [panel setDirectoryURL:dirUrl];
        }
        
        // Convert and set file types if specified
        if (!filter.isEmpty()) {
            NSArray *fileTypes = convertQtFilterToMacOS(filter).toNSString().componentsSeparatedByString(@",");
            [panel setAllowedFileTypes:fileTypes];
        }
        
        NSInteger buttonPressed = [panel runModal];
        if (buttonPressed == NSModalResponseOK) {
            NSURL *url = [panel URL];
            result = [url path];
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
        
        // Convert and set file types if specified
        if (!filter.isEmpty()) {
            NSArray *fileTypes = convertQtFilterToMacOS(filter).toNSString().componentsSeparatedByString(@",");
            [panel setAllowedFileTypes:fileTypes];
        }
        
        NSInteger buttonPressed = [panel runModal];
        if (buttonPressed == NSModalResponseOK) {
            NSArray *urls = [panel URLs];
            if ([urls count] > 0) {
                NSURL *url = [urls objectAtIndex:0];
                result = [url path];
            }
        }
    }
    
    return result ? QString::fromNSString(result) : QString();
}

bool NativeFileDialog::areNativeDialogsAvailablePlatform()
{
    // Native dialogs are always available on macOS
    return true;
}

