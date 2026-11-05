#pragma once
// Older Apple SDKs use a CF_ENUM extension rejected by C++26 in upstream LLVM.
// Confine that compatibility diagnostic to SDK headers; project code stays strict.
#pragma clang diagnostic push
#if __has_warning("-Welaborated-enum-base")
#pragma clang diagnostic ignored "-Welaborated-enum-base"
#endif
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#pragma clang diagnostic pop
