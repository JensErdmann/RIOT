/*
 * Copyright (C) 2015 Freie Universität Berlin
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     sys_shell_commands
 * @{
 *
 * @file
 * @brief       Shell commands for interacting with network devices
 *
 * @author      Hauke Petersen <hauke.petersen@fu-berlin.de>
 * @author      Martine Lenders <mlenders@inf.fu-berlin.de>
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "thread.h"
#include "net/ipv6/addr.h"
#include "net/gnrc/ipv6/netif.h"
#include "net/gnrc/netif.h"
#include "net/gnrc/netapi.h"
#include "net/netopt.h"
#include "net/gnrc/pkt.h"
#include "net/gnrc/pktbuf.h"
#include "net/gnrc/netif/hdr.h"
#include "net/gnrc/sixlowpan/netif.h"

/**
 * @brief   The maximal expected link layer address length in byte
 */
#define MAX_ADDR_LEN            (8U)

/**
 * @brief   The default IPv6 prefix length if not specified.
 */
#define SC_NETIF_IPV6_DEFAULT_PREFIX_LEN     (64)

/* utility functions */
static bool _is_number(char *str)
{
    for (; *str; str++) {
        if (*str < '0' || *str > '9') {
            return false;
        }
    }

    return true;
}

static bool _is_iface(kernel_pid_t dev)
{
    kernel_pid_t ifs[GNRC_NETIF_NUMOF];
    size_t numof = gnrc_netif_get(ifs);

    for (size_t i = 0; i < numof && i < GNRC_NETIF_NUMOF; i++) {
        if (ifs[i] == dev) {
            return true;
        }
    }

    return false;
}

static void _set_usage(char *cmd_name)
{
    printf("usage: %s <if_id> set <key> <value>\n", cmd_name);
    puts("      Sets an hardware specific specific value\n"
         "      <key> may be one of the following\n"
         "       * \"addr\" - sets (short) address\n"
         "       * \"addr_long\" - sets long address\n"
         "       * \"addr_short\" - alias for \"addr\"\n"
         "       * \"channel\" - sets the frequency channel\n"
         "       * \"chan\" - alias for \"channel\"\n"
         "       * \"csma_retries\" - set max. number of channel access attempts\n"
         "       * \"nid\" - sets the network identifier (or the PAN ID)\n"
         "       * \"pan\" - alias for \"nid\"\n"
         "       * \"pan_id\" - alias for \"nid\"\n"
         "       * \"power\" - TX power in dBm\n"
         "       * \"src_len\" - sets the source address length in byte\n"
         "       * \"state\" - set the device state\n");
}

static void _mtu_usage(char *cmd_name)
{
    printf("usage: %s <if_id> mtu <n>\n", cmd_name);
}

static void _flag_usage(char *cmd_name)
{
    printf("usage: %s <if_id> [-]{promisc|autoack|csma|autocca|preload|6lo|iphc}\n", cmd_name);
}

static void _add_usage(char *cmd_name)
{
    printf("usage: %s <if_id> add [anycast|multicast|unicast] "
           "<ipv6_addr>[/prefix_len]\n", cmd_name);
}

static void _del_usage(char *cmd_name)
{
    printf("usage: %s <if_id> del <ipv6_addr>\n",
           cmd_name);
}

static void _print_netopt(netopt_t opt)
{
    switch (opt) {
        case NETOPT_ADDRESS:
            printf("(short) address");
            break;

        case NETOPT_ADDRESS_LONG:
            printf("long address");
            break;

        case NETOPT_SRC_LEN:
            printf("source address length");
            break;

        case NETOPT_CHANNEL:
            printf("channel");
            break;

        case NETOPT_NID:
            printf("network identifier");
            break;

        case NETOPT_TX_POWER:
            printf("TX power [in dBm]");
            break;

        case NETOPT_CSMA_RETRIES:
            printf("CSMA retries");
            break;

        default:
            /* we don't serve these options here */
            break;
    }
}

