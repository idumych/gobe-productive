# The image part's plug-ins, built from source as the original shipped them:
# libimagemanip (public domain), the ImageManip add-ons and the GIMP filters
# (GPL, see COPYING). Haiku: make; Linux: make COSMOE_ROOT=<Hokku prefix>.
# Output in build/: lib/libimagemanip.so, ImageManip/<name>, GIMP/<name>.

UNAME := $(shell uname -s)
BUILD := build
CXX ?= c++
CC ?= cc

override CXXFLAGS += -O2 -fPIC -Wno-multichar -Wno-narrowing -std=gnu++17 -D_IMPEXP_IMAGEMANIP= \
	-Icompat -Ilibimagemanip -IImageManip -include beos.h
override CFLAGS += -O2 -fPIC

ifeq ($(UNAME),Haiku)
  LIBS := -lbe -ltracker
  ADDON_RPATH := -Wl,-rpath,'$$ORIGIN/../lib'
else
  COSMOE_DIRS := config os os/app os/interface os/kernel os/storage os/support os/translation posix
  override CXXFLAGS += -I$(COSMOE_ROOT)/include $(addprefix -I$(COSMOE_ROOT)/include/,$(COSMOE_DIRS))
  LIBS := -L$(COSMOE_ROOT)/lib -lbe -ltracker
  ADDON_RPATH := -Wl,-rpath,'$$ORIGIN/../lib'
endif
LDFLAGS_SO := -shared -Wl,--no-undefined

LIB := $(BUILD)/lib/libimagemanip.so
LIB_SRCS := libimagemanip/ImageManip.cpp libimagemanip/BitmapAccessor.cpp \
	libimagemanip/BBitmapAccessor.cpp

MANIP := $(notdir $(patsubst %/,%,$(dir $(wildcard ImageManip/*/*.cpp))))
MANIP := $(filter-out Blur,$(sort $(MANIP)))
# (Blur came in the source but not with the original: it stays out)
MANIP_OUT := $(addprefix $(BUILD)/ImageManip/,$(MANIP))
COMMON := ImageManip/GIMPGlue.cpp compat/libprefs.cpp

all: $(LIB) $(MANIP_OUT)

$(LIB): $(LIB_SRCS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LDFLAGS_SO) -Wl,-soname,libimagemanip.so -o $@ $^ $(LIBS)

# an add-on: its sources, the GIMP glue, libprefs, and the shared TSlider
# unless it brings its own
$(BUILD)/ImageManip/%: ImageManip/%/*.cpp ImageManip/%/*.h ImageManip/*.h compat/*.h $(COMMON) ImageManip/TSlider.cpp $(LIB)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -IImageManip/$* $(LDFLAGS_SO) -o $@ ImageManip/$*/*.cpp $(COMMON) \
		$(if $(shell grep -l 'class TSlider' ImageManip/$*/Config.h),,ImageManip/TSlider.cpp) \
		-L$(BUILD)/lib -limagemanip $(LIBS) $(ADDON_RPATH)

clean:
	rm -rf $(BUILD)

.PHONY: all clean
