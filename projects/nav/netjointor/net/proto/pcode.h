

#ifndef pcode_h
#define pcode_h

#define MEDIA_CLIENT_BASE 0
#define SESS_CLIENT_BASE 1

enum {
    P_COMMON_RESP_RETURN = 0,

    //media(voice)<->client (16bit)
    P_AUDIO_HEARTBEAT                       = 1,
    P_AUDIO_RESEND_REQ                      = 2,
    P_AUDIO_RTT_TEST                        = 3,

    P_AUDIO_VOICE_MIN                       = 100,
    P_AUDIO_SILK_8K                         = 100,
    P_AUDIO_SILK_16K                        = 101,
    P_AUDIO_VOICE_MAX                       = 300,

    P_VIDEO_RESEND_REQ                      = 502,
    P_VIDEO_H264_288x352                    = 600,
    P_VIDEO_H264_352x288                    = 601,
    P_VIDEO_H264_480x640                    = 602,
    P_VIDEO_H264_640x480                    = 603,
    P_VIDEO_H264_720x1080                   = 604,
    P_VIDEO_H264_1080x720                   = 605,
    P_VIDEO_H264_720x1280                   = 606,
    P_VIDEO_H264_1280x720                   = 607,
    P_VIDEO_H264_1080x1920                  = 608,
    P_VIDEO_H264_1920x1080                  = 609,
    P_VIDEO_H264_540x960                    = 610,
    P_VIDEO_H264_960x540                    = 611,

    //media(session)<->client
    P_SESS_REQ_JOIN_CHANNEL                 = ((SESS_CLIENT_BASE<<16)|1),
    P_SESS_REQ_LEAVE_CHANNEL                = ((SESS_CLIENT_BASE<<16)|2),
    P_SESS_REQ_HEARTBEAT                    = ((SESS_CLIENT_BASE<<16)|3),
    P_SESS_REPORT_USER_STATE                = ((SESS_CLIENT_BASE<<16)|4),
    P_SESS_RESP_JOIN_CHANNEL                = ((SESS_CLIENT_BASE<<16)|5),
    P_SESS_RESP_HEARTBEAT                   = ((SESS_CLIENT_BASE<<16)|6),
    P_SESS_BROADCAST_MSG                    = ((SESS_CLIENT_BASE<<16)|7),
	P_SESS_REQ_USER_LIST                    = ((SESS_CLIENT_BASE<<16)|8),
	P_SESS_RESP_USER_LIST                   = ((SESS_CLIENT_BASE<<16)|9),
};

enum {
    PROTO_RES_SUCCESS                 = 0,
    PROTO_RES_FAIL                    = 1,
    PROTO_RES_OVERLOAD                = 2,
    PROTO_RES_NO_SESSION              = 3,
    PROTO_RES_NO_USER                 = 4,
    PROTO_RES_REJECT                  = 5,
    PROTO_RES_BUSY                    = 6,
};

#endif
