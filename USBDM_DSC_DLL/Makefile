include ../Common.mk

ifeq ($(UNAME_S),Windows)
VSUFFIX=.4
else
VSUFFIX=
endif

target   = dsc
DLL_DEFS = -DTARGET=MC56F80xx -DUSBDMDSC_DLL_EXPORTS

usbdm-$(target):
	$(MAKE) dll -f Target.mk BUILDDIR=$@ TARGET=$@${VSUFFIX} CDEFS="$(DLL_DEFS)"

usbdm-$(target)-debug:
	$(MAKE) dll -f Target.mk BUILDDIR=$@ TARGET=$@${VSUFFIX} CDEFS="$(DLL_DEFS)" DEBUG="Y"

test:
	$(MAKE) exe -f Target.mk BUILDDIR=$@ TARGET=$@ MODULE=$@ CDEFS="$(DLL_DEFS)"  DEBUG="Y"

all: usbdm-$(target) usbdm-$(target)-debug #test

clean:
	${RMDIR} test
	${RMDIR} usbdm-$(target)
	${RMDIR} usbdm-$(target)-debug

.PHONY: all clean test usbdm-$(target) usbdm-$(target)-debug
