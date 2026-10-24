# Gobe Productive port: builds on Haiku, VitruvianOS (V\OS, a BeOS-like OS
# based on Linux) and Linux with Cosmoe (Hokku) from the same sources. The
# app reads the user's original "Gobe Productive" binary at runtime
# ($GOBE_ORIGINAL, next to the executable, where it's usually installed, or
# chosen in a file panel).
#
#   make          -> ./GobeProductive
#   make test     -> host-side tests (any OS, no Haiku API needed)
#   make install  -> copies the app to your apps folder
#
# Contract the packaging scripts rely on:
#   - `make` leaves the binary at ./$(NAME) and the compiled resources at
#     obj/$(NAME).rsrc (V\OS, Haiku) or obj/$(NAME).rsrc.o (Cosmoe);
#   - CXXFLAGS/LDFLAGS from the command line ADD to ours (override +=);
#   - COSMOE_ROOT selects the Cosmoe install.

NAME     := GobeProductive
RDEF     := GobeProductive.rdef
CXX      ?= g++
CXXFLAGS ?= -O2 -g
override CXXFLAGS += -std=c++17 -Wall -Wno-multichar
UNAME    := $(shell uname -s)
VOS_HEADERS ?= /system/develop/headers
COSMOE_ROOT ?= /usr
VOS_ARCH := $(if $(filter aarch64,$(shell uname -m)),arm64,$(shell uname -m))

SRCS     := $(wildcard src/core/*.cpp) $(wildcard src/app/*.cpp)
INCLUDES := src/core src/app
# Libraries beyond the Haiku API (same on every platform)
APP_LIBS := -lz

override CXXFLAGS += $(addprefix -I,$(INCLUDES))

ifeq ($(UNAME),Haiku)
  PLATFORM     := Haiku
  LIBS         := -lbe -ltracker -ltranslation -ltextencoding $(APP_LIBS)
  INSTALL_DIR  := $(shell finddir B_USER_NONPACKAGED_DIRECTORY)/apps
  RESOURCES     = rc -o obj/$(NAME).rsrc $(RDEF) && xres -o $@ obj/$(NAME).rsrc
else
  ifeq ($(shell pkg-config --exists vos 2>/dev/null && echo yes),yes)
    # VitruvianOS with vos-dev (pkg-config), or a link-time SDK's env.sh
    PLATFORM    := VitruvianOS
    override CXXFLAGS += $(shell pkg-config --cflags vos) -fPIC
    LIBS        := $(shell pkg-config --libs vos) -ltracker -ltranslation -ltextencoding $(APP_LIBS)
  else ifneq ($(wildcard $(VOS_HEADERS)/os/BeBuild.h),)
    # VitruvianOS 0.6.x: headers in the system package, no pkg-config
    PLATFORM    := VitruvianOS
    VOS_DIRS    := . os os/app os/drivers os/kernel os/support os/storage \
                   os/translation os/config os/add-ons/file_system \
                   os/add-ons/graphics os/add-ons/registrar \
                   os/add-ons/network_settings os/arch/$(VOS_ARCH) os/device \
                   os/drivers/bus os/game os/interface os/locale \
                   os/locale/tools os/mail os/net os/posix/compat/sys \
                   os/add-ons/input_server os/add-ons/tracker os/opengl \
                   os/media2 posix build build/config_headers config
    override CXXFLAGS += $(addprefix -I$(VOS_HEADERS)/,$(VOS_DIRS)) \
                   -include LinuxBuildCompatibility.h -fPIC
    LIBS        := -lbe -lroot -ltracker -ltranslation -ltextencoding $(APP_LIBS)
  else ifneq ($(wildcard $(COSMOE_ROOT)/include/os/app/Application.h),)
    # Plain Linux with Cosmoe (the Haiku API as a library on X11/Wayland)
    PLATFORM    := Cosmoe
    COSMOE_DIRS := config os os/app os/game os/interface os/kernel os/locale \
                   os/media os/net os/storage os/support os/translation \
                   os/be_apps/Tracker private/interface private/shared \
                   private/support posix
    override CXXFLAGS += -I$(COSMOE_ROOT)/include \
                   $(addprefix -I$(COSMOE_ROOT)/include/,$(COSMOE_DIRS)) \
                   -DAPP_COSMOE -fPIC
    # libbe finds Cosmoe's add-ons and data next to itself, so the AppImage
    # (bin/App + lib/) works wherever it is mounted.
    COSMOE_RPATH ?= $$ORIGIN/../lib
    LIBS        := -L$(COSMOE_ROOT)/lib -lbe -ltracker $(APP_LIBS) \
                   -Wl,-rpath,'$(COSMOE_RPATH)'
    INSTALL_DIR := $(HOME)/.local/bin
    # Cosmoe reads resources from a .cosmoe_rsrc section linked into the app
    EXTRA_OBJS  := obj/$(NAME).rsrc.o
    RESOURCES    = true
  else
    PLATFORM    := none
  endif
  ifeq ($(PLATFORM),VitruvianOS)
    INSTALL_DIR := $(or $(shell finddir B_USER_NONPACKAGED_DIRECTORY 2>/dev/null),$(HOME)/config/non-packaged)/apps
    # V\OS keeps the signature both as resources and as attributes; the
    # host-built xres/resattr need absolute paths.
    RESOURCES    = rc -o $(CURDIR)/obj/$(NAME).rsrc $(CURDIR)/$(RDEF) \
                   && xres -o $(CURDIR)/$@ $(CURDIR)/obj/$(NAME).rsrc \
                   && resattr -O -o $(CURDIR)/$@ $(CURDIR)/obj/$(NAME).rsrc
  endif
endif

OBJS := $(patsubst %.cpp,obj/%.o,$(SRCS))
HEADERS := $(wildcard src/*/*.h)

