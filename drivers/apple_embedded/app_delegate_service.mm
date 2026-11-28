/**************************************************************************/
/*  app_delegate_service.mm                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#import "app_delegate_service.h"

#include "core/config/project_settings.h"
#include "core/input/input.h"
#include "core/os/main_loop.h"
#include "core/os/os.h"
#include "drivers/apple_embedded/foreground_presentation.h"
#import "drivers/apple_embedded/godot_view_apple_embedded.h"
#import "drivers/apple_embedded/godot_view_controller.h"
#import "drivers/apple_embedded/os_apple_embedded.h"
#import "drivers/coreaudio/audio_driver_coreaudio.h"
#include "main/main.h"

#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioServices.h>

int gargc;
char **gargv;

extern int apple_embedded_main(int, char **);
extern void apple_embedded_finish();

#ifdef IOS_ENABLED
// The presenting addon posts these notifications on the main thread, using the
// same unique object for begin and end. The object identifies one presentation.
static NSNotificationName const presentation_began = @"GodotIOSForegroundPresentationBegan";
static NSNotificationName const presentation_ended = @"GodotIOSForegroundPresentationEnded";
static AppleEmbeddedForegroundPresentation foreground_presentation;

@interface GDTAppDelegateService ()
@property(strong, nonatomic) NSObject *presentationToken;
@property(strong, nonatomic) NSTimer *foregroundInactiveTimer;
@property(assign, nonatomic) BOOL inputWasDisabled;
@end
#endif

@implementation GDTAppDelegateService

enum {
	SESSION_CATEGORY_AMBIENT,
	SESSION_CATEGORY_MULTI_ROUTE,
	SESSION_CATEGORY_PLAY_AND_RECORD,
	SESSION_CATEGORY_PLAYBACK,
	SESSION_CATEGORY_RECORD,
	SESSION_CATEGORY_SOLO_AMBIENT
};

static GDTViewController *mainViewController = nil;

+ (GDTViewController *)viewController {
	return mainViewController;
}

+ (void)setViewController:(GDTViewController *)viewController {
	mainViewController = viewController;
}

#ifdef IOS_ENABLED
+ (BOOL)isForegroundPresentationBlockingInput {
	return foreground_presentation.suppresses_input();
}

- (void)updateForegroundPresentationInputFrom:(BOOL)wasBlocking {
	BOOL blocking = foreground_presentation.suppresses_input();
	Input *input = Input::get_singleton();
	if (input && wasBlocking != blocking) {
		if (blocking) {
			self.inputWasDisabled = input->is_input_disabled();
			input->set_disable_input(true);
		} else {
			input->set_disable_input(self.inputWasDisabled);
		}
	}
}

- (void)stopForegroundInactiveTimer {
	[self.foregroundInactiveTimer invalidate];
	self.foregroundInactiveTimer = nil;
}

- (void)clearForegroundPresentation {
	BOOL was_blocking = foreground_presentation.suppresses_input();
	foreground_presentation.leave_foreground();
	self.presentationToken = nil;
	[self stopForegroundInactiveTimer];
	[self updateForegroundPresentationInputFrom:was_blocking];
}

- (void)foregroundPresentationBegan:(NSNotification *)notification {
	if (![NSThread isMainThread]) {
		return;
	}
	if (@available(iOS 13.0, *)) {
		UIScene *scene = GDTAppDelegateService.viewController.viewIfLoaded.window.windowScene;
		if (!scene || scene.activationState != UISceneActivationStateForegroundActive ||
				UIApplication.sharedApplication.applicationState != UIApplicationStateActive ||
				!OS::get_singleton() || !OS::get_singleton()->get_main_loop()) {
			return;
		}
		if (foreground_presentation.begin((__bridge const void *)notification.object, (__bridge const void *)scene)) {
			self.presentationToken = notification.object;
			[self updateForegroundPresentationInputFrom:NO];
		}
	}
}

- (void)foregroundPresentationEnded:(NSNotification *)notification {
	if (![NSThread isMainThread] || !self.presentationToken || self.presentationToken != notification.object) {
		return;
	}
	BOOL was_blocking = foreground_presentation.suppresses_input();
	foreground_presentation.end((__bridge const void *)notification.object);
	self.presentationToken = nil;
	[self stopForegroundInactiveTimer];
	[self updateForegroundPresentationInputFrom:was_blocking];
}

- (void)processForegroundInactiveTimer:(NSTimer *)timer API_AVAILABLE(ios(13.0)) {
	if (timer != self.foregroundInactiveTimer || Main::is_iterating()) {
		return;
	}
	UIScene *scene = GDTAppDelegateService.viewController.viewIfLoaded.window.windowScene;
	if (!scene || scene.activationState != UISceneActivationStateForegroundInactive ||
			UIApplication.sharedApplication.applicationState == UIApplicationStateBackground ||
			!foreground_presentation.should_iterate((__bridge const void *)scene) ||
			!OS::get_singleton() || !OS::get_singleton()->get_main_loop()) {
		[self clearForegroundPresentation];
		return;
	}

	// Keep timers, network I/O and deferred callbacks on the normal engine thread
	// without drawing, pacing a display frame, or delivering gameplay input.
	Input::get_singleton()->flush_buffered_events();
	if (Main::iteration_foreground_inactive()) {
		[self clearForegroundPresentation];
	}
}

- (void)startForegroundInactiveTimerForScene:(UIScene *)scene API_AVAILABLE(ios(13.0)) {
	if (self.foregroundInactiveTimer || !foreground_presentation.should_iterate((__bridge const void *)scene)) {
		return;
	}
	self.foregroundInactiveTimer = [NSTimer timerWithTimeInterval:0.1 target:self selector:@selector(processForegroundInactiveTimer:) userInfo:nil repeats:YES];
	[[NSRunLoop mainRunLoop] addTimer:self.foregroundInactiveTimer forMode:NSRunLoopCommonModes];
}
#endif

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
	// TODO: might be required to make an early return, so app wouldn't crash because of timeout.
	// TODO: logo screen is not displayed while shaders are compiling
	// DummyViewController(Splash/LoadingViewController) -> setup -> GodotViewController

	// Fetch the command-line arguments from NSProcessInfo
	NSArray *arguments = [[NSProcessInfo processInfo] arguments];
	gargc = (int)[arguments count];
	gargv = (char **)malloc(sizeof(char *) * gargc);
	for (int i = 0; i < gargc; i++) {
		NSString *arg = arguments[i];
		gargv[i] = strdup([arg UTF8String]);
	}

	int err = apple_embedded_main(gargc, gargv);

	if (err != 0) {
		// bail, things did not go very well for us, should probably output a message on screen with our error code...
		exit(0);
		return NO;
	}

	[[NSNotificationCenter defaultCenter]
			addObserver:self
			   selector:@selector(onAudioInterruption:)
				   name:AVAudioSessionInterruptionNotification
				 object:[AVAudioSession sharedInstance]];

	int sessionCategorySetting = GLOBAL_GET("audio/general/ios/session_category");

	// Initialize with default Ambient category.
	AVAudioSessionCategory category = AVAudioSessionCategoryAmbient;
	AVAudioSessionCategoryOptions options = 0;

	if (GLOBAL_GET("audio/general/ios/mix_with_others")) {
		options |= AVAudioSessionCategoryOptionMixWithOthers;
	}

	if (sessionCategorySetting == SESSION_CATEGORY_MULTI_ROUTE) {
		category = AVAudioSessionCategoryMultiRoute;
	} else if (sessionCategorySetting == SESSION_CATEGORY_PLAY_AND_RECORD) {
		category = AVAudioSessionCategoryPlayAndRecord;
		options |= AVAudioSessionCategoryOptionDefaultToSpeaker;
		options |= AVAudioSessionCategoryOptionAllowBluetoothA2DP;
		options |= AVAudioSessionCategoryOptionAllowAirPlay;
	} else if (sessionCategorySetting == SESSION_CATEGORY_PLAYBACK) {
		category = AVAudioSessionCategoryPlayback;
	} else if (sessionCategorySetting == SESSION_CATEGORY_RECORD) {
		category = AVAudioSessionCategoryRecord;
	} else if (sessionCategorySetting == SESSION_CATEGORY_SOLO_AMBIENT) {
		category = AVAudioSessionCategorySoloAmbient;
	}

	[[AVAudioSession sharedInstance] setCategory:category withOptions:options error:nil];

#ifdef IOS_ENABLED
	[NSNotificationCenter.defaultCenter addObserver:self
										   selector:@selector(foregroundPresentationBegan:)
											   name:presentation_began
											 object:nil];
	[NSNotificationCenter.defaultCenter addObserver:self selector:@selector(foregroundPresentationEnded:) name:presentation_ended object:nil];
#endif

	return YES;
}

- (void)onAudioInterruption:(NSNotification *)notification {
	if ([notification.name isEqualToString:AVAudioSessionInterruptionNotification]) {
		if ([[notification.userInfo valueForKey:AVAudioSessionInterruptionTypeKey] isEqualToNumber:[NSNumber numberWithInt:AVAudioSessionInterruptionTypeBegan]]) {
			NSLog(@"Audio interruption began");
			OS_AppleEmbedded::get_singleton()->on_focus_out();
		} else if ([[notification.userInfo valueForKey:AVAudioSessionInterruptionTypeKey] isEqualToNumber:[NSNumber numberWithInt:AVAudioSessionInterruptionTypeEnded]]) {
#ifdef IOS_ENABLED
			if (foreground_presentation.is_inactive()) {
				return; // Scene activation owns the restart during a presentation.
			}
#endif
			NSLog(@"Audio interruption ended");
			OS_AppleEmbedded::get_singleton()->on_focus_in();
		}
	}
}

- (void)applicationDidReceiveMemoryWarning:(UIApplication *)application {
	if (OS::get_singleton()->get_main_loop()) {
		OS::get_singleton()->get_main_loop()->notification(MainLoop::NOTIFICATION_OS_MEMORY_WARNING);
	}
}

- (void)applicationWillTerminate:(UIApplication *)application {
#ifdef IOS_ENABLED
	[self clearForegroundPresentation];
#endif
	apple_embedded_finish();
}

// When application goes to background (e.g. user switches to another app or presses Home),
// then applicationWillResignActive -> applicationDidEnterBackground are called.
// When user opens the inactive app again,
// applicationWillEnterForeground -> applicationDidBecomeActive are called.

// There are cases when applicationWillResignActive -> applicationDidBecomeActive
// sequence is called without the app going to background. For example, that happens
// if you open the app list without switching to another app or open/close the
// notification panel by swiping from the upper part of the screen.

- (void)sceneDidDisconnect:(UIScene *)scene API_AVAILABLE(ios(13.0), tvos(13.0), visionos(1.0)) {
#ifdef IOS_ENABLED
	[self clearForegroundPresentation];
#endif
	OS_AppleEmbedded::get_singleton()->on_focus_out();
}

- (void)sceneWillResignActive:(UIScene *)scene API_AVAILABLE(ios(13.0), tvos(13.0), visionos(1.0)) {
#ifdef IOS_ENABLED
	foreground_presentation.resign_active((__bridge const void *)scene);
#endif
	OS_AppleEmbedded::get_singleton()->on_focus_out();
#ifdef IOS_ENABLED
	[self startForegroundInactiveTimerForScene:scene];
#endif
}

- (void)sceneDidBecomeActive:(UIScene *)scene API_AVAILABLE(ios(13.0), tvos(13.0), visionos(1.0)) {
#ifdef IOS_ENABLED
	BOOL was_blocking = foreground_presentation.suppresses_input();
	foreground_presentation.activate((__bridge const void *)scene);
	if (!foreground_presentation.is_inactive()) {
		[self stopForegroundInactiveTimer];
	}
	[self updateForegroundPresentationInputFrom:was_blocking];
#endif
	OS_AppleEmbedded::get_singleton()->on_focus_in();
}

- (void)sceneDidEnterBackground:(UIScene *)scene API_AVAILABLE(ios(13.0), tvos(13.0), visionos(1.0)) {
#ifdef IOS_ENABLED
	[self clearForegroundPresentation];
#endif
	OS_AppleEmbedded::get_singleton()->on_enter_background();
}

- (void)sceneWillEnterForeground:(UIScene *)scene API_AVAILABLE(ios(13.0), tvos(13.0), visionos(1.0)) {
	OS_AppleEmbedded::get_singleton()->on_exit_background();
}

- (void)applicationWillResignActive:(UIApplication *)application {
	OS_AppleEmbedded::get_singleton()->on_focus_out();
}

- (void)applicationDidBecomeActive:(UIApplication *)application {
	OS_AppleEmbedded::get_singleton()->on_focus_in();
}

- (void)applicationDidEnterBackground:(UIApplication *)application {
#ifdef IOS_ENABLED
	[self clearForegroundPresentation];
#endif
	OS_AppleEmbedded::get_singleton()->on_enter_background();
}

- (void)applicationWillEnterForeground:(UIApplication *)application {
	OS_AppleEmbedded::get_singleton()->on_exit_background();
}

- (void)dealloc {
	self.window = nil;
}

@end
