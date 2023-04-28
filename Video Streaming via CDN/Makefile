all: common miProxy nameserver

common:
	$(MAKE) -C common -j

miProxy: common
	$(MAKE) -C miProxy -j

nameserver: common
	$(MAKE) -C nameserver -j

%:
	$(MAKE) -C common -j $*
	$(MAKE) -C miProxy -j $*
	$(MAKE) -C nameserver -j $*


.PHONY: common miProxy nameserver