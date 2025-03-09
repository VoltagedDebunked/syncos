#ifndef SYNCOS_NET_NET_H
#define SYNCOS_NET_NET_H

#include <stdint.h>
#include <stdbool.h>

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10
#define TCP_FLAG_URG 0x20
#define TCP_FLAG_ECE 0x40
#define TCP_FLAG_CWR 0x80

// Ethernet frame
struct ethernet_frame {
    uint8_t dst_mac[6];
    uint8_t src_mac[6];
    uint16_t ethertype;
    uint8_t payload[];
} __attribute__((packed));

// IPv4 header
struct ipv4_header {
    uint8_t ihl : 4;
    uint8_t version : 4; 
    uint8_t ecn : 2;
    uint8_t dscp : 6;
    uint16_t total_length;
    uint16_t identification;
    uint16_t fragment_offset : 13;
    uint16_t flags : 3;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t header_checksum;
    uint32_t src_addr; 
    uint32_t dst_addr;
} __attribute__((packed));

// TCP header
struct tcp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint16_t ns : 1;
    uint16_t reserved : 3;
    uint16_t data_offset : 4;
    uint16_t fin : 1;
    uint16_t syn : 1;
    uint16_t rst : 1;
    uint16_t psh : 1; 
    uint16_t ack : 1;
    uint16_t urg : 1;
    uint16_t ece : 1;
    uint16_t cwr : 1;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_pointer;
} __attribute__((packed));

// DNS header
struct dns_header {
    uint16_t id;
    uint16_t flags;
    uint16_t question_count;
    uint16_t answer_rr_count;
    uint16_t authority_rr_count;
    uint16_t additional_rr_count;
} __attribute__((packed));

// DNS question section format
struct dns_question {
    uint16_t qtype;
    uint16_t qclass;
} __attribute__((packed));

// DNS resource record format
struct dns_rr {
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t rdlength;
    uint8_t rdata[];
} __attribute__((packed));

// HTTP request
struct http_request {
    char* method;
    char* path;
    char* version;
    char* headers;
    uint8_t* body;
};

// HTTP response 
struct http_response {
    char* version;
    int status_code;
    char* status_text;
    char* headers;
    uint8_t* body;
};

// Initialize networking
void net_init(void);

// Send an ethernet frame
void net_send_ethernet(struct ethernet_frame* frame, uint32_t len);

// Receive an ethernet frame
uint32_t net_receive_ethernet(struct ethernet_frame* frame);

// Resolve a hostname to an IP address
uint32_t net_dns_resolve(const char* hostname);

// Open an HTTP connection
int net_http_connect(const char* hostname, uint16_t port);

// Send an HTTP request
void net_http_send_request(int socket, struct http_request* req);

// Receive an HTTP response  
void net_http_receive_response(int socket, struct http_response* resp);

// Close an HTTP connection
void net_http_close(int socket);

#endif