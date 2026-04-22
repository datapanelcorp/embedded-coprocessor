---
title: "Embedded Coprocessor (ECP) Firmware"
subtitle: "Firmware Release Notes"
author: Data Panel Corporation
date: "2026-04-22"
dp-author-name: "Adam Jansen"
dp-author-role: "Software Engineer"
dp-release-level: "Draft"
dp-document-number: "4xxxxx-809"
dp-rev: X2
toc-own-page: true
figPrefix:
  - "Figure"
  - "Figures"
tblPrefix:
  - "Table"
  - "Tables"
eqnPrefix:
  - "Equation"
  - "Equations"
lstPrefix:
  - "Listing"
  - "Listings"
nameInLink: true
dp-rev-history:
  - rev: X1
    dcn: ECOxxx
    date: pending
    description: Initial release
---

# Changelog

Project
: 45116-561

% Make a new section for each release, with the newest release
% listed first.

## Version 0.1.0

Released
: 2026-04-22

Version
: 0.1.0+0

Flash Address
: 0x08000000

Software Part Number
: 45116-561

Hardware Part Number
: 

### Bug fixes

This release does not contain any bug fixes.

### Features and enhancements

- Support for reading digital inputs (digital, frequency, counter and encoder)
- Adjustable digital input threshold via DAC and comparator
- Formatting changes to `port status` command
- Sensor power control and monitoring (in input modes)
- Version information printed on boot

### Known Issues

- Release Status

  This is an engineering release for internal lab evaluation of the hardware design.
  Please report any issues to software engineering for resolution.
  
- Overcurrent sensitivity

  The I2T let-through algorithm for determining output overcurrent uses the same 
  tuning values as the 43016 H-bridge. Hardware-specific tuning may be required, 
  including the ramp times and analog filter constant.

- Hardware support

  Only 45116-203 rev X1 is supported by this release.
  
  To support configurable digital input threshold, the board must be modified at 
  each ECP to short the following nets:

  - COMP1P (TP7)
  - COMP2P (TP8)
  - DAC_OUT2 (TP9)
  
- Frequency input range

  The frequency inputs cannot read frequencies below about 8 Hz. This is due to the
  internal timer resolution. A future release will calculate these frequencies in software.
  
- PWMi output support

  Closed-loop control of PWM output current (PWMi) is not implemented in this release.


## Version 0.0.0

Released
: 2026-04-07

Version
: 0.0.0+0

Flash Address
: 0x08000000

Software Part Number
: 45116-561

Hardware Part Number
: 

### Bug fixes

% Describe any defects, issues, or bugs that were fixed
% or otherwise addressed in this release.

- This release does not contain any bug fixes

### Features & Enhancements

% Include any features or changes that were the result of new/modified requirements
% Any feature added because of a bug might make more sense in the bug fixes section.

- This is the initial release, focusing on supporting PWM outputs

  Added a new fancy feature to support additional needs.

### Known Issues

% This section should list any relevant limitations of this release,
% including supported hardware, identified bugs or other limitations
% that the reader should be made aware of.
%
% Mention any related software that might be related (bootloaders, PC tools
% like DPLoader or DP Dash, etc)

% If it's a beta/non-production-intent release, say so

- Release Status

  This is an engineering release for internal lab evaluation.
  Please report any issues to software engineering for resolution.

- Hardware support

  Only 45116-203 rev X1 is supported by this release.
