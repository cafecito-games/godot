/**************************************************************************/
/*  apple_embedded.mm                                                     */
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

#import "apple_embedded.h"

#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#import "drivers/apple_embedded/app_delegate_service.h"
#import "drivers/apple_embedded/godot_view_controller.h"

#import <CoreHaptics/CoreHaptics.h>
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#include <sys/sysctl.h>

static constexpr float TRANSIENT_DURATION_THRESHOLD_SECONDS = 0.030f;
static constexpr float MAX_CONTINUOUS_DURATION_SECONDS = 30.0f;

API_AVAILABLE(ios(13))
@interface GDTHapticController : NSObject {
	NSLock *_operationLock;
	NSLock *_stateLock;
	CHHapticEngine *_hapticEngine;
	id<CHHapticPatternPlayer> _activePlayer;
	uint64_t _engineIdentifier;
	uint64_t _runGeneration;
	BOOL _engineRunning;
	BOOL _engineNeedsStop;
}

- (void)vibrateWithDurationSeconds:(float)durationSeconds amplitude:(float)amplitude;
- (void)startEngine;
- (void)stopEngine;
- (void)stopActivePlayer;

@end

@implementation GDTHapticController

- (instancetype)init {
	self = [super init];
	if (self != nil) {
		_operationLock = [[NSLock alloc] init];
		_stateLock = [[NSLock alloc] init];
		_hapticEngine = nil;
		_activePlayer = nil;
		_engineIdentifier = 0;
		_runGeneration = 0;
		_engineRunning = NO;
		_engineNeedsStop = NO;
	}
	return self;
}

- (void)configureHandlersForEngine:(CHHapticEngine *)engine engineIdentifier:(uint64_t)engineIdentifier runGeneration:(uint64_t)runGeneration {
	// Handlers only touch _stateLock and never _operationLock or CoreHaptics APIs,
	// preventing deadlocks whether CoreHaptics dispatches callbacks synchronously or asynchronously.
	// Binding each block to runGeneration prevents a delayed idle-stop or reset callback from an
	// earlier run from clearing a player started after a subsequent startAndReturnError: restart.
	__weak GDTHapticController *weak_self = self;
	engine.stoppedHandler = ^(CHHapticEngineStoppedReason reason) {
		GDTHapticController *strong_self = weak_self;
		if (strong_self == nil) {
			return;
		}

		id<CHHapticPatternPlayer> cleared_player = nil;
		CHHapticEngine *discarded_engine = nil;
		BOOL engine_unrecoverable = (reason == CHHapticEngineStoppedReasonSystemError || reason == CHHapticEngineStoppedReasonEngineDestroyed);

		[strong_self->_stateLock lock];
		if (strong_self->_engineIdentifier == engineIdentifier) {
			if (engine_unrecoverable) {
				strong_self->_engineRunning = NO;
				strong_self->_engineNeedsStop = NO;
				cleared_player = strong_self->_activePlayer;
				strong_self->_activePlayer = nil;
				discarded_engine = strong_self->_hapticEngine;
				strong_self->_hapticEngine = nil;
				strong_self->_engineIdentifier++;
				strong_self->_runGeneration++;
			} else if (strong_self->_runGeneration == runGeneration) {
				strong_self->_engineRunning = NO;
				cleared_player = strong_self->_activePlayer;
				strong_self->_activePlayer = nil;
				strong_self->_runGeneration++;
			}
		}
		[strong_self->_stateLock unlock];

		(void)cleared_player;
		(void)discarded_engine;
	};

	engine.resetHandler = ^{
		GDTHapticController *strong_self = weak_self;
		if (strong_self == nil) {
			return;
		}

		id<CHHapticPatternPlayer> cleared_player = nil;
		[strong_self->_stateLock lock];
		if (strong_self->_engineIdentifier == engineIdentifier && strong_self->_runGeneration == runGeneration) {
			strong_self->_engineRunning = NO;
			cleared_player = strong_self->_activePlayer;
			strong_self->_activePlayer = nil;
			strong_self->_runGeneration++;
		}
		[strong_self->_stateLock unlock];

		(void)cleared_player;
	};
}

