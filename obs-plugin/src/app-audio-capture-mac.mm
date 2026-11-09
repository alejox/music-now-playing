#include "app-audio-capture.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreMedia/CoreMedia.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <mutex>
#include <string>
#include <vector>

// Captura del audio de una app con ScreenCaptureKit (macOS 13+).
//
// A diferencia de la captura de OBS, pide las apps con onScreenWindowsOnly:NO: así encuentra
// una app que suena sin ventana a la vista (Chrome detrás de la app instalada de YouTube Music).
//
// Todo el estado de la captura vive en una cola serie propia: los arranques, reintentos y
// cambios de app, y también las muestras de audio que entrega ScreenCaptureKit. Lo único que se
// comparte con los hilos de OBS es la fuente, protegida con un mutex: al destruirla se suelta
// primero, así ninguna muestra tardía llega a una fuente que ya no existe.

namespace {

constexpr double kFirstRetry = 2.0; // segundos
constexpr double kMaxRetry = 30.0;
constexpr int64_t kStopTimeoutNs = 1500000000; // espera máxima al destruir la fuente

std::string utf8(NSString *string)
{
	const char *chars = string.UTF8String;
	return chars ? std::string(chars) : std::string();
}

std::string describe(NSError *error)
{
	return error ? utf8(error.localizedDescription) + " (" + std::to_string(error.code) + ")" : std::string();
}

} // namespace

API_AVAILABLE(macos(13.0))
@interface NPAppAudioCapture : NSObject <SCStreamOutput, SCStreamDelegate>
- (instancetype)initWithSource:(obs_source_t *)source;
- (void)setTarget:(NSString *)bundleId;
- (void)shutdown;
@end

@implementation NPAppAudioCapture {
	// Solo con _sourceMutex: la fuente de OBS (nullptr después de shutdown).
	std::mutex _sourceMutex;
	obs_source_t *_source;

	// Solo en _queue.
	dispatch_queue_t _queue;
	NSString *_target;
	SCStream *_stream;
	uint64_t _generation; // cambia con cada app nueva: descarta respuestas viejas
	BOOL _closed;
	BOOL _starting;
	BOOL _retryPending;
	double _retryDelay;
	BOOL _missingLogged;
	BOOL _errorLogged;
	BOOL _permissionLogged;
	BOOL _formatLogged;
}

- (instancetype)initWithSource:(obs_source_t *)source
{
	self = [super init];
	if (self) {
		_source = source;
		_queue = dispatch_queue_create("com.alejoxgaming.obs-now-playing.app-audio", DISPATCH_QUEUE_SERIAL);
		_retryDelay = kFirstRetry;
	}
	return self;
}

// Cualquier hilo: cambia la app a capturar (vacía = ninguna).
- (void)setTarget:(NSString *)bundleId
{
	NSString *target = [bundleId copy];
	dispatch_async(_queue, ^{
		[self applyTarget:target];
	});
}

- (void)applyTarget:(NSString *)target
{
	if (_closed)
		return;
	if (_target && [_target caseInsensitiveCompare:target] == NSOrderedSame && (_stream || _starting || _retryPending))
		return;
	_target = target;
	_generation++;
	[self stopStream:nil];
	_starting = NO;
	_retryPending = NO;
	_retryDelay = kFirstRetry;
	_missingLogged = NO;
	_errorLogged = NO;
	[self startCapture];
}

- (void)startCapture
{
	if (_closed || _starting || _stream || _target.length == 0)
		return;
	_starting = YES;
	const uint64_t generation = _generation;
	[SCShareableContent
		getShareableContentExcludingDesktopWindows:NO
				       onScreenWindowsOnly:NO
					 completionHandler:^(SCShareableContent *content, NSError *error) {
						 dispatch_async(self->_queue, ^{
							 [self didGetContent:content error:error generation:generation];
						 });
					 }];
}