static void _print_netopt_state(netopt_state_t state)
{
    switch (state) {
        case NETOPT_STATE_OFF:
            printf("OFF");
            break;
        case NETOPT_STATE_SLEEP:
            printf("SLEEP");
            break;
        case NETOPT_STATE_IDLE:
            printf("IDLE");
            break;
        case NETOPT_STATE_RX:
            printf("RX");
            break;
        case NETOPT_STATE_TX:
            printf("TX");
            break;
        case NETOPT_STATE_RESET:
            printf("RESET");
            break;
        default:
            /* nothing to do then */
            break;
    }
}

static void _netif_list(kernel_pid_t dev)
{
    uint8_t hwaddr[MAX_ADDR_LEN];
    uint16_t u16;
    int16_t i16;
    uint8_t u8;
    int res;
    netopt_state_t state;
    netopt_enable_t enable;
    bool linebreak = false;
#ifdef MODULE_GNRC_IPV6_NETIF
    gnrc_ipv6_netif_t *entry = gnrc_ipv6_netif_get(dev);
    char ipv6_addr[IPV6_ADDR_MAX_STR_LEN];
#endif


    printf("Iface %2d  ", dev);

    res = gnrc_netapi_get(dev, NETOPT_ADDRESS, 0, hwaddr, sizeof(hwaddr));

    if (res >= 0) {
        char hwaddr_str[res * 3];
        printf(" HWaddr: ");
        printf("%s", gnrc_netif_addr_to_str(hwaddr_str, sizeof(hwaddr_str),
                                            hwaddr, res));
        printf(" ");
    }

    res = gnrc_netapi_get(dev, NETOPT_CHANNEL, 0, &u16, sizeof(u16));

    if (res >= 0) {
        printf(" Channel: %" PRIu16 " ", u16);
    }

    res = gnrc_netapi_get(dev, NETOPT_NID, 0, &u16, sizeof(u16));

    if (res >= 0) {
        printf(" NID: 0x%" PRIx16 " ", u16);
    }

    res = gnrc_netapi_get(dev, NETOPT_TX_POWER, 0, &i16, sizeof(i16));

    if (res >= 0) {
        printf(" TX-Power: %" PRIi16 "dBm ", i16);
    }

    res = gnrc_netapi_get(dev, NETOPT_STATE, 0, &state, sizeof(state));

    if (res >= 0) {
        printf(" State: ");
        _print_netopt_state(state);
    }

    res = gnrc_netapi_get(dev, NETOPT_CSMA_RETRIES, 0, &u8, sizeof(u8));

    if (res >= 0) {
        res = gnrc_netapi_get(dev, NETOPT_CSMA, 0, &enable, sizeof(enable));
        if ((res >= 0) && (enable == NETOPT_ENABLE)) {
            printf(" CSMA Retries: %" PRIu8 " ", *((uint8_t *) &u8));
        }
    }

    printf("\n           ");

    res = gnrc_netapi_get(dev, NETOPT_ADDRESS_LONG, 0, hwaddr, sizeof(hwaddr));

    if (res >= 0) {
        char hwaddr_str[res * 3];
        printf("Long HWaddr: ");
        printf("%s ", gnrc_netif_addr_to_str(hwaddr_str, sizeof(hwaddr_str),
                                             hwaddr, res));
        linebreak = true;
    }

    if (linebreak) {
        printf("\n           ");
    }

    res = gnrc_netapi_get(dev, NETOPT_PROMISCUOUSMODE, 0, &enable, sizeof(enable));

    if ((res >= 0) && (enable == NETOPT_ENABLE)) {
        printf("PROMISC  ");
        linebreak = true;
    }

    res = gnrc_netapi_get(dev, NETOPT_AUTOACK, 0, &enable, sizeof(enable));

    if ((res >= 0) && (enable == NETOPT_ENABLE)) {
        printf("AUTOACK  ");
        linebreak = true;
    }

    res = gnrc_netapi_get(dev, NETOPT_PRELOADING, 0, &enable, sizeof(enable));

    if ((res >= 0) && (enable == NETOPT_ENABLE)) {
        printf("PRELOAD  ");
        linebreak = true;
    }

    res = gnrc_netapi_get(dev, NETOPT_RAWMODE, 0, &enable, sizeof(enable));

    if ((res >= 0) && (enable == NETOPT_ENABLE)) {
        printf("RAWMODE  ");
        linebreak = true;
    }

    res = gnrc_netapi_get(dev, NETOPT_CSMA, 0, &enable, sizeof(enable));

    if ((res >= 0) && (enable == NETOPT_ENABLE)) {
        printf("CSMA  ");
        linebreak = true;
    }

    res = gnrc_netapi_get(dev, NETOPT_AUTOCCA, 0, &enable, sizeof(enable));

    if ((res >= 0) && (enable == NETOPT_ENABLE)) {
        printf("AUTOCCA  ");
        linebreak = true;
    }

#ifdef MODULE_GNRC_IPV6_NETIF
    if (entry != NULL) {
        printf("MTU:%" PRIu16 "  ", entry->mtu);
        if (entry->flags & GNRC_IPV6_NETIF_FLAGS_SIXLOWPAN) {
            printf("6LO  ");
        }
        linebreak = true;
    }
#endif

#if defined(MODULE_GNRC_SIXLOWPAN_NETIF) && defined(MODULE_GNRC_SIXLOWPAN_IPHC)
    gnrc_sixlowpan_netif_t *sixlo_entry = gnrc_sixlowpan_netif_get(dev);

    if ((sixlo_entry != NULL) && (sixlo_entry->iphc_enabled)) {
        printf("IPHC  ");
        linebreak = true;
    }
#endif

    if (linebreak) {
        printf("\n           ");
    }

    res = gnrc_netapi_get(dev, NETOPT_SRC_LEN, 0, &u16, sizeof(u16));

    if (res >= 0) {
        printf("Source address length: %" PRIu16 "\n           ", u16);
    }

#ifdef MODULE_GNRC_IPV6_NETIF
    if (entry == NULL) {
        puts("");
        return;
    }

    printf("Link type: %s", (entry->flags & GNRC_IPV6_NETIF_FLAGS_IS_WIRED) ?
           "wired" : "wireless");
    printf("\n           ");

    for (int i = 0; i < GNRC_IPV6_NETIF_ADDR_NUMOF; i++) {
        if (!ipv6_addr_is_unspecified(&entry->addrs[i].addr)) {
            printf("inet6 addr: ");

            if (ipv6_addr_to_str(ipv6_addr, &entry->addrs[i].addr,
                                 IPV6_ADDR_MAX_STR_LEN)) {
                printf("%s/%" PRIu8 "  scope: ", ipv6_addr,
                       entry->addrs[i].prefix_len);

                if ((ipv6_addr_is_link_local(&entry->addrs[i].addr))) {
                    printf("local");
                }
                else {
                    printf("global");
                }

                if (entry->addrs[i].flags & GNRC_IPV6_NETIF_ADDR_FLAGS_NON_UNICAST) {
                    if (ipv6_addr_is_multicast(&entry->addrs[i].addr)) {
                        printf(" [multicast]");
                    }
                    else {
                        printf(" [anycast]");
                    }
                }
            }
            else {
                printf("error in conversion");
            }

            printf("\n           ");
        }
    }
#endif

    puts("");
}