- (CHHapticEngine *)ensureEngineLockedWithIdentifier:(uint64_t *)outIdentifier {
	CHHapticEngine *existing_engine = nil;
	uint64_t existing_identifier = 0;
	[_stateLock lock];
	existing_engine = _hapticEngine;
	existing_identifier = _engineIdentifier;
	[_stateLock unlock];

	if (existing_engine != nil) {
		*outIdentifier = existing_identifier;
		return existing_engine;
	}

	NSError *init_error = nil;
	CHHapticEngine *created_engine = [[CHHapticEngine alloc] initAndReturnError:&init_error];
	if (created_engine == nil || init_error != nil) {
		if (init_error != nil) {
			NSLog(@"Could not initialize haptic engine: %@", init_error);
		}
		return nil;
	}

	[created_engine setAutoShutdownEnabled:YES];

	uint64_t created_identifier = 0;
	[_stateLock lock];
	_engineIdentifier++;
	created_identifier = _engineIdentifier;
	_runGeneration++;
	_hapticEngine = created_engine;
	_activePlayer = nil;
	_engineRunning = NO;
	_engineNeedsStop = NO;
	[_stateLock unlock];

	*outIdentifier = created_identifier;
	return created_engine;
}

- (void)discardEngine:(CHHapticEngine *)engine identifier:(uint64_t)engineIdentifier stopEngine:(BOOL)stopEngine {
	id<CHHapticPatternPlayer> cleared_player = nil;
	CHHapticEngine *discarded_engine = nil;
	BOOL should_stop_discarded_engine = NO;

	[_stateLock lock];
	if (_engineIdentifier == engineIdentifier && _hapticEngine == engine) {
		should_stop_discarded_engine = stopEngine && (_engineRunning || _engineNeedsStop);
		_engineRunning = NO;
		_engineNeedsStop = NO;
		cleared_player = _activePlayer;
		_activePlayer = nil;
		discarded_engine = _hapticEngine;
		_hapticEngine = nil;
		_engineIdentifier++;
		_runGeneration++;
	}
	[_stateLock unlock];

	if (cleared_player != nil) {
		NSError *player_stop_error = nil;
		[cleared_player stopAtTime:CHHapticTimeImmediate error:&player_stop_error];
		if (player_stop_error != nil && player_stop_error.code != CHHapticErrorCodeEngineNotRunning) {
			NSLog(@"Could not stop active haptic player: %@", player_stop_error);
		}
	}

	if (should_stop_discarded_engine && discarded_engine != nil) {
		[discarded_engine stopWithCompletionHandler:nil];
	}
}

- (BOOL)ensureEngineStartedLocked:(CHHapticEngine *)engine identifier:(uint64_t)engineIdentifier allowRetry:(BOOL)allowRetry outRunGeneration:(uint64_t *)outRunGeneration {
	BOOL already_running = NO;
	BOOL engine_is_current = NO;
	uint64_t started_run_generation = 0;

	[_stateLock lock];
	if (_engineIdentifier == engineIdentifier && _hapticEngine == engine) {
		engine_is_current = YES;
		if (_engineRunning) {
			already_running = YES;
			started_run_generation = _runGeneration;
		} else {
			_runGeneration++;
			started_run_generation = _runGeneration;
		}
	}
	[_stateLock unlock];

	if (!engine_is_current) {
		return NO;
	}

	if (already_running) {
		if (outRunGeneration != nullptr) {
			*outRunGeneration = started_run_generation;
		}
		return YES;
	}

	[self configureHandlersForEngine:engine engineIdentifier:engineIdentifier runGeneration:started_run_generation];

	NSError *start_error = nil;
	BOOL started = [engine startAndReturnError:&start_error];
	if (!started || start_error != nil) {
		[self discardEngine:engine identifier:engineIdentifier stopEngine:NO];
		if (start_error != nil && !allowRetry) {
			NSLog(@"Could not start haptic engine: %@", start_error);
		}
		return NO;
	}

	BOOL still_current = NO;
	[_stateLock lock];
	if (_engineIdentifier == engineIdentifier && _hapticEngine == engine && _runGeneration == started_run_generation) {
		_engineRunning = YES;
		_engineNeedsStop = YES;
		still_current = YES;
	}
	[_stateLock unlock];

	if (!still_current) {
		[self discardEngine:engine identifier:engineIdentifier stopEngine:NO];
		[engine stopWithCompletionHandler:nil];
		return NO;
	}

	if (outRunGeneration != nullptr) {
		*outRunGeneration = started_run_generation;
	}
	return YES;
}

