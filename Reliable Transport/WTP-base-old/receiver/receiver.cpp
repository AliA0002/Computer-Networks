#include "crc32.h"
#include "PacketHeader.h"
#include <iostream>
#include <string>
#include <string>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <vector>
#include <algorithm>
#include <fstream>
#include <arpa/inet.h> // htons()
#include <netdb.h>
#include <netinet/in.h>
#include <stdexcept>
#include <cstdint>
#include <cstring>

#define MAX_PACKET_SIZE 1472

using namespace std;

void parse_chunk(char *buf, char *c, size_t &len)
{
    struct PacketHeader p;
    memcpy(&p, buf, sizeof(struct PacketHeader));
    len = p.length;
    memcpy(c, buf + sizeof(struct PacketHeader), len);
}

void logging(FILE *log_file, struct PacketHeader p)
{
    fprintf(log_file, "%u %u %u %u\n", p.type, p.seqNum, p.length, p.checksum);
    fflush(log_file);
}

int send_ACK(int sockfd, int port, sockaddr_in client, int seqNum)
{
    char empty[1];
    struct PacketHeader p = {3, seqNum, 0, crc32(empty, 0)};
    char msg[MAX_PACKET_SIZE];

    memcpy(msg, &p, sizeof(PacketHeader));
    memcpy(msg + sizeof(PacketHeader), empty, 0); // Maybe we dont need this?
    // int sendto(int sockfd, const void *msg, int len, unsigned int flags, const struct sockaddr *to, socklen_t tolen);
    // struct sockaddr_in addr; // inet_ntoa(client.sin_addr), ntohs(client.sin_port)
    // addr.sin_family = AF_INET;
    // addr.sin_addr.s_addr = INADDR_ANY;
    // addr.sin_port = htons(port);
    int err = sendto(sockfd, msg, sizeof(PacketHeader), 0, (sockaddr *)&client, 0);
    if (err < 0)
    {
        cerr << "Error sending ACK\n";
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    // Parse command line
    int port_num = atoi(argv[1]);
    int window_size = atoi(argv[2]);
    char *output_dir = argv[3];
    char *log = argv[4];
    FILE *log_file = fopen(log, "a+");

    int file_idx = 0;
    vector<bool> ACK(window_size, false);
    vector<bool> true_key = {true};
    vector<string> window(window_size, "/0");
    int window_idx = 0;
    bool done = false;
    bool connected = false;
    struct PacketHeader packet_h;
    char chunk[MAX_PACKET_SIZE];
    size_t chunk_len = 0;
    int seqNum = 0;
    sockaddr_in client;

    // Initiate UDP socket
    int fd;
    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("error creating socket");
        exit(1);
    }
    struct sockaddr_in init;
    init.sin_family = AF_INET;
    init.sin_port = htons(port_num);
    init.sin_addr.s_addr = INADDR_ANY;
    memset(&(init.sin_zero), '\0', 8); // Optional

    if (bind(fd, (struct sockaddr *)&init, sizeof(struct sockaddr)) < 0)
    {
        perror("error binding socket");
        exit(1);
    }
    // End of initializing UDP

    char buffer[1024 * 8];
    bzero(buffer, sizeof(buffer));

    while (true)
    {
        connected = false;
        done = false;
        seqNum = 0;

        char out_file[strlen(output_dir) + 10];
        sprintf(out_file, "%s/FILE-%d", output_dir, file_idx);
        ofstream output(out_file); // Open new output file every connection

        // Reset window back to false
        for (int i = 0; i < window_size; i++)
            ACK[i] = false;
        window_idx = 0;

        bzero(chunk, sizeof(chunk));

        // loops until receieved END packet type
        while (!done)
        {
            // int bytes = recvfrom(fd, buffer, sizeof(buffer), 0); // look into this
            socklen_t sockaddr_len = sizeof(sockaddr_in);
            int bytes = recvfrom(fd, buffer, sizeof(buffer), 0, (sockaddr *)&client, &sockaddr_len);
            if (bytes < 0)
            {
                perror("Error receiving");
                exit(1);
            }
            memcpy(&packet_h, buffer, sizeof(struct PacketHeader));

            // Print to logfile and FLUSH
            logging(log_file, packet_h);

            // Sets chunk_len = length of data
            parse_chunk(buffer, chunk, chunk_len);

            // Gets calculated checksum
            uint32_t calculated_checksum = crc32(chunk, chunk_len);

            // For testing
            cout << "Calculated checksum: " << calculated_checksum << endl
                 << "Correct is: " << packet_h.checksum << endl;

            // Check if the checksums match
            if (calculated_checksum != packet_h.checksum)
            {
                cout << "Incorrect Checksum" << endl;
                continue;
            }

            // Perform operation based on packet type
            switch (packet_h.type)
            {
            case 0: // START
                if (!connected)
                {
                    // We are not in the middle of a connection right now
                    // Accept connection & ACK it
                    connected = true;
                    seqNum = packet_h.seqNum;
                    send_ACK(fd, port_num, client, seqNum);
                }
                // Else we are in middle of connection so just ignore it
                break;
            case 1: // END
                if (!connected)
                {
                    // Drop the packet
                    continue;
                }
                send_ACK(fd, port_num, client, seqNum);
                done = true;
                file_idx++;
                connected = false;
                break;
            case 2: // DATA
            {
                if (packet_h.seqNum >= window_size + seqNum || !connected)
                {
                    // Drop the packet
                    continue;
                }
                // Store packet
                int index = packet_h.seqNum % window_size;
                window[index] = string(chunk);
                ACK[index] = true;
                if (packet_h.seqNum == seqNum)
                {
                    // Check for the highest sequence number (say M) of the in­order packets it has already received
                    vector<bool>::iterator start_itr = find(ACK.begin(), ACK.end(), true); // first occurence of true
                    if (std::equal(ACK.begin() + 1, ACK.end(), ACK.begin()))
                    {
                        // All equal to true
                        // Write to log & clear vectors
                        bool end_of_packets = false;
                        int i = seqNum;
                        while (!end_of_packets)
                        {
                            ACK[i] = false;
                            output.write(window[i].c_str(), sizeof(window[i].c_str()));
                            window[i] = "/0";
                            ++i;
                            if (i >= window_size)
                            {
                                i = 0;
                            }
                            if (i == seqNum)
                            {
                                end_of_packets = true;
                            }
                        }

                        seqNum += window_size;
                    }
                    else if ((seqNum % window_size) == (start_itr - ACK.begin()))
                    {
                        // First occurence of true is seqNum
                        vector<bool>::iterator end_itr = find_end(ACK.begin(), ACK.end(), true_key.begin(), true_key.end()); // last occurence of true
                        seqNum += end_itr - start_itr + 1;

                        // Write to log & clear vectors
                        for (int i = start_itr - ACK.begin(); i <= end_itr - ACK.begin(); ++i)
                        {
                            ACK[i] = false;
                            output.write(window[i].c_str(), sizeof(window[i].c_str()));
                            window[i] = "/0";
                        }
                    }
                    else if (start_itr != ACK.end() && (seqNum % window_size) > (start_itr - ACK.begin()))
                    {
                        seqNum += window_size - (seqNum % window_size) + (start_itr - ACK.begin());

                        // Write to log & clear vectors
                        int i = seqNum % window_size;
                        bool cleared = false;
                        while (!cleared)
                        {
                            ACK[i] = false;
                            output.write(window[i].c_str(), sizeof(window[i].c_str()));
                            window[i] = "/0";
                            ++i;
                            if (i >= window_size)
                            {
                                i = 0;
                            }
                            if (i == start_itr - ACK.begin() + 1)
                            {
                                cleared = true;
                            }
                        }
                    }
                    // ACK seqNum (already updated based on cases)
                    send_ACK(fd, port_num, client, seqNum);
                }
                break;
            }
            default: // ACK
                break;
            } // End of Switch
        }     // End of Connection Loop
        output.close();
    } // End of program loop
}