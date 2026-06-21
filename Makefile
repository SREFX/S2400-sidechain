NAME = SCSend
FILES_DSP = SCSend.cpp

# Link to POSIX RT library for shared memory
LDFLAGS += -lrt

# Effect so use the plugin template
include ../../Makefile.plugins.mk

# Link to framework
all: lv2
		@echo "---manually forcing TTL generation---"
		@../../utils/lv2_ttl_generator/lv2_ttl_generator ../../bin/SCSend.lv2/SCSend.so
		@cp *.ttl ../../bin/SCSend.lv2
		@echo "ttls copied to plugin directory"