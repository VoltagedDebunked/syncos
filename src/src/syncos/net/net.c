#include <syncos/net/net.h>
#include <core/drivers/net/e1000.h>
#include <kstd/string.h>
#include <kstd/stdio.h>
#include <syncos/vmm.h>
#include <syncos/timer.h>

#define ETHERTYPE_IPV4 0x0800
#define ETHERTYPE_ARP  0x0806

#define IP_PROTOCOL_ICMP 1
#define IP_PROTOCOL_TCP  6
#define IP_PROTOCOL_UDP  17 

#define TCP_OPTION_END 0
#define TCP_OPTION_NOP 1
#define TCP_OPTION_MSS 2

#define HTONS(n) (((((uint16_t)(n) & 0xFF)) << 8) | (((uint16_t)(n) & 0xFF00) >> 8))
#define HTONL(n) (((((uint32_t)(n) & 0xFF)) << 24) | \
                  ((((uint32_t)(n)& 0xFF00)) << 8) | \
                  ((((uint32_t)(n)& 0xFF0000)) >> 8) | \
                  ((((uint32_t)(n)& 0xFF000000)) >> 24))

// Local MAC and IP
static uint8_t local_mac[6];
static uint32_t local_ip = 0xC0A80002; // 192.168.0.2
static uint8_t gateway_mac[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC}; // Gateway MAC (placeholder)

// Simple LCG random number generator state
static uint32_t net_random_state = 1;

// TCP connection states
#define TCP_STATE_CLOSED      0
#define TCP_STATE_SYN_SENT    1
#define TCP_STATE_ESTABLISHED 2
#define TCP_STATE_FIN_WAIT_1  3
#define TCP_STATE_FIN_WAIT_2  4
#define TCP_STATE_TIME_WAIT   5
#define TCP_STATE_CLOSE_WAIT  6
#define TCP_STATE_LAST_ACK    7

// TIME_WAIT timeout (in milliseconds)
#define TCP_TIME_WAIT_TIMEOUT 2000 // 2 seconds (standard is 2 minutes, shortened for testing)

// TCP connection structure
struct tcp_connection {
    uint32_t remote_ip;
    uint16_t remote_port;
    uint16_t local_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t state;
    uint16_t window_size;
    uint64_t timeout_timestamp; // Timestamp for timeouts
};

// Array of active TCP connections
#define MAX_TCP_CONNECTIONS 8
static struct tcp_connection tcp_connections[MAX_TCP_CONNECTIONS];
static int next_local_port = 49152; // Start of dynamic port range

// Buffers for Tx/Rx
static uint8_t tx_buffer[2048];
static uint8_t rx_buffer[2048];

// Forward declarations
static uint16_t calculate_checksum(uint16_t* data, uint16_t length);
static int find_free_tcp_connection(void);
static void tcp_send_packet(struct tcp_connection* conn, uint8_t flags, void* data, uint16_t data_len);
static void handle_tcp_packet(struct ethernet_frame* eth, struct ipv4_header* ip, struct tcp_header* tcp, uint16_t data_len);
static uint32_t net_random(void);

