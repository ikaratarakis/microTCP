/***************************************************************
 *
 * file: iperf.c
 * 
 * @brief   Main function for the needs of CS-435 ass2 2023. Dr. Papadakis Stefanos.
 ****************************************************************/
#define _XOPEN_SOURCE   600
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <getopt.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <ifaddrs.h>
#include <sys/time.h>
#include <time.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <math.h>

#include "udp.h"

#define CHUNK_SIZE 4096
#define NUM_THREADS 10

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wvariadic-macros"
/* Uncomment the following line to enable debugging prints
 * or comment to disable it 
#define DEBUG*/
#ifdef DEBUG
#define DPRINT(...) fprintf(stderr, __VA_ARGS__);
#else /* DEBUG */
#define DPRINT(...)
#endif /* DEBUG */
#pragma GCC diagnostic pop
#define DEFAULT_FILE "output.json" 
#define DEFAULT_INTERVAL 1 

typedef struct{
  char* interface_addr;
  int primary_port;
} args;

args a = { NULL, -1 };

static volatile int stop_traffic = 0;
volatile int countdown = 0;
static int measure_delay = 0;
volatile int num_streams = 0;
double bandwidth_limit = 0;
static size_t packet_size = CHUNK_SIZE;

void
sig_handler(int signal)
{
  if(signal == SIGINT) {
    DPRINT("Stopping traffic generator...");
    stop_traffic = 1;
  }
}

void print_help() {
  printf("Usage: ./client -s/c -p [port] [options]\n");
  printf("Options:\n");
  printf("\t-a [IP address]\tIP address to bind or connect to\n");
  printf("\t-i [interval]\tInterval in seconds to print progress information (default %d)\n", DEFAULT_INTERVAL);
  printf("\t-f [filename]\tOutput file name (default %s)\n", DEFAULT_FILE);
  printf("\t-l [packet size]\tUDP packet size in bytes (client only)\n");
  printf("\t-b [bandwidth]\tBandwidth in bits per second of the data stream (client only)\n");
  printf("\t-n [num streams]\tNumber of parallel data streams (client only)\n");
  printf("\t-t [duration]\tExperiment duration in seconds (client only)\n");
  printf("\t-d\tMeasure one way delay instead of throughput, jitter, and packet loss (client only)\n");
  printf("\t-w [wait duration]\tWait duration in seconds before starting data transmission (client only)\n");
}

static void print_statistics (ssize_t received, struct timespec start, struct timespec end, uint64_t packets_lost, uint64_t packets_received, uint64_t bytes_lost)
{
  double elapsed = end.tv_sec - start.tv_sec + (end.tv_nsec - start.tv_nsec) * 1e-9;
  double megabytes = received / (1024.0 * 1024.0);
  double megabytes_lost = bytes_lost / (1024.0 * 1024.0);

  printf ("\n-------- Assignment 2 (iperf) Measurements --------\n\n");
  printf ("Data received: %f MB\n", megabytes);
  printf ("Transfer time: %f seconds\n\n", elapsed);

  printf ("- Average Throughput achieved is: %f MB/s\n", megabytes / elapsed);
  printf ("- Average Goodput achieved is: %f MB/s\n", (megabytes - megabytes_lost) / elapsed);
  printf ("- Packet Loss Percentage is: %ld%%\n", packets_lost / packets_received);
  printf ("- Average Jitter is: TODO\n");
  printf ("- Standard Deviation of Jitter is: TODO\n");
  printf ("\n---------------------------------------------------\n\n");
}

static void print_owd (struct timespec OWD_start, struct timespec OWD_end, ssize_t received, struct timespec start, struct timespec end)
{
  double elapsed = end.tv_sec - start.tv_sec + (end.tv_nsec - start.tv_nsec) * 1e-9;
  double owd = OWD_end.tv_sec - OWD_start.tv_sec + (OWD_end.tv_nsec - OWD_start.tv_nsec) * 1e-9;
  double megabytes = received / (1024.0 * 1024.0);

  printf ("\n-------- Assignment 2 (iperf) Measurements --------\n\n");
  printf ("Data received: %f MB\n", megabytes);
  printf ("Transfer time: %f seconds\n\n", elapsed);
  printf ("- One Way Delay is: %f seconds\n", owd);
  printf ("\n---------------------------------------------------\n\n");
}

