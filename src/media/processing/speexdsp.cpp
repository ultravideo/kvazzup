#include "speexdsp.h"

// this is how many frames the audio capture seems to send

#include <QSettings>

#include "common.h"
#include "settingskeys.h"
#include "global.h"
#include "logger.h"


SpeexDSP::SpeexDSP(QAudioFormat format):
  format_(format),
  samplesPerFrame_(format.sampleRate()/AUDIO_FRAMES_PER_SECOND),
  processMutex_(),
  preprocessor_(nullptr)
{}


void SpeexDSP::updateSettings()
{
  if ( preprocessor_ != nullptr)
  {
    QSettings settings(settingsFile, settingsFileFormat);

    processMutex_.lock();

    int activeState = 1;
    int inactiveState = 0;

    if (denoise_ && settings.value(SettingsKey::audioDenoise) == 1)
    {
      speex_preprocess_ctl(preprocessor_, SPEEX_PREPROCESS_SET_DENOISE, &activeState);
    }
    else
    {
      speex_preprocess_ctl(preprocessor_, SPEEX_PREPROCESS_SET_DENOISE, &inactiveState);
    }

    if (dereverb_ && settings.value(SettingsKey::audioDereverb) == 1)
    {
      speex_preprocess_ctl(preprocessor_, SPEEX_PREPROCESS_SET_DEREVERB, &activeState);
    }
    else
    {
      speex_preprocess_ctl(preprocessor_, SPEEX_PREPROCESS_SET_DEREVERB, &inactiveState);
    }

    if (agc_ && settings.value(SettingsKey::audioAGC) == 1)
    {
      speex_preprocess_ctl(preprocessor_, SPEEX_PREPROCESS_SET_AGC, &activeState);

      if (agcLevel_ == 0)
      {
        Logger::getLogger()->printProgramWarning(this, "AGC level not set. Using 0");
      }

      speex_preprocess_ctl(preprocessor_, SPEEX_PREPROCESS_SET_AGC_LEVEL, &agcLevel_);

      int increment = 10;
      speex_preprocess_ctl(preprocessor_, SPEEX_PREPROCESS_SET_AGC_INCREMENT, &increment);
      int decrement = -40;
      speex_preprocess_ctl(preprocessor_, SPEEX_PREPROCESS_SET_AGC_DECREMENT, &decrement);

      if (agcMaxGain_ == 0)
      {
        Logger::getLogger()->printProgramWarning(this, "AGC max gain not set. Using 0");
      }

      // we set a low gain to avoid background noises from coming through during pauses
      speex_preprocess_ctl(preprocessor_, SPEEX_PREPROCESS_SET_AGC_MAX_GAIN , &agcMaxGain_);

      Logger::getLogger()->printDebug(DEBUG_NORMAL, this, "AGC has been enabled",
                 {"Level", "Increment", "Decrement"},
                 {QString::number(agcLevel_),
                  QString::number(increment), QString::number(decrement)});


      // VAD could be used to fix this AGC increment problem (background sounds
      // are too loud after a long quiet time), but it doesn't work in current version of Speex
      //speex_preprocess_ctl(preprocessor_, SPEEX_PREPROCESS_SET_VAD, &activeState);
    }
    else
    {
      speex_preprocess_ctl(preprocessor_, SPEEX_PREPROCESS_SET_AGC, &inactiveState);
    }

    processMutex_.unlock();
  }
  else
  {
    Logger::getLogger()->printProgramWarning(this, "Preprocessor state was not "
                                                   "set when updating settings");
  }
}


void SpeexDSP::init(bool agc, bool denoise, bool dereverb, int32_t agcLevel, int agcMaxGain)
{
  agc_ = agc;
  denoise_ = denoise;
  dereverb_ = dereverb;

  agcLevel_ = agcLevel;
  agcMaxGain_ = agcMaxGain;

  if (preprocessor_ != nullptr)
  {
    cleanup();
  }

  processMutex_.lock();

  if  (agc_ || denoise_ || dereverb_)
  {
    preprocessor_ = speex_preprocess_state_init(samplesPerFrame_,
                                                format_.sampleRate());
  }
  else
  {
    Logger::getLogger()->printProgramWarning(this, "Speex preprocessor has not "
                                                   "been set to do anything");
  }

  processMutex_.unlock();

  updateSettings();
}


void SpeexDSP::cleanup()
{
  processMutex_.lock();

  if (preprocessor_ != nullptr)
  {
    speex_preprocess_state_destroy(preprocessor_);
    preprocessor_ = nullptr;
  }

  processMutex_.unlock();
}


std::unique_ptr<uchar[]> SpeexDSP::processInputFrame(std::unique_ptr<uchar[]> input,
                                                     uint32_t dataSize)
{
  if (dataSize != samplesPerFrame_*format_.bytesPerFrame())
  {
    Logger::getLogger()->printProgramError(this, "Wrong size of input frame for DSP input");
    return nullptr;
  }

  processMutex_.lock();

  // Do preprocess trickery defined in init for input.
  // Preprocessor is run after echo cancellation so the tail suppression
  // takes effect.
  if(preprocessor_ != nullptr)
  {
    // The return value of run function is voice activity (if enabled). In my tests
    // it didn't work very well
    speex_preprocess_run(preprocessor_, (int16_t*)input.get());

  }
  else
  {
    Logger::getLogger()->printProgramWarning(this, "Preprocessor state not set for processing");
  }

  processMutex_.unlock();

  return input;
}

