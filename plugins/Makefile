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

# (Haiku arm64 cross-compiled: make HAIKU_CROSS=1 with the sysroot's env.sh)
HAIKU := $(if $(HAIKU_CROSS),1,$(if $(filter Haiku,$(UNAME)),1,))
ifneq ($(HAIKU),)
  override CXXFLAGS += $(HAIKU_CROSS_CFLAGS)
  override CFLAGS += $(HAIKU_CROSS_CFLAGS)
  LIBS := $(HAIKU_CROSS_LDFLAGS) -lbe -ltracker -ltranslation
  ADDON_RPATH := -Wl,-rpath,'$$ORIGIN/../lib'
  GIMP_RPATH :=
else
  COSMOE_DIRS := config os os/app os/interface os/kernel os/storage os/support os/translation posix \
                 private/interface
  override CXXFLAGS += -I$(COSMOE_ROOT)/include $(addprefix -I$(COSMOE_ROOT)/include/,$(COSMOE_DIRS))
  LIBS := -L$(COSMOE_ROOT)/lib -lbe -ltracker
  ADDON_RPATH := -Wl,-rpath,'$$ORIGIN/../lib'
  # (the filters are programs of their own: Hokku's libraries where they are;
  # the AppImage sets theirs to its own)
  GIMP_RPATH := -Wl,-rpath,$(COSMOE_ROOT)/lib
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

# the GIMP filters: programs each built with gimp.c (sparkl.c with gems.c)
GIMP := $(filter-out gimp gems,$(basename $(notdir $(wildcard GIMP/*.c))))
GIMP_OUT := $(addprefix $(BUILD)/GIMP/,$(GIMP))
override CFLAGS += -IGIMP -include string.h -include stdlib.h -Wno-implicit-function-declaration \
	-Wno-int-conversion -Wno-incompatible-pointer-types
ifeq ($(HAIKU),)
  override CFLAGS += -I$(COSMOE_ROOT)/include $(addprefix -I$(COSMOE_ROOT)/include/,$(COSMOE_DIRS))
endif

all: $(LIB) $(MANIP_OUT) $(GIMP_OUT) $(BUILD)/GIMP/index

$(LIB): $(LIB_SRCS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LDFLAGS_SO) -Wl,-soname,libimagemanip.so -o $@ $^ $(LIBS)

# an add-on: its sources, the GIMP glue, libprefs, and the shared TSlider
# unless it brings its own
# (every add-on's headers: Relief has none, which a pattern can't match)
ADDON_HEADERS := $(wildcard ImageManip/*/*.h) $(wildcard ImageManip/*.h) $(wildcard compat/*.h)
$(BUILD)/ImageManip/%: ImageManip/%/*.cpp $(ADDON_HEADERS) $(COMMON) ImageManip/TSlider.cpp $(LIB)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -IImageManip/$* $(LDFLAGS_SO) -o $@ ImageManip/$*/*.cpp $(COMMON) \
		$(if $(shell grep -l 'class TSlider' ImageManip/$*/Config.h),,ImageManip/TSlider.cpp) \
		-L$(BUILD)/lib -limagemanip $(LIBS) $(ADDON_RPATH)

$(BUILD)/GIMP/%: GIMP/%.c GIMP/gimp.c GIMP/gems.c GIMP/*.h GIMP/GIMPMessage.cpp
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c GIMP/$*.c -o $@.o
	$(CC) $(CFLAGS) -c GIMP/gimp.c -o $@.gimp.o
	$(CC) $(CFLAGS) -c GIMP/gems.c -o $@.gems.o
	$(CXX) $(CXXFLAGS) -c GIMP/GIMPMessage.cpp -o $@.msg.o
	$(CXX) -o $@ $@.o $@.gimp.o $@.gems.o $@.msg.o $(LIBS) -lm $(GIMP_RPATH)
	rm -f $@.o $@.gimp.o $@.gems.o $@.msg.o

# what the original kept in each filter's attributes (name, category,
# GIMP_DESC, GIMP_AUTHOR, GIMP_EMAIL), for systems that lose them
$(BUILD)/GIMP/index: GIMP/index
	@mkdir -p $(dir $@)
	cp GIMP/index $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
