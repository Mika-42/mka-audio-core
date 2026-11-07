#include <windows.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>

// MinGW's KS headers declare these GUIDs without a matching libuuid definition.
// Use the SDK initializers in one WASAPI translation unit. Coalescible symbols
// also allow linking against a toolchain that already provides the definitions.
EXTERN_C const GUID DECLSPEC_SELECTANY KSDATAFORMAT_SUBTYPE_PCM = {
    STATIC_KSDATAFORMAT_SUBTYPE_PCM
};
EXTERN_C const GUID DECLSPEC_SELECTANY KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = {
    STATIC_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
};
