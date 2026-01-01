/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020-2025 Raspberry Pi Ltd
 */

/*
 * Stub suspend inhibitor for CLI-only builds
 * 
 * CLI applications typically don't need suspend inhibition because:
 * 1. They're often run in headless/server environments where suspend is disabled
 * 2. They're run with elevated privileges (for disk writes) which usually prevents suspend
 * 3. The writing process itself keeps the system active
 * 
 * This stub implementation does nothing, which is acceptable for CLI-only operation.
 */

#include "suspend_inhibitor.h"

class StubSuspendInhibitor : public SuspendInhibitor
{
public:
    StubSuspendInhibitor() {}
    virtual ~StubSuspendInhibitor() {}
    
    // No-op implementation for CLI builds
    // The system will naturally stay awake during disk I/O operations
};

SuspendInhibitor *CreateSuspendInhibitor()
{
    return new StubSuspendInhibitor();
}

