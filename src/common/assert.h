#pragma once

// Helper for switch cases.
#define DefaultCaseIsUnreachable() \
  default: \
    break
