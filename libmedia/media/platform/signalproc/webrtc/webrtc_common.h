/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_COMMON_H_
#define WEBRTC_COMMON_H_

#include <map>

namespace webrtc
{

// Class Config is designed to ease passing a set of options across webrtc code.
// Options are identified by typename in order to avoid incorrect casts.
//
// Usage:
// * declaring an option:
//    struct Algo1_CostFunction {
//      virtual float cost(int x) const { return x; }
//      virtual ~Algo1_CostFunction() {}
//    };
//
// * accessing an option:
//    config.Get<Algo1_CostFunction>().cost(value);
//
// * setting an option:
//    struct SqrCost : Algo1_CostFunction {
//      virtual float cost(int x) const { return x*x; }
//    };
//    config.Set<Algo1_CostFunction>(new SqrCost());
//
// Note: This class is thread-compatible (like STL containers).
    class Config
    {
    public:
        // Returns the option if set or a default constructed one.
        // Callers that access options too often are encouraged to cache the result.
        // Returned references are owned by this.
        //
        // Requires std::is_default_constructible<T>
        template<typename T> const T& Get() const;

        // Set the option, deleting any previous instance of the same.
        // This instance gets ownership of the newly set value.
        template<typename T> void Set(T* value);

        Config() {}
        ~Config() {
            // Note: this method is inline so webrtc public API depends only
            // on the headers.
            for (OptionMap::iterator it = options_.begin();
                 it != options_.end(); ++it) {
                delete it->second;
            }
        }

    private:
        typedef void* OptionIdentifier;

        struct BaseOption {
            virtual ~BaseOption() {}
        };

        template<typename T>
        struct Option : BaseOption {
            explicit Option(T* v): value(v) {}
            ~Option() {
                delete value;
            }
            T* value;
        };

        // Own implementation of rtti-subset to avoid depending on rtti and its costs.
        template<typename T>
        static OptionIdentifier identifier() {
            static char id_placeholder;
            return &id_placeholder;
        }

        // Used to instantiate a default constructed object that doesn't needs to be
        // owned. This allows Get<T> to be implemented without requiring explicitly
        // locks.
        template<typename T>
        static const T& default_value() {
            static const T def;
            return def;
        }

        typedef std::map<OptionIdentifier, BaseOption*> OptionMap;
        OptionMap options_;

        // DISALLOW_COPY_AND_ASSIGN
        Config(const Config&);
        void operator=(const Config&);
    };

    template<typename T>
    const T& Config::Get() const
    {
        OptionMap::const_iterator it = options_.find(identifier<T>());
        if (it != options_.end()) {
            const T* t = static_cast<Option<T>*>(it->second)->value;
            if (t) {
                return *t;
            }
        }
        return default_value<T>();
    }

    template<typename T>
    void Config::Set(T* value)
    {
        BaseOption*& it = options_[identifier<T>()];
        delete it;
        it = new Option<T>(value);
    }

}  // namespace webrtc

#endif  // WEBRTC_COMMON_H_
