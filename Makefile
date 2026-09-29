# Repo chores: the pebble tool's common flows, and the screenshot harness.
# The harness itself lives in tools/shots.sh (per-platform scenes) and
# tools/screenshots.sh (one scene, one platform) -- which platform gets which
# scene, and why the colour picker copies into the assets instead of writing
# them, are facts for bash to know, not make.
#
## Targets
##   make build               compile all six platforms into build/
##   make run [PLAT=basalt]   build and install onto an emulator (also boots it)
##   make kill                stop every running emulator
##   make clean               remove build/ (screenshots live in tmp/, ignored)
##   make shots               refresh assets/screenshots/ for every platform
##   make shot-<platform>     ...for one platform (e.g. make shot-chalk)
##   make renders             composite the shots into device frames (assets/renders/)
##   make check               diff the empty menu against the upstream baselines
PLATFORMS := aplite basalt chalk diorite emery gabbro
SHOTSET   := tools/shots.sh

PLAT ?= basalt

.NOTPARALLEL:

.PHONY: help build run kill clean shots renders check $(PLATFORMS:%=shot-%)

help:
	@sed -n '/^##/s/^## \{0,1\}//p' Makefile

build:
	pebble build

sideload:
	pebble install --phone

# Guard the target: `make run PLAT=blsa` would otherwise hand the typo to the
# pebble tool and fail at install time with a stranger error.
run:
	@if [ -z "$(filter $(PLATFORMS),$(PLAT))" ]; then \
		echo "unknown platform '$(PLAT)' -- try: $(PLATFORMS)"; exit 1; \
	fi
	pebble build && pebble install --emulator $(PLAT)

kill:
	pebble kill --force || true

clean:
	rm -rf build

shots:
	$(SHOTSET)
	tools/renders.sh

renders:
	tools/renders.sh

# An explicit list, not a shot-% pattern rule: macOS ships GNU make 3.81,
# which silently skips pattern rules once .PHONY has created empty entries
# for the same names.
$(PLATFORMS:%=shot-%):
	$(SHOTSET) -p $(@:shot-%=%)

check:
	$(SHOTSET) check
