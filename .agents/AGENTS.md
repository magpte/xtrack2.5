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

## UI Fonts & Character Set Rules
- **Built-in Fonts**: Bahnschrift fonts (`bahnschrift_13`, `bahnschrift_17`, `bahnschrift_32`, `bahnschrift_65`) ONLY encode standard ASCII characters `0x0020 ~ 0x007E` (space to `~`, 95 printable characters).
- **STRICT RULE**: Do NOT use non-ASCII characters or Unicode symbols (such as degree symbol `°`, Celsius `℃`, arrows `↑↓←→`, or CJK characters) in LVGL labels or string formatters (`snprintf`, `lv_label_set_text`).
- **Supported Special Symbols**: `! " # $ % & ' ( ) * + , - . / : ; < = > ? @ [ \ ] ^ _ ` { | } ~`
- For angles: Display pure numeric degrees (e.g. `0`, `356`).


