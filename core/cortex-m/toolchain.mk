# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

ifeq ($(cc-name),gcc)
# coreboot sdk
CROSS_COMPILE_ARM_DEFAULT:=/opt/coreboot-sdk/bin/arm-eabi-
else
# llvm sdk
CROSS_COMPILE_ARM_DEFAULT:=armv7m-cros-eabi-
endif
CMAKE_SYSTEM_PROCESSOR ?= armv7
# TODO(b/275450331): Enable the asm after we fix the crash.
OPENSSL_NO_ASM ?= 1

$(call set-option,CROSS_COMPILE,\
	$(CROSS_COMPILE_arm),\
	$(CROSS_COMPILE_ARM_DEFAULT))
