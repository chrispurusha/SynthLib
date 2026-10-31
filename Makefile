# One architecture of libSynthLib.a. Run through do-build, which makes each one and joins them.
# Every source in src/, audio/ and plugin/ goes in, so a new file needs no list.

ifndef ARCH
$(error run ./do-build, not make)
endif

MIN ?= 12.0
O   := build/$(ARCH)

SOURCES := $(wildcard src/*.c src/*.cpp src/*.m audio/*.c plugin/*.c plugin/*.m)

# The wrapper's half that needs the SDK, and the SDK's own files that give its interface ids a home.
ifdef SDK
VST3_SOURCES := plugin/synthlibPluginVst3.cpp plugin/synthlibPluginVst3View.mm
SDK_SOURCES  := pluginterfaces/base/funknown.cpp pluginterfaces/base/coreiids.cpp \
                pluginterfaces/base/ustring.cpp pluginterfaces/base/conststringtable.cpp \
                public.sdk/source/vst/vstinitiids.cpp public.sdk/source/common/commoniids.cpp
SDKFLAGS     := -I"$(SDK)"
endif

OBJECTS := $(addprefix $(O)/,$(addsuffix .o,$(SOURCES) $(VST3_SOURCES))) \
           $(addprefix $(O)/sdk/,$(addsuffix .o,$(notdir $(SDK_SOURCES))))

COMMON   := -arch $(ARCH) -mmacosx-version-min=$(MIN) -O2 -g -Wall -Wextra -MMD -MP
INCLUDES := -Isrc -Iaudio -Iplugin -IThirdParty/glfw/include -IThirdParty/freetype/include
CFLAGS   := $(COMMON) $(INCLUDES) -std=gnu17
CXXFLAGS := $(COMMON) $(INCLUDES) -std=gnu++17
ARC      := -fobjc-arc

$(O)/libSynthLib.a: $(OBJECTS)
	@rm -f $@
	@libtool -static -no_warning_for_no_symbols -o $@ $(OBJECTS)

# Changing a flag or the SDK has to rebuild everything, though no file's date moved.
$(O)/flags: FORCE
	@mkdir -p $(O)
	@echo "$(COMMON) $(INCLUDES) $(SDK)" | cmp -s - $@ || echo "$(COMMON) $(INCLUDES) $(SDK)" > $@

$(O)/%.c.o: %.c $(O)/flags
	@mkdir -p $(dir $@)
	@clang $(CFLAGS) -c $< -o $@

$(O)/%.cpp.o: %.cpp $(O)/flags
	@mkdir -p $(dir $@)
	@clang++ $(CXXFLAGS) $(SDKFLAGS) -c $< -o $@

$(O)/%.m.o: %.m $(O)/flags
	@mkdir -p $(dir $@)
	@clang $(CFLAGS) $(ARC) -c $< -o $@

$(O)/%.mm.o: %.mm $(O)/flags
	@mkdir -p $(dir $@)
	@clang++ $(CXXFLAGS) $(SDKFLAGS) $(ARC) -c $< -o $@

define sdk_rule
$(O)/sdk/$(notdir $(1)).o: $(SDK)/$(1) $(O)/flags
	@mkdir -p $$(dir $$@)
	@clang++ $$(CXXFLAGS) $$(SDKFLAGS) -w -c $$< -o $$@
endef

$(foreach file,$(SDK_SOURCES),$(eval $(call sdk_rule,$(file))))

FORCE:

.PHONY: FORCE

-include $(OBJECTS:.o=.d)
