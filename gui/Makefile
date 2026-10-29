CC = zig cc
CFLAGS = -target arm-linux-musleabi -mcpu=cortex_a5 -mfloat-abi=soft -ffreestanding -nostdlib -fno-builtin -static -s -O2
SRCS = src/rt.c src/fb.c src/main.c
TARGET = a6400_gui.elf

all: $(TARGET)

$(TARGET): $(SRCS) src/rt.h src/fb.h src/font.h
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

clean:
	rm -f $(TARGET)
