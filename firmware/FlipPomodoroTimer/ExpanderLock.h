#pragma once
#include "I2CLock.h"

// Kept for backward compatibility; all TCA9554 & Wire access shares i2cMutex().
using ExpanderLock = I2CLock;
