# Gobe Productive port: builds on Haiku and on Linux with Cosmoe (Hokku)
# from the same sources. The
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
#   - COSMOE_ROOT selects the Cosmoe install;
#   - HAIKU_CROSS=1 (with tools/haiku-arm64's env.sh sourced) cross-compiles
#     for Haiku arm64 (packaging/build-hpkg-arm64.sh).

NAME     := GobeProductive
RDEF     := GobeProductive.rdef
CXX      ?= g++
CXXFLAGS ?= -O2 -g
override CXXFLAGS += -std=c++17 -Wall -Wno-multichar
UNAME    := $(shell uname -s)
COSMOE_ROOT ?= /usr

SRCS     := $(wildcard src/core/*.cpp) $(wildcard src/app/*.cpp)
INCLUDES := src/core src/app
# Libraries beyond the Haiku API (same on every platform)
APP_LIBS := -lz

override CXXFLAGS += $(addprefix -I,$(INCLUDES))

ifneq ($(HAIKU_CROSS),)
  # Haiku arm64, cross-compiled on Linux: . <work>/env.sh from
  # tools/haiku-arm64/make-sysroot.sh, then make HAIKU_CROSS=1 (the
  # sysroot has zlib from the nightly's bootstrap packages)
  PLATFORM     := Haiku
  override CXXFLAGS += $(HAIKU_CROSS_CFLAGS)
  override LDFLAGS  += $(HAIKU_CROSS_LDFLAGS)
  LIBS         := -lbe -ltracker -ltranslation -ltextencoding -lGL $(APP_LIBS)
  RESOURCES     = rc -o obj/$(NAME).rsrc $(RDEF) && xres -o $@ obj/$(NAME).rsrc
else ifeq ($(UNAME),Haiku)
  PLATFORM     := Haiku
  LIBS         := -lbe -ltracker -ltranslation -ltextencoding -lGL $(APP_LIBS)
  INSTALL_DIR  := $(shell finddir B_USER_NONPACKAGED_DIRECTORY)/apps
  RESOURCES     = rc -o obj/$(NAME).rsrc $(RDEF) && xres -o $@ obj/$(NAME).rsrc
else
  ifneq ($(wildcard $(COSMOE_ROOT)/include/os/app/Application.h),)
    # Plain Linux with Cosmoe (the Haiku API as a library on X11/Wayland)
    PLATFORM    := Cosmoe
    COSMOE_DIRS := config os os/add-ons/graphics os/app os/game os/interface os/kernel os/locale \
                   os/media os/net os/opengl os/storage os/support os/translation \
                   os/be_apps/Tracker private/interface private/shared \
                   private/support posix
    override CXXFLAGS += -I$(COSMOE_ROOT)/include \
                   $(addprefix -I$(COSMOE_ROOT)/include/,$(COSMOE_DIRS)) \
                   -DAPP_COSMOE -fPIC
    # libbe finds Cosmoe's add-ons and data next to itself, so the AppImage
    # (bin/App + lib/) works wherever it is mounted.
    COSMOE_RPATH ?= $$ORIGIN/../lib
    LIBS        := -L$(COSMOE_ROOT)/lib -lbe -ltracker -lopengl -lGL $(APP_LIBS) \
                   -Wl,-rpath,'$(COSMOE_RPATH)'
    INSTALL_DIR := $(HOME)/.local/bin
    # Cosmoe reads resources from a .cosmoe_rsrc section linked into the app
    EXTRA_OBJS  := obj/$(NAME).rsrc.o
    RESOURCES    = true
  else
    PLATFORM    := none
  endif
endif

OBJS := $(patsubst %.cpp,obj/%.o,$(SRCS))
HEADERS := $(wildcard src/*/*.h)

all: check-platform $(NAME)

check-platform:
ifeq ($(PLATFORM),none)
	@echo "$(NAME) needs Haiku, or Linux with Cosmoe."
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
	@if [ -z "$(HAIKU_CROSS)" ] && command -v mimeset >/dev/null 2>&1; then mimeset -f $@; fi

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

.PHONY: all check-platform test install clean hpkg