- (void)didGetContent:(SCShareableContent *)content error:(NSError *)error generation:(uint64_t)generation
{
	if (_closed || generation != _generation)
		return;
	if (!content) {
		[self failed:@"no se pudo pedir la lista de apps" error:error];
		return;
	}

	SCRunningApplication *app = nil;
	for (SCRunningApplication *candidate in content.applications) {
		if ([candidate.bundleIdentifier caseInsensitiveCompare:_target] == NSOrderedSame) {
			app = candidate;
			break;
		}
	}
	if (!app) {
		if (!_missingLogged) {
			_missingLogged = YES;
			obs_log(LOG_INFO, "audio del canal: %s no está abierta; se reintenta", utf8(_target).c_str());
		}
		_starting = NO;
		[self scheduleRetry];
		return;
	}

	// El filtro necesita una pantalla; el audio es el de la app, esté donde esté su ventana.
	SCDisplay *display = nil;
	const CGDirectDisplayID mainDisplay = CGMainDisplayID();
	for (SCDisplay *candidate in content.displays) {
		if (candidate.displayID == mainDisplay) {
			display = candidate;
			break;
		}
	}
	if (!display)
		display = content.displays.firstObject;
	if (!display) {
		[self failed:@"no hay pantallas" error:nil];
		return;
	}

	SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:display
						     includingApplications:@[app]
							  exceptingWindows:@[]];
	SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
	config.capturesAudio = YES;
	config.excludesCurrentProcessAudio = YES;
	config.sampleRate = 48000;
	config.channelCount = 2;
	// Video mínimo: ScreenCaptureKit lo exige para entregar el audio; los cuadros se descartan.
	config.width = 2;
	config.height = 2;
	config.minimumFrameInterval = CMTimeMake(1, 1);
	config.queueDepth = 3;
	config.showsCursor = NO;

	SCStream *stream = [[SCStream alloc] initWithFilter:filter configuration:config delegate:self];
	NSError *outputError = nil;
	if (![stream addStreamOutput:self type:SCStreamOutputTypeScreen sampleHandlerQueue:_queue error:&outputError] ||
	    ![stream addStreamOutput:self type:SCStreamOutputTypeAudio sampleHandlerQueue:_queue error:&outputError]) {
		[self failed:@"no se pudo preparar la captura" error:outputError];
		return;
	}

	_stream = stream;
	[stream startCaptureWithCompletionHandler:^(NSError *startError) {
		dispatch_async(self->_queue, ^{
			[self didStart:stream error:startError generation:generation];
		});
	}];
}

- (void)didStart:(SCStream *)stream error:(NSError *)error generation:(uint64_t)generation
{
	if (_closed || generation != _generation || stream != _stream) {
		// Una captura de una app anterior que terminó de arrancar tarde.
		if (!error)
			[stream stopCaptureWithCompletionHandler:^(NSError *) {
			}];
		return;
	}
	_starting = NO;
	if (error) {
		_stream = nil;
		[self failed:@"no se pudo iniciar la captura" error:error];
		return;
	}
	_retryDelay = kFirstRetry;
	_missingLogged = NO;
	_errorLogged = NO;
	obs_log(LOG_INFO, "audio del canal: capturando %s", utf8(_target).c_str());
}

// En _queue: algo salió mal; se avisa una vez y se reintenta más tarde.
- (void)failed:(NSString *)what error:(NSError *)error
{
	_starting = NO;
	const BOOL permission = [error.domain isEqualToString:SCStreamErrorDomain] &&
				error.code == SCStreamErrorUserDeclined;
	if (permission) {
		if (!_permissionLogged) {
			_permissionLogged = YES;
			obs_log(LOG_WARNING,
				"audio del canal: macOS no da permiso de Grabación de pantalla y audio a OBS (%s); "
				"actívalo en Ajustes del Sistema → Privacidad y seguridad",
				describe(error).c_str());
		}
	} else if (!_errorLogged) {
		_errorLogged = YES;
		obs_log(LOG_WARNING, "audio del canal: %s de %s: %s; se reintenta", utf8(what).c_str(),
			utf8(_target).c_str(), describe(error).c_str());
	}
	[self scheduleRetry];
}

