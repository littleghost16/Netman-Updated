#include "general.h"
#include "netinterfaces.h"

int getInterfaceStatus(char *interface) {
    if (!interface || !*interface) {
        return ERR_NULL;
    }

    int fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        printERR("Unable to create socket.");
        return ERR_SOCKET;
    }

    struct ifreq ethreq;
    memset(&ethreq, 0, sizeof(ethreq));
    snprintf(ethreq.ifr_name, sizeof(ethreq.ifr_name), "%s", interface);

    int res = ioctl(fd, SIOCGIFFLAGS, &ethreq);
    int err_save = errno;
    close(fd);

    if (res < 0) {
        errno = err_save;
        printERR("Unable to check %s's status.", interface);
        return res;
    }

    return (ethreq.ifr_flags & IFF_UP) ? 0 : 1;
}

int isInterfaceUp(char *interface) {
    return getInterfaceStatus(interface) == 0 ? 1 : 0;
}

void interfaces(list **interfaces) {
    if (!interfaces) {
        return;
    }

    struct ifaddrs *ifap = NULL;
    if (getifaddrs(&ifap) != 0 || !ifap) {
        return;
    }

    list *prev = NULL;
    for (struct ifaddrs *itmp = ifap; itmp; itmp = itmp->ifa_next) {
        if (itmp->ifa_data != NULL && itmp->ifa_addr != NULL && itmp->ifa_addr->sa_family == AF_LINK) {
            list *tmp = malloc(sizeof(list));
            if (!tmp) {
                continue;
            }

            struct interface *info = calloc(1, sizeof(struct interface));
            if (!info) {
                free(tmp);
                continue;
            }

            tmp->content = info;
            tmp->next = NULL;

            if (itmp->ifa_name) {
                size_t name_len = strlen(itmp->ifa_name);
                info->name = malloc(name_len + 1);
                if (info->name) {
                    memcpy(info->name, itmp->ifa_name, name_len + 1);
                }
            }

            info->if_addr = malloc(sizeof(struct sockaddr));
            if (info->if_addr) {
                memcpy(info->if_addr, itmp->ifa_addr, sizeof(struct sockaddr));
            }

            struct if_data *data = (struct if_data *)itmp->ifa_data;
            info->obytes = data->ifi_obytes;
            info->ibytes = data->ifi_ibytes;

            if (*interfaces == NULL) {
#ifdef DEBUG
                printDEBUG("name %s ibytes: %u obytes: %u %p", itmp->ifa_name, data->ifi_ibytes, data->ifi_obytes, (void *)tmp);
#endif
                *interfaces = tmp;
                prev = tmp;
            } else if (prev) {
#ifdef DEBUG
                printDEBUG("name %s ibytes: %u obytes: %u %p %p", itmp->ifa_name, data->ifi_ibytes, data->ifi_obytes, (void *)tmp, (void *)prev);
#endif
                prev->next = tmp;
                prev = tmp;
            }
        }
    }

    freeifaddrs(ifap);
}

void freeInterface(struct interface *i) {
    if (!i) {
        return;
    }

    if (i->name) {
        free(i->name);
        i->name = NULL;
    }
    if (i->if_addr) {
        free(i->if_addr);
        i->if_addr = NULL;
    }
    free(i);
}

void freeInterfaces(list **interfaces) {
    if (!interfaces || !*interfaces) {
        return;
    }

    list *root = *interfaces;
    while (root != NULL) {
        list *next = root->next;
        if (root->content) {
            freeInterface((struct interface *)root->content);
        }
        free(root);
        root = next;
    }

    *interfaces = NULL;
}

int loopInterfaces(list *interfaces, int (*f)(struct interface *)) {
    if (!f) {
        return 0;
    }

    list *root = interfaces;
    int ans = 0;
    while (root != NULL) {
        if (root->content) {
            ans |= f((struct interface *)root->content);
        }
        root = root->next;
    }
    return ans;
}

int print(struct interface *i) {
    if (!i) {
        return 0;
    }

    printf("interface <%p>: { %s,  ibytes: %lu, obytes: %lu }\n",
           (void *)i,
           i->name ? i->name : "unknown",
           i->ibytes,
           i->obytes);
    return 0;
}

int set_if_flags(char *ifname, short flags) {
    if (!ifname || !*ifname) {
        return ERR_NULL;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = flags;
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", ifname);

    int skfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (skfd < 0) {
        printERR("Unable to create socket.");
        return -1;
    }

    int res = ioctl(skfd, SIOCSIFFLAGS, &ifr);
    int err_save = errno;
    close(skfd);

    if (res < 0) {
        errno = err_save;
        printERR("Interface '%s' SIOCSIFFLAGS failed.", ifname);
    } else {
        printVERBOSE("Interface '%s': flags set to %04X.", ifname, (unsigned short)flags);
    }

    return res;
}

int set_if_up(char *ifname, short flags) {
    return set_if_flags(ifname, (short)(flags | IFF_UP));
}

int set_up(struct interface *i) {
    if (!i || !i->name) {
        return ERR_NULL;
    }
    return set_if_up(i->name, 0);
}

int set_if_down(char *ifname, short flags) {
    return set_if_flags(ifname, (short)(flags & ~IFF_UP));
}

int set_down(struct interface *i) {
    if (!i || !i->name) {
        return ERR_NULL;
    }
    return set_if_down(i->name, 0);
}
