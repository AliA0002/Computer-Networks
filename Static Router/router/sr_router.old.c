/**********************************************************************
 * file:  sr_router.c
 * date:  Mon Feb 18 12:50:42 PST 2002
 * Contact: casado@stanford.edu
 *
 * Description:
 *
 * This file contains all the functions that interact directly
 * with the routing table, as well as the main entry method
 * for routing.
 *
 **********************************************************************/

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sr_arpcache.h"
#include "sr_if.h"
#include "sr_protocol.h"
#include "sr_router.h"
#include "sr_rt.h"
#include "sr_utils.h"

/* ---------Checklist----------
1. Check if IP or ARP Packet - DONE
2. Checksum checking - DONE
3. Forward packet - In Progress (call send_arp_req?)
  -Decrement TTL
4. Personal packet - In Progress
  -ICMP packets - DONE
  -TCP/UDP packets - DONE
  -Expired packets - In Progress
5. Longest prefix - DONE (make sure dest_router_rt is sent by reference)
6. Handle packets with a ttl < 1 - In Progress
7. ARP Packets - DONE
 - ARP reply - DONE?
 - ARP request - In Progress
---------End of Checklist----------*/

// IMPORTANT TODO: For forward_packet, make sure to consider the case when the
// lpm is NOT found

// TODO: add the debugging functions
// TODO: when testing, make sure that pass by reference works for checksum
// function!!
// TODO: uncomment debugging functions when testing
// TODO: check htons stuff when sending ip packet
/*---------------------------------------------------------------------
 * Method: sr_init(void)
 * Scope:  Global
 *
 * Initialize the routing subsystem
 *
 *---------------------------------------------------------------------*/

void sr_init(struct sr_instance *sr) {
    /* REQUIRES */
    assert(sr);

    /* Initialize cache and cache cleanup thread */
    sr_arpcache_init(&(sr->cache));

    pthread_attr_init(&(sr->attr));
    pthread_attr_setdetachstate(&(sr->attr), PTHREAD_CREATE_JOINABLE);
    pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
    pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
    pthread_t thread;

    pthread_create(&thread, &(sr->attr), sr_arpcache_timeout, sr);

    /* Add initialization code here! */

} /* -- sr_init -- */

// Function to validate checksum
int valid_checksum(sr_ip_hdr_t *ip_hdr) {
    uint16_t old_checksum, new_checksum;
    old_checksum = ip_hdr->ip_sum;
    ip_hdr->ip_sum = 0;

    new_checksum = cksum((void *)ip_hdr, sizeof(sr_ip_hdr_t));

    // If the checksum doesn't match, return and drop packet
    if (new_checksum != old_checksum)
        return 0;

    ip_hdr->ip_sum = old_checksum;
    return 1;
}

int icmp_valid_checksum(sr_icmp_hdr_t *validate, unsigned int len) {
    unsigned int temp = len - 34;
    uint16_t old_checksum, new_checksum;
    old_checksum = validate->icmp_sum;
    validate->icmp_sum = 0;
    new_checksum = cksum((void *)validate, temp);
    if (new_checksum != old_checksum)
        return 0;
    return 1;
}

// Returns new interface via lpm
char *longest_prefix_matching(struct sr_instance *sr, uint32_t ip_dst, struct sr_rt *dest_router_rt) // MAKE SURE TO SEND dest_router_rt BY REFERENCE
{
    struct sr_rt *routing_table = sr->routing_table;
    dest_router_rt = sr->routing_table;
    int max_mask = 0;
    routing_table = routing_table->next; // CHECK THIS LATER
    char *new_interface = routing_table->interface;
    while (!routing_table) {
        int mask_len = 0;
        for (int i = 1; i <= 32; ++i) {
            if (((routing_table->mask.s_addr >> i) & 1) == 1)
                mask_len++;
        }

        uint32_t tmp = (routing_table->mask.s_addr) & ntohl(ip_dst);

        if (tmp == (ntohl(routing_table->dest.s_addr) & routing_table->mask.s_addr) && mask_len > max_mask) {
            new_interface = routing_table->interface;
            max_mask = mask_len;
            dest_router_rt = routing_table;
        }
        routing_table = routing_table->next;
    }

    return new_interface;
}

void handle_expired_packet() {}