// Helper to calculate TCP checksum
static uint16_t calculate_tcp_checksum(struct ipv4_header* ipv4_hdr, struct tcp_header* tcp_hdr, uint16_t tcp_len) {
    uint32_t sum = 0;
    uint16_t len = tcp_len;
    
    // Pseudo header
    sum += (ipv4_hdr->src_addr >> 16) & 0xFFFF; 
    sum += (ipv4_hdr->src_addr) & 0xFFFF;
    sum += (ipv4_hdr->dst_addr >> 16) & 0xFFFF;
    sum += (ipv4_hdr->dst_addr) & 0xFFFF;
    sum += HTONS(IP_PROTOCOL_TCP);
    sum += HTONS(len);
    
    // TCP header and data
    uint16_t* ptr = (uint16_t*)tcp_hdr;
    while(len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    
    if(len > 0) {
        sum += *(uint8_t*)ptr;
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return (uint16_t)~sum;
}

// Calculate generic IP checksum
static uint16_t calculate_checksum(uint16_t* data, uint16_t length) {
    uint32_t sum = 0;
    
    while(length > 1) {
        sum += *data++;
        length -= 2;
    }
    
    if(length > 0) {
        sum += *(uint8_t*)data;
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return (uint16_t)~sum;
}

// TCP connection timeout checker
static void tcp_timeout_checker(uint64_t tick_count, void *context) {
    // Check TIME_WAIT connections for timeout
    for(int i = 0; i < MAX_TCP_CONNECTIONS; i++) {
        if(tcp_connections[i].state == TCP_STATE_TIME_WAIT) {
            if(timer_get_uptime_ms() >= tcp_connections[i].timeout_timestamp) {
                tcp_connections[i].state = TCP_STATE_CLOSED;
            }
        }
    }
}

// Initialize networking
void net_init(void) {
    // Get MAC address
    e1000_read_mac(local_mac);

    // Initialize TCP connections
    for(int i = 0; i < MAX_TCP_CONNECTIONS; i++) {
        tcp_connections[i].state = TCP_STATE_CLOSED;
    }
    
    // Initialize random number generator with timer
    net_random_state = timer_get_ticks();
    
    // Register TCP connection timeout checker
    timer_register_callback(tcp_timeout_checker, NULL, 100); // Check every 100ms

    printf("Network initialized. MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
        local_mac[0], local_mac[1], local_mac[2], 
        local_mac[3], local_mac[4], local_mac[5]);
}

// Send ethernet frame
void net_send_ethernet(struct ethernet_frame* frame, uint32_t len) {
    memcpy(tx_buffer, frame, len);
    e1000_transmit(tx_buffer, len);
}

// Receive ethernet frame
uint32_t net_receive_ethernet(struct ethernet_frame* frame) {
    uint16_t len = e1000_receive(rx_buffer);
    if(len > 0) {
        memcpy(frame, rx_buffer, len);
    }
    return len;  
}

// Process incoming packet
void net_process_packet(void) {
    struct {
        struct ethernet_frame eth;
        union {
            struct ipv4_header ip;
            uint8_t data[2048 - sizeof(struct ethernet_frame)];
        };
    } packet;

    uint16_t size = net_receive_ethernet(&packet.eth);
    if(size == 0) return;

    // Process by ethertype
    if(packet.eth.ethertype == HTONS(ETHERTYPE_IPV4)) {
        // Check if IP packet is for us
        if(packet.ip.dst_addr == local_ip) {
            // Process by protocol
            if(packet.ip.protocol == IP_PROTOCOL_TCP) {
                struct tcp_header* tcp = (struct tcp_header*)(((uint8_t*)&packet.ip) + packet.ip.ihl * 4);
                uint16_t data_len = HTONS(packet.ip.total_length) - (packet.ip.ihl * 4) - (tcp->data_offset * 4);
                handle_tcp_packet(&packet.eth, &packet.ip, tcp, data_len);
            }
        }
    }
}

// Find a free TCP connection slot
static int find_free_tcp_connection(void) {
    for(int i = 0; i < MAX_TCP_CONNECTIONS; i++) {
        if(tcp_connections[i].state == TCP_STATE_CLOSED) {
            return i;
        }
    }
    return -1;
}

// Send a TCP packet
static void tcp_send_packet(struct tcp_connection* conn, uint8_t flags, void* data, uint16_t data_len) {
    struct {
        struct ethernet_frame eth;
        struct ipv4_header ip;
        struct tcp_header tcp;
        uint8_t options[12]; // Space for options
        uint8_t data[1024];  // Space for data
    } packet;

    // Fill ethernet header
    memcpy(packet.eth.dst_mac, gateway_mac, 6); // To gateway
    memcpy(packet.eth.src_mac, local_mac, 6);
    packet.eth.ethertype = HTONS(ETHERTYPE_IPV4);

    // Fill IP header
    packet.ip.version = 4;
    packet.ip.ihl = 5;
    packet.ip.dscp = 0;
    packet.ip.ecn = 0;
    uint16_t total_ip_len = sizeof(struct ipv4_header) + sizeof(struct tcp_header);
    
    // Add TCP options if SYN flag is set (MSS option)
    uint8_t tcp_header_len = sizeof(struct tcp_header);
    if(flags & TCP_FLAG_SYN) {
        // Add MSS option
        packet.options[0] = TCP_OPTION_MSS;
        packet.options[1] = 4; // Option length
        *(uint16_t*)&packet.options[2] = HTONS(1460); // MSS value
        
        // Add EOL option
        packet.options[4] = TCP_OPTION_END;
        
        // Pad to 4-byte boundary
        packet.options[5] = 0;
        packet.options[6] = 0;
        packet.options[7] = 0;
        
        tcp_header_len += 8; // 8 bytes of options
        packet.tcp.data_offset = tcp_header_len / 4; // 7 32-bit words
        total_ip_len += 8;
    } else {
        packet.tcp.data_offset = tcp_header_len / 4; // 5 32-bit words
    }
    
    // Add data length
    total_ip_len += data_len;
    if(data_len > 0 && data != NULL) {
        memcpy(packet.data, data, data_len);
    }
    
    packet.ip.total_length = HTONS(total_ip_len);
    packet.ip.identification = 0;
    packet.ip.fragment_offset = 0;
    packet.ip.flags = 0;
    packet.ip.ttl = 64;
    packet.ip.protocol = IP_PROTOCOL_TCP;
    packet.ip.src_addr = local_ip;
    packet.ip.dst_addr = conn->remote_ip;
    packet.ip.header_checksum = 0;
    packet.ip.header_checksum = calculate_checksum((uint16_t*)&packet.ip, sizeof(struct ipv4_header));

    // Fill TCP header
    packet.tcp.fin = (flags & TCP_FLAG_FIN) ? 1 : 0;
    packet.tcp.syn = (flags & TCP_FLAG_SYN) ? 1 : 0;
    packet.tcp.rst = (flags & TCP_FLAG_RST) ? 1 : 0;
    packet.tcp.psh = (flags & TCP_FLAG_PSH) ? 1 : 0;
    packet.tcp.ack = (flags & TCP_FLAG_ACK) ? 1 : 0;
    packet.tcp.urg = (flags & TCP_FLAG_URG) ? 1 : 0;
    packet.tcp.ece = (flags & TCP_FLAG_ECE) ? 1 : 0;
    packet.tcp.cwr = (flags & TCP_FLAG_CWR) ? 1 : 0;

    // Calculate TCP checksum
    packet.tcp.checksum = calculate_tcp_checksum(&packet.ip, &packet.tcp, tcp_header_len + data_len);

    // Send packet
    net_send_ethernet(&packet.eth, sizeof(struct ethernet_frame) + total_ip_len);
    
    // Update sequence number if we're sending data or SYN/FIN
    if(data_len > 0) {
        conn->seq_num += data_len;
    }
    if(flags & (TCP_FLAG_SYN | TCP_FLAG_FIN)) {
        conn->seq_num++;
    }
}

// Handle incoming TCP packet
static void handle_tcp_packet(struct ethernet_frame* eth, struct ipv4_header* ip, struct tcp_header* tcp, uint16_t data_len) {
    uint16_t src_port = HTONS(tcp->src_port);
    uint16_t dst_port = HTONS(tcp->dst_port);
    uint32_t seq_num = HTONL(tcp->seq_num);
    uint32_t ack_num = HTONL(tcp->ack_num);
    uint8_t flags = 0;
    
    // Find matching connection
    int conn_idx = -1;
    for(int i = 0; i < MAX_TCP_CONNECTIONS; i++) {
        if(tcp_connections[i].state != TCP_STATE_CLOSED &&
           tcp_connections[i].remote_ip == ip->src_addr &&
           tcp_connections[i].remote_port == src_port &&
           tcp_connections[i].local_port == dst_port) {
            conn_idx = i;
            break;
        }
    }
    
    // No connection found and not a SYN packet
    if(conn_idx == -1 && !(flags & TCP_FLAG_SYN)) {
        // Send RST if it's not a RST packet
        if(!(flags & TCP_FLAG_RST)) {
            // Create temporary connection for RST
            struct tcp_connection temp_conn;
            temp_conn.remote_ip = ip->src_addr;
            temp_conn.remote_port = src_port;
            temp_conn.local_port = dst_port;
            temp_conn.seq_num = 0;
            temp_conn.ack_num = seq_num + data_len;
            if(flags & TCP_FLAG_SYN) temp_conn.ack_num++;
            temp_conn.window_size = 0;
            
            // Send RST packet
            tcp_send_packet(&temp_conn, TCP_FLAG_RST | TCP_FLAG_ACK, NULL, 0);
        }
        return;
    }
    
    // Handle based on connection state
    struct tcp_connection* conn = &tcp_connections[conn_idx];
    
    if(conn_idx != -1) {
        // Existing connection
        switch(conn->state) {
            case TCP_STATE_SYN_SENT:
                if(flags & TCP_FLAG_SYN && flags & TCP_FLAG_ACK) {
                    // Got SYN-ACK, send ACK
                    conn->ack_num = seq_num + 1;
                    conn->state = TCP_STATE_ESTABLISHED;
                    tcp_send_packet(conn, TCP_FLAG_ACK, NULL, 0);
                }
                break;
                
            case TCP_STATE_ESTABLISHED:
                // Handle data
                if(data_len > 0) {
                    // Update ACK number
                    conn->ack_num = seq_num + data_len;
                    
                    // Send ACK
                    tcp_send_packet(conn, TCP_FLAG_ACK, NULL, 0);
                }
                
                // Handle FIN
                if(flags & TCP_FLAG_FIN) {
                    conn->ack_num++;
                    conn->state = TCP_STATE_CLOSE_WAIT;
                    tcp_send_packet(conn, TCP_FLAG_ACK, NULL, 0);
                    
                    // Immediately move to LAST_ACK by sending our FIN
                    tcp_send_packet(conn, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
                    conn->state = TCP_STATE_LAST_ACK;
                }
                break;
                
            case TCP_STATE_FIN_WAIT_1:
                if(flags & TCP_FLAG_ACK) {
                    conn->state = TCP_STATE_FIN_WAIT_2;
                }
                
                if(flags & TCP_FLAG_FIN) {
                    conn->ack_num++;
                    tcp_send_packet(conn, TCP_FLAG_ACK, NULL, 0);
                    conn->state = TCP_STATE_TIME_WAIT;
                    conn->timeout_timestamp = timer_get_uptime_ms() + TCP_TIME_WAIT_TIMEOUT;
                }
                break;
                
            case TCP_STATE_FIN_WAIT_2:
                if(flags & TCP_FLAG_FIN) {
                    conn->ack_num++;
                    tcp_send_packet(conn, TCP_FLAG_ACK, NULL, 0);
                    conn->state = TCP_STATE_TIME_WAIT;
                    conn->timeout_timestamp = timer_get_uptime_ms() + TCP_TIME_WAIT_TIMEOUT;
                }
                break;
                
            case TCP_STATE_LAST_ACK:
                if(flags & TCP_FLAG_ACK) {
                    conn->state = TCP_STATE_CLOSED;
                }
                break;
                
            case TCP_STATE_TIME_WAIT:
                // Check if TIME_WAIT timeout has expired
                if (timer_get_uptime_ms() >= conn->timeout_timestamp) {
                    conn->state = TCP_STATE_CLOSED;
                }
                break;
        }
    }
}

// Open TCP connection
int net_http_connect(const char* hostname, uint16_t port) {
    // Resolve IP address
    uint32_t ip = net_dns_resolve(hostname); 
    if(!ip) return -1;

    // Find free TCP connection slot
    int conn_idx = find_free_tcp_connection();
    if(conn_idx == -1) return -1;
    
    // Initialize connection
    struct tcp_connection* conn = &tcp_connections[conn_idx];
    conn->remote_ip = ip;
    conn->remote_port = port;
    conn->local_port = next_local_port++;
    if(next_local_port > 65535) next_local_port = 49152;
    conn->seq_num = net_random(); // Use random initial sequence number
    conn->ack_num = 0;
    conn->state = TCP_STATE_SYN_SENT;
    conn->window_size = 8192;
    
    // Send SYN packet
    tcp_send_packet(conn, TCP_FLAG_SYN, NULL, 0);
    
    // Wait for connection to establish
    uint64_t timeout_time = timer_get_uptime_ms() + 5000; // 5 second timeout
    while(conn->state == TCP_STATE_SYN_SENT && timer_get_uptime_ms() < timeout_time) {
        net_process_packet();
        timer_sleep_ms(10); // Sleep 10ms between packet processing
    }
    
    if(conn->state != TCP_STATE_ESTABLISHED) {
        conn->state = TCP_STATE_CLOSED;
        return -1;
    }
    
    return conn_idx;
}

// Send HTTP request
void net_http_send_request(int socket, struct http_request* req) {
    if(socket < 0 || socket >= MAX_TCP_CONNECTIONS) return;
    struct tcp_connection* conn = &tcp_connections[socket];
    
    if(conn->state != TCP_STATE_ESTABLISHED) return;

    char buffer[1024];
    
    // Format request line
    snprintf(buffer, sizeof(buffer), "%s %s %s\r\n", req->method, req->path, req->version);

    // Append headers  
    strcat(buffer, req->headers);
    strcat(buffer, "\r\n");

    // Send the request data
    tcp_send_packet(conn, TCP_FLAG_ACK, buffer, strlen(buffer));
}

// Receive HTTP response
void net_http_receive_response(int socket, struct http_response* resp) {
    if(socket < 0 || socket >= MAX_TCP_CONNECTIONS) return;
    struct tcp_connection* conn = &tcp_connections[socket];
    
    if(conn->state != TCP_STATE_ESTABLISHED) {
        resp->status_code = 0;
        return;
    }

    // Buffer for receiving data
    static char response_buffer[4096];
    int buffer_pos = 0;
    uint64_t timeout_time = timer_get_uptime_ms() + 5000; // 5 second timeout
    
    // Receive data
    while(timer_get_uptime_ms() < timeout_time) {
        net_process_packet();
        
        // Check for new data
        struct {
            struct ethernet_frame eth;
            struct ipv4_header ip;
            struct tcp_header tcp;
            char data[2048];
        } packet;
        
        uint16_t size = net_receive_ethernet(&packet.eth);
        if(size > 0 && 
           packet.eth.ethertype == HTONS(ETHERTYPE_IPV4) &&
           packet.ip.protocol == IP_PROTOCOL_TCP) {
            
            // Extract TCP header
            struct tcp_header* tcp = (struct tcp_header*)(((uint8_t*)&packet.ip) + packet.ip.ihl * 4);
            uint16_t data_len = HTONS(packet.ip.total_length) - (packet.ip.ihl * 4) - (tcp->data_offset * 4);
            
            // Check if packet is for our connection
            if(packet.ip.src_addr == conn->remote_ip &&
               HTONS(tcp->src_port) == conn->remote_port &&
               HTONS(tcp->dst_port) == conn->local_port &&
               data_len > 0) {
                
                // Extract data portion
                char* data = ((char*)tcp) + (tcp->data_offset * 4);
                
                // Append to buffer
                if(buffer_pos + data_len < sizeof(response_buffer)) {
                    memcpy(response_buffer + buffer_pos, data, data_len);
                    buffer_pos += data_len;
                    response_buffer[buffer_pos] = '\0';
                }
                
                // Update ACK
                conn->ack_num = HTONL(tcp->seq_num) + data_len;
                tcp_send_packet(conn, TCP_FLAG_ACK, NULL, 0);
                
                // Check if we've received the end of headers
                if(strstr(response_buffer, "\r\n\r\n")) {
                    // We have the headers, parse them
                    char* headers_end = strstr(response_buffer, "\r\n\r\n");
                    *headers_end = '\0'; // Temporarily terminate at end of headers
                    
                    // Parse status line
                    char* status_line = response_buffer;
                    char* space1 = strchr(status_line, ' ');
                    if(space1) {
                        *space1 = '\0';
                        resp->version = status_line;
                        
                        char* status_code_str = space1 + 1;
                        char* space2 = strchr(status_code_str, ' ');
                        if(space2) {
                            *space2 = '\0';
                            resp->status_code = atoi(status_code_str);
                            resp->status_text = space2 + 1;
                        }
                    }
                    
                    // Set headers
                    resp->headers = space1 + 1;
                    
                    // Set body
                    resp->body = headers_end + 4; // Skip \r\n\r\n
                    
                    // Restore the \r\n\r\n
                    *headers_end = '\r';
                    
                    return; // We have a complete response
                }
            }
        }
        
        timer_sleep_ms(10); // Sleep 10ms between packet processing
    }
    
    // Timeout, return error
    resp->status_code = 0;
}

// Close HTTP connection  
void net_http_close(int socket) {
    if(socket < 0 || socket >= MAX_TCP_CONNECTIONS) return;
    struct tcp_connection* conn = &tcp_connections[socket];
    
    if(conn->state == TCP_STATE_ESTABLISHED) {
        // Send FIN packet
        tcp_send_packet(conn, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
        conn->state = TCP_STATE_FIN_WAIT_1;
        
        // Wait for connection to close
        uint64_t timeout_time = timer_get_uptime_ms() + 3000; // 3 second timeout
        while(conn->state != TCP_STATE_CLOSED && conn->state != TCP_STATE_TIME_WAIT && 
              timer_get_uptime_ms() < timeout_time) {
            net_process_packet();
            timer_sleep_ms(10); // Sleep 10ms between packet processing
        }
    }
    
    // Force connection to closed state
    conn->state = TCP_STATE_CLOSED;
}

// Generate a random number using LCG algorithm
static uint32_t net_random(void) {
    // Linear Congruential Generator (LCG) constants
    // Using values from Microsoft C/C++ runtime
    const uint32_t a = 214013;
    const uint32_t c = 2531011;
    const uint32_t m = 0x80000000; // 2^31
    
    // Mix in some timing information to improve entropy
    net_random_state = net_random_state ^ ((uint32_t)timer_get_ticks() & 0xFFFF);
    
    // LCG formula: state = (a * state + c) % m
    net_random_state = (a * net_random_state + c) & 0x7FFFFFFF;
    
    return net_random_state;
}