
#ifndef audcap_h
#define audcap_h

#include "mydef.h"
#include "jointor.h"
#include "fmt.h"
#include "audioctl.h"
#include "codec_hook.h"

capi jointor* audcap_open(audio_format* iofmt, enum aec_t aec, callback_t* hook);
capi void audcap_close(jointor* join);

#endif