// Handle forwarding the packet
void forward_packet(struct sr_instance *sr, sr_ip_hdr_t *ip_hdr, sr_ethernet_hdr_t *ethernet_frame_hdr, unsigned int len, uint8_t *packet) {
    if (ip_hdr->ip_ttl == 1) // If packet is expired (should ttl be 0 or 1?)
    {
        handle_expired_packet();
        return;
    }
    struct sr_rt *dest_router_rt = NULL;

    char *new_interface = longest_prefix_matching(sr, ip_hdr->ip_dst, &dest_router_rt); // Make sure that dest_router changes

    struct sr_arpentry *arp_entry = sr_arpcache_lookup(&sr->cache, ntohl(dest_router_rt->gw.s_addr));

    if (arp_entry) // if arp entry is found within the cache
    {
        struct sr_if *dest_interface = sr_get_interface(sr, new_interface);
        uint8_t mac = arp_entry->mac;
        uint8_t *data = malloc(len - 34), *dst_payload = malloc(len);

        // Set up Ethernet Frame Header
        memcpy(ethernet_frame_hdr->ether_shost, dest_interface->addr, 6);
        memcpy(ethernet_frame_hdr->ether_dhost, mac, 6);

        // Set up IP Header
        ip_hdr->ip_sum = cksum((void *)(ip_hdr), 20);
        ip_hdr->ip_ttl--;

        memcpy(data, packet + 14 + 20, len - 34);

        // Set up and send payload
        memcpy(dst_payload, ethernet_frame_hdr, 14);
        memcpy(dst_payload + 14, ip_hdr, 20);
        memcpy(dst_payload + 14 + 20, data, len - 34);
        sr_send_packet(sr, dst_payload, len, new_interface);
    } else // If arp entry is not found in the cache
    {
        sr_arpcache_queuereq(&sr->cache, ntohl(dest_router_rt->gw.s_addr), packet, len, new_interface);
        // TODO: call send_arp_req?
    }
}

// Function to handle ICMP Packets
void handle_icmp_packet(struct sr_instance *sr, uint8_t *packet, unsigned int len, sr_ethernet_hdr_t *ethernet_frame_hdr, sr_ip_hdr_t *ip_hdr,
                        struct sr_if *interface) {
    // Validate ICMP checksum
    sr_icmp_hdr_t *validate = (struct sr_icmp_hdr *)(packet + 14 + 20);
    if (icmp_valid_checksum != 1) {
        printf("Invalid ICMP packet\n");
        return;
    }

    sr_icmp_hdr_t *icmp_request = malloc(sizeof(struct sr_icmp_hdr));
    uint8_t *data = malloc(len - 38), *header_dhost = malloc(6);
    memcpy(icmp_request, packet + 14 + 20, 4); // Check if it's 4 or 8 bytes

    // Set ICMP echo variables
    icmp_request->icmp_type = 0;
    icmp_request->icmp_code = 0;
    icmp_request->icmp_sum = cksum((void *)(packet + 14 + 20), len - 34); // Check this later

    memcpy(data, packet + 14 + 20 + 4, len - 38);

    // Exchange dhost and shost for the frame header
    memcpy(header_dhost, ethernet_frame_hdr->ether_dhost, 6);
    memcpy(ethernet_frame_hdr->ether_dhost, ethernet_frame_hdr->ether_shost, 6);
    memcpy(ethernet_frame_hdr->ether_shost, header_dhost, 6);

    // Set IP header variables
    uint32_t temp = ip_hdr->ip_dst;
    ip_hdr->ip_dst = ip_hdr->ip_src;
    ip_hdr->ip_src = temp;
    ip_hdr->ip_sum = cksum((void *)(ip_hdr), 20);
    ip_hdr->ip_ttl = 64;
    ip_hdr->ip_id = htons(0); // Check this later
    ip_hdr->ip_off = IP_DF;
    ip_hdr->ip_tos = htons(0);

    // Set up ICMP Packet
    uint8_t *icmp_packet = malloc(len);
    memcpy(icmp_packet, ethernet_frame_hdr, 14);
    memcpy(icmp_packet + 14, ip_hdr, 20);
    memcpy(icmp_packet + 14 + 20, icmp_request, 4);
    memcpy(icmp_packet + 14 + 2 + 4, data, len - 38);
    sr_send_packet(sr, icmp_packet, len, interface); // Send ICMP Packet
}