int
client (const char *serverip, uint16_t server_port)
{
  struct sockaddr_in sin;
  uint8_t *buffer;
  ssize_t data_sent;
  microtcp_sock_t s;
  time_t start_time; 
  size_t i;
  time_t elapsed_time;
  int sleep_time_us = 0;
  int packet_size_bits = packet_size * 8;
  srand(time(NULL));

  if (bandwidth_limit > 0) {
    double packet_time = (double) packet_size_bits / (double) bandwidth_limit;
    sleep_time_us = (int) round((packet_time - (double) 0.000005) * 1000000); 
  }

  /* Allocate memory for the application receiver buffer */
  buffer = (uint8_t *) malloc (packet_size);
  if (!buffer) {
    perror ("Allocate application receiver buffer");
    return -EXIT_FAILURE;
  }

  s = microtcp_socket(AF_INET, SOCK_DGRAM, 0);

  /* Clears header and initialize it */
  memset(&sin, 0, sizeof(struct sockaddr_in)); 
  sin.sin_family = AF_INET; 
  sin.sin_port = htons(server_port);
  sin.sin_addr.s_addr = inet_addr(serverip);

  s.address = sin; 
  s.address_len = sizeof(struct sockaddr_in);

  /* Connects... */
  microtcp_connect(&s, (struct sockaddr *)&sin, sizeof(struct sockaddr_in));

  start_time = time(NULL);
  /* Start sending the data */
  printf ("Sending data...\n");
  while (stop_traffic == 0) {
    for (i = 0; i < packet_size; i++) {
      buffer[i] = (uint8_t) (rand() % 256);
    }

    data_sent = microtcp_send (&s, buffer, packet_size * sizeof(uint8_t), (uint32_t)measure_delay);

    /*inter-arrival time*/
    if(sleep_time_us)
      usleep(sleep_time_us);

    /* Case Failed to send */
    if ((long unsigned int)data_sent != packet_size * sizeof(uint8_t)) {
      printf ("Failed to send file data.\n");
      free (buffer);
      return -EXIT_FAILURE;
    }
    /*need to find a more carefull way*/
    elapsed_time = time(NULL) - start_time;
    if(elapsed_time >= countdown && countdown > 0)
      break;

  }
  
  free (buffer);
 
  microtcp_shutdown(&s, SHUT_RDWR);

  return 0;
}

void *client_thread(void *arg) {
  long id;
  id = (long) arg;
  client(a.interface_addr, a.primary_port);
  return NULL;
}

void * handle_connection(void * ps_client);

int
server (uint16_t listen_port)
{
  struct sockaddr_in sin;
  int received = -1;
  microtcp_sock_t sock;
  microtcp_sock_t *sclient;

  /* Create UDP socket */
  sock = microtcp_socket(AF_INET, SOCK_DGRAM, 0);
  memset(&sin, 0, sizeof(struct sockaddr_in));

  sin.sin_family = AF_INET; 
  sin.sin_port = htons(listen_port);
  sin.sin_addr.s_addr = INADDR_ANY; 

  sock.address = sin;
  sock.address_len = sizeof(struct sockaddr_in);

  /* Binds the socket and waits for accept */
  microtcp_bind(&sock, (struct sockaddr *)&sin, sizeof(struct sockaddr_in));

  microtcp_accept(&sock, (struct sockaddr *)&sin, sizeof(struct sockaddr_in));
  sock.address = sin; 
  sock.address_len = sizeof(struct sockaddr_in);

  sclient = malloc(sizeof(microtcp_sock_t));
  *sclient = sock;
  handle_connection(sclient);

  return 0;
 
}

void * handle_connection(void * ps_client){
  microtcp_sock_t s_client = *((microtcp_sock_t*)ps_client);
  int received = -1;
  uint8_t* buffer = (uint8_t*)malloc(packet_size);
  struct timespec start_time;
  struct timespec end_time;
  struct timespec start_owd;
  struct timespec end_owd;
  
  free(ps_client);

   /* Starts receiving data--> starts time, writes to file and resets buffer */
  clock_gettime (CLOCK_MONOTONIC_RAW, &start_time);
  memset(buffer, '\0', packet_size);

  /* Uses resv to store/write the bits received */
  while((received = microtcp_recv(&s_client, (void*)buffer, packet_size, 0)) > 0){
    clock_gettime (CLOCK_MONOTONIC_RAW, &end_owd);
    start_owd = getStart();
    memset(buffer, '\0', packet_size);
  }

  free(buffer);

  clock_gettime (CLOCK_MONOTONIC_RAW, &end_time);
  if(getMeas_Delay() == 0)
    print_statistics (s_client.bytes_received, start_time, end_time, s_client.packets_lost, s_client.packets_received, s_client.bytes_lost);
  else if(getMeas_Delay() == 1)
    print_owd(start_owd, end_owd, s_client.bytes_received, start_time, end_time);

  return 0;
}

