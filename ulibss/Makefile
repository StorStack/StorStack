.PHONY: all clean install

CC=gcc

# FLAGS=-DSS_DEBUG

INC=-Ispdk/include

OBJS=ulibss.o ulibss_aio.o pseudo_cache.o profile_utils.o

# FLAGS_GPROF=-pg

# OBJS=ulibss.o ulibss_aio.o pseudo_cache.o

SPDK_DIR=./spdk

TARGETS=spdk $(OBJS) libstorstack utrayss

SPDK_LIB_DIR = spdk/build/lib
PKG_CONFIG_PATH = $(SPDK_LIB_DIR)/pkgconfig

SPDK_LIB := $(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" pkg-config --libs spdk_nvme)
DPDK_LIB := $(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" pkg-config --libs spdk_env_dpdk)
SPDK_DPDK_LIB := $(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" pkg-config --libs spdk_event spdk_event_bdev spdk_env_dpdk)
SYS_LIB := $(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" pkg-config --libs --static spdk_syslibs)
GLIB_CFLAGS := $(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" pkg-config --cflags glib-2.0)
GLIB_LIBS := $(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" pkg-config --libs glib-2.0)

all: $(TARGETS)

spdk:
	@+make -C $(SPDK_DIR)

utrayss.o: utrayss.c spdk 
	$(CC) $(FLAGS) $(INC) -o utrayss.o -c utrayss.c 

utrayss: utrayss.o
	$(CC) $(FLAGS) $(INC) -o $@ utrayss.o $(SPDK_DPDK_LIB) $(DPDK_LIB)

libstorstack: $(OBJS)
	$(CC) -shared -o libstorstack.so $(OBJS) \
	-Wl,--whole-archive $(SPDK_LIB) $(SPDK_DPDK_LIB) $(DPDK_LIB) $(GLIB_LIBS) \
	-Wl,--no-whole-archive $(SYS_LIB) \
	$(FLAGS_GPROF)

pseudo_cache.o: pseudo_cache.c
	$(CC) $(FLAGS) $(INC) $(GLIB_CFLAGS) -c pseudo_cache.c -o pseudo_cache.o $(GLIB_LIBS) -fPIC $(FLAGS_GPROF)

profile_utils.o: profile_utils.c
	$(CC) $(FLAGS) $(INC) $(GLIB_CFLAGS) -c profile_utils.c -o profile_utils.o $(GLIB_LIBS) -fPIC

%.o: %.c
	$(CC) $(FLAGS) $(INC) -o $@ -c $^ -fPIC $(FLAGS_GPROF)

clean:
	rm -f libstorstack.so
	rm -f $(OBJS)
	rm -f utrayss
	rm -f utrayss.o

install:
	sudo cp -u ./libstorstack.so /usr/lib/libstorstack.so

rebuild: clean all install