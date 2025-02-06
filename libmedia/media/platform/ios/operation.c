
#include "session.h"
#include "lock.h"
#include "my_errno.h"

// manslaughter returns only when called by user
// on_interrupt will kill both, so no manslaughter
// user can determine which one is killed by fault
// because he know which one his intents to kill.
int audio_control(int cmd, int record_or_play, enum aec_t aec)
{
    int category_play = !!(record_or_play & play);
    int category_record = !!(record_or_play & record);
    if (cmd == runing || cmd == resume) {
        if (category_play && category_record) {
            errno = EINVAL;
            return -1;
        }
    }

    int n = 0;
    void* killed = not_killed;
    relock(&session.lck);

    if (category_play) {
        int composite = (input_stat == runing);
        if (composite && !force_vpio) {
            composite = (aec_type == ios);
        }

        if (cmd == runing) {
            if (session.state[0].interrupted == 1) {
                n = -1;
                errno = EACCES;
            } else {
                if (force_vpio) {
                    n = output_start_vpio(composite);
                } else {
                    n = output_start(composite);
                }

                if (n == 0) {
                    output_stat = runing;
                }
            }
        } else if (cmd == resume) {
            if (output_stat == paused) {
                if (force_vpio) {
                    n = output_start_vpio(composite);
                } else {
                    n = output_start(composite);
                }

                if (n == 0) {
                    output_stat = runing;
                }
            } else {
                // audio_start_output give this word
                my_assert(output_stat == stopped);
            }
        } else {
            if (output_stat == runing) {
                if (force_vpio) {
                    if(-1 == output_stop_vpio(composite)) {
                        input_stat = stopped;
                        killed = in_killed;
                    }

                    if (!composite && toggle_cate) {
                        toggle_category();
                    }
                } else {
                    output_stop(composite);
                    if (toggle_cate) {
                        toggle_category();
                    }

                    if (composite && !category_record) {
                        if (-1 == input_start(0)) {
                            input_stat = stopped;
                            killed = in_killed;
                        }
                    }
                }
                output_stat = cmd;
            } else {
                // on_interrupt give the word no double pause
                output_stat = stopped;
            }
        }
    }

    if (category_record) {
        int composite = (output_stat == runing);
        if (composite && !force_vpio) {
            if (cmd == runing) {
                composite = (aec == ios);
            } else {
                composite = (aec_type == ios);
            }
        }

        if (cmd == runing) {
            if (session.state[0].interrupted == 1) {
                n = -1;
                errno = EACCES;
            } else if (session.state[0].microphone == 0) {
                n = -1;
                errno = ENODEV;
            } else {
                if (force_vpio) {
                    n = input_start_vpio(composite);
                } else {
                    n = input_start(composite);
                }

                if (n == 0) {
                    input_stat = runing;
                    session.unit.aec = aec;
                }
            }
        } else {
            if (input_stat == runing) {
                if (force_vpio) {
                    if (-1 == input_stop_vpio(composite)) {
                        output_stat = stopped;
                        killed = out_killed;
                    }

                    if (!composite && toggle_cate) {
                        toggle_category();
                    }
                } else {
                    input_stop(composite);
                    if (toggle_cate) {
                        toggle_category();
                    }

                    if (composite) {
                        if (-1 == output_start(0)) {
                            output_stat = stopped;
                            killed = out_killed;
                        }
                    }
                }
                input_stat = stopped;
                session.unit.aec = none;
            }
        }
    }

    unlock(&session.lck);
    if (killed != not_killed) {
        audio_notify(killed);
    }
    return n;
}
