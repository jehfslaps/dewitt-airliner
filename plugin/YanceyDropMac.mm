// Dewitt: drag audio files from Finder onto the window (Mac). Windows has its own in YanceyPlatform.cpp.
//
// The plug-in view is pugl's NSView. It doesn't take drags, so the four drag methods are added to its class at
// run time (once; DPF gives every plug-in its own copy of the pugl classes, so this touches only Dewitt), and
// a table maps each view to its callback.
#import <Cocoa/Cocoa.h>
#include <objc/runtime.h>
#include <map>
#include "YanceyPlatform.hpp"

namespace {
struct DropTarget { yancey::DropCallback cb; void* ctx; void* frame; };   // frame: the view whose bounds map to 0..1
std::map<void*, DropTarget>& targets() { static auto* m = new std::map<void*, DropTarget>(); return *m; }

NSURL* firstFileURL(id<NSDraggingInfo> info)
{
    NSArray* urls = [[info draggingPasteboard] readObjectsForClasses:@[[NSURL class]]
                                                              options:@{ NSPasteboardURLReadingFileURLsOnlyKey: @YES }];
    return urls.count > 0 ? urls[0] : nil;
}

NSDragOperation dragEntered(id self, SEL, id<NSDraggingInfo> info)
{
    return (targets().count((__bridge void*)self) && firstFileURL(info) != nil) ? NSDragOperationCopy : NSDragOperationNone;
}
BOOL dragPrepare(id, SEL, id<NSDraggingInfo>) { return YES; }
BOOL dragPerform(id self, SEL, id<NSDraggingInfo> info)
{
    auto it = targets().find((__bridge void*)self);
    NSURL* url = firstFileURL(info);
    if (it == targets().end() || url == nil) return NO;
    NSView* v = (__bridge NSView*)it->second.frame;
    const NSPoint p = [v convertPoint:[info draggingLocation] fromView:nil];
    const NSRect b = [v bounds];
    if (b.size.width <= 0 || b.size.height <= 0) return NO;
    const float fx = (float)(p.x / b.size.width);
    const float fy = (float)([v isFlipped] ? p.y / b.size.height : 1.0 - p.y / b.size.height);
    it->second.cb(it->second.ctx, [[url path] fileSystemRepresentation], fx, fy);
    return YES;
}

void addDragMethods(Class c)
{
    static std::map<Class, bool>* done = new std::map<Class, bool>();
    if ((*done)[c]) return;
    (*done)[c] = true;
    class_addMethod(c, @selector(draggingEntered:), (IMP)dragEntered, "Q@:@");
    class_addMethod(c, @selector(draggingUpdated:), (IMP)dragEntered, "Q@:@");
    class_addMethod(c, @selector(prepareForDragOperation:), (IMP)dragPrepare, "c@:@");
    class_addMethod(c, @selector(performDragOperation:), (IMP)dragPerform, "c@:@");
}
} // namespace

namespace yancey {

bool installFileDrop(uintptr_t nativeWindow, DropCallback cb, void* ctx)
{
    NSView* v = (__bridge NSView*)(void*)nativeWindow;
    if (v == nil || targets().count((void*)nativeWindow)) return false;
    // the view and the drawing view inside it, so the drop lands whichever one is under the pointer
    NSMutableArray* views = [NSMutableArray arrayWithObject:v];
    [views addObjectsFromArray:[v subviews]];
    for (NSView* w in views) {
        addDragMethods([w class]);
        targets()[(__bridge void*)w] = DropTarget { cb, ctx, (void*)nativeWindow };
        [w registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
    }
    return true;
}

void removeFileDrop(uintptr_t nativeWindow)
{
    for (auto it = targets().begin(); it != targets().end();) {
        if (it->second.frame == (void*)nativeWindow) { [(__bridge NSView*)it->first unregisterDraggedTypes]; it = targets().erase(it); }
        else ++it;
    }
}

} // namespace yancey
