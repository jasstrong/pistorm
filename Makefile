EXENAME          = emulator

MAINFILES        = emulator.c \
	memory_mapped.c \
	config_file/config_file.c \
	config_file/rominfo.c \
	input/input.c \
	gpio/ps_protocol.c \
	platforms/platforms.c \
	platforms/mac68k/mac68k-platform.c \
	platforms/dummy/dummy-platform.c \
	platforms/dummy/dummy-registers.c \
	platforms/shared/rtc.c \
	platforms/shared/common.c \
	vnc/vnc.c

MUSASHIFILES     = m68kcpu.c m68kdasm.c softfloat/softfloat.c softfloat/softfloat_fpsp.c
MUSASHIGENCFILES = m68kops.c
MUSASHIGENHFILES = m68kops.h
MUSASHIGENERATOR = m68kmake

# EXE = .exe
# EXEPATH = .\\
EXE =
EXEPATH = ./

.CFILES   = $(MAINFILES) $(MUSASHIFILES) $(MUSASHIGENCFILES)
.OFILES   = $(.CFILES:%.c=%.o)

CC        = gcc
CXX       = g++
WARNINGS  = -Wall -Wextra -pedantic

ifeq ($(PLATFORM),PI3_BULLSEYE)
	LFLAGS    = $(WARNINGS) -L/usr/local/lib -ldl -lstdc++ -lasound -lvncserver -lm
	CFLAGS    = $(WARNINGS) -I. -march=armv8-a -mfloat-abi=hard -mfpu=neon-fp-armv8 -O3 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -lstdc++ $(ACFLAGS)
else ifeq ($(PLATFORM),PI4_AARCH64)
	LFLAGS    = $(WARNINGS) -L/usr/local/lib -ldl -lstdc++ -lasound -lvncserver -lm
	CFLAGS    = $(WARNINGS) -I. -march=armv8-a -O3 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -lstdc++ $(ACFLAGS)
else ifeq ($(PLATFORM),PI4)
	LFLAGS    = $(WARNINGS) -L/usr/local/lib -ldl -lstdc++ -lasound -lvncserver -lm
	CFLAGS    = $(WARNINGS) -I. -march=armv8-a -mfloat-abi=hard -mfpu=neon-fp-armv8 -O3 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -lstdc++ $(ACFLAGS)
else
	CFLAGS    = $(WARNINGS) -I. -march=armv8-a -mfloat-abi=hard -mfpu=neon-fp-armv8 -O3 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -lstdc++ $(ACFLAGS)
	LFLAGS    = $(WARNINGS) -ldl -lstdc++ -lasound -lvncserver -lm
endif

TARGET = $(EXENAME)$(EXE)

DELETEFILES = $(MUSASHIGENCFILES) $(MUSASHIGENHFILES) $(.OFILES) $(.OFILES:%.o=%.d) $(TARGET) $(MUSASHIGENERATOR)$(EXE)


all: $(MUSASHIGENCFILES) $(MUSASHIGENHFILES) $(TARGET) buptest

clean:
	rm -f $(DELETEFILES)

$(TARGET):  $(MUSAHIGENCFILES:%.c=%.o) $(.CFILES:%.c=%.o)
	$(CC) -o $@ $^ -O3 -pthread $(LFLAGS) -lm -lstdc++

buptest: buptest.c gpio/ps_protocol.c
	$(CC) $^ -o $@ -I./ $(CFLAGS) -O0

$(MUSASHIGENCFILES) $(MUSASHIGENHFILES): $(MUSASHIGENERATOR)$(EXE)
	$(EXEPATH)$(MUSASHIGENERATOR)$(EXE)

$(MUSASHIGENERATOR)$(EXE):  $(MUSASHIGENERATOR).c
	$(CC) -o  $(MUSASHIGENERATOR)$(EXE)  $(MUSASHIGENERATOR).c

-include $(.CFILES:%.c=%.d) $(MUSASHIGENCFILES:%.c=%.d) $(MUSASHIGENERATOR).d