/**
 * @brief The main function
 *
 * @param argc Number of arguments
 * @param argv Argument vector
 *
 * @return 0 on success
 *         1 on failure
 */
int main(int argc, char **argv)
{
  /* Initializations */
	FILE *fin = NULL;
  int server_mode = 0;
  int client_mode = 0;
  char* server_addr = NULL;
  int wait_duration = 0;
  char* output_file = DEFAULT_FILE;
  int interval = DEFAULT_INTERVAL;
  int exit_code = 0;
  pthread_t threads[NUM_THREADS];
  int rc;
  long t;

  int opt;
  while ((opt = getopt(argc, argv, "sp:a:i:f:cl:b:n:t:dw:h")) != -1) {
	  DPRINT("\nReadin arg\n");
    switch (opt) 
		{   
      case 'h':
        print_help();
        exit(EXIT_FAILURE);
      case 's':
        DPRINT("option: %c\n", opt); 
        server_mode = 1;
        break;
      case 'a':
        DPRINT("ip: %s\n", optarg); 
        a.interface_addr = strdup(optarg);
        break;
      case 'p':
        DPRINT("port: %s\n", optarg); 
        a.primary_port = atoi(optarg);
        break;
      case 'i':
        DPRINT("interval: %s\n", optarg); 
        interval = atoi(optarg);
        break;
      case 'f':
        output_file = optarg;
        if ((fin = fopen(output_file, "w")) == NULL){
          DPRINT("\n Could not open file: %s\n", output_file);
          return EXIT_FAILURE;
        }
        DPRINT("filename: %s\n", output_file); 
        break;
      case 'c':
        DPRINT("option: %c\n", opt); 
        client_mode = 1;
        break;   
      case 'l':
        DPRINT("packet_size: %s\n", optarg); 
        packet_size = atoi(optarg);
        break; 
      case 'b':
        DPRINT("bandwidth: %s\n", optarg); 
        bandwidth_limit = atoi(optarg);
        break;
      case 'n':
        DPRINT("num_streams: %s\n", optarg); 
        num_streams = atoi(optarg);
        if (num_streams > 10){
          DPRINT("\n Max number of streams is 10 \n");
          return EXIT_FAILURE;
        }
        break;
      case 't':
        DPRINT("duration: %s\n", optarg);  
        countdown = atoi(optarg);
        break;
      case 'd':
        DPRINT("option: %c\n", opt); 
        measure_delay = 1;
        break;
      case 'w':
        DPRINT("wait_duration: %s\n", optarg);  
        wait_duration = atoi(optarg);
        break;
      case '?':
        if (optopt == 'a' || optopt == 'p' || optopt == 'i' || optopt == 'f'
            || optopt == 'l' || optopt == 'b' || optopt == 'n' || optopt == 't' || optopt == 'w'){
          printf("Option -%c requires an argument. Help -h\n", optopt);
        }
        else if (isprint(optopt)){
          printf("Unknown option `-%c'. Help -h \n", optopt);
        }
        else{
          printf("Unknown option character `\\x%x'.\n", optopt);
        }
        exit(EXIT_FAILURE); 
      default:
        print_help();
        exit(EXIT_FAILURE); 
      }
	}
  /*
   * Register a signal handler so we can terminate the generator with
   * Ctrl+C
   */
  signal(SIGINT, sig_handler);

  /*Sleeping for w*/
  sleep(wait_duration);

  if (server_mode) 
    exit_code = server(a.primary_port);
  else {
    for(t=0; t<num_streams; t++){
      DPRINT("In main: creating thread %ld\n", t);
      rc = pthread_create(&threads[t], NULL, client_thread, (void *)t);
      if(rc){
        DPRINT("ERROR; return code from pthread_create() is %d\n", rc);
        exit(EXIT_FAILURE);
      }
    }
    if(num_streams == 0){
      exit_code = client(a.interface_addr, a.primary_port);
    }
  }

  /* Wait for all threads to complete */
  for(t = 0; t < num_streams; t++) {
    pthread_join(threads[t], NULL);
  }

  if(fin != NULL)
	  fclose(fin);

  if(a.interface_addr != NULL)
    free(a.interface_addr);

	return (exit_code);
}
