.DEFAULT_GOAL := all
PREFIX ?= arm-none-eabi-
CC := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE := $(PREFIX)size
CLANG ?= clang
PYTHON ?= python3
CFLAGS := -mcpu=arm7tdmi -mthumb -mthumb-interwork -O2 -g -std=c11 -ffreestanding -fno-builtin -fno-strict-aliasing -fno-common -ffunction-sections -fdata-sections -Wall -Wextra -Icommon
LDFLAGS := -mcpu=arm7tdmi -mthumb -mthumb-interwork -nostdlib -Tcommon/gba.ld -Wl,--gc-sections
COMMON := common/start.s common/main.c common/gba.c common/memory.c
D2 := $(wildcard diablo2/*.c)
DEN_FLAGS := -DD2_DEN_OF_EVIL -Idiablo2 -I.local/d2/generated
DEN_INPUTS := .local/d2/generated/d2_den_assets.h .local/d2/generated/d2_town_assets.h .local/d2/generated/d2_den_items.h

.PHONY: all build verify test clean den verify-den test-den test-den-render
all: verify
build: verify
verify: build/Diablo2-GBA.gba
	$(PYTHON) tools/verify_roms.py

build/diablo2.elf: $(COMMON) $(D2) diablo2/*.h common/gba.h common/gba.ld Makefile
	mkdir -p build
	$(CC) $(CFLAGS) $(COMMON) $(D2) $(LDFLAGS) -Wl,-Map,build/diablo2.map -lgcc -o $@
	$(SIZE) $@

build/Diablo2-GBA.gba: build/diablo2.elf tools/fix_rom.py
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/fix_rom.py $@ 'D2 GBA DEMO' D2GD

build/test-diablo2: tests/host.c common/gba.c $(D2) $(wildcard diablo2/*.h) common/gba.h
	mkdir -p build
	$(CLANG) -std=c11 -O1 -g -fsanitize=address,undefined -DGBA_HOST -Icommon tests/host.c common/gba.c $(D2) -o $@

build/test-den-items: tests/den_items.c diablo2/den_items.h common/gba.h
	mkdir -p build
	$(CLANG) -std=c11 -O2 -g -Wall -Wextra -fsanitize=address,undefined -DGBA_HOST -DDEN_ITEMS_TEST_FIXTURES $< -o $@

build/test-den-window: tests/den_gfx_window.c common/gba.c common/gba.h
	mkdir -p build
	$(CLANG) -std=c11 -O1 -g -Wall -Wextra -fsanitize=address,undefined -DGBA_HOST -Icommon tests/den_gfx_window.c common/gba.c -o $@

test: build/test-diablo2 build/test-den-items build/test-den-state build/test-den-window
	./build/test-diablo2
	./build/test-den-items
	./build/test-den-state
	./build/test-den-window
	$(PYTHON) -m unittest discover -s tests -p 'test_d2*.py'

build/test-den-state: tests/den_state.c diablo2/den_sim.h diablo2/den_items.h common/gba.h common/gba.c
	mkdir -p build
	$(CLANG) -std=c11 -O2 -g -Wall -Wextra -fsanitize=address,undefined -DGBA_HOST -DDEN_ITEMS_TEST_FIXTURES tests/den_state.c common/gba.c -o $@

test-den: build/test-den-items build/test-den-state build/test-den-window build/test-den build/test-den-render
	./build/test-den-items
	./build/test-den-state
	./build/test-den-window
	./build/test-den --trace build/den-completion-input.json
	./build/test-den-render

build/test-den: tests/den_run.c $(wildcard diablo2/*.h) .local/d2/generated/d2_den_items.h common/gba.h common/gba.c
	mkdir -p build
	$(CLANG) -std=c11 -O2 -g -Wall -Wextra -fsanitize=address,undefined -DGBA_HOST -Idiablo2 -I.local/d2/generated tests/den_run.c common/gba.c -o $@

build/test-den-render: tests/host.c common/gba.c $(D2) $(wildcard diablo2/*.h) $(DEN_INPUTS)
	mkdir -p build
	$(CLANG) -std=c11 -O1 -g -fsanitize=address,undefined -DGBA_HOST -Icommon $(DEN_FLAGS) tests/host.c common/gba.c $(D2) -o $@

test-den-render: build/test-den-render build/test-den-window
	./build/test-den-render
	./build/test-den-window

# Optional private-data profile. Prepare through ./dev den prepare.
den: verify-den
$(DEN_INPUTS):
	@echo 'Private Den assets are missing. Run ./dev den prepare first.'
	@false
build/diablo2-den.elf: $(COMMON) $(D2) $(wildcard diablo2/*.h) $(DEN_INPUTS) common/gba.h common/gba.ld Makefile
	mkdir -p build
	$(CC) $(CFLAGS) $(DEN_FLAGS) $(COMMON) $(D2) $(LDFLAGS) -Wl,-Map,build/diablo2-den.map -lgcc -o $@
	$(SIZE) $@
build/Diablo2-Den-of-Evil-GBA.gba: build/diablo2-den.elf tools/fix_rom.py
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/fix_rom.py $@ 'D2 DEN EVIL' D2DE
verify-den: build/Diablo2-Den-of-Evil-GBA.gba
	$(PYTHON) tools/verify_roms.py --den
clean:
	rm -rf build
