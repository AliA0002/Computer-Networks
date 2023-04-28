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

int icmp_cksum(struct sr_icmp_hdr *icmp_hdr, unsigned int len) {
    uint16_t old_checksum = icmp_hdr->icmp_sum;
    icmp_hdr->icmp_sum = 0;
    if (cksum((void *)icmp_hdr, len) != old_checksum) {
        printf("ICMP checksums don't match\n");
        return 0;
    }
    return 1;
}

int for_me(struct sr_if *sr_interface, const uint32_t ip) {
    while (sr_interface != NULL) {
        if (sr_interface->ip == ip) {
            return 1;
        }
        sr_interface = sr_interface->next;
    }
    return 0;
}

// Handle expired packets
void ip_handle_expire(struct sr_instance *sr, uint8_t *packet, sr_ethernet_hdr_t *ethernet_frame_header, unsigned int len, char *interface,
                      uint32_t ip_src) {
    printf("Expired packet\n");
    uint8_t *temp = malloc(6);
    uint8_t *reply = malloc(14 + 20 + 4);
    memcpy(temp, ethernet_frame_header->ether_shost, 6);
    memcpy(ethernet_frame_header->ether_shost, ethernet_frame_header->ether_dhost, 6);
    memcpy(ethernet_frame_header->ether_dhost, temp, 6);

    memcpy(reply, ethernet_frame_header, 14);
    sr_ip_hdr_t *ip_hdr = malloc(sizeof(sr_ip_hdr_t));

    ip_hdr->ip_v = 4;
    ip_hdr->ip_hl = 5;
    ip_hdr->ip_tos = 0;
    ip_hdr->ip_len = htons(sizeof(sr_ip_hdr_t) + sizeof(sr_icmp_t3_hdr_t));
    ip_hdr->ip_id = htons(ID);
    ID++;
    ip_hdr->ip_off = htons(IP_DF);
    ip_hdr->ip_p = ip_protocol_icmp;
    struct sr_if *in = sr_get_interface(sr, interface);
    uint32_t ip_dst = in->ip;
    ip_hdr->ip_dst = ip_src;
    ip_hdr->ip_src = ip_dst;
    ip_hdr->ip_ttl = 64;
    ip_hdr->ip_sum = 0;
    ip_hdr->ip_sum = cksum((void *)ip_hdr, sizeof(sr_ip_hdr_t));

    sr_icmp_t3_hdr_t *icmp_exp = malloc(sizeof(sr_icmp_t3_hdr_t));
    icmp_exp->icmp_type = 11;
    icmp_exp->icmp_code = 0;
    icmp_exp->icmp_sum = 0;
    memcpy(icmp_exp->data, packet + 14, ICMP_DATA_SIZE);
    icmp_exp->icmp_sum = cksum((void *)icmp_exp, sizeof(sr_icmp_t3_hdr_t));

    memcpy(reply + 14, ip_hdr, 20);
    memcpy(reply + 14 + 20, icmp_exp, sizeof(sr_icmp_t3_hdr_t));
    sr_send_packet(sr, reply, 14 + 20 + sizeof(sr_icmp_t3_hdr_t), interface);
}

// LPM
struct sr_rt *longest_prefix_match(struct sr_instance *sr, sr_ip_hdr_t *ip_hdr) {
    struct sr_rt *routing_table = sr->routing_table; //*dest_router = sr->routing_table;
    struct sr_rt *dest_router = sr->routing_table;
    int max = 0;
    routing_table = routing_table->next;
    while (routing_table != NULL) {
        int index = 1;
        int c = 0;
        while (index <= 32) {
            if (((routing_table->mask.s_addr >> index) & 1) == 1)
                c++;
            index++;
        }
        uint32_t ip_tmp = (routing_table->mask.s_addr) & ntohl(ip_hdr->ip_dst);
        if (ip_tmp == (ntohl(routing_table->dest.s_addr) & routing_table->mask.s_addr) && c > max) {
            max = c;
            dest_router = routing_table;
        }
        routing_table = routing_table->next;
    }
    return dest_router;
}

