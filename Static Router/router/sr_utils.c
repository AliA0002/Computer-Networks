#include "sr_utils.h"
#include "sr_if.h"
#include "sr_protocol.h"
#include "sr_router.h"
#include "sr_rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tgmath.h>

uint16_t cksum(const void *_data, int len) {
    const uint8_t *data = _data;
    uint32_t sum;

    for (sum = 0; len >= 2; data += 2, len -= 2)
        sum += data[0] << 8 | data[1];
    if (len > 0)
        sum += data[0] << 8;
    while (sum > 0xffff)
        sum = (sum >> 16) + (sum & 0xffff);
    sum = htons(~sum);
    return sum ? sum : 0xffff;
}

uint16_t ethertype(uint8_t *buf) {
    sr_ethernet_hdr_t *ehdr = (sr_ethernet_hdr_t *)buf;
    return ntohs(ehdr->ether_type);
}

uint8_t ip_protocol(uint8_t *buf) {
    sr_ip_hdr_t *iphdr = (sr_ip_hdr_t *)(buf);
    return iphdr->ip_p;
}

/* Prints out formatted Ethernet address, e.g. 00:11:22:33:44:55 */
void print_addr_eth(uint8_t *addr) {
    int pos = 0;
    uint8_t cur;
    for (; pos < ETHER_ADDR_LEN; pos++) {
        cur = addr[pos];
        if (pos > 0)
            fprintf(stderr, ":");
        fprintf(stderr, "%02X", cur);
    }
    fprintf(stderr, "\n");
}

/* Prints out IP address as a string from in_addr */
void print_addr_ip(struct in_addr address) {
    char buf[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &address, buf, 100) == NULL)
        fprintf(stderr, "inet_ntop error on address conversion\n");
    else
        fprintf(stderr, "%s\n", buf);
}

/* Prints out IP address from integer value */
void print_addr_ip_int(uint32_t ip) {
    uint32_t curOctet = ip >> 24;
    fprintf(stderr, "%d.", curOctet);
    curOctet = (ip << 8) >> 24;
    fprintf(stderr, "%d.", curOctet);
    curOctet = (ip << 16) >> 24;
    fprintf(stderr, "%d.", curOctet);
    curOctet = (ip << 24) >> 24;
    fprintf(stderr, "%d\n", curOctet);
}

/* Prints out fields in Ethernet header. */
void print_hdr_eth(uint8_t *buf) {
    sr_ethernet_hdr_t *ehdr = (sr_ethernet_hdr_t *)buf;
    fprintf(stderr, "ETHERNET header:\n");
    fprintf(stderr, "\tdestination: ");
    print_addr_eth(ehdr->ether_dhost);
    fprintf(stderr, "\tsource: ");
    print_addr_eth(ehdr->ether_shost);
    fprintf(stderr, "\ttype: %d\n", ntohs(ehdr->ether_type));
}

/* Prints out fields in IP header. */
void print_hdr_ip(uint8_t *buf) {
    sr_ip_hdr_t *iphdr = (sr_ip_hdr_t *)(buf);
    fprintf(stderr, "IP header:\n");
    fprintf(stderr, "\tversion: %d\n", iphdr->ip_v);
    fprintf(stderr, "\theader length: %d\n", iphdr->ip_hl);
    fprintf(stderr, "\ttype of service: %d\n", iphdr->ip_tos);
    fprintf(stderr, "\tlength: %d\n", ntohs(iphdr->ip_len));
    fprintf(stderr, "\tid: %d\n", ntohs(iphdr->ip_id));

    if (ntohs(iphdr->ip_off) & IP_DF)
        fprintf(stderr, "\tfragment flag: DF\n");
    else if (ntohs(iphdr->ip_off) & IP_MF)
        fprintf(stderr, "\tfragment flag: MF\n");
    else if (ntohs(iphdr->ip_off) & IP_RF)
        fprintf(stderr, "\tfragment flag: R\n");

    fprintf(stderr, "\tfragment offset: %d\n", ntohs(iphdr->ip_off) & IP_OFFMASK);
    fprintf(stderr, "\tTTL: %d\n", iphdr->ip_ttl);
    fprintf(stderr, "\tprotocol: %d\n", iphdr->ip_p);

    /*Keep checksum in NBO*/
    fprintf(stderr, "\tchecksum: %d\n", iphdr->ip_sum);

    fprintf(stderr, "\tsource: ");
    print_addr_ip_int(ntohl(iphdr->ip_src));

    fprintf(stderr, "\tdestination: ");
    print_addr_ip_int(ntohl(iphdr->ip_dst));
}

