#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <linux/if_packet.h>
#include <net/ethernet.h>

#include <netinet/tcp.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>

#include <sys/ioctl.h>
#include <net/if.h>

#include <time.h>

#include "my_includes/network_helper.h"
#include "my_includes/packet_service.h"

#ifndef DEBUG
    #define DEBUG 1
#endif

#define MAX_PORT 65535
#define MAC_LEN 6
#define IP_LEN 4

struct in_addr * get_gw_ip_address(char *dev_name);

char * ip_to_mac(char *ip_address);

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("usage: mps <destination_ip> <interface_name>\n");

        return 0;
    }

    printf("===================\n");
    printf("Matt's Port Scanner\n");
    printf("===================\n\n");

    struct in_addr *dest_ip;
    unsigned short start_prt = 1;
    unsigned short end_prt = MAX_PORT;
    char *dev_name;
    unsigned char *mac_dest;
    
    int loc_int_index;                      // Local interface index
    unsigned char *loc_mac_add;             // Local MAC address
    struct in_addr *loc_ip_add;             // Local IP address

    // Set destination IP
    dest_ip = get_ip_from_str(argv[1]);

    // Set network interface ID
    dev_name = argv[2];

    if (dest_ip == NULL) {
        fprintf(stderr, "ERROR: Cannot parse IP address\n");

        return -1;
    }

    // Search the ARP table for the MAC address associated with dest_ip.
    char* mac_str = ip_to_mac(get_ip_str(dest_ip));

    if (mac_str == NULL) {
        printf("Cannot get ARP entry for IP address: %s\n", get_ip_str(dest_ip));
        printf("Setting MAC_ADDRESS to gateway address.\n");
        struct in_addr *gw_ip_add = get_gw_ip_address(dev_name);

        if(gw_ip_add == NULL) {
            fprintf(stderr, "ERROR: Unable to get destination MAC address.\n");

            return -1;
        }

        char* gw_ip_str = get_ip_str(gw_ip_add);
        if (gw_ip_str == NULL) {
            fprintf(stderr, 
                    "ERROR: Unable to convert IP address into string.\n");
        }

        char *temp = ip_to_mac(gw_ip_str);
        mac_dest = get_mac_from_str(temp);

        if (mac_dest == NULL) {
            fprintf(stderr, "ERROR: Unable to get destination MAC address.\n");

            return -1;
        }
    } else {
        mac_dest = get_mac_from_str(mac_str);
    }

    // Verbose tag
    printf("Information\n");
    printf("-----------\n\n");
    printf("Destination IP:             %s\n", get_ip_str(dest_ip));
    printf("Destination ports:          %d-%d\n", start_prt, end_prt);
    printf("Destination MAC address:    %s\n", get_mac_str(mac_dest));
    printf("Local network device:       %s\n", "enp4s0");

    int sock_raw = socket(AF_PACKET, SOCK_RAW, IPPROTO_RAW);
    if(sock_raw == -1) {
        fprintf(stderr, "ERROR: Cannot open raw socket!\n");

        return -1;
    }

    // Get interface index
    loc_int_index = get_interface_index(&sock_raw, dev_name);
    if (loc_int_index == -1) {
        fprintf(stderr, "ERROR: Cannot get interface index.\n");

        return -1;
    }

    // Get MAC address of the interface
    loc_mac_add = get_mac_address(&sock_raw, dev_name);
    if (loc_mac_add == NULL) {
        fprintf(stderr, "ERROR: Cannot get MAC address.\n");

        return -1;
    }

    // Get IP address of the interface
    loc_ip_add = get_ip_address(&sock_raw, dev_name);
    if (loc_ip_add == NULL) {
        fprintf(stderr, "ERROR: Cannot get IP address.\n");

        return -1;
    }

    printf("Local device index:         %d\n", loc_int_index);
    printf("Local MAC address:          %s\n", get_mac_str(loc_mac_add));
    printf("Local IP address:           %s\n\n", get_ip_str(loc_ip_add));

    unsigned char *packet = construct_icmp_packet(get_ip_str(loc_ip_add), 
            get_ip_str(dest_ip), loc_mac_add, mac_dest);

    int send_len = send_packet(packet, 64, sock_raw, loc_int_index, 
            loc_mac_add);

    if (send_len < 0) {
        fprintf(stderr, "ERROR: problem sending packet!\n");

        return -1;
    }

    close(sock_raw);

    printf("Socket closed!\n");
    printf("Exiting!\n");

    return 0;
}