static int _netif_set_u16(kernel_pid_t dev, netopt_t opt, char *u16_str)
{
    unsigned int res;
    bool hex = false;

    if (_is_number(u16_str)) {
        if ((res = strtoul(u16_str, NULL, 10)) == ULONG_MAX) {
            puts("error: unable to parse value.\n"
                 "Must be a 16-bit unsigned integer (dec or hex)\n");
            return 1;
        }
    }
    else {
        if ((res = strtoul(u16_str, NULL, 16)) == ULONG_MAX) {
            puts("error: unable to parse value.\n"
                 "Must be a 16-bit unsigned integer (dec or hex)\n");
            return 1;
        }

        hex = true;
    }

    if (res > 0xffff) {
        puts("error: unable to parse value.\n"
             "Must be a 16-bit unsigned integer (dec or hex)\n");
        return 1;
    }

    if (gnrc_netapi_set(dev, opt, 0, (uint16_t *)&res, sizeof(uint16_t)) < 0) {
        printf("error: unable to set ");
        _print_netopt(opt);
        puts("");
        return 1;
    }

    printf("success: set ");
    _print_netopt(opt);
    printf(" on interface %" PRIkernel_pid " to ", dev);

    if (hex) {
        printf("0x%04x\n", res);
    }
    else {
        printf("%u\n", res);
    }

    return 0;
}

