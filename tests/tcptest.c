#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nstack_socket.h"

/* How to test:
 * Open a new terminal, enter in following command:

    $ nc -lv 10.0.0.1 10000

   then start this program. Normally you will see message
   print out in the terminal metioned above.
*/
static char buf[2048];

int main(void)
{
    void *sock = nstack_listen("/tmp/tnetcat.sock");
    if (!sock) {
        perror("Failed to open sock");
        exit(1);
    }
    struct nstack_sockaddr addr;
    addr.inet4_addr = 167772161;
    addr.port = 10000;
    size_t r;
    while (1) {
        memset(buf, 0, sizeof(buf));
        buf[0] = 'f';
        buf[1] = 'o';
        buf[2] = 'o';
        r = nstack_sendto(sock, buf, 3, 0, &addr);
        // r = nstack_recvfrom(sock, buf, sizeof(buf) - 1, 0, &addr);
        // if (r > 0)
        //     write(STDOUT_FILENO, buf, r);
        sleep(20);
    }
}