/* Prints out ICMP header fields */
void print_hdr_icmp(uint8_t *buf) {
    sr_icmp_hdr_t *icmp_hdr = (sr_icmp_hdr_t *)(buf);
    fprintf(stderr, "ICMP header:\n");
    fprintf(stderr, "\ttype: %d\n", icmp_hdr->icmp_type);
    fprintf(stderr, "\tcode: %d\n", icmp_hdr->icmp_code);
    /* Keep checksum in NBO */
    fprintf(stderr, "\tchecksum: %d\n", icmp_hdr->icmp_sum);
}

/* Prints out fields in ARP header */
void print_hdr_arp(uint8_t *buf) {
    sr_arp_hdr_t *arp_hdr = (sr_arp_hdr_t *)(buf);
    fprintf(stderr, "ARP header\n");
    fprintf(stderr, "\thardware type: %d\n", ntohs(arp_hdr->ar_hrd));
    fprintf(stderr, "\tprotocol type: %d\n", ntohs(arp_hdr->ar_pro));
    fprintf(stderr, "\thardware address length: %d\n", arp_hdr->ar_hln);
    fprintf(stderr, "\tprotocol address length: %d\n", arp_hdr->ar_pln);
    fprintf(stderr, "\topcode: %d\n", ntohs(arp_hdr->ar_op));

    fprintf(stderr, "\tsender hardware address: ");
    print_addr_eth(arp_hdr->ar_sha);
    fprintf(stderr, "\tsender ip address: ");
    print_addr_ip_int(ntohl(arp_hdr->ar_sip));

    fprintf(stderr, "\ttarget hardware address: ");
    print_addr_eth(arp_hdr->ar_tha);
    fprintf(stderr, "\ttarget ip address: ");
    print_addr_ip_int(ntohl(arp_hdr->ar_tip));
}

/* Prints out all possible headers, starting from Ethernet */
void print_hdrs(uint8_t *buf, uint32_t length) {

    /* Ethernet */
    int minlength = sizeof(sr_ethernet_hdr_t);
    if (length < minlength) {
        fprintf(stderr, "Failed to print ETHERNET header, insufficient length\n");
        return;
    }

    uint16_t ethtype = ethertype(buf);
    print_hdr_eth(buf);

    if (ethtype == ethertype_ip) { /* IP */
        minlength += sizeof(sr_ip_hdr_t);
        if (length < minlength) {
            fprintf(stderr, "Failed to print IP header, insufficient length\n");
            return;
        }

        print_hdr_ip(buf + sizeof(sr_ethernet_hdr_t));
        uint8_t ip_proto = ip_protocol(buf + sizeof(sr_ethernet_hdr_t));

        if (ip_proto == ip_protocol_icmp) { /* ICMP */
            minlength += sizeof(sr_icmp_hdr_t);
            if (length < minlength)
                fprintf(stderr, "Failed to print ICMP header, insufficient length\n");
            else
                print_hdr_icmp(buf + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));
        }
    } else if (ethtype == ethertype_arp) { /* ARP */
        minlength += sizeof(sr_arp_hdr_t);
        if (length < minlength)
            fprintf(stderr, "Failed to print ARP header, insufficient length\n");
        else
            print_hdr_arp(buf + sizeof(sr_ethernet_hdr_t));
    } else {
        fprintf(stderr, "Unrecognized Ethernet Type: %d\n", ethtype);
    }
}

struct sr_rt *find_routing_entry(struct sr_instance *sr, uint32_t ip) {
    struct sr_rt *routing_table = sr->routing_table;
    struct sr_rt *longest_prefix_match = NULL;
    uint32_t longest_prefix = 0;
    while (routing_table != NULL) {
        if ((routing_table->dest.s_addr & routing_table->mask.s_addr) == (ip & routing_table->mask.s_addr)) {
            if (longest_prefix_match == NULL || routing_table->mask.s_addr > longest_prefix) {
                longest_prefix = routing_table->mask.s_addr;
                longest_prefix_match = routing_table;
            }
        }
        routing_table = routing_table->next;
    }
    return longest_prefix_match;
}

