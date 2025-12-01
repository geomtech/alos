# Network Configuration System

## Overview

This document outlines the design for network configuration files supporting both DHCP and static IP configurations.

## Configuration File Location

```
/etc/net/
├── interfaces.conf      # Main interface configuration
├── dns.conf             # DNS resolver settings
└── routes.conf          # Static routes (optional)
```

## Interface Configuration Format

### File: `/etc/net/interfaces.conf`

```ini
# Network Interface Configuration
# Syntax: key=value

[eth0]
# Mode: dhcp | static
mode=dhcp

[eth1]
mode=static
address=192.168.1.100
netmask=255.255.255.0
gateway=192.168.1.1
dns=8.8.8.8,8.8.4.4
```

### Static Configuration Fields

| Field     | Required | Description                    | Example           |
|-----------|----------|--------------------------------|-------------------|
| `mode`    | Yes      | `static` or `dhcp`             | `static`          |
| `address` | Yes*     | IPv4 address                   | `192.168.1.100`   |
| `netmask` | Yes*     | Subnet mask                    | `255.255.255.0`   |
| `gateway` | No       | Default gateway                | `192.168.1.1`     |
| `dns`     | No       | DNS servers (comma-separated)  | `8.8.8.8,1.1.1.1` |

*Required only when `mode=static`

### DHCP Configuration Fields

| Field     | Required | Description              | Example     |
|-----------|----------|--------------------------|-------------|
| `mode`    | Yes      | Must be `dhcp`           | `dhcp`      |
| `hostname`| No       | Hostname to send         | `myhost`    |

## DNS Configuration

### File: `/etc/net/dns.conf`

```ini
# DNS Resolver Configuration

[resolver]
nameserver=8.8.8.8
nameserver=8.8.4.4
search=local,lan
timeout=5
```

## Data Structures (C)

```c
/* src/net/netconfig.h */

#define MAX_INTERFACES  4
#define MAX_DNS_SERVERS 4
#define IF_NAME_LEN     16
#define MAX_SEARCH_DOMAINS 4

typedef enum {
    NET_MODE_DISABLED = 0,
    NET_MODE_DHCP,
    NET_MODE_STATIC
} net_mode_t;

typedef struct {
    char name[IF_NAME_LEN];
    net_mode_t mode;
    
    /* Static configuration */
    uint32_t address;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns[MAX_DNS_SERVERS];
    uint8_t  dns_count;
    
    /* DHCP state */
    uint32_t lease_time;
    uint32_t lease_obtained;
} net_interface_t;

typedef struct {
    net_interface_t interfaces[MAX_INTERFACES];
    uint8_t interface_count;
} net_config_t;
```

## API Functions

```c
/* Load configuration from files */
int netconfig_load(net_config_t *config);

/* Save configuration to files */
int netconfig_save(const net_config_t *config);

/* Apply configuration to interface */
int netconfig_apply(const char *ifname);

/* Get interface configuration */
net_interface_t* netconfig_get_interface(const char *ifname);

/* Set interface to DHCP mode */
int netconfig_set_dhcp(const char *ifname);

/* Set interface to static mode */
int netconfig_set_static(const char *ifname, uint32_t addr, 
                         uint32_t mask, uint32_t gw);
```

## Usage Example

### Switch from DHCP to Static
```c
netconfig_set_static("eth0", 
    inet_addr("192.168.1.50"),
    inet_addr("255.255.255.0"),
    inet_addr("192.168.1.1"));
netconfig_save(&config);
netconfig_apply("eth0");
```

### Switch from Static to DHCP
```c
netconfig_set_dhcp("eth0");
netconfig_save(&config);
netconfig_apply("eth0");
```