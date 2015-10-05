#include "src/audio/audio.h"
#include "src/core/toxcall.h"
#include "src/core/coreav.h"
#include "src/persistence/settings.h"
#include "src/video/camerasource.h"
#include "src/video/corevideosource.h"
#include <QTimer>

#ifdef QTOX_FILTER_AUDIO
#include "src/audio/audiofilterer.h"
#endif

using namespace std;

ToxCall::ToxCall(uint32_t CallId)
    : sendAudioTimer{new QTimer}, callId{CallId},
      inactive{true}, muteMic{false}, muteVol{false}, alSource{0}
{
    sendAudioTimer->setInterval(5);
    sendAudioTimer->setSingleShot(true);

    Audio::suscribeInput();

#ifdef QTOX_FILTER_AUDIO
    if (Settings::getInstance().getFilterAudio())
    {
        filterer = new AudioFilterer();
        filterer->startFilter(AUDIO_SAMPLE_RATE);
    }
    else
    {
        filterer = nullptr;
    }
#endif
}

ToxCall::ToxCall(ToxCall&& other)
    : sendAudioTimer{other.sendAudioTimer}, callId{other.callId},
      inactive{other.inactive}, muteMic{other.muteMic}, muteVol{other.muteVol},
      alSource{other.alSource}
{
    other.sendAudioTimer = nullptr;
    other.callId = numeric_limits<decltype(callId)>::max();
    other.alSource = 0;

#ifdef QTOX_FILTER_AUDIO
    filterer = other.filterer;
    other.filterer = nullptr;
#endif
}

ToxCall::~ToxCall()
{
    if (sendAudioTimer)
    {
        QObject::disconnect(sendAudioTimer, nullptr, nullptr, nullptr);
        sendAudioTimer->stop();
        Audio::unsuscribeInput();
    }

    if (alSource)
        Audio::deleteSource(&alSource);

#ifdef QTOX_FILTER_AUDIO
    if (filterer)
        delete filterer;
#endif
}

const ToxCall& ToxCall::operator=(ToxCall&& other)
{
    sendAudioTimer = other.sendAudioTimer;
    other.sendAudioTimer = nullptr;
    callId = other.callId;
    other.callId = numeric_limits<decltype(callId)>::max();
    inactive = other.inactive;
    muteMic = other.muteMic;
    muteVol = other.muteVol;
    alSource = other.alSource;
    other.alSource = 0;

    #ifdef QTOX_FILTER_AUDIO
        filterer = other.filterer;
        other.filterer = nullptr;
    #endif

    return *this;
}

ToxFriendCall::ToxFriendCall(uint32_t FriendNum, bool VideoEnabled, CoreAV& av)
    : ToxCall(FriendNum),
      videoEnabled{VideoEnabled}, videoSource{nullptr},
      state{static_cast<TOXAV_FRIEND_CALL_STATE>(0)}
{
    auto audioTimerCopy = sendAudioTimer; // this might change after a move, but not sendAudioTimer
    QObject::connect(sendAudioTimer, &QTimer::timeout, [FriendNum,&av,audioTimerCopy]()
    {
        // If sendCallAudio returns false, there was a serious error and we might as well stop the timer
        if (av.sendCallAudio(FriendNum))
            audioTimerCopy->start();
    });
    sendAudioTimer->start();

    if (videoEnabled)
    {
        videoSource = new CoreVideoSource;
        CameraSource& source = CameraSource::getInstance();
        if (!source.isOpen())
            source.open();
        source.subscribe();
        QObject::connect(&source, &VideoSource::frameAvailable,
                [FriendNum,&av](shared_ptr<VideoFrame> frame){av.sendCallVideo(FriendNum,frame);});
    }
}

ToxFriendCall::ToxFriendCall(ToxFriendCall&& other)
    : ToxCall(move(other)),
      videoEnabled{other.videoEnabled}, videoSource{other.videoSource},
      state{other.state}
{
    other.videoEnabled = false;
    other.videoSource = nullptr;
}

ToxFriendCall::~ToxFriendCall()
{
    if (videoEnabled)
    {
        CameraSource::getInstance().unsubscribe();
        if (videoSource)
        {
            videoSource->setDeleteOnClose(true);
            videoSource = nullptr;
        }
    }
}

const ToxFriendCall& ToxFriendCall::operator=(ToxFriendCall&& other)
{
    ToxCall::operator =(move(other));
    videoEnabled = other.videoEnabled;
    other.videoEnabled = false;
    videoSource = other.videoSource;
    other.videoSource = nullptr;
    state = other.state;

    return *this;
}

ToxGroupCall::ToxGroupCall(int GroupNum, CoreAV &av)
    : ToxCall(static_cast<decltype(callId)>(GroupNum))
{
    static_assert(numeric_limits<decltype(callId)>::max() >= numeric_limits<decltype(GroupNum)>::max(),
                  "The callId must be able to represent any group number, change its type if needed");

    auto audioTimerCopy = sendAudioTimer; // this might change after a move, but not sendAudioTimer
    QObject::connect(sendAudioTimer, &QTimer::timeout, [GroupNum,&av,audioTimerCopy]()
    {
        // If sendGroupCallAudio returns false, there was a serious error and we might as well stop the timer
        if (av.sendGroupCallAudio(GroupNum))
            audioTimerCopy->start();
    });
    sendAudioTimer->start();
}

ToxGroupCall::ToxGroupCall(ToxGroupCall&& other)
    : ToxCall(move(other))
{
}

const ToxGroupCall &ToxGroupCall::operator=(ToxGroupCall &&other)
{
    ToxCall::operator =(move(other));

    return *this;
}