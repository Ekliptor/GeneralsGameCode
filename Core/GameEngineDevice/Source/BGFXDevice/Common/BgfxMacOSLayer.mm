/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
*/

// Objective-C helper: on macOS the CAMetalLayer bgfx attaches to a
// SDL-managed NSWindow sometimes ends up with a bounds/frame that
// doesn't match the actual NSView it lives in — the drawable is the
// right size, but the layer displays only a corner of it. That's the
// "pink border" we chased down through init/reset/viewport and could
// not explain from traces alone. This file gives us a tiny Cocoa probe
// to (a) log what we have, and (b) forcibly align the layer frame with
// the content view so the drawable maps 1:1 to on-screen pixels.

#if defined(__APPLE__)

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

namespace BgfxMacOSLayer
{

static CAMetalLayer* Resolve_Metal_Layer(void* nsWindowOrView)
{
	if (!nsWindowOrView) return nil;
	id obj = (__bridge id)nsWindowOrView;
	NSView* contentView = nil;
	if ([obj isKindOfClass:[NSWindow class]])
		contentView = ((NSWindow*)obj).contentView;
	else if ([obj isKindOfClass:[NSView class]])
		contentView = (NSView*)obj;
	if (!contentView) return nil;
	CALayer* layer = contentView.layer;
	if ([layer isKindOfClass:[CAMetalLayer class]])
		return (CAMetalLayer*)layer;
	return nil;
}

bool Fit_Layer_To_View(void* nsWindowOrView)
{
	CAMetalLayer* layer = Resolve_Metal_Layer(nsWindowOrView);
	if (!layer) return false;
	// The drawable is sized in pixels; the layer frame is in points of
	// the parent view. contentsScale converts between the two. Match the
	// layer frame to the view bounds, force autoresizing so future
	// fullscreen transitions keep it aligned, and align contentsScale
	// with the window's backing scale so points↔pixels stays consistent.
	id obj = (__bridge id)nsWindowOrView;
	NSView* contentView = nil;
	if ([obj isKindOfClass:[NSWindow class]])
		contentView = ((NSWindow*)obj).contentView;
	else if ([obj isKindOfClass:[NSView class]])
		contentView = (NSView*)obj;
	if (!contentView) return false;
	layer.frame = contentView.bounds;
	layer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
	contentView.autoresizesSubviews = YES;
	CGFloat scale = 1.0;
	if (contentView.window && contentView.window.screen)
		scale = contentView.window.backingScaleFactor;
	layer.contentsScale = scale;
	return true;
}

} // namespace BgfxMacOSLayer

#endif // __APPLE__