- (void)stopActivePlayerLocked {
	id<CHHapticPatternPlayer> previous_player = nil;
	[_stateLock lock];
	previous_player = _activePlayer;
	_activePlayer = nil;
	[_stateLock unlock];

	if (previous_player != nil) {
		NSError *stop_error = nil;
		[previous_player stopAtTime:CHHapticTimeImmediate error:&stop_error];
		if (stop_error != nil && stop_error.code != CHHapticErrorCodeEngineNotRunning) {
			NSLog(@"Could not stop active haptic player: %@", stop_error);
		}
	}
}

- (void)vibrateLockedWithDurationSeconds:(float)durationSeconds amplitude:(float)amplitude {
	[self stopActivePlayerLocked];

	NSMutableDictionary *event_dictionary = [NSMutableDictionary dictionaryWithCapacity:4];
	event_dictionary[CHHapticPatternKeyTime] = @(CHHapticTimeImmediate);
	if (durationSeconds <= TRANSIENT_DURATION_THRESHOLD_SECONDS) {
		event_dictionary[CHHapticPatternKeyEventType] = CHHapticEventTypeHapticTransient;
	} else {
		float clamped_duration_seconds = MIN(durationSeconds, MAX_CONTINUOUS_DURATION_SECONDS);
		event_dictionary[CHHapticPatternKeyEventType] = CHHapticEventTypeHapticContinuous;
		event_dictionary[CHHapticPatternKeyEventDuration] = @(clamped_duration_seconds);
	}

	if (amplitude > 0.0f) {
		float clamped_amplitude = CLAMP(amplitude, 0.0f, 1.0f);
		event_dictionary[CHHapticPatternKeyEventParameters] = @[
			@{
				CHHapticPatternKeyParameterID : CHHapticEventParameterIDHapticIntensity,
				CHHapticPatternKeyParameterValue : @(clamped_amplitude),
			},
		];
	}

	NSDictionary *pattern_dictionary = @{
		CHHapticPatternKeyPattern : @[
			@{
				CHHapticPatternKeyEvent : event_dictionary,
			},
		],
	};

	NSError *pattern_error = nil;
	CHHapticPattern *pattern = [[CHHapticPattern alloc] initWithDictionary:pattern_dictionary error:&pattern_error];
	if (pattern == nil || pattern_error != nil) {
		if (pattern_error != nil) {
			NSLog(@"Could not create haptic pattern: %@", pattern_error);
		}
		return;
	}

	for (int attempt = 0; attempt < 2; attempt++) {
		BOOL allow_retry = (attempt == 0);
		uint64_t engine_identifier = 0;
		CHHapticEngine *engine = [self ensureEngineLockedWithIdentifier:&engine_identifier];
		if (engine == nil) {
			return;
		}

		uint64_t active_run_generation = 0;
		if (![self ensureEngineStartedLocked:engine identifier:engine_identifier allowRetry:allow_retry outRunGeneration:&active_run_generation]) {
			if (allow_retry) {
				[self discardEngine:engine identifier:engine_identifier stopEngine:NO];
				continue;
			}
			return;
		}

		NSError *player_error = nil;
		id<CHHapticPatternPlayer> player = [engine createPlayerWithPattern:pattern error:&player_error];
		if (player == nil || player_error != nil) {
			if (allow_retry) {
				[self discardEngine:engine identifier:engine_identifier stopEngine:YES];
				continue;
			}
			if (player_error != nil) {
				NSLog(@"Could not create haptic pattern player: %@", player_error);
			}
			return;
		}

		NSError *start_error = nil;
		BOOL started = [player startAtTime:CHHapticTimeImmediate error:&start_error];
		if (started && start_error == nil) {
			id<CHHapticPatternPlayer> displaced_player = nil;
			BOOL keep_player = NO;
			[_stateLock lock];
			if (_engineIdentifier == engine_identifier && _hapticEngine == engine && _runGeneration == active_run_generation) {
				displaced_player = _activePlayer;
				_activePlayer = player;
				keep_player = YES;
			}
			[_stateLock unlock];
			(void)displaced_player;

			if (!keep_player) {
				NSError *cleanup_stop_error = nil;
				[player stopAtTime:CHHapticTimeImmediate error:&cleanup_stop_error];
				if (cleanup_stop_error != nil && cleanup_stop_error.code != CHHapticErrorCodeEngineNotRunning) {
					NSLog(@"Could not stop active haptic player: %@", cleanup_stop_error);
				}
				if (allow_retry) {
					[self discardEngine:engine identifier:engine_identifier stopEngine:YES];
					continue;
				}
			}
			return;
		}

		if (allow_retry) {
			[self discardEngine:engine identifier:engine_identifier stopEngine:YES];
			continue;
		}

		if (start_error != nil) {
			NSLog(@"Could not start haptic pattern player: %@", start_error);
		}
	}
}