/*
 * Returns the gateway of the IP address. 
 * Returns NULL on error.
 */
struct in_addr * get_gw_ip_address(char *dev_name) {
    printf("Trying to find IP address of default gateway!\n");

    FILE *fp;
    const char* path = "route -n | grep ";
    
    const int P_BUFF_SIZE = sizeof(char) * 100;
    char* path_buff = malloc(P_BUFF_SIZE);
    memset(path_buff, 0, P_BUFF_SIZE);
    strncpy(path_buff, path, strlen(path));
    strncat(path_buff, dev_name, P_BUFF_SIZE - 1 - strlen(path_buff));

    fp = popen(path_buff, "r");

    const int OUTPUT_SIZE = sizeof(char) * 100;
    char *output = malloc(OUTPUT_SIZE);
    memset(output, 0, OUTPUT_SIZE);

    char *retVal;
    char *token;
    while((retVal = fgets(output, OUTPUT_SIZE, fp)) != NULL) {
        printf("OUTPUT: %s\n", output);
        
        // Parse output
        token = strtok(output, " ");
        for (int i = 0; token != NULL; i++) {
            if(strcmp("0.0.0.0", token) == 0) {
                printf("Gateway row obtained!\n");
                token = strtok(NULL, " ");
                
                // ERROR: Could not obtain IP address for default gateway
                if (token == NULL)
                    return NULL;
                
                struct in_addr *ip_add = get_ip_from_str(token);

                if (ip_add == NULL)
                    return NULL;

                // Default gateway IP address found!
                printf("Default gateway IP found: %s!\n", get_ip_str(ip_add));

                return ip_add;
            } else {
                token = strtok(NULL, " ");
            }
        }
    }

    return NULL;
}

/* 
 * Queries the ARP table to get the assigned MAC address of the IP.
 * Note that if the IP address is not found in the table, then the
 * gateway address is returned.
 * Returns NULL if entry in ARP table cannot be found or error.
*/
char * ip_to_mac(char *ip_address) {
    if (DEBUG > 1)
        printf("Searching ARP table for IP address: %s.\n", ip_address);

    const char *COMMAND = "arp -a ";

    FILE *fp;
    const int MAX_PATH = 200;
    char *path = malloc(sizeof(char) * MAX_PATH);
    memset(path, 0, sizeof(char) * MAX_PATH);

    strncpy(path, COMMAND, MAX_PATH - 1);
    strncat(path, ip_address, (MAX_PATH - 1) - strlen(path));

    fp = popen(path, "r");

    char* output = malloc(sizeof(char) * 100);
    memset(output, 0, sizeof(char) * 100);
    
    char *retVal = fgets(output, sizeof(char) * 100, fp);
    if (retVal == NULL) {
        return NULL;
    }

    // Check if arp entry exists
    if (strstr(output, "no match found") != NULL) {
        printf("No ARP entry found!\n");

        return NULL;
    }

    char *token;
    token = strtok(output, " ");

    const int MAX_TOKENS = 10;
    char *tokens[MAX_TOKENS];
    for (int i = 0; i < MAX_TOKENS; i++) {
        tokens[i] = NULL;
    }

    // Walk through other tokens
    for(int i = 0; token != NULL && i < MAX_TOKENS; i++) {
        tokens[i] = token;

        token = strtok(NULL, " ");
    }
    
    for(int i = 0; tokens[i] != NULL; i++) {
        printf("TOKENS: %s\n", tokens[i]);
    }

    printf("MAC address found: %s!\n", tokens[3]);

    return tokens[3];
}