#include "general.h"

static const char *const VERSION = "1.0";

void version(void) {
    println("netman %s", VERSION);
}

void usage(void) {
    println("usage: netman [interface] [options...] [command]");

    println("\nCommands:");
    println("  bytes                 Print the byte information for the specified interface(s)\n"
            "                        and exit. (default)");
    println("  up                    Turn the selected interface(s) up and exit. (privileged)");
    println("  down                  Turn the selected interface(s) down and exit. (privileged)");
    println("  monitor               Monitor the selected interface(s). (privileged)");

    println("\nOptions:");
    println("  -v, --version         Print the version number of netman and exit.");
    println("  --quite, --silent     Don't echo commands.");
    println("  --verbose             Echo additional messages.");
    println("  --help                Print this message and exit.");
    println("  --label               Print byte labels.");
    println("  -H                    Use (decimal) megabytes instead of bytes.");

    println("\nbyte Options:");
    println("  -t, --totalbytes      Print the (RX + TX) bytes. (default)");
    println("  -i, --ibytes          Print the RX bytes.");
    println("  -o, --obytes          Print the TX bytes.");

    println("\nmonitor Options:");
    println("  -l, --limit           The byte limit. In MB if -H is set, otherwise B.");
    println("  -c, --command         Command to run.");
    println("  --run                 Run the specified command until completion then print\n"
            "                        the total RX + TX bytes. This ignores any limit set.");
}

void removeThread(void) {
    pthread_t self = pthread_self();
    pthread_mutex_lock(&thread_mutex);
    const size_t max_threads = sizeof(threads) / sizeof(threads[0]);
    for (size_t i = 0; i < max_threads; ++i) {
        if (pthread_equal(threads[i], self)) {
            threads[i] = (pthread_t)0;
            break;
        }
    }
    pthread_mutex_unlock(&thread_mutex);
}

int threadCount(void) {
    int count = 0;
    pthread_mutex_lock(&thread_mutex);
    const size_t max_threads = sizeof(threads) / sizeof(threads[0]);
    for (size_t i = 0; i < max_threads; ++i) {
        if (threads[i] != (pthread_t)0) {
            ++count;
        }
    }
    pthread_mutex_unlock(&thread_mutex);
    return count;
}

pid_t runCmd(char *cmd) {
    if (!cmd || *cmd == '\0') {
        return 0;
    }

    pid_t pid = fork();
    if (pid == 0) {
        if (geteuid() == 0) {
            const char *env = getenv("SUDO_UID");
            if (!env) {
                printERR("Failed to get SUDO_UID environment variable.");
                _exit(ERR_SUDO);
            }

            char *endptr = NULL;
            errno = 0;
            long uid_val = strtol(env, &endptr, 10);
            if (errno != 0 || endptr == env || *endptr != '\0' || uid_val < 0) {
                printERR("Failed to convert SUDO_UID environment variable to a valid number.");
                _exit(ERR_SUDO);
            }

            uid_t uid = (uid_t)uid_val;
            if (setreuid(uid, uid) != 0) {
                printERR("Failed to drop root privileges to UID %u.", (unsigned int)uid);
                _exit(ERR_UID);
            }
        }

        printVERBOSE("Starting command '%s'", cmd);
        printVERBOSE("uid %d; euid %d; cmd '%s'", (int)getuid(), (int)geteuid(), cmd);
        char *const parmList[] = {"sh", "-c", cmd, NULL};
        execv("/bin/sh", parmList);
        _exit(127);
    } else if (pid < 0) {
        printERR("Failed to fork process.");
        return ERR_FORK;
    }

    return pid;
}

void *monitor(void *ifname) {
    if (!ifname) {
        return (void *)(intptr_t)ERR_NULL;
    }

    char *name = (char *)ifname;
    printVERBOSE("[%s] Opening BPF device.", name);

    int fd = open_dev();
    if (fd < 0) {
        printVERBOSE("[%s] Unable to open BPF device: %s", name, strerror(errno));
        removeThread();
        return (void *)(intptr_t)ERR_OPEN;
    }

    printVERBOSE("[%s] Configuring BPF device options.", name);
    if (set_options(fd, name) < 0) {
        printVERBOSE("[%s] Unable to set BPF options: %s", name, strerror(errno));
        close(fd);
        removeThread();
        return (void *)(intptr_t)ERR_OPTIONS;
    }

    printVERBOSE("[%s] Checking datalink type.", name);
    if (check_dlt(fd, name) < 0) {
        close(fd);
        removeThread();
        return (void *)(intptr_t)ERR_DLT;
    }

    printVERBOSE("[%s] Starting packet capture.", name);
    int read_res = read_packets(fd, name);

    printVERBOSE("[%s] Finished packet capture.", name);
    close(fd);
    removeThread();
    return (void *)(intptr_t)read_res;
}