// Handle forwarding packet
void forward_packet(sr_ip_hdr_t *ip_hdr, uint8_t *packet, sr_ethernet_hdr_t *ethernet_frame_header, char *interface, unsigned int len,
                    struct sr_instance *sr) {
    printf("Forwarding packet\n");
    if (ip_hdr->ip_ttl == 1) { // Should ttl be 0 or 1?
        ip_handle_expire(sr, packet, ethernet_frame_header, len, interface, ip_hdr->ip_src);
        return;
    }

    struct sr_rt *dest_router = longest_prefix_match(sr, ip_hdr);
    char *new_interface = dest_router->interface;
    struct sr_if *sr_new_interface = sr_get_interface(sr, new_interface);

    struct sr_arpentry *arp_entry = sr_arpcache_lookup(&sr->cache, ntohl(dest_router->gw.s_addr));

    if (arp_entry) { // If arp entry is found
        printf("Entry found in ARP cache\n");
        uint8_t *mac = arp_entry->mac, *ip_payload = malloc(len - 34), *send_packet = malloc(len);
        ip_hdr->ip_ttl = ip_hdr->ip_ttl - 1;
        ip_hdr->ip_sum = 0;
        ip_hdr->ip_sum = cksum((void *)(ip_hdr), 20);

        memcpy(ethernet_frame_header->ether_shost, sr_new_interface->addr, 6);
        memcpy(ethernet_frame_header->ether_dhost, mac, 6);

        // Fill in payload
        memcpy(ip_payload, packet + 14 + 20, len - 34);

        // Set up and send payload
        memcpy(send_packet, ethernet_frame_header, 14);
        memcpy(send_packet + 14, ip_hdr, 20);
        memcpy(send_packet + 14 + 20, ip_payload, len - 34);
        sr_send_packet(sr, send_packet, len, new_interface);
        return;
    } else { // If arp entry is not found
        printf("Entry NOT found in ARP cache\n");
        sr_arpcache_queuereq(&sr->cache, ntohl(dest_router->gw.s_addr), packet, len, new_interface);
        handle_request(sr, sr->cache.requests);
        return;
    }
}

void handle_tcpudp_packets(uint8_t *packet, sr_ethernet_hdr_t *ethernet_frame_header, struct sr_instance *sr, char *interface, sr_ip_hdr_t *ip_hdr) {
    printf("This is a UDP/TCP packet\n");

    uint8_t *send_packet = malloc(14 + 20 + sizeof(sr_icmp_t3_hdr_t));

    sr_icmp_t3_hdr_t *icmp_net_unreachable = malloc(sizeof(sr_icmp_t3_hdr_t));
    icmp_net_unreachable->icmp_code = 3;
    icmp_net_unreachable->icmp_type = 3;
    icmp_net_unreachable->icmp_sum = 0;
    memcpy(icmp_net_unreachable->data, packet + 14, ICMP_DATA_SIZE);
    icmp_net_unreachable->icmp_sum = cksum((void *)icmp_net_unreachable, sizeof(sr_icmp_t3_hdr_t));

    uint8_t *temp = malloc(6);
    memcpy(temp, ethernet_frame_header->ether_dhost, 6);
    memcpy(ethernet_frame_header->ether_dhost, ethernet_frame_header->ether_shost, 6);
    memcpy(ethernet_frame_header->ether_shost, temp, 6);

    ip_hdr->ip_dst = ip_hdr->ip_src;
    ip_hdr->ip_src = sr_get_interface(sr, interface)->ip;
    ip_hdr->ip_tos = 0;
    ip_hdr->ip_off = htons(IP_DF);
    ip_hdr->ip_len = htons(sizeof(sr_ip_hdr_t) + sizeof(sr_icmp_t3_hdr_t));
    ip_hdr->ip_ttl = 64;
    ip_hdr->ip_id = htons(ID);
    ID++;
    ip_hdr->ip_p = ip_protocol_icmp;
    ip_hdr->ip_sum = 0;
    ip_hdr->ip_sum = cksum((void *)(ip_hdr), 20);

    memcpy(send_packet, ethernet_frame_header, 14);
    memcpy(send_packet + 14, ip_hdr, 20);
    memcpy(send_packet + 14 + 20, icmp_net_unreachable, sizeof(sr_icmp_t3_hdr_t));
    sr_send_packet(sr, send_packet, 14 + 20 + sizeof(sr_icmp_t3_hdr_t), interface);
    return;
}

