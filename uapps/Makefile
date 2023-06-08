.PHONY: all clean hmac

CC=gcc
MAKE=make

INC=-I../ulibss/spdk/include
INC+=-I../ulibss
INC+=

FLAGS=-L../ulibss -lstorstack -Wl,-rpath -Wl,../ulibss -lrt -pthread
# FLAGS+=-L../ulibss/spdk/build/lib -lspdk
# FLAGS+=-Wl,--allow-shlib-undefined
FLAGS+=-DSS_DEBUG
# FLAGs+=-Wl,-rpath=/usr/local/lib -lspdk

# TARGETS=keytest tokentest klibtest spdktest key2file aiotest
TARGETS=spdktest aiotest threadtest cachetest seqwtest mybench_sw

SPDK_LIB_DIR := /usr/local/lib
PKG_CONFIG_PATH = $(SPDK_LIB_DIR)/pkgconfig

#DEPLIBS := $(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" pkg-config --libs spdk_env_dpdk spdk_util spdk_nvme)


all: $(TARGETS)

# $(TARGETS): %: %.c
# 	$(CC) -o $@ $^ $(FLAGS)

# hmac: 
# 	make -C ./hmac

keytest tokentest key2file: %: %.c
	$(CC) -o $@ $^ $(FLAGS) $(INC)

hmac:
	$(MAKE) -C hmac hmac.o
	$(MAKE) -C hmac sha2.o

klibtest: klibtest.o hmac
	$(CC) -o $@ klibtest.o hmac/hmac.o hmac/sha2.o $(FLAGS)

klibtest.o: klibtest.c
	$(CC) -c klibtest.c -o klibtest.o $(INC)

spdktest: spdktest.o
	$(CC) -o $@ spdktest.o $(FLAGS)

spdktest.o:
	$(CC) -c spdktest.c -o spdktest.o $(INC)
	# -Wl,-rpath=$(SPDK_LIB_DIR),--no-as-needed  $(DEPLIBS) -lrte_pci

aiotest: aiotest.o
	$(CC) -o $@ aiotest.o $(FLAGS)

aiotest.o:
	$(CC) -c aiotest.c -o aiotest.o $(INC)

threadtest: threadtest.o
	$(CC) -o $@ threadtest.o $(FLAGS)

threadtest.o: 
	$(CC) -c threadtest.c -o threadtest.o $(INC)

cachetest: cachetest.o
	$(CC) -o $@ cachetest.o $(FLAGS)

cachetest.o:
	$(CC) -c cachetest.c -o cachetest.o $(INC)

seqwtest: seqwtest.o
	$(CC) -o $@ seqwtest.o $(FLAGS)

seqwtest.o:
	$(CC) -c seqwtest.c -o seqwtest.o $(INC)

mybench_sw: mybench_sw.o
	$(CC) -o $@ mybench_sw.o $(FLAGS)

mybench_sw.o:
	$(CC) -c mybench_sw.c -o mybench_sw.o $(INC)

timerectest: timerectest.o
	$(CC) -o $@ timerectest.o $(FLAGS)

timerectest.o:
	$(CC) -c timerectest.c -o timerectest.o $(INC)


debug:
    $(warning SPDK_LIB_DIR: $(SPDK_LIB_DIR))

clean:
	make clean -C hmac
	rm  -f $(TARGETS)
	rm  -f *.o

rebuild: clean all