void send_icmp_type_3(struct sr_instance *sr, uint8_t *packet, unsigned int len, char *interface, int type, int code) {
    // construct ICMP packet
    unsigned long resp_len = sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t) + sizeof(sr_icmp_t3_hdr_t);
    uint8_t *resp_packet = malloc(resp_len);
    sr_ethernet_hdr_t *resp_eth_hdr = (sr_ethernet_hdr_t *)resp_packet;
    sr_ip_hdr_t *resp_ip_hdr = (sr_ip_hdr_t *)(resp_packet + sizeof(sr_ethernet_hdr_t));
    sr_icmp_t3_hdr_t *resp_icmp_hdr = (sr_icmp_t3_hdr_t *)(resp_packet + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));

    sr_ethernet_hdr_t *eth_hdr = (sr_ethernet_hdr_t *)packet;
    sr_ip_hdr_t *ip_hdr = (sr_ip_hdr_t *)(packet + sizeof(sr_ethernet_hdr_t));
    // construct ethernet header
    memcpy(resp_eth_hdr->ether_dhost, eth_hdr->ether_shost, ETHER_ADDR_LEN);
    memcpy(resp_eth_hdr->ether_shost, eth_hdr->ether_dhost, ETHER_ADDR_LEN);
    resp_eth_hdr->ether_type = htons(ethertype_ip);

    // construct ip header

    resp_ip_hdr->ip_hl = 5;
    resp_ip_hdr->ip_v = 4;
    resp_ip_hdr->ip_tos = 0;
    resp_ip_hdr->ip_len = htons(sizeof(sr_ip_hdr_t) + sizeof(sr_icmp_t3_hdr_t));
    resp_ip_hdr->ip_id = ip_hdr->ip_id;
    resp_ip_hdr->ip_off = 0;
    resp_ip_hdr->ip_ttl = 64;
    resp_ip_hdr->ip_p = ip_protocol_icmp;
    resp_ip_hdr->ip_src = sr_get_interface(sr, interface)->ip;
    resp_ip_hdr->ip_dst = ip_hdr->ip_src;
    resp_ip_hdr->ip_sum = 0;
    resp_ip_hdr->ip_sum = cksum(resp_ip_hdr, sizeof(sr_ip_hdr_t));

    // construct icmp header
    resp_icmp_hdr->icmp_type = type;
    resp_icmp_hdr->icmp_code = code;

    resp_icmp_hdr->unused = 0;
    resp_icmp_hdr->next_mtu = 0;
    memcpy(&resp_icmp_hdr->data, ip_hdr, ICMP_DATA_SIZE);

    resp_icmp_hdr->icmp_sum = 0;
    resp_icmp_hdr->icmp_sum = cksum(resp_icmp_hdr, len - ((uint8_t *)resp_icmp_hdr - resp_packet));

    sr_send_packet(sr, resp_packet, len, interface);
}

// DONE
void send_icmp_type_0(struct sr_instance *sr, uint8_t *packet, unsigned int len, char *interface, int type, int code) {
    // construct ICMP packet
    uint8_t *resp_packet = malloc(len);
    memcpy(resp_packet, packet, len);
    sr_ethernet_hdr_t *resp_eth_hdr = (sr_ethernet_hdr_t *)resp_packet;
    sr_ip_hdr_t *resp_ip_hdr = (sr_ip_hdr_t *)(resp_packet + sizeof(sr_ethernet_hdr_t));
    sr_icmp_hdr_t *resp_icmp_hdr = (sr_icmp_hdr_t *)(resp_packet + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));

    sr_ethernet_hdr_t *eth_hdr = (sr_ethernet_hdr_t *)packet;
    sr_ip_hdr_t *ip_hdr = (sr_ip_hdr_t *)(packet + sizeof(sr_ethernet_hdr_t));
    // construct ethernet header
    memcpy(resp_eth_hdr->ether_dhost, eth_hdr->ether_shost, ETHER_ADDR_LEN);
    memcpy(resp_eth_hdr->ether_shost, eth_hdr->ether_dhost, ETHER_ADDR_LEN);

    // construct ip header

    resp_ip_hdr->ip_src = sr_get_interface(sr, interface)->ip;
    resp_ip_hdr->ip_dst = ip_hdr->ip_src;
    resp_ip_hdr->ip_sum = 0;
    resp_ip_hdr->ip_sum = cksum(resp_ip_hdr, sizeof(sr_ip_hdr_t));

    // construct icmp header
    resp_icmp_hdr->icmp_type = type;
    resp_icmp_hdr->icmp_code = code;
    resp_icmp_hdr->icmp_sum = 0;
    resp_icmp_hdr->icmp_sum = cksum(resp_icmp_hdr, len - ((uint8_t *)resp_icmp_hdr - resp_packet));

    sr_send_packet(sr, resp_packet, len, interface);
}