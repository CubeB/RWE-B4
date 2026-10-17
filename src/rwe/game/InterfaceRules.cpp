#include "InterfaceRules.h"

namespace rwe
{
    bool armedCaptureAccepts(bool ordererCanCapture, PlayerId ordererOwner, PlayerId targetOwner)
    {
        return ordererCanCapture && ordererOwner != targetOwner;
    }
}
