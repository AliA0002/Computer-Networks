
all: receiver

# Compile the file server
# Note: No autotag here, only runs when submit is run

receiver: receiver
	$(MAKE) -C receiver -j
	cp receiver/receiver .


%:
	$(MAKE) -C receiver -j $*

.PHONY: all clean receiver