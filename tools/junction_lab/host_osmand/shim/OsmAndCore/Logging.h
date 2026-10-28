// Host shim: Core's OsmAndCore/Logging.h needs Qt. Routing code only uses OsmAnd::LogPrintf and
// LogSeverityLevel, which core-legacy's Logging.h provides (implemented in logging_stub.cpp).
// Angle brackets so the include-path search finds core-legacy/native/include/Logging.h, not us.
#pragma once
#include <Logging.h>
