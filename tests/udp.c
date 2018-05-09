#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVER "10.0.0.2"
#define PORT 10

char buf[1400];

int main(void)
{
    struct sockaddr_in si_other = {.sin_family = AF_INET,
                                   .sin_port = htons(PORT)};
    int s;

    if (inet_aton(SERVER, &si_other.sin_addr) == 0) {
        fprintf(stderr, "inet_aton() failed\n");
        exit(1);
    }

    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        perror("Failed to open socket");
        exit(1);
    }

    while (1) {
        if (sendto(s, buf, sizeof(buf), 0, (struct sockaddr *) &si_other,
                   sizeof(struct sockaddr_in)) == -1) {
            perror("sendto()");
            exit(1);
        }
    }

    close(s);
    return 0;
}