// Handle IP Packets
void handle_ip_packet(uint8_t *packet, unsigned int len, struct sr_instance *sr, sr_ethernet_hdr_t *ethernet_frame_header, char *interface) {
    sr_ip_hdr_t *ip_hdr = malloc(sizeof(sr_ip_hdr_t));
    memcpy(ip_hdr, packet + 14, 20);

    // Checksum verification
    uint16_t old_checksum = ip_hdr->ip_sum;
    ip_hdr->ip_sum = 0;
    if (cksum((void *)ip_hdr, sizeof(sr_ip_hdr_t)) != old_checksum) {
        printf("Checksums don't match\n");
        return;
    }
    ip_hdr->ip_sum = old_checksum;

    int forMe = for_me(sr->if_list, ip_hdr->ip_dst);

    if (forMe == 1) {
        printf("Packet is for me\n");
        if (ip_hdr->ip_p == ip_protocol_icmp) {
            struct sr_icmp_hdr *request = malloc(sizeof(struct sr_icmp_hdr));
            memcpy(request, packet + 14 + 20, 4);

            struct sr_icmp_hdr *tmp_hdr = (struct sr_icmp_hdr *)(packet + 14 + 20);
            if (icmp_cksum(tmp_hdr, len - 34) == 0)
                return;

            request->icmp_type = 0;
            request->icmp_code = 0;
            request->icmp_sum = 0;
            request->icmp_sum = cksum((void *)(packet + 14 + 20), len - 34);

            uint8_t *data = malloc(len - 38);
            memcpy(data, packet + 14 + 20 + 4, len - 38);

            uint8_t *temp = malloc(6);
            memcpy(temp, ethernet_frame_header->ether_dhost, 6);
            memcpy(ethernet_frame_header->ether_dhost, ethernet_frame_header->ether_shost, 6);
            memcpy(ethernet_frame_header->ether_shost, temp, 6);

            uint32_t temp_dst = ip_hdr->ip_dst;
            ip_hdr->ip_dst = ip_hdr->ip_src;
            ip_hdr->ip_src = temp_dst;
            ip_hdr->ip_ttl = 64;
            ip_hdr->ip_id = htons(ID);
            ID++;
            ip_hdr->ip_sum = 0;
            ip_hdr->ip_sum = cksum((void *)(ip_hdr), 20);

            uint8_t *icmp_packet = malloc(len);
            memcpy(icmp_packet, ethernet_frame_header, 14);
            memcpy(icmp_packet + 14, ip_hdr, 20);
            memcpy(icmp_packet + 14 + 20, request, 4);
            memcpy(icmp_packet + 14 + 20 + 4, data, len - 38);
            sr_send_packet(sr, icmp_packet, len, interface);
        } else {
            handle_tcpudp_packets(packet, ethernet_frame_header, sr, interface, ip_hdr);
            return;
        }
    } else {
        forward_packet(ip_hdr, packet, ethernet_frame_header, interface, len, sr);
        return;
    }
}

