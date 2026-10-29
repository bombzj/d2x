#include "input.hpp"
#ifdef _WIN32
#include <windows.h>
#include <imm.h>
#endif

namespace d2x {
bool nativeImeComposing(void *window) {
#ifdef _WIN32
    if(!window) return false;
    const auto handle=static_cast<HWND>(window);
    const auto context=ImmGetContext(handle);if(!context) return false;
    const bool composing=ImmGetCompositionStringW(context,GCS_COMPSTR,nullptr,0)>0;
    ImmReleaseContext(handle,context);return composing;
#else
    (void)window;return false;
#endif
}
}
