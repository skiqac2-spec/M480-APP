HOST := $(if $(HOST),$(HOST),$(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..))
SYS := menu
EXENAME := Emulator
NOCORE := 1
include $(HOST)/common.mk