static int _netif_set_i16(kernel_pid_t dev, netopt_t opt, char *i16_str)
{
    int16_t val = (int16_t)atoi(i16_str);

    if (gnrc_netapi_set(dev, opt, 0, (int16_t *)&val, sizeof(int16_t)) < 0) {
        printf("error: unable to set ");
        _print_netopt(opt);
        puts("");
        return 1;
    }

    printf("success: set ");
    _print_netopt(opt);
    printf(" on interface %" PRIkernel_pid " to %i\n", dev, val);

    return 0;
}

static int _netif_set_u8(kernel_pid_t dev, netopt_t opt, char *u8_str)
{
    uint8_t val = (uint8_t)atoi(u8_str);

    if (gnrc_netapi_set(dev, opt, 0, (uint8_t *)&val, sizeof(uint8_t)) < 0) {
        printf("error: unable to set ");
        _print_netopt(opt);
        puts("");
        return 1;
    }

    printf("success: set ");
    _print_netopt(opt);
    printf(" on interface %" PRIkernel_pid " to %i\n", dev, val);

    return 0;
}

static int _netif_set_flag(kernel_pid_t dev, netopt_t opt,
                           netopt_enable_t set)
{
    if (gnrc_netapi_set(dev, opt, 0, &set, sizeof(netopt_enable_t)) < 0) {
        puts("error: unable to set option");
        return 1;
    }
    printf("success: %sset option\n", (set) ? "" : "un");
    return 0;
}

static int _netif_set_addr(kernel_pid_t dev, netopt_t opt, char *addr_str)
{
    uint8_t addr[MAX_ADDR_LEN];
    size_t addr_len = gnrc_netif_addr_from_str(addr, sizeof(addr), addr_str);

    if (addr_len == 0) {
        puts("error: unable to parse address.\n"
             "Must be of format [0-9a-fA-F]{2}(:[0-9a-fA-F]{2})*\n"
             "(hex pairs delimited by colons)");
        return 1;
    }

    if (gnrc_netapi_set(dev, opt, 0, addr, addr_len) < 0) {
        printf("error: unable to set ");
        _print_netopt(opt);
        puts("");
        return 1;
    }

    printf("success: set ");
    _print_netopt(opt);
    printf(" on interface %" PRIkernel_pid " to %s\n", dev, addr_str);

    return 0;
}

static int _netif_set_state(kernel_pid_t dev, char *state_str)
{
    netopt_state_t state;
    if ((strcmp("off", state_str) == 0) || (strcmp("OFF", state_str) == 0)) {
        state = NETOPT_STATE_OFF;
    }
    else if ((strcmp("sleep", state_str) == 0) ||
             (strcmp("SLEEP", state_str) == 0)) {
        state = NETOPT_STATE_SLEEP;
    }
    else if ((strcmp("idle", state_str) == 0) ||
             (strcmp("IDLE", state_str) == 0)) {
        state = NETOPT_STATE_IDLE;
    }
    else if ((strcmp("reset", state_str) == 0) ||
             (strcmp("RESET", state_str) == 0)) {
        state = NETOPT_STATE_RESET;
    }
    else {
        puts("usage: ifconfig <if_id> set state [off|sleep|idle|reset]");
        return 1;
    }
    if (gnrc_netapi_set(dev, NETOPT_STATE, 0,
                        &state, sizeof(netopt_state_t)) < 0) {
        printf("error: unable to set state to ");
        _print_netopt_state(state);
        puts("");
        return 1;
    }
    printf("success: set state of interface %" PRIkernel_pid " to ", dev);
    _print_netopt_state(state);
    puts("");

    return 0;
}