void handle_arp_request(sr_arp_hdr_t *arp_hdr, struct sr_if *sr_interface, struct sr_instance *sr) {
    printf("Received ARP Request\n");
    if (arp_hdr->ar_tip == sr_interface->ip) {
        sr_ethernet_hdr_t *ethernet_frame_header = malloc(sizeof(sr_ethernet_hdr_t));
        memcpy(ethernet_frame_header->ether_dhost, arp_hdr->ar_sha, ETHER_ADDR_LEN);
        memcpy(ethernet_frame_header->ether_shost, sr_interface->addr, ETHER_ADDR_LEN);
        ethernet_frame_header->ether_type = htons(ethertype_arp);

        sr_arp_hdr_t *reply = malloc(sizeof(sr_arp_hdr_t));
        reply->ar_hrd = htons(arp_hrd_ethernet);
        reply->ar_pro = htons(ethertype_ip);
        reply->ar_hln = ETHER_ADDR_LEN;
        reply->ar_pln = 4;
        reply->ar_op = htons(arp_op_reply);
        memcpy(reply->ar_sha, sr_interface->addr, ETHER_ADDR_LEN);
        reply->ar_sip = sr_interface->ip;
        memcpy(reply->ar_tha, arp_hdr->ar_sha, ETHER_ADDR_LEN);
        reply->ar_tip = arp_hdr->ar_sip;

        uint8_t *packet = malloc(14 + 28);
        memcpy(packet, ethernet_frame_header, 14);
        memcpy(packet + 14, reply, 28);
        sr_send_packet(sr, packet, 14 + 28, sr_interface->name);
    }
}

void handle_arp_reply(sr_arp_hdr_t *arp_hdr, struct sr_if *sr_interface, struct sr_instance *sr) {
    printf("Received ARP Reply\n");
    if (arp_hdr->ar_tip == sr_interface->ip) {
        struct sr_arpreq *request = sr_arpcache_insert(&sr->cache, arp_hdr->ar_sha, ntohl(arp_hdr->ar_sip));
        if (request != NULL) {
            printf("Sending requested packets\n");

            while (request->packets != NULL) {
                struct sr_packet *current_packet = request->packets;
                memcpy(((sr_ethernet_hdr_t *)current_packet->buf)->ether_shost, sr_interface->addr, ETHER_ADDR_LEN);
                memcpy(((sr_ethernet_hdr_t *)current_packet->buf)->ether_dhost, arp_hdr->ar_sha, ETHER_ADDR_LEN);
                sr_send_packet(sr, current_packet->buf, current_packet->len, current_packet->iface);
                request->packets = request->packets->next;
            }
            sr_arpreq_destroy(&sr->cache, request);
        } else {
            printf("No requests found\n");
        }
    }
}

// Handle ARP Packets
void handle_arp_packet(uint8_t *packet, unsigned int len, struct sr_if *iface, struct sr_instance *sr) {
    printf("Received ARP packet\n");

    sr_arp_hdr_t *arp_hdr = malloc(sizeof(sr_arp_hdr_t));
    memcpy(arp_hdr, packet + 14, 28);

    if (ntohs(arp_hdr->ar_op) == arp_op_request) {
        handle_arp_request(arp_hdr, iface, sr);
        return;
    } else if (ntohs(arp_hdr->ar_op) == arp_op_reply) {
        handle_arp_reply(arp_hdr, iface, sr);
        return;
    }
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

void sr_handlepacket(struct sr_instance *sr, uint8_t *packet, unsigned int len, char *interface) {
    /* REQUIRES */
    assert(sr);
    assert(packet);
    assert(interface);

    printf("*** -> Received packet of length %d \n", len);

    sr_ethernet_hdr_t *ethernet_frame_header = malloc(sizeof(sr_ethernet_hdr_t));
    memcpy(ethernet_frame_header, packet, 14);
    print_hdr_eth((uint8_t *)ethernet_frame_header);

    struct sr_if *sr_interface = sr_get_interface(sr, interface);

    if (ntohs(ethernet_frame_header->ether_type) == ethertype_ip) {
        handle_ip_packet(packet, len, sr, ethernet_frame_header, interface); // Handle IP packet
        return;
    } else if (ntohs(ethernet_frame_header->ether_type) == ethertype_arp) {
        handle_arp_packet(packet, len, sr_interface, sr); // Handle ARP packet
        return;

    } else {
        printf("Unknown ethertype\n");
        return;
    }
}