// Function to handle TCP/UDP Packets
void handle_tcpudp_packet(struct sr_instance *sr, uint8_t *packet, sr_ethernet_hdr_t *ethernet_frame_hdr, sr_ip_hdr_t *ip_hdr,
                          struct sr_if *sr_interface) {
    sr_icmp_t3_hdr_t *icmp_port_u = malloc(sizeof(struct sr_icmp_t3_hdr));

    // Fill in header variables
    icmp_port_u->icmp_code = 3;
    icmp_port_u->icmp_type = 3;
    icmp_port_u->icmp_sum = cksum((void *)icmp_port_u, sizeof(struct sr_icmp_t3_hdr));
    memcpy(icmp_port_u, packet + 14, ICMP_DATA_SIZE);

    // Switch dhost and shost
    uint8_t *header_dhost = malloc(6);
    memcpy(header_dhost, ethernet_frame_hdr->ether_dhost, 6);
    memcpy(ethernet_frame_hdr->ether_dhost, ethernet_frame_hdr->ether_shost, 6);
    memcpy(ethernet_frame_hdr->ether_shost, header_dhost, 6);

    // Set up IP header variables
    ip_hdr->ip_dst = ip_hdr->ip_src;
    ip_hdr->ip_src = sr_interface->ip;
    ip_hdr->ip_id = htons(0);
    ip_hdr->ip_tos = htons(0);
    ip_hdr->ip_off = htons(IP_DF);
    ip_hdr->ip_len = htons(sizeof(sr_ip_hdr_t) + sizeof(sr_icmp_t3_hdr_t));
    ip_hdr->ip_p = ip_protocol_icmp;
    ip_hdr->ip_sum = cksum((void *)(ip_hdr), 20);
    ip_hdr->ip_ttl = 64;

    // Set up and send ICMP Packet
    uint8_t *icmp_packet = malloc(14 + 20 + sizeof(sr_icmp_t3_hdr_t));
    memcpy(icmp_packet, ethernet_frame_hdr, 14);
    memcpy(icmp_packet + 14, ip_hdr, 20);
    memcpy(icmp_packet + 14 + 20, icmp_port_u, sizeof(sr_icmp_t3_hdr_t));
    sr_send_packet(sr, icmp_packet, 14 + 20 + sizeof(sr_icmp_t3_hdr_t), sr_interface);
}

// Function to handle IP Packets
void handle_ip_packet(struct sr_instance *sr, uint8_t *packet, unsigned int len, struct sr_if *sr_interface) {
    uint8_t *HEAD = packet;
    sr_ethernet_hdr_t *ethernet_frame_hdr = malloc(sizeof(struct sr_ethernet_hdr));
    memcpy(ethernet_frame_hdr, HEAD, sizeof(struct sr_ethernet_hdr));

    HEAD += sizeof(struct sr_ethernet_hdr);
    sr_ip_hdr_t *ip_hdr = malloc(sizeof(sr_ip_hdr_t));
    memcpy(ip_hdr, HEAD, sizeof(sr_ip_hdr_t));

    // Copy IP header

    // Drop Packet if it's not IPv4
    if (ip_hdr->ip_v != 4) {
        printf("Dropping non-IPv4 packet\n");
        return;
    }

    // Checking checksum
    if (valid_checksum(&ip_hdr) != 1)
        return;

    // Check if packet is for me or another router
    struct sr_if *sr_interface = sr->if_list;
    int for_me = 0; // 1 if packet is for me, 0 otherwise
    while (sr_interface != NULL) {
        if (sr_interface->ip == ip_hdr->ip_dst) {
            for_me = 1;
            break;
        }
        sr_interface = sr_interface->next;
    }
    if (for_me != 1) // Forward packet
    {
        printf("Nothing personal\n");
        forward_packet(sr, ip_hdr, ethernet_frame_hdr, len, packet);
        return;
    } else // Handle personal packet
        printf("This is getting personal\n");

    if (ip_hdr->ip_p == ip_protocol_icmp) // Handle icmp packet
    {
        printf("This is an icmp packet\n");
        handle_icmp_packet(sr, packet, len, ethernet_frame_hdr, ip_hdr, sr_interface);
    } else {
        printf("This is a TCP/UDP packet\n");
        handle_tcpudp_packet(sr, packet, ethernet_frame_hdr, ip_hdr, sr_interface);
    }
}

