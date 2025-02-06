
#include "session.h"
#include "ios.h"

OSStatus input_push(void*                          inRefCon,
                    AudioUnitRenderActionFlags*    ioActionFlags,
                    const AudioTimeStamp*          inTimeStamp,
                    UInt32                         inBusNumber,
                    UInt32                         inNumberFrames,
                    AudioBufferList*               ioData);

OSStatus output_pull(void*                          inRefCon,
                     AudioUnitRenderActionFlags*    ioActionFlags,
                     const AudioTimeStamp*          inTimeStamp,
                     UInt32                         inBusNumber,
                     UInt32                         inNumberFrames,
                     AudioBufferList*               ioData);

AudioComponentInstance new_audio_unit(OSType type)
{
    OSStatus status;

    AudioComponentDescription desc;
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = type;
    desc.componentFlags = 0;
    desc.componentFlagsMask = 0;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;

    AudioComponent inputComponent = AudioComponentFindNext(NULL, &desc);
    if(inputComponent == NULL){
        return NULL;
    }

    AudioComponentInstance unit;
    status = AudioComponentInstanceNew(inputComponent, &unit);
    if(status != 0){
        return NULL;
    }
    return unit;
}

void destroy_audio_unit_internal(AudioComponentInstance audioUnit)
{
    OSStatus status;
    if (audioUnit == session.unit.unit_io) {
        if (!lazy_init_voicep) {
            status = AudioUnitUninitialize(audioUnit);
            check_status(status, (void) 0);
        }
    } else if (!lazy_init_remote) {
        status = AudioUnitUninitialize(audioUnit);
        check_status(status, (void) 0);
    }

    status = AudioComponentInstanceDispose(audioUnit);
    check_status(status, (void) 0);
}

AudioComponentInstance create_unit_internal(AudioStreamBasicDescription* in_fmt, AudioStreamBasicDescription* out_fmt)
{
    OSStatus status;
    OSType type = kAudioUnitSubType_RemoteIO;
    if (in_fmt != NULL && out_fmt != NULL) {
        type = kAudioUnitSubType_VoiceProcessingIO;
    }

    AudioComponentInstance unit = new_audio_unit(type);
    if(unit == NULL){
        return NULL;
    }

    if (in_fmt != NULL) {
        status = AudioUnitSetProperty(unit,
                                      kAudioUnitProperty_StreamFormat,
                                      kAudioUnitScope_Output,
                                      1,
                                      in_fmt,
                                      sizeof(AudioStreamBasicDescription));
        check_status(status, goto LABEL);

        UInt32 flag = 1;
        status = AudioUnitSetProperty(unit,
                                      kAudioOutputUnitProperty_EnableIO,
                                      kAudioUnitScope_Input,
                                      1,
                                      &flag,
                                      sizeof(UInt32));
        check_status(status, goto LABEL);

        flag = 0;
        status = AudioUnitSetProperty(unit,
                                      kAudioUnitProperty_ShouldAllocateBuffer,
                                      kAudioUnitScope_Output,
                                      1,
                                      &flag,
                                      sizeof(flag));
        check_status(status, goto LABEL);

        AURenderCallbackStruct callbackStruct;
        callbackStruct.inputProc = input_push;
        callbackStruct.inputProcRefCon = unit;
        status = AudioUnitSetProperty(unit,
                                      kAudioOutputUnitProperty_SetInputCallback,
                                      kAudioUnitScope_Global,
                                      1,
                                      &callbackStruct,
                                      sizeof(callbackStruct));
        check_status(status, goto LABEL);
    }

#if 0
    if (in_fmt != NULL && out_fmt != NULL) {
        UInt32 val = 64;
        status = AudioUnitSetProperty(unit,
                                      kAUVoiceIOProperty_VoiceProcessingQuality,
                                      kAudioUnitScope_Global,
                                      1,
                                      &val,
                                      sizeof(UInt32));
        check_status(status, goto LABEL);
    }
#endif

    UInt32 flag = 0;
    if (out_fmt != NULL) {
        status = AudioUnitSetProperty(unit,
                                      kAudioUnitProperty_StreamFormat,
                                      kAudioUnitScope_Input,
                                      0,
                                      out_fmt,
                                      sizeof(AudioStreamBasicDescription));
        check_status(status, goto LABEL);

        AURenderCallbackStruct callbackStruct;
        callbackStruct.inputProc = output_pull;
        callbackStruct.inputProcRefCon = unit;
        status = AudioUnitSetProperty(unit,
                                      kAudioUnitProperty_SetRenderCallback,
                                      kAudioUnitScope_Input,
                                      0,
                                      &callbackStruct,
                                      sizeof(callbackStruct));
        check_status(status, goto LABEL);
        flag = 1;
    }

    status = AudioUnitSetProperty(unit,
                                  kAudioOutputUnitProperty_EnableIO,
                                  kAudioUnitScope_Output,
                                  0,
                                  &flag,
                                  sizeof(UInt32));
    check_status(status, goto LABEL);

    if (in_fmt == NULL || out_fmt == NULL) {
        if (!lazy_init_remote) {
            status = AudioUnitInitialize(unit);
            check_status(status, goto LABEL);
        }
    } else if (!lazy_init_voicep) {
        status = AudioUnitInitialize(unit);
        check_status(status, goto LABEL);
    }

    return unit;
LABEL:
    AudioComponentInstanceDispose(unit);
    return NULL;
}

int hardware_latency()
{
    static int ms = 0;

    if (ms == 0) {
        Float32 sec1 , sec2 = (Float32) 0.015;
        UInt32 size = sizeof(Float32);
        OSStatus status = AudioSessionGetProperty(
                            kAudioSessionProperty_CurrentHardwareOutputLatency,
                            &size, &sec2);
        if (status == 0) {
            status = AudioSessionGetProperty(
                        kAudioSessionProperty_CurrentHardwareIOBufferDuration,
                        &size, &sec1);
            if (status == 0) {
                sec2 += sec1;
            }
        }
        ms = sec2 * 1000;
    }
    return ms;
}
