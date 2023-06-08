#ifndef UTRAY_H
#define UTRAY_H

#include <sys/types.h>
#include <string.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include "klibss.h"

#include "spdk/stdinc.h"
#include "spdk/nvme.h"
#include "spdk/nvme_spec.h"
#include "spdk/env.h"
#include "spdk/string.h"
#include "spdk/log.h"

#define NO_ROOT -1
#define SEND_ERROR -2
#define SUCCESS 0

#endif