static int _netif_set(char *cmd_name, kernel_pid_t dev, char *key, char *value)
{
    if ((strcmp("addr", key) == 0) || (strcmp("addr_short", key) == 0)) {
        return _netif_set_addr(dev, NETOPT_ADDRESS, value);
    }
    else if (strcmp("addr_long", key) == 0) {
        return _netif_set_addr(dev, NETOPT_ADDRESS_LONG, value);
    }
    else if ((strcmp("channel", key) == 0) || (strcmp("chan", key) == 0)) {
        return _netif_set_u16(dev, NETOPT_CHANNEL, value);
    }
    else if ((strcmp("nid", key) == 0) || (strcmp("pan", key) == 0) ||
             (strcmp("pan_id", key) == 0)) {
        return _netif_set_u16(dev, NETOPT_NID, value);
    }
    else if (strcmp("power", key) == 0) {
        return _netif_set_i16(dev, NETOPT_TX_POWER, value);
    }
    else if (strcmp("src_len", key) == 0) {
        return _netif_set_u16(dev, NETOPT_SRC_LEN, value);
    }
    else if (strcmp("state", key) == 0) {
        return _netif_set_state(dev, value);
    }
    else if (strcmp("csma_retries", key) == 0) {
        return _netif_set_u8(dev, NETOPT_CSMA_RETRIES, value);
    }

    _set_usage(cmd_name);
    return 1;
}

static int _netif_flag(char *cmd, kernel_pid_t dev, char *flag)
{
    netopt_enable_t set = NETOPT_ENABLE;

    if (flag[0] == '-') {
        set = NETOPT_DISABLE;
        flag++;
    }

    if (strcmp(flag, "promisc") == 0) {
        return _netif_set_flag(dev, NETOPT_PROMISCUOUSMODE, set);
    }
    else if (strcmp(flag, "preload") == 0) {
        return _netif_set_flag(dev, NETOPT_PRELOADING, set);
    }
    else if (strcmp(flag, "autoack") == 0) {
        return _netif_set_flag(dev, NETOPT_AUTOACK, set);
    }
    else if (strcmp(flag, "raw") == 0) {
        return _netif_set_flag(dev, NETOPT_RAWMODE, set);
    }
    else if (strcmp(flag, "csma") == 0) {
        return _netif_set_flag(dev, NETOPT_CSMA, set);
    }
    else if (strcmp(flag, "autocca") == 0) {
        return _netif_set_flag(dev, NETOPT_AUTOCCA, set);
    }
    else if (strcmp(flag, "6lo") == 0) {
#ifdef MODULE_GNRC_IPV6_NETIF
        gnrc_ipv6_netif_t *entry = gnrc_ipv6_netif_get(dev);

        if (entry == NULL) {
            puts("error: unable to (un)set 6LoWPAN support");
            return 1;
        }

        mutex_lock(&entry->mutex);

        if (set) {
            entry->flags |= GNRC_IPV6_NETIF_FLAGS_SIXLOWPAN;
            printf("success: set 6LoWPAN support on interface %" PRIkernel_pid
                   "\n", dev);
        }
        else {
            entry->flags &= ~GNRC_IPV6_NETIF_FLAGS_SIXLOWPAN;
            printf("success: unset 6LoWPAN support on interface %" PRIkernel_pid
                   "\n", dev);
        }

        mutex_unlock(&entry->mutex);

        return 0;
#else
        puts("error: unable to (un)set 6LoWPAN support");

        return 1;
#endif
    }
    else if (strcmp(flag, "iphc") == 0) {
#if defined(MODULE_GNRC_SIXLOWPAN_NETIF) && defined(MODULE_GNRC_SIXLOWPAN_IPHC)
        gnrc_sixlowpan_netif_t *entry = gnrc_sixlowpan_netif_get(dev);

        if (entry == NULL) {
            puts("error: unable to (un)set IPHC");
            return 1;
        }

        if (set) {
            entry->iphc_enabled = true;
            printf("success: enable IPHC on interface %" PRIkernel_pid "\n", dev);
        }
        else {
            entry->iphc_enabled = false;
            printf("success: disable IPHC on interface %" PRIkernel_pid "\n", dev);
        }
        return 0;
#else
        puts("error: unable to (un)set IPHC.");
        return 1;
#endif
    }

    _flag_usage(cmd);
    return 1;
}

