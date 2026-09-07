#pragma once

#ifndef PLUGIN_VERSION
#define PLUGIN_VERSION "0.8.19-2-alpha"
#endif

#ifndef FXM_VERSION_LABEL
#define FXM_VERSION_LABEL "2026 - v0.8.19-2-alpha"
#endif

// Manually assigned delivery identifier. Increment this once per delivered
// package, never per local compile.
#ifndef FXM_DEVELOPMENT_VERSION
#define FXM_DEVELOPMENT_VERSION "420"
#endif
#define FXM_DEVELOPMENT_DISPLAY "Development Version " FXM_DEVELOPMENT_VERSION
#define FXM_BUILD_DISPLAY FXM_VERSION_LABEL
#define FXM_FULL_VERSION_DISPLAY FXM_VERSION_LABEL
