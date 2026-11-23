#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <syslog.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* TCP port used by the server */
#define PORT 9000

/* File used to store all received packets */
#define DATA_FILE "/var/tmp/aesdsocketdata"

/* Temporary buffer size used for receiving and transmitting data */
#define BUFFER_SIZE 1024

/*
 * Set when SIGINT or SIGTERM is received.
 * sig_atomic_t allows safe access to this variable from a signal handler.
 */
static volatile sig_atomic_t stop_requested = 0;

/*
 * Signal handler for SIGINT and SIGTERM.
 * Only sets a flag so that the main program can perform cleanup safely.
 */
static void signal_handler(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

/*
 * Register handlers for SIGINT and SIGTERM.
 * Returns -1 if either handler cannot be installed.
 */
static int install_signal_handlers(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = signal_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    if (sigaction(SIGINT, &action, NULL) == -1)
        return -1;

    if (sigaction(SIGTERM, &action, NULL) == -1)
        return -1;

    return 0;
}

/*
 * Create a TCP socket and bind it to port 9000.
 *
 * The socket is configured to reuse its local address so that the
 * server can restart without waiting for the previous connection state
 * to expire.
 *
 * Returns the socket file descriptor on success, or -1 on failure.
 */
static int create_server_socket(void)
{
    int server_fd;
    int reuse = 1;
    struct sockaddr_in server_addr;

    /* Create an IPv4 TCP stream socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd == -1)
    {
        syslog(LOG_ERR, "socket failed: %s", strerror(errno));
        return -1;
    }

    /* Allow the address and port to be reused after the server exits */
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &reuse, sizeof(reuse)) == -1)
    {
        syslog(LOG_ERR, "setsockopt failed: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    /* Configure the server address */
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    /* Bind the socket to all available network interfaces */
    if (bind(server_fd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == -1)
    {
        syslog(LOG_ERR, "bind failed: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    return server_fd;
}

/*
 * Convert the server process into a daemon.
 *
 * This function is called only after the socket has been created and
 * successfully bound to port 9000.
 *
 * The parent process exits while the child continues running in the
 * background.
 */
static int daemonize(int server_fd)
{
    pid_t pid;

    pid = fork();

    if (pid == -1)
    {
        syslog(LOG_ERR, "fork failed: %s", strerror(errno));
        return -1;
    }

    /* Parent process: close its socket copy and exit */
    if (pid > 0)
    {
        close(server_fd);
        _exit(EXIT_SUCCESS);
    }

    /* Child process: create a new session */
    if (setsid() == -1)
    {
        syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
        return -1;
    }

    /* Set file creation permissions and move to the root directory */
    umask(0);

    if (chdir("/") == -1)
    {
        syslog(LOG_ERR, "chdir failed: %s", strerror(errno));
        return -1;
    }

    /* Redirect standard input, output, and error to /dev/null */
    int null_fd = open("/dev/null", O_RDWR);

    if (null_fd == -1)
    {
        syslog(LOG_ERR, "open /dev/null failed: %s", strerror(errno));
        return -1;
    }

    for (int i = 0; i < 3; i++)
    {
        if (dup2(null_fd, i) == -1)
        {
            syslog(LOG_ERR, "dup2 failed: %s", strerror(errno));
            close(null_fd);
            return -1;
        }
    }

    if (null_fd > 2)
        close(null_fd);

    return 0;
}

/*
 * Write all bytes to a file descriptor.
 *
 * write() may write fewer bytes than requested, so this function
 * continues writing until the entire buffer has been processed.
 */
static int write_all(int fd, const char *buffer, size_t length)
{
    size_t total_written = 0;

    while (total_written < length)
    {
        ssize_t written = write(fd, buffer + total_written,
                                length - total_written);

        if (written == -1)
        {
            if (errno == EINTR && !stop_requested)
                continue;

            return -1;
        }

        if (written == 0)
            return -1;

        total_written += (size_t)written;
    }

    return 0;
}

/*
 * Send all bytes to a connected client.
 *
 * send() may transmit only part of the requested data, so this function
 * loops until the complete buffer has been sent.
 *
 * MSG_NOSIGNAL prevents SIGPIPE if the client disconnects unexpectedly.
 */
static int send_all(int fd, const char *buffer, size_t length)
{
    size_t total_sent = 0;

    while (total_sent < length)
    {
        ssize_t sent = send(fd, buffer + total_sent,
                            length - total_sent, MSG_NOSIGNAL);

        if (sent == -1)
        {
            if (errno == EINTR && !stop_requested)
                continue;

            return -1;
        }

        if (sent == 0)
            return -1;

        total_sent += (size_t)sent;
    }

    return 0;
}

/*
 * Append one complete newline-terminated packet to the data file.
 *
 * O_APPEND ensures that new packets are added to the end of the file.
 * O_CREAT creates the file if it does not already exist.
 */
static int append_packet(const char *packet, size_t packet_length)
{
    int file_fd;

    file_fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);

    if (file_fd == -1)
    {
        syslog(LOG_ERR, "open data file failed: %s", strerror(errno));
        return -1;
    }

    int result = write_all(file_fd, packet, packet_length);

    if (close(file_fd) == -1)
    {
        syslog(LOG_ERR, "close data file failed: %s", strerror(errno));
        return -1;
    }

    if (result == -1)
    {
        syslog(LOG_ERR, "writing data file failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}

/*
 * Send the complete contents of the data file to a client.
 *
 * The file is read and transmitted in small chunks instead of being
 * loaded entirely into memory. This allows the file to grow beyond
 * the available heap size.
 */
static int send_file_contents(int client_fd)
{
    char buffer[BUFFER_SIZE];
    int file_fd;

    file_fd = open(DATA_FILE, O_RDONLY);

    if (file_fd == -1)
    {
        syslog(LOG_ERR, "open data file for reading failed: %s",
               strerror(errno));
        return -1;
    }

    while (1)
    {
        /* Read the next chunk from the file */
        ssize_t bytes_read = read(file_fd, buffer, sizeof(buffer));

        if (bytes_read == -1)
        {
            if (errno == EINTR && !stop_requested)
                continue;

            close(file_fd);
            return -1;
        }

        /* End of file reached */
        if (bytes_read == 0)
            break;

        /* Forward the current chunk to the connected client */
        if (send_all(client_fd, buffer, (size_t)bytes_read) == -1)
        {
            close(file_fd);
            return -1;
        }
    }

    if (close(file_fd) == -1)
    {
        syslog(LOG_ERR, "close data file failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}

/*
 * Append received data to a dynamically allocated packet buffer.
 *
 * The buffer grows whenever additional capacity is needed.
 * This allows packets to be larger than BUFFER_SIZE.
 */
static int append_to_packet(char **packet, size_t *packet_length,
                            size_t *packet_capacity,
                            const char *data, size_t data_length)
{
    if (data_length > SIZE_MAX - *packet_length)
        return -1;

    size_t required = *packet_length + data_length;

    /* Allocate or expand the packet buffer when necessary */
    if (required > *packet_capacity)
    {
        size_t new_capacity = (*packet_capacity == 0) ?
                              BUFFER_SIZE : *packet_capacity;

        /* Double the capacity until it can hold the incoming data */
        while (new_capacity < required)
        {
            if (new_capacity > SIZE_MAX / 2)
            {
                new_capacity = required;
                break;
            }

            new_capacity *= 2;
        }

        char *new_packet = realloc(*packet, new_capacity);

        if (new_packet == NULL)
        {
            syslog(LOG_ERR, "packet allocation failed: %s",
                   strerror(errno));
            return -1;
        }

        *packet = new_packet;
        *packet_capacity = new_capacity;
    }

    /* Copy the new data after the previously received packet data */
    memcpy(*packet + *packet_length, data, data_length);
    *packet_length += data_length;

    return 0;
}

/*
 * Handle communication with one connected client.
 *
 * Data is accumulated until a newline character is received.
 * Every complete packet is appended to the data file, followed by
 * transmission of the entire file contents to the client.
 *
 * A packet may arrive through multiple recv() calls, and one recv()
 * call may contain multiple complete packets.
 */
static int handle_client(int client_fd)
{
    char receive_buffer[BUFFER_SIZE];
    char *packet = NULL;

    size_t packet_length = 0;
    size_t packet_capacity = 0;

    while (!stop_requested)
    {
        /* Receive the next chunk of data from the client */
        ssize_t bytes_received = recv(client_fd, receive_buffer,
                                      sizeof(receive_buffer), 0);

        if (bytes_received == -1)
        {
            if (errno == EINTR && !stop_requested)
                continue;

            if (!stop_requested)
                syslog(LOG_ERR, "recv failed: %s", strerror(errno));

            break;
        }

        /* A return value of zero means the client disconnected */
        if (bytes_received == 0)
            break;

        size_t offset = 0;

        /*
         * Process all received data.
         * Search for newline characters to identify complete packets.
         */
        while (offset < (size_t)bytes_received)
        {
            char *newline = memchr(receive_buffer + offset, '\n',
                                   (size_t)bytes_received - offset);

            size_t segment_length;

            if (newline != NULL)
            {
                /* Include the newline in the completed packet */
                segment_length = (size_t)(newline -
                                  (receive_buffer + offset)) + 1;
            }
            else
            {
                /* No newline found: accumulate the remaining data */
                segment_length = (size_t)bytes_received - offset;
            }

            /* Append the received segment to the current packet */
            if (append_to_packet(&packet, &packet_length,
                                 &packet_capacity,
                                 receive_buffer + offset,
                                 segment_length) == -1)
            {
                free(packet);
                return -1;
            }

            offset += segment_length;

            /*
             * A newline marks the end of a packet.
             * Append it to the file and return the complete file to
             * the client.
             */
            if (newline != NULL)
            {
                if (append_packet(packet, packet_length) == -1)
                {
                    free(packet);
                    return -1;
                }

                if (send_file_contents(client_fd) == -1)
                {
                    free(packet);
                    return -1;
                }

                /* Reset the packet length to begin accumulating a new packet */
                packet_length = 0;
            }
        }
    }

    /* Release dynamically allocated packet memory */
    free(packet);

    return 0;
}

/*
 * Main application entry point.
 *
 * Creates the server socket, optionally daemonizes the process,
 * listens for incoming connections, and handles clients until
 * SIGINT or SIGTERM requests termination.
 */
int main(int argc, char *argv[])
{
    int daemon_mode = 0;
    int server_fd;
    int client_fd;
    int option;

    struct sockaddr_in client_addr;
    socklen_t client_addr_length;

    /* Initialize syslog for application logging */
    openlog("aesdsocket", LOG_PID, LOG_USER);

    /* Parse command-line arguments */
    while ((option = getopt(argc, argv, "d")) != -1)
    {
        if (option == 'd')
        {
            daemon_mode = 1;
        }
        else
        {
            syslog(LOG_ERR, "Invalid command line option");
            closelog();
            return -1;
        }
    }

    /* Register signal handlers before entering the server loop */
    if (install_signal_handlers() == -1)
    {
        syslog(LOG_ERR, "Signal handler installation failed: %s",
               strerror(errno));
        closelog();
        return -1;
    }

    /* Create and bind the server socket */
    server_fd = create_server_socket();

    if (server_fd == -1)
    {
        closelog();
        return -1;
    }

    /*
     * Enter daemon mode if requested.
     * The socket has already been successfully bound at this point.
     */
    if (daemon_mode)
    {
        if (daemonize(server_fd) == -1)
        {
            close(server_fd);
            closelog();
            return -1;
        }
    }

    /* Start listening for incoming TCP connections */
    if (listen(server_fd, 10) == -1)
    {
        syslog(LOG_ERR, "listen failed: %s", strerror(errno));
        close(server_fd);
        closelog();
        return -1;
    }

    /*
     * Main server loop.
     * Accept and handle clients until a termination signal is received.
     */
    while (!stop_requested)
    {
        client_addr_length = sizeof(client_addr);

        /* Wait for an incoming client connection */
        client_fd = accept(server_fd,
                           (struct sockaddr *)&client_addr,
                           &client_addr_length);

        if (client_fd == -1)
        {
            /* accept() is interrupted when a termination signal arrives */
            if (errno == EINTR && stop_requested)
                break;

            if (errno == EINTR)
                continue;

            syslog(LOG_ERR, "accept failed: %s", strerror(errno));
            continue;
        }

        /* Convert the client's IP address into a printable string */
        char client_ip[INET_ADDRSTRLEN];

        if (inet_ntop(AF_INET, &client_addr.sin_addr,
                      client_ip, sizeof(client_ip)) == NULL)
        {
            snprintf(client_ip, sizeof(client_ip), "unknown");
        }

        /* Log the accepted connection */
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        /* Process the client's incoming data */
        handle_client(client_fd);

        /* Close the client socket after communication ends */
        close(client_fd);

        /* Log the closed connection */
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
    }

    /*
     * Graceful shutdown:
     * Log termination, close the server socket, and remove the data file.
     */
    syslog(LOG_INFO, "Caught signal, exiting");

    close(server_fd);
    unlink(DATA_FILE);

    closelog();

    return 0;
}