- (void)vibrateWithDurationSeconds:(float)durationSeconds amplitude:(float)amplitude {
	[_operationLock lock];
	[self vibrateLockedWithDurationSeconds:durationSeconds amplitude:amplitude];
	[_operationLock unlock];
}

- (void)startEngine {
	[_operationLock lock];
	for (int attempt = 0; attempt < 2; attempt++) {
		BOOL allow_retry = (attempt == 0);
		uint64_t engine_identifier = 0;
		CHHapticEngine *engine = [self ensureEngineLockedWithIdentifier:&engine_identifier];
		if (engine == nil) {
			break;
		}
		if ([self ensureEngineStartedLocked:engine identifier:engine_identifier allowRetry:allow_retry outRunGeneration:nullptr]) {
			break;
		}
		if (allow_retry) {
			[self discardEngine:engine identifier:engine_identifier stopEngine:NO];
		}
	}
	[_operationLock unlock];
}

- (void)stopEngine {
	[_operationLock lock];
	id<CHHapticPatternPlayer> player_to_stop = nil;
	CHHapticEngine *engine_to_stop = nil;
	BOOL should_stop_engine = NO;

	[_stateLock lock];
	player_to_stop = _activePlayer;
	_activePlayer = nil;
	engine_to_stop = _hapticEngine;
	_hapticEngine = nil;
	should_stop_engine = (engine_to_stop != nil) && (_engineRunning || _engineNeedsStop);
	_engineRunning = NO;
	_engineNeedsStop = NO;
	if (engine_to_stop != nil || player_to_stop != nil) {
		_engineIdentifier++;
		_runGeneration++;
	}
	[_stateLock unlock];

	if (player_to_stop != nil) {
		NSError *player_stop_error = nil;
		[player_to_stop stopAtTime:CHHapticTimeImmediate error:&player_stop_error];
		if (player_stop_error != nil && player_stop_error.code != CHHapticErrorCodeEngineNotRunning) {
			NSLog(@"Could not stop active haptic player: %@", player_stop_error);
		}
	}

	if (should_stop_engine && engine_to_stop != nil) {
		[engine_to_stop stopWithCompletionHandler:^(NSError *returned_error) {
			if (returned_error != nil && returned_error.code != CHHapticErrorCodeEngineNotRunning) {
				NSLog(@"Could not stop haptic engine: %@", returned_error);
			}
		}];
	}
	[_operationLock unlock];
}

- (void)stopActivePlayer {
	[_operationLock lock];
	[self stopActivePlayerLocked];
	[_operationLock unlock];
}

@end

