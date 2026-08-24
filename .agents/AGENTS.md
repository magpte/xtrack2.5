# Agent Instructions & Project Rules for X-Track 2.5

## Target Hardware Scoping
- **Primary Target Hardware**: AT32F435 (ARM Cortex-M4F).
- **Primary Hardware Project Directory**: [MDK-ARM_F435](file:///e:/XTRACK/xtrack2.5/Software/X-Track/MDK-ARM_F435)
- **Primary HAL/BSP Location**: `Software/X-Track/MDK-ARM_F435/Platform/` and `Software/X-Track/USER/HAL/`

## Rules for Legacy F403 (AT32F403A) Support
- The directory `Software/X-Track/MDK-ARM_F403A/` is a legacy hardware backup folder.
- **STRICT RULE**: AI agents MUST NOT navigate into, read, or search within `Software/X-Track/MDK-ARM_F403A/` for HAL, MCU configs, driver implementations, or build configuration tasks.
- Always perform hardware-related code searches and edits strictly within [MDK-ARM_F435](file:///e:/XTRACK/xtrack2.5/Software/X-Track/MDK-ARM_F435) and [USER](file:///e:/XTRACK/xtrack2.5/Software/X-Track/USER).

## Toolchain & Build Configuration
- **Keil MDK Location**: MUST use `C:\Keil_v543\UV4\UV4.exe` for building and compiling MDK-ARM projects. Do NOT use `C:\Keil_v5\`.