void handle_arp_reply(sr_arp_hdr_t *arp_hdr, struct sr_if *interface, struct sr_instance *sr) {
    printf("This is an ARP Reply\n");
    struct sr_arpreq *requests = NULL;
    struct sr_packet *tmp_packet = NULL;
    if (arp_hdr->ar_tip == interface->ip) {
        requests = sr_arpcache_insert(&sr->cache, arp_hdr->ar_sha, ntohl(arp_hdr->ar_sip));
        printf("Requests found\n");
        if (requests) {
            while (requests->packets) {
                tmp_packet = requests->packets;

                memcpy(((sr_ethernet_hdr_t *)tmp_packet->buf)->ether_shost, interface->addr, 6);
                memcpy(((sr_ethernet_hdr_t *)tmp_packet->buf)->ether_dhost, arp_hdr->ar_sha, 6);
                printf("Sending outstanding packet...\n");
                sr_send_packet(sr, tmp_packet, tmp_packet->len, tmp_packet->iface);
                requests->packets = requests->packets->next;
            }
            sr_arpreq_destroy(&sr->cache, requests);
        }
    }
}

void handle_arp_request() {}

// Function to handle ARP Packets
void handle_arp_packet(struct sr_instance *sr, struct sr_if *interface, uint8_t *packet) {
    printf("Received ARP Packet\n");
    sr_arp_hdr_t *arp_hdr = malloc(sizeof(sr_arp_hdr_t));
    memcpy(arp_hdr, packet + 14, 28);

    switch (ntohs(arp_hdr->ar_op)) {
    case arp_op_request:
        printf("This is an ARP Request\n");
        break;
    case arp_op_reply:
        handle_arp_reply(arp_hdr, interface, sr);
        break;
    default:
        printf("This is an unknown ARP Packet\n");
        break;
    }
}

struct sr_rt *find_routing_entry(struct sr_instance *sr, uint32_t ip) {
    struct sr_rt *routing_table = sr->routing_table;
    struct sr_rt *longest_prefix_match = NULL;
    uint32_t longest_prefix = 0;
    while (routing_table != NULL) {
        if ((routing_table->dest.s_addr & routing_table->mask.s_addr) == (ip & routing_table->mask.s_addr)) {
            if (routing_table->mask.s_addr > longest_prefix) {
                longest_prefix = routing_table->mask.s_addr;
                longest_prefix_match = routing_table;
            }
        }
        routing_table = routing_table->next;
    }
    return longest_prefix_match;
}

/*---------------------------------------------------------------------
 * Method: sr_handlepacket(uint8_t* p,char* interface)
 * Scope:  Global
 *
 * This method is called each time the router receives a packet on the
 * interface.  The packet buffer, the packet length and the receiving
 * interface are passed in as parameters. The packet is complete with
 * ethernet headers.
 *
 * Note: Both the packet buffer and the character's memory are handled
 * by sr_vns_comm.c that means do NOT delete either.  Make a copy of the
 * packet instead if you intend to keep it around beyond the scope of
 * the method call.
 *
 *---------------------------------------------------------------------*/

