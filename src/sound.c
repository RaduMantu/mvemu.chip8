#include <string.h>     /* memset        */
#include <portaudio.h>  /* portaudio API */
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265
#endif

#include "sound.h"
#include "util.h"

/******************************************************************************
 **************************** INTERNAL STRUCTURES *****************************
 ******************************************************************************/

static int32_t  pa_initialized = 0; /* was portaudio initialized already? */
static float    tone_freq;          /* buzzer tone frequency              */
static PaStream *stream = NULL;     /* output audio stream                */

/******************************************************************************
 ****************************** HELPER FUNCTIONS ******************************
 ******************************************************************************/

/* init_portaudio - initializes the portaudio library
 *  @return : 0 if everything went well
 *
 * NOTE: Pa_Initialize() prints out some output to stderr; might be jarring.
 */
static int32_t
init_portaudio(void)
{
    int32_t ans;    /* answer */

    /* initialize library */
    ans = Pa_Initialize();
    RET(ans != paNoError, -1, "unable to initialize libportaudio (%s)",
        Pa_GetErrorText(ans));

    /* mark library as initialized (for internal use) */
    pa_initialized = 1;

    return 0;
}

/******************************************************************************
 ************************** AUDIO SAMPLE GENERATORS ***************************
 ******************************************************************************/

/* sin_samplegen - sin-based audio sample generator callback
 *  @input       : input audio sample buffer (N/A)
 *  @output      : output audio sample buffer (configured as float[])
 *  @frame_count : number of samples requested
 *  @time_info   : expected output time for the first generated sample
 *  @status_flag : callback status bitfield
 *  @user_data   : user provided data (N/A)
 *
 *  @return : 0 if output generation successful
 *            certain return values can indicate that playback should stop
 *            we don't use those
 *
 * This function is used as a callback when the audio engine needs more
 * samples to consume during playback. The stream is configured such that
 * the output buffer size (aka. requested frame count) can vary based on the
 * engine's calculations for minimizing output latency. The stream is also
 * configured to use a single output channel (mono); otherwise, the output
 * buffer would have to contain tuples of N samples (consecutive in memory)
 * for each of the N channels, for every time slice.
 */
static int32_t
sin_samplegen(const void                     *input,
              void                           *output,
              uint64_t                       frame_count,
              const PaStreamCallbackTimeInfo *time_info,
              PaStreamCallbackFlags          status_flags,
              void                           *user_data)
{
    static size_t sample_num = 0;           /* current sample counter      */
    float         *out = (float *) output;  /* type cast for output buffer */

    /* generate audio samples */
    for (size_t i = 0; i < frame_count; i++)
        *out++ = sin(sample_num++ * tone_freq / Fs * 2 * M_PI);

    return 0;
}

/******************************************************************************
 ************************* PUBLIC API IMPLEMENTATION **************************
 ******************************************************************************/

/* init_audio - initializes the output audio device & sound sample generator
 *  @dev_idx    : output audio device index (see list_audio_devs())
 *  @_tone_freq : buzzer tone frequency [Hz]
 *
 *  @return : 0 if everything went well
 */
int32_t
init_audio(int32_t dev_idx, float _tone_freq)
{
    int                 ans;            /* answer                     */
    const PaDeviceInfo  *dev_info;      /* device information         */
    PaStreamParameters  stream_par;     /* audio stream configuration */

    /* save buzzer tone frequency in global private storage */
    tone_freq = _tone_freq;

    /* it's highly unlikely for this to be skipped                            *
     * we keep the check in case we might want list_audio_devs() to be called *
     * in states prior to init_audio() that do not lead to critical failures  */
    if (!pa_initialized) {
        ans = init_portaudio();
        RET(ans, -1, "unable to perform first time portuadio initialization");
    }

    /* get selected device information */
    dev_info = Pa_GetDeviceInfo(dev_idx);
    RET(!dev_info, -1, "device parameter out of range: %d", dev_idx);

    /* initialize stream parameters */
    memset(&stream_par, 0, sizeof(stream_par));
    stream_par.channelCount              = 1;
    stream_par.device                    = dev_idx;
    stream_par.hostApiSpecificStreamInfo = NULL;
    stream_par.sampleFormat              = paFloat32;
    stream_par.suggestedLatency          = dev_info->defaultLowOutputLatency;
    stream_par.hostApiSpecificStreamInfo = NULL;

    /* check if desired sample rate is supported by device        *
     * NOTE: _highly_ unlikely for Fs=44.1kHz not to be supported */
    ans = Pa_IsFormatSupported(NULL, &stream_par, Fs);
    RET(ans != paFormatIsSupported, -1, "unsupported audio format (%s)",
        Pa_GetErrorText(ans));

    /* open output stream (but don't start playback) */
    ans = Pa_OpenStream(
            &stream,                        /* output stream              */
            NULL,                           /* no input stream parameters */
            &stream_par,                    /* output stream parameters   */
            Fs,                             /* sampling rate              */
            paFramesPerBufferUnspecified,   /* variable number of samples */
            paNoFlag,                       /* no extra options           */
            sin_samplegen,                  /* audio sample generator     */
            NULL);                          /* no user data               */
    RET(ans != paNoError, -1, "unable to open audio stream (%s)",
        Pa_GetErrorText(ans));

    return 0;
}