// En _queue: un solo reintento pendiente, cada vez más espaciado (de 2 a 30 segundos).
- (void)scheduleRetry
{
	if (_closed || _retryPending)
		return;
	_retryPending = YES;
	const uint64_t generation = _generation;
	const double delay = _retryDelay;
	_retryDelay = MIN(_retryDelay * 2.0, kMaxRetry);
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(delay * (double)NSEC_PER_SEC)), _queue, ^{
		if (generation != self->_generation)
			return;
		self->_retryPending = NO;
		[self startCapture];
	});
}

// En _queue: detiene la captura actual (si hay); done corre al terminar, en cualquier hilo.
- (void)stopStream:(void (^)(void))done
{
	SCStream *stream = _stream;
	_stream = nil;
	if (!stream) {
		if (done)
			done();
		return;
	}
	[stream stopCaptureWithCompletionHandler:^(NSError *) {
		if (done)
			done();
	}];
}

// Cualquier hilo: suelta la fuente y detiene la captura, esperando como mucho 1,5 segundos.
- (void)shutdown
{
	{
		std::lock_guard<std::mutex> lock(_sourceMutex);
		_source = nullptr;
	}
	dispatch_semaphore_t stopped = dispatch_semaphore_create(0);
	dispatch_async(_queue, ^{
		self->_closed = YES;
		self->_generation++;
		[self stopStream:^{
			dispatch_semaphore_signal(stopped);
		}];
	});
	if (dispatch_semaphore_wait(stopped, dispatch_time(DISPATCH_TIME_NOW, kStopTimeoutNs)) != 0)
		obs_log(LOG_WARNING, "audio del canal: la captura tardó en detenerse");
}

// ---- SCStreamDelegate / SCStreamOutput ----

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error
{
	dispatch_async(_queue, ^{
		if (self->_closed || stream != self->_stream)
			return;
		self->_stream = nil;
		obs_log(LOG_WARNING, "audio del canal: la captura de %s se detuvo: %s; se reintenta",
			utf8(self->_target).c_str(), describe(error).c_str());
		self->_errorLogged = YES; // ya se avisó: el reintento no vuelve a avisar
		[self scheduleRetry];
	});
}

- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type
{
	(void)stream;
	if (type != SCStreamOutputTypeAudio || !CMSampleBufferIsValid(sampleBuffer) ||
	    !CMSampleBufferDataIsReady(sampleBuffer))
		return; // los cuadros de video se descartan

	const AudioStreamBasicDescription *asbd =
		CMAudioFormatDescriptionGetStreamBasicDescription(CMSampleBufferGetFormatDescription(sampleBuffer));
	if (!asbd || asbd->mChannelsPerFrame < 1 || asbd->mChannelsPerFrame > 2 ||
	    !(asbd->mFormatFlags & kAudioFormatFlagIsFloat) || asbd->mBitsPerChannel != 32) {
		if (!_formatLogged) {
			_formatLogged = YES;
			obs_log(LOG_WARNING, "audio del canal: formato de audio no soportado; se ignora");
		}
		return;
	}

	size_t listSize = 0;
	if (CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(sampleBuffer, &listSize, nullptr, 0, nullptr,
								   nullptr, 0, nullptr) != noErr ||
	    listSize == 0)
		return;
	std::vector<uint8_t> storage(listSize);
	auto *list = reinterpret_cast<AudioBufferList *>(storage.data());
	CMBlockBufferRef block = nullptr;
	if (CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(
		    sampleBuffer, nullptr, list, listSize, kCFAllocatorDefault, kCFAllocatorDefault,
		    kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment, &block) != noErr)
		return;

	const uint32_t channels = asbd->mChannelsPerFrame;
	struct obs_source_audio audio = {};
	const bool planar = (asbd->mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0;
	if (planar) {
		for (uint32_t i = 0; i < list->mNumberBuffers && i < channels && i < MAX_AV_PLANES; i++)
			audio.data[i] = static_cast<const uint8_t *>(list->mBuffers[i].mData);
		audio.format = AUDIO_FORMAT_FLOAT_PLANAR;
	} else {
		audio.data[0] = static_cast<const uint8_t *>(list->mBuffers[0].mData);
		audio.format = AUDIO_FORMAT_FLOAT;
	}
	audio.frames = static_cast<uint32_t>(CMSampleBufferGetNumSamples(sampleBuffer));
	audio.speakers = channels == 1 ? SPEAKERS_MONO : SPEAKERS_STEREO;
	audio.samples_per_sec = static_cast<uint32_t>(asbd->mSampleRate);
	// El reloj de ScreenCaptureKit es el del sistema, el mismo de os_gettime_ns (como hace OBS).
	const CMTime pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
	audio.timestamp = CMTIME_IS_NUMERIC(pts) ? static_cast<uint64_t>(CMTimeGetSeconds(pts) * (double)NSEC_PER_SEC)
						 : os_gettime_ns();

	const bool complete = planar ? list->mNumberBuffers >= channels : list->mNumberBuffers >= 1;
	if (complete && audio.frames > 0) {
		std::lock_guard<std::mutex> lock(_sourceMutex);
		if (_source)
			obs_source_output_audio(_source, &audio);
	}
	if (block)
		CFRelease(block);
}

@end

// ---- La fuente de OBS ----

namespace {

NSString *settingsTarget(obs_data_t *settings)
{
	const char *application = obs_data_get_string(settings, "application");
	return [NSString stringWithUTF8String:application ? application : ""];
}

const char *appAudioName(void *)
{
	return obs_module_text("NowPlaying.Channel.AudioSource");
}

void *appAudioCreate(obs_data_t *settings, obs_source_t *source)
{
	if (@available(macOS 13.0, *)) {
		NPAppAudioCapture *capture = [[NPAppAudioCapture alloc] initWithSource:source];
		[capture setTarget:settingsTarget(settings)];
		return (__bridge_retained void *)capture;
	}
	return nullptr;
}

void appAudioDestroy(void *data)
{
	if (@available(macOS 13.0, *)) {
		NPAppAudioCapture *capture = (__bridge_transfer NPAppAudioCapture *)data;
		[capture shutdown];
	}
}

void appAudioUpdate(void *data, obs_data_t *settings)
{
	if (@available(macOS 13.0, *)) {
		NPAppAudioCapture *capture = (__bridge NPAppAudioCapture *)data;
		[capture setTarget:settingsTarget(settings)];
	}
}

void appAudioDefaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "application", "");
}

obs_properties_t *appAudioProperties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_properties_add_text(props, "application", obs_module_text("NowPlaying.Channel.AudioApp"), OBS_TEXT_DEFAULT);
	return props;
}

} // namespace

void register_app_audio_capture()
{
	if (@available(macOS 13.0, *)) {
		struct obs_source_info info = {};
		info.id = kAppAudioSourceId;
		info.type = OBS_SOURCE_TYPE_INPUT;
		info.output_flags = OBS_SOURCE_AUDIO | OBS_SOURCE_CAP_DISABLED;
		info.icon_type = OBS_ICON_TYPE_PROCESS_AUDIO_OUTPUT;
		info.get_name = appAudioName;
		info.create = appAudioCreate;
		info.destroy = appAudioDestroy;
		info.update = appAudioUpdate;
		info.get_defaults = appAudioDefaults;
		info.get_properties = appAudioProperties;
		obs_register_source(&info);
	} else {
		obs_log(LOG_INFO, "audio del canal: requiere macOS 13 o posterior");
	}
}

bool app_audio_is_capturable(const QString &bundleId)
{
	if (bundleId.isEmpty())
		return false;
	@autoreleasepool {
		NSString *target = bundleId.toNSString();
		for (NSRunningApplication *app in NSWorkspace.sharedWorkspace.runningApplications) {
			if (app.bundleIdentifier && [app.bundleIdentifier caseInsensitiveCompare:target] == NSOrderedSame &&
			    !app.terminated)
				return true;
		}
	}
	return false;
}