void sr_handlepacket(struct sr_instance *sr, uint8_t *packet /* lent */, unsigned int len, char *interface /* lent */) {
    /* REQUIRES */
    assert(sr);
    assert(packet);
    assert(interface);

    printf("*** -> Received packet of length %d \n", len);

    // workflow
    // if arp
    //    if request for me, send reply
    //    if reply for me, resolve that requiest
    //    if for someone else, forward it
    // if ip
    //    if for me
    //        if ICMP - respond
    //        if TCP/UDP - ICMP unreachable
    //    if for someone else, check routing table
    //        if not found - ICMP unreachable
    //        if found, check arp cache
    //            if found, forward
    //            if miss, send arp request

    /* TODO: FILL IN YOUR CODE HERE */
    struct sr_if *sr_interface = sr_get_interface(sr, interface);
    sr_ethernet_hdr_t *eth_hdr = (sr_ethernet_hdr_t *)packet;
    // print_hdr_eth((uint8_t *)&eth_hdr); // FOR DEBUGGING

    short TYPE = ntohs(eth_hdr->ether_type);

    if (TYPE == ethertype_arp) {
        sr_arp_hdr_t *arp_hdr = (sr_arp_hdr_t *)(packet + sizeof(sr_ethernet_hdr_t));
        if (memcmp(arp_hdr->ar_tha, sr_interface->addr, ETHER_ADDR_LEN) == 0) {
            // for me
            if (arp_hdr->ar_op == arp_op_request) {
                // construct and send ARP reply

            } else if (arp_hdr->ar_op == arp_op_reply) {
                // Resolve ARP request
            }
        } else {
            // Forward ARP packet
        }
    } else if (TYPE == ethertype_ip) {
        sr_ip_hdr_t *ip_hdr = (sr_ip_hdr_t *)(packet + sizeof(sr_ethernet_hdr_t));
        if (ip_hdr->ip_dst == sr_interface->ip) {
            // for me
            if (ip_hdr->ip_p == ip_protocol_icmp) {
                sr_icmp_hdr_t *icmp_hdr = (sr_icmp_hdr_t *)(packet + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));
                // construct and send ICMP ping reply
                uint8_t icmp_packet[len];
                memcpy(icmp_packet, packet, len);

                sr_ethernet_hdr_t *icmp_eth_hdr = (sr_ethernet_hdr_t *)icmp_packet;
                sr_ip_hdr_t *icmp_ip_hdr = (sr_ip_hdr_t *)(icmp_packet + sizeof(sr_ethernet_hdr_t));
                sr_icmp_hdr_t *icmp_icmp_hdr = (sr_icmp_hdr_t *)(icmp_packet + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));
                uint8_t *end = icmp_packet + len;

                if (icmp_hdr->icmp_type == 8) { // icmp_type_echo_request
                                                //  set icmp reply
                    icmp_icmp_hdr->icmp_type = 0;
                    icmp_icmp_hdr->icmp_code = 0;
                    // set ip header
                    icmp_ip_hdr->ip_ttl = 64;
                    icmp_ip_hdr->ip_sum = cksum(icmp_icmp_hdr, end - (uint8_t *)icmp_icmp_hdr);
                    // swap
                    uint32_t temp = icmp_ip_hdr->ip_src;
                    icmp_ip_hdr->ip_src = icmp_ip_hdr->ip_dst;
                    icmp_ip_hdr->ip_dst = temp;

                    // set eth header
                    icmp_eth_hdr->ether_type = htons(ethertype_ip);
                    memcpy(eth_hdr->ether_dhost, icmp_eth_hdr->ether_shost, ETHER_ADDR_LEN);
                    memcpy(eth_hdr->ether_shost, icmp_eth_hdr->ether_dhost, ETHER_ADDR_LEN);
                    // send
                    sr_send_packet(sr, (uint8_t *)&icmp_packet, sizeof(icmp_packet), interface)
                } else {
                    // if not echo request, drop packet
                }

            } else {
                // construct and send ICMP unreachable
                sr_icmp_hdr_t *icmp_hdr = (sr_icmp_hdr_t *)(packet + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));
                uint8_t icmp_packet[len];
                memcpy(icmp_packet, packet, len);

                sr_ethernet_hdr_t *icmp_eth_hdr = (sr_ethernet_hdr_t *)icmp_packet;
                sr_ip_hdr_t *icmp_ip_hdr = (sr_ip_hdr_t *)(icmp_packet + sizeof(sr_ethernet_hdr_t));
                sr_icmp_hdr_t *icmp_icmp_hdr = (sr_icmp_hdr_t *)(icmp_packet + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));
                uint8_t *end = icmp_packet + len;

                //  set icmp reply
                icmp_icmp_hdr->icmp_type = 0;
                icmp_icmp_hdr->icmp_code = 0;
                // set ip header
                icmp_ip_hdr->ip_ttl = 64;
                icmp_ip_hdr->ip_sum = cksum(icmp_icmp_hdr, end - (uint8_t *)icmp_icmp_hdr);
                // swap
                uint32_t temp = icmp_ip_hdr->ip_src;
                icmp_ip_hdr->ip_src = icmp_ip_hdr->ip_dst;
                icmp_ip_hdr->ip_dst = temp;

                // set eth header
                icmp_eth_hdr->ether_type = htons(ethertype_ip);
                memcpy(eth_hdr->ether_dhost, icmp_eth_hdr->ether_shost, ETHER_ADDR_LEN);
                memcpy(eth_hdr->ether_shost, icmp_eth_hdr->ether_dhost, ETHER_ADDR_LEN);
                // send
                sr_send_packet(sr, (uint8_t *)&icmp_packet, sizeof(icmp_packet), interface)
            }
        } else {
            // if for someone else, check routing table
            struct sr_rt *rte = find_routing_entry(ip_hdr->ip_dst);
            if (rte == NULL) {
                //     if not found - ICMP unreachable
            } else {
                //     if found, check arp cache
                struct sr_arpentry *arp_entry = sr_arpcache_lookup(&sr->cache, ip_hdr->ip_dst);
                if (arp_entry) {
                    //         if found, forward
                } else {
                    //         if miss, send arp request
                }
            }
        }
    } else {
        printf("Unknown Packet\n"); // Drop packet
    }

} /* end sr_ForwardPacket */