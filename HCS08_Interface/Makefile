include ../Common.mk

TARGET = HCS08
target = hcs08

$(TARGET)_FlashProgrammer:
	$(MAKE) exe -f ../ProgrammerTarget.mk   BUILDDIR=$@ MODULE=programmer           CDEFS='$(PROGRAMMER_DEFS)'

$(TARGET)_FlashProgrammer-debug:
	$(MAKE) exe -f ../ProgrammerTarget.mk   BUILDDIR=$@ MODULE=programmer           CDEFS='$(PROGRAMMER_DEFS)' DEBUG='Y'

usbdm-$(target)-gdi:
	$(MAKE) dll -f ../GdiTarget.mk          BUILDDIR=$@ MODULE=gdi                  CDEFS='$(GDI_DEFS)'

usbdm-$(target)-gdi-debug:
	$(MAKE) dll -f ../GdiTarget.mk          BUILDDIR=$@ MODULE=gdi                  CDEFS='$(GDI_DEFS)' DEBUG='Y'


allCommon: $(TARGET)_FlashProgrammer        $(TARGET)_FlashProgrammer-debug 
allCommon: usbdm-$(target)-gdi              usbdm-$(target)-gdi-debug

cleanCommon:
	${RMDIR} $(TARGET)_FlashProgrammer
	${RMDIR} $(TARGET)_FlashProgrammer-debug
	${RMDIR} usbdm-$(target)-gdi
	${RMDIR} usbdm-$(target)-gdi-debug

allWindows:
cleanWindows:

all: allCommon allWindows

#Test: usbdm-$(target)-gdi-debug
Test: $(TARGET)_FlashProgrammer-debug

clean: cleanCommon cleanWindows

.PHONY: all clean test
.PHONY: allCommon allWindows cleanCommon cleanWIndows
.PHONY: $(TARGET)_FlashProgrammer $(TARGET)_FlashProgrammer-debug 
.PHONY: usbdm-$(target)-gdi       usbdm-$(target)-gdi-debug 