all: check-platform $(NAME)

check-platform:
ifeq ($(PLATFORM),none)
	@echo "$(NAME) needs Haiku, VitruvianOS, or Linux with Cosmoe."
	@echo "V\OS: pkg-config vos, or headers in $(VOS_HEADERS)."
	@echo "Linux: Cosmoe installed under COSMOE_ROOT=$(COSMOE_ROOT)."
	@false
endif

obj/%.o: %.cpp $(HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

obj/$(NAME).rsrc.o: $(RDEF)
	@mkdir -p obj
	LD_LIBRARY_PATH=$(COSMOE_ROOT)/lib $(COSMOE_ROOT)/bin/rc -o obj/$(NAME).rsrc $(RDEF)
	cd obj && ld -r -b binary $(NAME).rsrc -o $(NAME).rsrc.o
	objcopy --rename-section .data=.cosmoe_rsrc \
		--add-section .note.GNU-stack=/dev/null \
		--set-section-flags .note.GNU-stack=noload,readonly obj/$(NAME).rsrc.o

$(NAME): $(OBJS) $(EXTRA_OBJS) $(RDEF)
	@mkdir -p obj
	$(CXX) $(LDFLAGS) $(OBJS) $(EXTRA_OBJS) -o $@ $(LIBS)
	$(RESOURCES)
	@if command -v mimeset >/dev/null 2>&1; then mimeset -f $@; fi

deb: all
	sh packaging/build-deb.sh

hpkg:
	sh packaging/build-hpkg.sh

# Host-side tests: only the portable core, no Haiku headers. With the
# original Gobe Productive installed at ../original/installed they run on
# its sample documents too.
test:
	sh tests/run_host_tests.sh

install: all
	mkdir -p "$(INSTALL_DIR)"
	cp $(NAME) "$(INSTALL_DIR)/"
	@if command -v mimeset >/dev/null 2>&1; then mimeset -f "$(INSTALL_DIR)/$(NAME)"; fi

clean:
	rm -rf obj $(NAME)

.PHONY: all check-platform test install clean deb hpkg
