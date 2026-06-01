# Quartus build chain for pistorm CPLD (MAX II / MAX V)
# Replaces the Windows-only make.bat

QUARTUS_BIN ?= /opt/intelFPGA_lite/21.1/quartus/bin
PROJECT     ?= pistorm
PI_HOST     ?= pi@macbrain4.local

# Sources that the .pof depends on. Re-list as needed.
SOURCES = $(wildcard *.v) $(PROJECT).qpf $(PROJECT).qsf $(PROJECT).sdc

.PHONY: all clean compile svf flash

all: bitstream.svf

# Quartus full compile produces output_files/$(PROJECT).pof
output_files/$(PROJECT).pof: $(SOURCES)
	$(QUARTUS_BIN)/quartus_sh --flow compile $(PROJECT)

# Convert .pof to .svf for openocd flashing
bitstream.svf: output_files/$(PROJECT).pof
	$(QUARTUS_BIN)/quartus_cpf -c -q 100KHz -g 3.3 -n p $< $@

# Convenience: copy SVF to the Pi and trigger the openocd flash
flash: bitstream.svf
	scp bitstream.svf $(PI_HOST):~/pistorm/rtl/bitstream.svf
	ssh $(PI_HOST) "cd ~/pistorm && ./flash.sh"

clean:
	rm -rf output_files db incremental_db bitstream.svf
	rm -f *.qws *.rpt *.summary *.smsg *.jdi *.pin *.sld
