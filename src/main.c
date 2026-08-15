#include "general.h"
#include "netinterfaces.h"
#include <inttypes.h>
#include <sys/wait.h>

int main(int argc, char *argv[]) {
    static const struct option long_options[] = {
        {"verbose",    no_argument,       &verbose_flag, 1},
        {"quite",      no_argument,       &verbose_flag, 0},
        {"silent",     no_argument,       &verbose_flag, 0},
        {"label",      no_argument,       &label_flag,   1},
        {"help",       no_argument,       NULL,          'h'},
        {"version",    no_argument,       NULL,          'v'},
        {"totalbytes", no_argument,       NULL,          't'},
        {"ibytes",     no_argument,       NULL,          'i'},
        {"obytes",     no_argument,       NULL,          'o'},
        {"interface",  required_argument, NULL,          'I'},
        {"command",    required_argument, NULL,          'c'},
        {"limit",      required_argument, NULL,          'l'},
        {"all",        no_argument,       NULL,          'a'},
        {"run",        no_argument,       NULL,          'r'},
        {"human",      no_argument,       NULL,          'H'},
        {NULL,         0,                 NULL,          0}
    };

    char *interface_to_use = NULL;
    char *command = NULL;
    int ch = -1;
    int totalFlag = 0;
    int inFlag = 0;
    int outFlag = 0;
    uint64_t limit = 0;
    int humanFlag = 0;
    int option_index = 0;
    int num_options = 0;
    int runtilComplete = 0;
    COMMAND cmd = BYTES;
    bytesRead = 0;

    while ((ch = getopt_long(argc, argv, "aHI:c:hil:ortv", (struct option *)long_options, &option_index)) != -1) {
        num_options++;
        switch (ch) {
            case 'I':
                interface_to_use = optarg;
                break;
            case 'r':
                runtilComplete = 1;
                break;
            case 'v':
                version();
                return 0;
            case 'c':
                command = optarg;
                break;
            case 't':
                totalFlag = 1;
                break;
            case 'i':
                inFlag = 1;
                break;
            case 'H':
                humanFlag = 1;
                break;
            case 'l': {
                char *endptr = NULL;
                unsigned long long parsed_limit = strtoull(optarg, &endptr, 10);
                if (endptr != optarg) {
                    limit = (uint64_t)parsed_limit;
                }
                break;
            }
            case 'o':
                outFlag = 1;
                break;
            case 'h':
                usage();
                return 0;
            case 0:
                if (long_options[option_index].flag != NULL) {
                    break;
                }
                printf("option %s", long_options[option_index].name);
                if (optarg) {
                    printf(" with arg %s", optarg);
                }
                printf("\n");
                break;
            case 'a':
                break;
            default:
                usage();
                return 0;
        }
    }

#ifdef TEST
    printDEBUG("Going to run tests");
    return run_tests();
#endif

    if (humanFlag == 1 && limit > 0) {
        limit = limit * 1000000ULL;
    }

    if (optind < argc) {
        for (int count = optind; count < argc; ++count) {
            if (strcmp(argv[count], "up") == 0) {
                cmd = UP;
            } else if (strcmp(argv[count], "down") == 0) {
                cmd = DOWN;
            } else if (strcmp(argv[count], "bytes") == 0) {
                cmd = BYTES;
            } else if (strcmp(argv[count], "monitor") == 0) {
                cmd = MONITOR;
            } else if (argv[count][0] != '-' && !interface_to_use) {
                interface_to_use = argv[count];
            } else if (argv[count][0] != '-') {
                printERR("Unknown command '%s'.", argv[count]);
                usage();
                return 0;
            }
        }
    }

    list *interfaceList = NULL;
    printVERBOSE("argc %d num_options %d", argc, num_options);

    if (interface_to_use) {
        printVERBOSE("using selected interface %s", interface_to_use);
        interfaceList = calloc(1, sizeof(list));
        if (interfaceList) {
            struct interface *if_entry = calloc(1, sizeof(struct interface));
            interfaceList->content = if_entry;
            if (if_entry) {
                size_t name_len = strlen(interface_to_use);
                if_entry->name = malloc(name_len + 1);
                if (if_entry->name) {
                    memcpy(if_entry->name, interface_to_use, name_len + 1);
                }

                list *tmpInterfaceList = NULL;
                interfaces(&tmpInterfaceList);
                for (list *root = tmpInterfaceList; root != NULL; root = root->next) {
                    struct interface *cur = (struct interface *)root->content;
                    if (cur && cur->name && strcmp(cur->name, interface_to_use) == 0) {
                        if (cur->if_addr) {
                            if_entry->if_addr = malloc(sizeof(struct sockaddr));
                            if (if_entry->if_addr) {
                                memcpy(if_entry->if_addr, cur->if_addr, sizeof(struct sockaddr));
                            }
                        }
                        if_entry->obytes = cur->obytes;
                        if_entry->ibytes = cur->ibytes;
                        break;
                    }
                }
                if (tmpInterfaceList) {
                    freeInterfaces(&tmpInterfaceList);
                }
            }
        }
    } else {
        printVERBOSE("using all interfaces");
        interfaces(&interfaceList);
    }

    if (verbose_flag) {
        printf("=== Selected interfaces ===\n");
        loopInterfaces(interfaceList, print);
        printf("=== end ===\n");
        if (command) {
            printf("Using command: %s\n", command);
        }
        if (cmd == MONITOR) {
            if (limit == 0) {
                printf("Limit is unlimited.\n");
            } else {
                printf("Limit is %" PRIu64 "\n", limit);
            }
        }
    }

    int ret_status = 0;
    switch (cmd) {
        case UP:
            ret_status = loopInterfaces(interfaceList, set_up);
            if (ret_status != 0) {
                printERR("Failed to turn on interface, make sure you are sudo.");
            }
            break;
        case DOWN:
            ret_status = loopInterfaces(interfaceList, set_down);
            if (ret_status != 0) {
                printERR("Failed to shutdown interface, make sure you are sudo.");
            }
            break;
        case MONITOR: {
            const size_t max_thread_slots = sizeof(threads) / sizeof(threads[0]);
            size_t threadCounter = 0;
            for (list *root = interfaceList; root != NULL && threadCounter < max_thread_slots; root = root->next) {
                struct interface *curr_if = (struct interface *)root->content;
                if (!curr_if || !curr_if->name) {
                    continue;
                }
                pthread_t thread;
                printDEBUG("creating pthread for %s", curr_if->name);
                int create_err = pthread_create(&thread, NULL, monitor, (void *)curr_if->name);
                if (create_err == 0) {
                    pthread_mutex_lock(&thread_mutex);
                    threads[threadCounter++] = thread;
                    pthread_mutex_unlock(&thread_mutex);
                } else {
                    ret_status |= create_err;
                }
            }

            if (ret_status != 0 && threadCounter == 0) {
                printERR("Failed to create monitoring threads.");
                if (geteuid() != 0) {
                    printERR("Try again with sudo.");
                }
                break;
            }

            for (int i = 0; i < 5; ++i) {
                sleep(1);
            }

            printDEBUG("thread count: %d", threadCount());
            if (threadCount() <= 0) {
                printERR("No threads to monitor.");
                if (geteuid() != 0) {
                    printERR("Try again with sudo.");
                }
                ret_status = -1;
                break;
            }

            pid_t pid = runCmd(command);
            if (pid < 0) {
                printERR("Failed to start command.");
                break;
            }

            if (pid > 0 && runtilComplete) {
                printVERBOSE("Running command to completion...");
                int status = 0;
                waitpid(pid, &status, 0);
                printVERBOSE("cmd status: %d", status);

                if (verbose_flag || label_flag) {
                    printf("Total RX+TX: ");
                }

                pthread_mutex_lock(&thread_mutex);
                uint64_t total_captured = bytesRead;
                pthread_mutex_unlock(&thread_mutex);

                if (humanFlag == 1) {
                    double mb = (double)total_captured / 1000000.0;
                    printf("%.2f Mb\n", mb);
                } else {
                    printf("%" PRIu64 "%s\n", total_captured, (verbose_flag || label_flag) ? " bytes" : "");
                }
                break;
            }

            while (true) {
                pthread_mutex_lock(&thread_mutex);
                uint64_t current_bytes = bytesRead;
                pthread_mutex_unlock(&thread_mutex);

                if (limit > 0 && current_bytes >= limit) {
                    printVERBOSE("Byte limit reached (%" PRIu64 " / %" PRIu64 " bytes)", current_bytes, limit);
                    if (pid > 0) {
                        printVERBOSE("Terminating command");
                        kill(pid, SIGTERM);
                        usleep(50000);
                        int status = 0;
                        if (waitpid(pid, &status, WNOHANG) == 0) {
                            kill(pid, SIGKILL);
                            waitpid(pid, &status, 0);
                        }
                    }
                    break;
                }

                if (pid > 0) {
                    int status = 0;
                    pid_t result = waitpid(pid, &status, WNOHANG);
                    if (result != 0) {
                        printVERBOSE("Command finished before limit was reached");
                        break;
                    }
                }

                usleep(20000);
            }
            break;
        }
        default: {
            uint64_t in_bytes = 0;
            uint64_t out_bytes = 0;

            for (list *root = interfaceList; root != NULL; root = root->next) {
                struct interface *curr_if = (struct interface *)root->content;
                if (curr_if) {
                    in_bytes += (uint64_t)curr_if->ibytes;
                    out_bytes += (uint64_t)curr_if->obytes;
                }
            }

            if (humanFlag == 1) {
                double in_mb = (double)in_bytes / 1000000.0;
                double out_mb = (double)out_bytes / 1000000.0;

                if (inFlag == 1) {
                    if (verbose_flag || label_flag) printf("RX: ");
                    printf("%.2f Mb\n", in_mb);
                } else if (outFlag == 1) {
                    if (verbose_flag || label_flag) printf("TX: ");
                    printf("%.2f Mb\n", out_mb);
                } else {
                    if (verbose_flag || label_flag) printf("RX+TX: ");
                    printf("%.2f Mb\n", in_mb + out_mb);
                }
            } else {
                if (inFlag == 1) {
                    if (verbose_flag || label_flag) printf("RX: ");
                    printf("%" PRIu64 "%s\n", in_bytes, (verbose_flag || label_flag) ? " bytes" : "");
                } else if (outFlag == 1) {
                    if (verbose_flag || label_flag) printf("TX: ");
                    printf("%" PRIu64 "%s\n", out_bytes, (verbose_flag || label_flag) ? " bytes" : "");
                } else {
                    if (verbose_flag || label_flag) printf("RX+TX: ");
                    printf("%" PRIu64 "%s\n", in_bytes + out_bytes, (verbose_flag || label_flag) ? " bytes" : "");
                }
            }
            break;
        }
    }

    if (interfaceList) {
        freeInterfaces(&interfaceList);
    }

    return ret_status;
}