#pragma once
#include "native.hpp"
#include <atomic>

namespace mka_coreaudio_detail {
// Private SDK boundary; no Apple type is exported by the module facade.
struct Api {
    decltype(&AudioObjectGetPropertyDataSize) size=&AudioObjectGetPropertyDataSize;
    decltype(&AudioObjectGetPropertyData) get=&AudioObjectGetPropertyData;
    decltype(&AudioObjectSetPropertyData) set=&AudioObjectSetPropertyData;
    decltype(&AudioObjectIsPropertySettable) settable=&AudioObjectIsPropertySettable;
    decltype(&AudioDeviceCreateIOProcID) create=&AudioDeviceCreateIOProcID;
    decltype(&AudioDeviceDestroyIOProcID) destroy=&AudioDeviceDestroyIOProcID;
    decltype(&AudioDeviceStart) start=&AudioDeviceStart;
    decltype(&AudioDeviceStop) stop=&AudioDeviceStop;
    decltype(&AudioObjectAddPropertyListener) add=&AudioObjectAddPropertyListener;
    decltype(&AudioObjectRemovePropertyListener) remove=&AudioObjectRemovePropertyListener;
};
#ifdef MKA_COREAUDIO_TESTING
inline const Api* testApi=nullptr; // Changed only while no backend exists.
#endif
inline const Api& nativeApi() noexcept {
#ifdef MKA_COREAUDIO_TESTING
    if(testApi)return *testApi;
#endif
    static const Api api;
    return api;
}
inline AudioObjectPropertyAddress property(AudioObjectPropertySelector selector,
    AudioObjectPropertyScope scope=kAudioObjectPropertyScopeGlobal) noexcept {
    return {selector,scope,kAudioObjectPropertyElementMain};
}
template<class T>
bool read(const Api& api,AudioObjectID object,AudioObjectPropertySelector selector,T& value,
    AudioObjectPropertyScope scope=kAudioObjectPropertyScopeGlobal) noexcept {
    auto address=property(selector,scope);UInt32 bytes=sizeof(T);
    return api.get(object,&address,0,nullptr,&bytes,&value)==noErr && bytes==sizeof(T);
}
}