#ifdef MODULE_GNRC_IPV6_NETIF
static uint8_t _get_prefix_len(char *addr)
{
    int prefix_len = SC_NETIF_IPV6_DEFAULT_PREFIX_LEN;

    while ((*addr != '/') && (*addr != '\0')) {
        addr++;
    }

    if (*addr == '/') {
        *addr = '\0';
        prefix_len = atoi(addr + 1);

        if ((prefix_len < 1) || (prefix_len > IPV6_ADDR_BIT_LEN)) {
            prefix_len = SC_NETIF_IPV6_DEFAULT_PREFIX_LEN;
        }
    }

    return prefix_len;
}
#endif

static int _netif_add(char *cmd_name, kernel_pid_t dev, int argc, char **argv)
{
#ifdef MODULE_GNRC_IPV6_NETIF
    enum {
        _UNICAST = 0,
        _MULTICAST,     /* multicast value just to check if given addr is mc */
        _ANYCAST
    } type = _UNICAST;
    char *addr_str = argv[0];
    ipv6_addr_t addr;
    ipv6_addr_t *assigned_address;
    uint8_t prefix_len;

    if (argc > 1) {
        if (strcmp(argv[0], "anycast") == 0) {
            type = _ANYCAST;
            addr_str = argv[1];
        }
        else if (strcmp(argv[0], "multicast") == 0) {
            type = _MULTICAST;
            addr_str = argv[1];
        }
        else if (strcmp(argv[0], "unicast") == 0) {
            /* type already set to unicast */
            addr_str = argv[1];
        }
        else {
            _add_usage(cmd_name);
            return 1;
        }
    }

    prefix_len = _get_prefix_len(addr_str);

    if (ipv6_addr_from_str(&addr, addr_str) == NULL) {
        puts("error: unable to parse IPv6 address.");
        return 1;
    }

    if ((argc > 1) && (ipv6_addr_is_multicast(&addr)) && (type != _MULTICAST)) {
        puts("error: address was not a multicast address.");
        return 1;
    }

    if ((assigned_address = gnrc_ipv6_netif_add_addr(dev, &addr, prefix_len,
                                                    (type == _ANYCAST) ?
                                                    GNRC_IPV6_NETIF_ADDR_FLAGS_NON_UNICAST :
                                                    GNRC_IPV6_NETIF_ADDR_FLAGS_UNICAST))
                           == NULL) {
        printf("error: unable to add IPv6 address\n");
        return 1;
    }
    gnrc_ipv6_netif_addr_t *netif_addr = gnrc_ipv6_netif_addr_get(assigned_address);
    netif_addr->flags &= ~GNRC_IPV6_NETIF_ADDR_FLAGS_TENTATIVE;

    printf("success: added %s/%d to interface %" PRIkernel_pid "\n", addr_str,
           prefix_len, dev);

    return 0;
#else
    (void)cmd_name;
    (void)dev;
    (void)argc;
    (void)argv;
    puts("error: unable to add IPv6 address.");

    return 1;
#endif
}

static int _netif_del(kernel_pid_t dev, char *addr_str)
{
#ifdef MODULE_GNRC_IPV6_NETIF
    ipv6_addr_t addr;

    if (ipv6_addr_from_str(&addr, addr_str) == NULL) {
        puts("error: unable to parse IPv6 address.");
        return 1;
    }

    gnrc_ipv6_netif_remove_addr(dev, &addr);

    printf("success: removed %s to interface %" PRIkernel_pid "\n", addr_str,
           dev);

    return 0;
#else
    (void)dev;
    (void)addr_str;
    puts("error: unable to delete IPv6 address.");
    return 1;
#endif
}

static int _netif_mtu(kernel_pid_t dev, char *mtu_str)
{
#ifdef MODULE_GNRC_IPV6_NETIF
    int mtu;
    gnrc_ipv6_netif_t *entry;
    if (((mtu = atoi(mtu_str)) < IPV6_MIN_MTU) || (mtu > UINT16_MAX)) {
        printf("error: MTU must be between %" PRIu16 " and %" PRIu16 "\n",
               (uint16_t)IPV6_MIN_MTU, (uint16_t)UINT16_MAX);
        return 1;
    }
    if ((entry = gnrc_ipv6_netif_get(dev)) == NULL) {
        puts("error: unable to set MTU.");
        return 1;
    }
    entry->mtu = IPV6_MIN_MTU;
    printf("success: set MTU %u interface %" PRIkernel_pid "\n", mtu,
           dev);
    return 0;
#else
    (void)dev;
    (void)mtu_str;
    puts("error: unable to set MTU.");
    return 1;
#endif
}


