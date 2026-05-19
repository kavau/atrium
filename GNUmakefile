.PHONY: all install clean setup reconfigure

all:
	ninja -C build

install:
	ninja -C build install

clean:
	ninja -C build -t clean

setup:
	meson setup build

reconfigure:
	meson setup --reconfigure build