int open_dev_at(int start) {
    char dev[32];
    int start_idx = (start < 0) ? 0 : start;

    for (int i = start_idx; i < 256; ++i) {
        snprintf(dev, sizeof(dev), "/dev/bpf%d", i);
        int fd = open(dev, O_RDWR);
        if (fd >= 0) {
            return fd;
        }

        if (errno != EBUSY) {
            return -1;
        }
    }

    errno = ENOENT;
    return -1;
}

int open_dev(void) {
    return open_dev_at(0);
}

int check_dlt(int fd, char *name) {
    if (fd < 0 || !name) {
        return ERR_DLT;
    }

    uint32_t dlt = 0;
    if (ioctl(fd, BIOCGDLT, &dlt) < 0) {
        return ERR_DLT;
    }

    switch (dlt) {
        case DLT_EN10MB:
            return 0;
        case DLT_NULL:
            printVERBOSE("Unsupported NULL datalink type for %s.", name);
            break;
        case DLT_EN3MB:
            printVERBOSE("Unsupported EN3MB datalink type for %s.", name);
            break;
        case DLT_AX25:
            printVERBOSE("Unsupported AX25 datalink type for %s.", name);
            break;
        case DLT_PRONET:
            printVERBOSE("Unsupported PRONET datalink type for %s.", name);
            break;
        case DLT_CHAOS:
            printVERBOSE("Unsupported CHAOS datalink type for %s.", name);
            break;
        case DLT_IEEE802:
            printVERBOSE("Unsupported IEEE802 datalink type for %s.", name);
            break;
        case DLT_ARCNET:
            printVERBOSE("Unsupported ARCNET datalink type for %s.", name);
            break;
        case DLT_SLIP:
            printVERBOSE("Unsupported SLIP datalink type for %s.", name);
            break;
        case DLT_PPP:
            printVERBOSE("Unsupported PPP datalink type for %s.", name);
            break;
        case DLT_FDDI:
            printVERBOSE("Unsupported FDDI datalink type for %s.", name);
            break;
        case DLT_ATM_RFC1483:
            printVERBOSE("Unsupported ATM_RFC1483 datalink type for %s.", name);
            break;
        case DLT_RAW:
            printVERBOSE("Unsupported RAW datalink type for %s.", name);
            break;
        default:
            printVERBOSE("Unsupported datalink type 0x%x for %s.", dlt, name);
            break;
    }

    return ERR_DLT;
}

int set_options(int fd, char *iface) {
    if (!iface || fd < 0) {
        return ERR_NULL;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", iface);

    if (ioctl(fd, BIOCSETIF, &ifr) < 0) {
        return ERR_SETIF;
    }

    uint32_t enable = 1;
    if (ioctl(fd, BIOCSHDRCMPLT, &enable) < 0) {
        return ERR_HDRCMPLT;
    }

    if (ioctl(fd, BIOCSSEESENT, &enable) < 0) {
        return ERR_BIDIRECTION;
    }

    if (ioctl(fd, BIOCIMMEDIATE, &enable) < 0) {
        return ERR_IMMEDIATE;
    }

    return 0;
}

int read_packets(int fd, char *iface) {
    if (!iface || fd < 0) {
        return ERR_NULL;
    }

    size_t blen = 0;
    if (ioctl(fd, BIOCGBLEN, &blen) < 0 || blen == 0) {
        return ERR_BLEN;
    }

    char *buf = malloc(blen);
    if (!buf) {
        return ERR_ALLOC;
    }

    printVERBOSE("Reading packets for '%s'...", iface);

    while (true) {
        ssize_t n = read(fd, buf, blen);
        if (n <= 0) {
            free(buf);
            return ERR_READ;
        }

        char *p = buf;
        while (p < buf + n) {
            struct bpf_hdr *bh = (struct bpf_hdr *)p;

            pthread_mutex_lock(&thread_mutex);
            bytesRead += (uint64_t)bh->bh_caplen;
            uint64_t current_total = bytesRead;
            pthread_mutex_unlock(&thread_mutex);

            struct ether_header *eh = (struct ether_header *)(p + bh->bh_hdrlen);

            printVERBOSE("%s: %02x:%02x:%02x:%02x:%02x:%02x -> "
                         "%02x:%02x:%02x:%02x:%02x:%02x "
                         "[type=%u] [len=%u/%u (%llu)]",
                         iface,
                         eh->ether_shost[0], eh->ether_shost[1], eh->ether_shost[2],
                         eh->ether_shost[3], eh->ether_shost[4], eh->ether_shost[5],
                         eh->ether_dhost[0], eh->ether_dhost[1], eh->ether_dhost[2],
                         eh->ether_dhost[3], eh->ether_dhost[4], eh->ether_dhost[5],
                         eh->ether_type, (unsigned int)bh->bh_datalen, (unsigned int)bh->bh_caplen,
                         (unsigned long long)current_total);

            p += BPF_WORDALIGN(bh->bh_hdrlen + bh->bh_caplen);
        }
    }

    free(buf);
    return 0;
}