/* shell commands */
int _netif_send(int argc, char **argv)
{
    kernel_pid_t dev;
    uint8_t addr[MAX_ADDR_LEN];
    size_t addr_len;
    gnrc_pktsnip_t *pkt;
    gnrc_netif_hdr_t *nethdr;
    uint8_t flags = 0x00;

    if (argc < 4) {
        printf("usage: %s <if> [<addr>|bcast] <data>\n", argv[0]);
        return 1;
    }

    /* parse interface */
    dev = (kernel_pid_t)atoi(argv[1]);

    if (!_is_iface(dev)) {
        puts("error: invalid interface given");
        return 1;
    }

    /* parse address */
    addr_len = gnrc_netif_addr_from_str(addr, sizeof(addr), argv[2]);

    if (addr_len == 0) {
        if (strcmp(argv[2], "bcast") == 0) {
            flags |= GNRC_NETIF_HDR_FLAGS_BROADCAST;
        }
        else {
            puts("error: invalid address given");
            return 1;
        }
    }

    /* put packet together */
    pkt = gnrc_pktbuf_add(NULL, argv[3], strlen(argv[3]), GNRC_NETTYPE_UNDEF);
    pkt = gnrc_pktbuf_add(pkt, NULL, sizeof(gnrc_netif_hdr_t) + addr_len,
                          GNRC_NETTYPE_NETIF);
    nethdr = (gnrc_netif_hdr_t *)pkt->data;
    gnrc_netif_hdr_init(nethdr, 0, addr_len);
    gnrc_netif_hdr_set_dst_addr(nethdr, addr, addr_len);
    nethdr->flags = flags;
    /* and send it */
    gnrc_netapi_send(dev, pkt);

    return 0;
}

int _netif_config(int argc, char **argv)
{
    if (argc < 2) {
        kernel_pid_t ifs[GNRC_NETIF_NUMOF];
        size_t numof = gnrc_netif_get(ifs);

        for (size_t i = 0; i < numof && i < GNRC_NETIF_NUMOF; i++) {
            _netif_list(ifs[i]);
        }

        return 0;
    }
    else if (_is_number(argv[1])) {
        kernel_pid_t dev = (kernel_pid_t)atoi(argv[1]);

        if (_is_iface(dev)) {
            if (argc < 3) {
                _netif_list(dev);
                return 0;
            }
            else if (strcmp(argv[2], "set") == 0) {
                if (argc < 5) {
                    _set_usage(argv[0]);
                    return 1;
                }

                return _netif_set(argv[0], dev, argv[3], argv[4]);
            }
            else if (strcmp(argv[2], "add") == 0) {
                if (argc < 4) {
                    _add_usage(argv[0]);
                    return 1;
                }

                return _netif_add(argv[0], (kernel_pid_t)dev, argc - 3, argv + 3);
            }
            else if (strcmp(argv[2], "del") == 0) {
                if (argc < 4) {
                    _del_usage(argv[0]);
                    return 1;
                }

                return _netif_del((kernel_pid_t)dev, argv[3]);
            }
            else if (strcmp(argv[2], "mtu") == 0) {
                if (argc < 4) {
                    _mtu_usage(argv[0]);
                    return 1;
                }

                return _netif_mtu((kernel_pid_t)dev, argv[3]);
            }
            else {
                return _netif_flag(argv[0], dev, argv[2]);
            }
        }
        else {
            puts("error: invalid interface given");
            return 1;
        }
    }

    printf("usage: %s [<if_id>]\n", argv[0]);
    _set_usage(argv[0]);
    _mtu_usage(argv[0]);
    _flag_usage(argv[0]);
    _add_usage(argv[0]);
    _del_usage(argv[0]);
    return 1;
}