void AppleEmbedded::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_rate_url", "app_id"), &AppleEmbedded::get_rate_url);
	ClassDB::bind_method(D_METHOD("supports_haptic_engine"), &AppleEmbedded::supports_haptic_engine);
	ClassDB::bind_method(D_METHOD("start_haptic_engine"), &AppleEmbedded::start_haptic_engine);
	ClassDB::bind_method(D_METHOD("stop_haptic_engine"), &AppleEmbedded::stop_haptic_engine);
}

bool AppleEmbedded::supports_haptic_engine() {
	if (@available(iOS 13, *)) {
		id<CHHapticDeviceCapability> capabilities = [CHHapticEngine capabilitiesForHardware];
		return capabilities.supportsHaptics;
	}

	return false;
}

void AppleEmbedded::vibrate_haptic_engine(float p_duration_seconds, float p_amplitude) {
	if (p_duration_seconds <= 0.0f || !Math::is_finite(p_duration_seconds) || !Math::is_finite(p_amplitude) || p_amplitude == 0.0f) {
		if (@available(iOS 13, *)) {
			if (haptic_controller != nullptr) {
				[haptic_controller stopActivePlayer];
			}
		}
		return;
	}

	if (@available(iOS 13, *)) { // We need the @available check every time to make the compiler happy...
		if (supports_haptic_engine()) {
			if (p_amplitude > 0.0f) {
				p_amplitude = CLAMP(p_amplitude, 0.0f, 1.0f);
			}
			if (haptic_controller != nullptr) {
				[haptic_controller vibrateWithDurationSeconds:p_duration_seconds amplitude:p_amplitude];
			}
			return;
		}
	}

	NSLog(@"Haptic engine is not supported");
}

void AppleEmbedded::start_haptic_engine() {
	if (@available(iOS 13, *)) {
		if (supports_haptic_engine()) {
			if (haptic_controller != nullptr) {
				[haptic_controller startEngine];
			}
			return;
		}
	}

	NSLog(@"Haptic engine is not supported");
}

void AppleEmbedded::stop_haptic_engine() {
	if (@available(iOS 13, *)) {
		if (haptic_controller != nullptr) {
			[haptic_controller stopEngine];
		}
	}
}

void AppleEmbedded::alert(const char *p_alert, const char *p_title) {
	NSString *title = [NSString stringWithUTF8String:p_title];
	NSString *message = [NSString stringWithUTF8String:p_alert];

	UIAlertController *alert = [UIAlertController alertControllerWithTitle:title message:message preferredStyle:UIAlertControllerStyleAlert];
	UIAlertAction *button = [UIAlertAction actionWithTitle:@"OK"
													 style:UIAlertActionStyleCancel
												   handler:^(id){
												   }];

	[alert addAction:button];

	[GDTAppDelegateService.viewController presentViewController:alert animated:YES completion:nil];
}

String AppleEmbedded::get_model() const {
	// [[UIDevice currentDevice] model] only returns "iPad" or "iPhone".
	size_t size;
	sysctlbyname("hw.machine", nullptr, &size, nullptr, 0);
	char *model = (char *)malloc(size);
	if (model == nullptr) {
		return "";
	}
	sysctlbyname("hw.machine", model, &size, nullptr, 0);
	NSString *platform = [NSString stringWithCString:model encoding:NSUTF8StringEncoding];
	free(model);
	const char *str = [platform UTF8String];
	return String::utf8(str != nullptr ? str : "");
}

String AppleEmbedded::get_rate_url(int p_app_id) const {
	String app_url_path = "itms-apps://itunes.apple.com/app/idAPP_ID";

	String ret = app_url_path.replace("APP_ID", String::num_int64(p_app_id));

	print_verbose(vformat("Returning rate url %s", ret));
	return ret;
}

AppleEmbedded::AppleEmbedded() {
	if (@available(iOS 13, *)) {
		haptic_controller = [[GDTHapticController alloc] init];
	}
}

AppleEmbedded::~AppleEmbedded() {
	stop_haptic_engine();
}
