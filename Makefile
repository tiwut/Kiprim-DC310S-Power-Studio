CXX ?= g++
PKG_CONFIG ?= pkg-config

LOCAL_PREFIX := $(HOME)/.local/usr
export PKG_CONFIG_PATH := $(LOCAL_PREFIX)/lib64/pkgconfig:$(LOCAL_PREFIX)/share/pkgconfig:$(PKG_CONFIG_PATH)

CXXFLAGS ?= -std=c++20 -O3 -Wall -Wextra -pthread $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) $(PKG_CONFIG) --cflags libadwaita-1 gtk4) -Isrc
LDFLAGS ?= $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) $(PKG_CONFIG) --libs libadwaita-1 gtk4) -L$(LOCAL_PREFIX)/lib64 -Wl,-rpath,$(LOCAL_PREFIX)/lib64

TARGET = kiprim-control
SRCS = src/main.cpp \
       src/serial_port.cpp \
       src/kiprim_device.cpp \
       src/graph_widget.cpp \
       src/sequencer.cpp \
       src/data_logger.cpp \
       src/app_window.cpp

OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

install: $(TARGET)
	install -d $(HOME)/.local/bin
	install -m 755 $(TARGET) $(HOME)/.local/bin/$(TARGET)
	install -d $(HOME)/.local/share/applications
	install -m 644 data/org.kiprim.dc310s.desktop $(HOME)/.local/share/applications/

.PHONY: all clean install