/* terminate_audio - closes audio stream & deinitializes portaudio
 *  @return : 0 if everything went well
 */
int32_t
terminate_audio(void)
{
    int32_t ans;     /* answer */

    /* close stream only if pulseaudio was initialized via init_audio() */
    if (stream) {
        ans = Pa_CloseStream(stream);
        RET(ans != paNoError, -1, "unable to close audio stream (%s)",
            Pa_GetErrorText(ans));
    }

    /* invoke portaudio library cleanup routine */
    ans = Pa_Terminate();
    RET(ans != paNoError, -1, "unable to terminate libportaudio (%s)",
        Pa_GetErrorText(ans));

    return 0;
}

/* list_audio_devs - lists available backing output audio devices
 *  @return : 0 if everything went well
 */
int32_t
list_audio_devs(void)
{
    int32_t             ans;        /* answer                      */
    int32_t             num_devs;   /* number of available devices */
    const PaDeviceInfo  *dev_info;  /* device information          */

    /* this function is most likely called before init_audio *
     * perform portaudio initialization if never done before */
    if (!pa_initialized) {
        ans = init_portaudio();
        RET(ans, -1, "unable to perform first time portuadio initialization");
    }

    /* get number of backing audio devices */
    num_devs = Pa_GetDeviceCount();
    RET(num_devs < 0, -1, "unable to get number of audio devices");

    DEBUG("Listing output audio devices:");

    /* print relevant information about each _output_ audio device */
    for (size_t i = 0; i < num_devs; i++) {
        dev_info = Pa_GetDeviceInfo(i);
        RET(!dev_info, -1, "device parameter out of range: %lu", i);

        /* skip devices with no output channels */
        if (dev_info->maxOutputChannels == 0)
            continue;

        DEBUG("    dev_id=%-3lu | name=\"%s\"", i, dev_info->name);
    }

    return 0;
}

/* start_playback - starts playing the generated tone
 *  @return : 0 if everything went well
 *
 * Errors caused by initiating playback on an already playing stream will be
 * silently ignored. It is perfectly possible for the Sound Timer to be written
 * to while still couting down.
 */
int32_t
start_playback(void)
{
    int32_t ans;     /* answer */

    ans = Pa_StartStream(stream);
    RET(ans != paNoError && ans != paStreamIsNotStopped, -1,
        "unable to start audio playback (%s)", Pa_GetErrorText(ans));

    return 0;
}

/* stop_playback - stops playing the generated tone
 *  @return : 0 if everything went well
 *
 * Errors caused by stopping an already stopped stream will be silently ignored.
 * If starting playback has failed but the Sound Timer was still armed, just let
 * the callback do its job without being overly verbose.
 *
 * NOTE: we are in fact aborting the stream, not stopping it. This means that
 *       the playback will be interrupted immediately and all remaining samples
 *       from the output buffer will be dropped.
 */
int32_t
stop_playback(void)
{
    int32_t ans;     /* answer */

    ans = Pa_AbortStream(stream);
    RET(ans != paNoError && ans != paStreamIsStopped, -1,
        "unable to stop audio playback (%s)", Pa_GetErrorText(ans));

    return 0;
}

