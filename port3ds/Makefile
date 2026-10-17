# Nativer 3DS-Port von pokered-de
#
#   make            -> 3dsx (Homebrew Launcher)
#   make cia        -> CIA mit Title ID 0004000000171300 (braucht makerom + bannertool)
#   make host       -> Linux-Testbuild (headless, Screenshots/Trace)
#   make difftest   -> Interpreter vs. praezises Recompilat, Frame fuer Frame
#
# Ablauf: ROM bauen (rgbds) -> Recompiler -> syms.h -> devkitARM

PYTHON   ?= python3
ROOT     := ..
ROM      := $(ROOT)/pokered.gbc
SYM      := $(ROOT)/pokered.sym
MAP      := $(ROOT)/pokered.map
GEN      := build/gen
SYMS_H   := build/syms.h
DATA_BIN := 3ds/data/pokered.bin
MAKEROM  ?= makerom
BANNERTOOL ?= bannertool
CIA      := pokered-de.cia

.PHONY: all 3dsx cia host difftest gen clean rom

all: 3dsx

rom:
	$(MAKE) -C $(ROOT) red

$(ROM) $(SYM) $(MAP): rom

# --- Recompiler -------------------------------------------------------
$(GEN)/.stamp: $(ROM) $(SYM) $(MAP) recomp/recomp.py recomp/seeds.txt
	@mkdir -p $(GEN)
	rm -f $(GEN)/*.c $(GEN)/*.o
	$(PYTHON) recomp/recomp.py $(ROM) $(SYM) $(MAP) $(GEN) --seeds recomp/seeds.txt
	@touch $@

$(SYMS_H): $(SYM) tools/gen_syms.py
	@mkdir -p build
	$(PYTHON) tools/gen_syms.py $(SYM) $@

gen: $(GEN)/.stamp $(SYMS_H)

$(DATA_BIN): $(ROM)
	@mkdir -p 3ds/data
	cp $(ROM) $@

# --- 3DS --------------------------------------------------------------
3dsx: gen $(DATA_BIN)
	$(MAKE) -C 3ds

3ds/build/banner.bnr: 3ds/meta/banner.png 3ds/meta/banner.wav
	@mkdir -p 3ds/build
	$(BANNERTOOL) makebanner -i 3ds/meta/banner.png -a 3ds/meta/banner.wav -o $@

3ds/build/icon.smdh: 3ds/meta/icon.png
	@mkdir -p 3ds/build
	$(BANNERTOOL) makesmdh -s "Pokemon Rote Edition" -l "Pokemon Rote Edition (nativer 3DS-Port)" \
		-p "Sephie12" -i 3ds/meta/icon.png -o $@ -f visible

cia: 3dsx 3ds/build/banner.bnr 3ds/build/icon.smdh
	$(MAKEROM) -f cia -o $(CIA) -elf 3ds/pokered-de.elf -rsf 3ds/meta/app.rsf \
		-icon 3ds/build/icon.smdh -banner 3ds/build/banner.bnr -exefslogo -target t
	@echo "Fertig: $(CIA)  (Title ID 0004000000171300)"

# --- Host-Tests -------------------------------------------------------
HOST_CFLAGS ?= -std=gnu89 -O2 -Wall -Wno-unused-label -Wno-unused-but-set-variable -Icore -Ibuild
HOST_CORE := core/gb_core.c core/gb_cpu.c core/gb_ppu.c core/gb_apu.c core/gb_sgb.c \
             core/gb_recomp_table.c host/main_host.c ui/ui.c
GEN_OBJ = $(patsubst %.c,%.o,$(wildcard $(GEN)/*.c))

$(GEN)/%.o: $(GEN)/%.c core/gb_recomp.h core/gb_mem.h core/gb_alu.h core/gb.h
	$(CC) $(HOST_CFLAGS) -c -o $@ $<

host: gen
	$(MAKE) build/pokered_host

build/pokered_host: $(HOST_CORE) $(GEN_OBJ)
	$(CC) $(HOST_CFLAGS) -o $@ $(HOST_CORE) $(GEN_OBJ)

build/pokered_interp: $(HOST_CORE) host/recomp_none.c $(SYMS_H)
	$(CC) $(HOST_CFLAGS) -o $@ $(HOST_CORE) host/recomp_none.c

build/gen_precise/.stamp: $(ROM) $(SYM) $(MAP) recomp/recomp.py
	@mkdir -p build/gen_precise
	rm -f build/gen_precise/*.c build/gen_precise/*.o
	$(PYTHON) recomp/recomp.py $(ROM) $(SYM) $(MAP) build/gen_precise --precise --seeds recomp/seeds.txt
	@touch $@

build/pokered_precise: build/gen_precise/.stamp $(HOST_CORE) $(SYMS_H)
	$(MAKE) build/gen_precise/objs HOST_CFLAGS="$(HOST_CFLAGS) -O1"
	$(CC) $(HOST_CFLAGS) -o $@ $(HOST_CORE) build/gen_precise/*.o

build/gen_precise/objs: $(patsubst %.c,%.o,$(wildcard build/gen_precise/*.c))

build/gen_precise/%.o: build/gen_precise/%.c
	$(CC) $(HOST_CFLAGS) -O1 -c -o $@ $<

difftest: build/pokered_interp build/pokered_precise
	@mkdir -p shots
	$(PYTHON) tools/mkinput.py tests/route_intro.txt build/difftest_input.txt
	./build/pokered_interp $(ROM) -f 25000 -i build/difftest_input.txt -t build/trace_interp.txt
	./build/pokered_precise $(ROM) -f 25000 -i build/difftest_input.txt -t build/trace_precise.txt
	cmp build/trace_interp.txt build/trace_precise.txt && echo "Differenztest OK: identisch"

clean:
	rm -rf build shots $(DATA_BIN) $(CIA)
	-$(MAKE) -C 3ds clean
