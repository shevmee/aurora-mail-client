#include "MacNativeCursorFilter.hpp"

// Built only on APPLE (see desktop/CMakeLists.txt).

#import <AppKit/AppKit.h>
#import <objc/runtime.h>

#include <QGuiApplication>

namespace {

/**
 * AppKit calls private +[NSCursor _setOverrideCursor:type:] during window resize; that loads
 * cursor assets via ImageIO→PNG. On some Qt 6.9 + macOS 15 setups this hits a bad code pointer
 * inside ImageIO (PC≈0xbad4007). Forcing the arrow cursor avoids that bundle/ImageIO path.
 */
void replaceNSCursorSetOverrideCursor()
{
    SEL sel = NSSelectorFromString(@"_setOverrideCursor:type:");
    Method m = class_getClassMethod([NSCursor class], sel);
    if (m == nullptr) {
        return;
    }

    IMP replacement = imp_implementationWithBlock(
        ^(Class /*cls*/, SEL /*cmd*/, id /*cursor*/, NSInteger /*type*/) {
            while (QGuiApplication::overrideCursor()) {
                QGuiApplication::restoreOverrideCursor();
            }
            [[NSCursor arrowCursor] set];
        });
    method_setImplementation(m, replacement);
}

void installLocalMouseMonitor()
{
    static id monitor = nil;
    if (monitor != nil) {
        return;
    }

    monitor = [NSEvent
        addLocalMonitorForEventsMatchingMask:(NSEventMaskLeftMouseDown | NSEventMaskLeftMouseDragged)
                                       handler:^NSEvent*(NSEvent* event) {
                                           while (QGuiApplication::overrideCursor()) {
                                               QGuiApplication::restoreOverrideCursor();
                                           }
                                           [[NSCursor arrowCursor] set];
                                           return event;
                                       }];
}

} // namespace

void installMacNativeCursorFilter()
{
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        replaceNSCursorSetOverrideCursor();
        installLocalMouseMonitor();
    });
}
