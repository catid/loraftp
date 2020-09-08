// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

#pragma once

#include <stdint.h>

namespace lora {


//------------------------------------------------------------------------------
// Constants

static const int kVersion = 100; // 1.0.0


//------------------------------------------------------------------------------
// Tools

uint64_t GetTimeUsec();
uint64_t GetTimeMsec();


} // namespace lora
