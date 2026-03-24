#include "udp.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include "crc32.h"
#define PRINTS 1
#define _BSD_SOURCE

microtcp_sock_t
microtcp_socket (int domain, int type, int protocol)
{
  microtcp_sock_t sock;
  int microtcp_sock;
  srand(time(NULL));

  /* Initialize socket description */
  microtcp_sock = socket(domain, SOCK_DGRAM, protocol);
  if (microtcp_sock == -1)
	{
		perror("Could not create socket\n");
    exit(EXIT_FAILURE);
	}

  /* Passes to socket */
  sock.sd = microtcp_sock;
  sock.state = UNKNOWN;// isos provlimatiko
  sock.packets_lost = 0;
  sock.packets_received = 0;
  sock.packets_send = 0;
  sock.bytes_lost = 0;
  sock.bytes_received = 0;
  sock.bytes_send = 0;

  /* Set timeout */
  struct timeval timeout;
  timeout.tv_sec = 0;
  timeout.tv_usec = MICROTCP_ACK_TIMEOUT_US;
  setsockopt(sock.sd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(struct timeval));


  return sock;
}

int
microtcp_bind (microtcp_sock_t *socket, const struct sockaddr *address,
               socklen_t address_len)
{
  int rc;
  int s;

  rc = bind(socket->sd, address, address_len);

  if(rc == -1){
    perror("Couldn't Bind, something went wrong\n");
    socket->state = INVALID;
    exit(EXIT_FAILURE);
  }

  socket->state = LISTEN;
  return 0;
}

int
microtcp_connect (microtcp_sock_t *socket, const struct sockaddr *address,
                  socklen_t address_len)
{
  microtcp_header_t client, server; 

  /* Initialize client header sizes */ 
  client.window = htons(MICROTCP_WIN_SIZE);
  client.data_len = htonl(32);

  /* Means a packet is received(when received =-1)*/
  int received = -1;

  /* makes sure the headers are cleared */
  memset(&client, 0, sizeof(microtcp_header_t));
  memset(&server, 0, sizeof(microtcp_header_t));

  /* Initialize client socket, with random seq number like in real life */
  socket->type = CLIENT;
  socket->cwnd = MICROTCP_INIT_CWND;
  socket->ssthresh = MICROTCP_INIT_SSTHRESH;
  socket->seq_number = rand();
  socket->ack_number = 0;

  /* Initialize client SYN(header) */
  client.seq_number = htonl(socket->seq_number); 
  client.ack_number = htonl(socket->ack_number);
  client.control = htons(SYN);
  sendto(socket->sd,(const void *)&client,sizeof(microtcp_header_t),0,address,address_len);  /* Adds info to client socket */
  socket->packets_send++;
  socket->bytes_send += sizeof(microtcp_header_t);

  /* Waits for server repsonse(SYNACK) */
  while(received < 0){
    received = recvfrom(socket->sd,(void *)&server,sizeof(microtcp_header_t),  0,(struct sockaddr *)address, &address_len);
  }

  /* Checks if server received the "ghost byte" */
  if(ntohl(server.ack_number) == ntohl(client.seq_number) + 1){
    /* If response is type SYNACK */
    if(ntohs(server.control) == SYNACK){
      /* Update sucket and client header info */
      socket->seq_number = ntohl(server.ack_number); //N + 1
      socket->ack_number = ntohl(server.seq_number) + 1;//M + 1
      socket->init_win_size = ntohs(server.window);
      socket->curr_win_size = ntohs(server.window);

      client.ack_number = htonl(socket->ack_number);
      client.seq_number = htonl(socket->seq_number);
    }else{
      perror("3-Way handshake failed, connection couldn't be established\n");
      socket->state = INVALID;
      return -1;
    }
  }else{
    perror("3-Way handshake failed, connection couldn't be established\n");
    socket->state = INVALID;
    return -1;
  }

  /* Prepares ACK response */
  client.control = htons(ACK);
  sendto(socket->sd,(const void *)&client,sizeof(microtcp_header_t), 0,address,address_len);  
  socket->packets_send++;
  socket->bytes_send += sizeof(microtcp_header_t);

  /* Update sucket state */
  socket->state = ESTABLISHED;
  if(PRINTS) printf("--CLIENT-- INIT_WIN = %ld CURR_WIN = %ld\n", socket->init_win_size, socket->curr_win_size);
}

int
microtcp_accept (microtcp_sock_t *socket, struct sockaddr *address,
                 socklen_t address_len)
{
  /* Checks if bind was succcesful */
  if ((intptr_t)microtcp_bind == -1){
    if(PRINTS) printf("3-Way handshake failed. Port number hasn't been assgned, connection couldn't be established\n");
    return -1 ; 
  }

  microtcp_header_t client, server; // Headers
  client.window = htons(MICROTCP_WIN_SIZE);
  client.data_len = htonl(32);
  int received = -1;
  /* Initialize server socket */
  socket->type = SERVER;
  socket->init_win_size = MICROTCP_WIN_SIZE;
  socket->curr_win_size = MICROTCP_WIN_SIZE;
  socket->recvbuf = malloc(MICROTCP_RECVBUF_LEN);
  socket->buf_fill_level = 0;

  /*Makes sure the headers are cleared*/
  memset(&client, 0, sizeof(microtcp_header_t));
  memset(&server, 0, sizeof(microtcp_header_t));
  
  /* Waits for server SYN to start the 3-way Handshake */
  while(received < 0){
    received = recvfrom(socket->sd,(void *)&client,sizeof(microtcp_header_t),0,address,&address_len);
  }

  /*Checks if the response is SYN*/
  if(ntohs(client.control) != SYN){
    perror("3-Way handshake failed, connection couldn't be established\n");
    socket->state = INVALID;
    return -1;
  }

  /* Initisialize server window to know how many data it can send */
  if(PRINTS) printf("%ld\n", socket->curr_win_size);
  server.window = htons(socket->curr_win_size);

  /* Update sucket and client header info */
  socket->ack_number = ntohl(client.seq_number) + 1;//N + 1
  socket->seq_number = rand();//random M 
  server.seq_number = htonl(socket->seq_number);
  server.ack_number = htonl(socket->ack_number);

  /* Prepares SYNACK response */
  server.control = htons(SYNACK);
  sendto(socket->sd,(const void *)&server,sizeof(microtcp_header_t),0,address,address_len);
  
  /* Waits for client response(ACK) */
  received = -1;
  while(received < 0){
    received = recvfrom(socket->sd,(void *)&client,sizeof(microtcp_header_t),0,address,&address_len);
  }
  
  /* Checks if packet is ACK with the correct seq_number */
  if(ntohl(client.ack_number) == ntohl(server.seq_number) + 1){
    if(ntohs(client.control) == ACK){
      if(PRINTS) printf("3-Way handshake completed\n");
      socket->seq_number = ntohl(client.ack_number);//M+1
      socket->state = ESTABLISHED;
      //return 0;
    }else{
      socket->state = INVALID;
      if(PRINTS) printf("3-Way handshake failed, connection couldn't be established\n");
      //return -1;
    }
  }else{
    socket->state = INVALID;
    if(PRINTS) printf("3-Way handshake failed, connection couldn't be established\n");
    //return -1;
  }
}

/* The socket passed is client's , also the client initiates the shutdown and we suppose the SERVER packets are sended by an outside source 
   we only check them if they are correct */
int
microtcp_shutdown (microtcp_sock_t *socket, int how)
{
  microtcp_header_t client, server; 
  client.window = htons(MICROTCP_WIN_SIZE);
  client.data_len = htonl(32);
  struct sockaddr_in address = socket->address; 
  socklen_t address_len = socket->address_len;
  int received = -1;

  /*Makes sure the headers are cleared*/
  memset(&client, 0, sizeof(microtcp_header_t));
  memset(&server, 0, sizeof(microtcp_header_t));
  
  /* Client prepares and sends FINACK  */
  client.seq_number = htonl(rand()); // X
  //client.ack_number = htonl(0); // no
  client.control = htons(FINACK);
  sendto(socket->sd,(const void *)&client,sizeof(microtcp_header_t),0,(struct sockaddr *)&address,address_len);
  /* Client waits for server response(ACK) */
  while(received < 0){
    received = recvfrom(socket->sd,(void *)&server,sizeof(microtcp_header_t),  0,(struct sockaddr *)&address, &address_len);
  }

  received = -1; 

  /* Client checks if response is ACK and has the correct ack_number = X+1 */
  if(ntohs(server.control) == ACK){
    if(socket->type == CLIENT && ntohl(server.ack_number) == ntohl(client.seq_number) + 1){
      socket->state = CLOSING_BY_HOST;
    }
  }else{
    perror("Shutdown proccess failed");
    return 1;
  }

  if(socket->type == SERVER)
    return 0;
 
  /* Client waits for server response(FINACK) */
  while(received < 0){
    received = recvfrom(socket->sd,(void *)&server,sizeof(microtcp_header_t),  0,(struct sockaddr *)&address, &address_len);
  }

  /* Client checks if response is FINACK */
  if(ntohs(server.control) == FINACK){
    if(PRINTS) printf("I requested a connection shutdown\n");
  }else{
    perror("Shutdown proccess failed again...");
    return 1;
  }

  /* Client prepares and sends ACK  */
  client.seq_number = server.ack_number; // X+1
  client.ack_number = htonl(ntohl(server.seq_number) + 1); // ACK Y+1
  client.control = htons(ACK);
  sendto(socket->sd,(const void *)&client,sizeof(microtcp_header_t),0,(struct sockaddr *)&address,address_len);  
  if(PRINTS) printf("Connection shutdown\n");
  socket->state = CLOSED;
}

/* Return maximum packet length */
static inline
int
getMaxPacketSize(int remaining, int cwnd, int win)
{
  if(remaining < cwnd && remaining < win)
    return remaining;
  else if(cwnd < win)
    return cwnd;
  else
    return win;
}

ssize_t
microtcp_send (microtcp_sock_t *socket, const void *buffer, size_t length,
               uint32_t md)
{
  int i, rec = 0, remaining = length, data_sent = 0, to_send, chunks = 0, bytes_lost, last_ack = 0, dup_acks = 0;
  void* buff = malloc(sizeof(microtcp_header_t) + MICROTCP_MSS), *recbuff = malloc(sizeof(microtcp_header_t) + MICROTCP_MSS);
  struct sockaddr_in address = socket->address; 
  socklen_t address_len = socket->address_len;
  microtcp_header_t client, server;
  struct timespec st_owd_fun;
  if(PRINTS) printf("data sent: %d, length: %ld\n", data_sent, length);

  /* Cleaning headers */
  memset(&client, 0, sizeof(microtcp_header_t));
  memset(&server, 0, sizeof(microtcp_header_t));

  clock_gettime (CLOCK_MONOTONIC_RAW, &st_owd_fun);

  /* While we have data to sent we separate it into chunks */
  while(data_sent < length){
    /* Finds chunk size */
    to_send = getMaxPacketSize(remaining, socket->cwnd, MICROTCP_WIN_SIZE);
    
    if(PRINTS) printf("Received %d | REMAINING = %d CWND = %ld SSTHRESH = %ld WINDOWSIZE = %ld\n", to_send, remaining, socket->cwnd, socket->ssthresh, socket->curr_win_size);
    
    chunks = (to_send / MICROTCP_MSS);

    /* Puts the correct data in each chunk */
    for(i = 0; i < chunks; i++){
      memset(buff, 0, sizeof(microtcp_header_t) + MICROTCP_MSS);

      if(PRINTS) printf("Sending chunk of 1460 number %d\n", i);

      /* Get data length and seq num */
      ((microtcp_header_t*)buff)->data_len = htonl(MICROTCP_MSS);
      ((microtcp_header_t*)buff)->seq_number = htonl(socket->seq_number + i * MICROTCP_MSS);
      ((microtcp_header_t*)buff)->timestamp = st_owd_fun;
      ((microtcp_header_t*)buff)->meas_delay = md;
      
      /* Put current buffer part in packet */ 
      memcpy(buff + sizeof(microtcp_header_t), (buffer + data_sent), MICROTCP_MSS);

      /* Checksum */
      ((microtcp_header_t*)buff)->checksum = htonl(crc32(buff, sizeof(microtcp_header_t) + MICROTCP_MSS));
      if(PRINTS) printf("Checksum %u\n", ntohl(((microtcp_header_t*)buff)->checksum));

      sendto(socket->sd,buff,sizeof(microtcp_header_t) + MICROTCP_MSS,0,(struct sockaddr *)&address,address_len);
      data_sent += MICROTCP_MSS;
      remaining = length - data_sent;
    }
    /* Check if there is a semi -filled chunk */
    /* Checks if there is a remaining chunk smaller than MSS and repeates the above */
    if(to_send % MICROTCP_MSS != 0){
      chunks++;
      memset(buff, 0, sizeof(microtcp_header_t) + MICROTCP_MSS);
      if(PRINTS) printf("Sending remaining %d bytes\n", to_send % MICROTCP_MSS);

      ((microtcp_header_t*)buff)->data_len = htonl(to_send % MICROTCP_MSS);
      ((microtcp_header_t*)buff)->seq_number = htonl(socket->seq_number + i * MICROTCP_MSS);
      ((microtcp_header_t*)buff)->timestamp = st_owd_fun;
      ((microtcp_header_t*)buff)->meas_delay = md;

      memcpy(buff + sizeof(microtcp_header_t), (buffer + data_sent), MICROTCP_MSS);
      ((microtcp_header_t*)buff)->checksum = htonl(crc32(buff, sizeof(microtcp_header_t) + to_send % MICROTCP_MSS));
      if(PRINTS) printf("Checksum %u\n", ntohl(((microtcp_header_t*)buff)->checksum));
      
      sendto(socket->sd,buff,sizeof(microtcp_header_t) + ntohl(((microtcp_header_t*)buff)->data_len),0,(struct sockaddr *)&address,address_len);
      data_sent += to_send % MICROTCP_MSS;
    }

    /* receiving ACKS */ 
    for(int i = 0; i < chunks; i++){
      rec = recvfrom(socket->sd, &server, sizeof(microtcp_header_t), 0, (struct sockaddr *)&(socket->address), (socklen_t *)&(socket->address_len));
      if(rec < 0){
        /* Timeout case */
        socket->ssthresh = (socket->cwnd) / 2;
        socket->cwnd = MICROTCP_MSS;
        bytes_lost = socket->seq_number - ntohl(server.ack_number);
        if(PRINTS) printf("RETRANSMITTING %d LOST BYTES\n", bytes_lost);
        socket->seq_number = ntohl(server.ack_number);
        socket->curr_win_size = ntohl(server.window);
        data_sent -= bytes_lost;
      }

      /* Checks if response is ACK */
      if(ntohs(server.control) != ACK){
        continue;
      }

      /* Νοrmal case */
      if(ntohl(server.ack_number) > socket->seq_number){ 
        socket->seq_number = ntohl(server.ack_number);
        socket->curr_win_size = ntohl(server.window);
        last_ack = ntohl(server.ack_number);
        dup_acks = 0;
      }

      /* Triple ACK case */
      else if(dup_acks >= 3){
        socket->ssthresh = (socket->cwnd)/2;
        socket->cwnd = socket->ssthresh + MICROTCP_INIT_CWND;  
        bytes_lost = socket->seq_number - ntohl(server.ack_number);
        if(PRINTS) printf("RETRANSMITTING %d LOST BYTES\n", bytes_lost);
        socket->seq_number = ntohl(server.ack_number);
        socket->curr_win_size = ntohl(server.window);
        data_sent -= bytes_lost;
      }

      /* Duplicate ACK case */
      else if(ntohl(server.ack_number) <= socket->seq_number + MICROTCP_MSS && last_ack == ntohl(server.ack_number)){
        dup_acks++;
      }

      /* Congestion Control Slow Start */
      if(socket->cwnd <= socket->ssthresh){
        /* exponential increase */
        socket->cwnd += MICROTCP_MSS;
      }

      /* Congestion Control Congestion Avoidance */
      else if(socket->cwnd > socket->ssthresh){
        /* linear increase */
        socket->cwnd += 1;
      }

      /* Flow Control ,sends empty packets until the flow is resotred */
      if(socket->curr_win_size == 0){//if buffer is overflowed then win_size = 0
        sendto(socket->sd,buff,sizeof(microtcp_header_t),0,(struct sockaddr *)&address,address_len);        
        usleep(rand() % MICROTCP_ACK_TIMEOUT_US);
      }
    }
  }

  /* Free/delete buffers used */
  free(buff);
  free(recbuff);

  return data_sent;
}

void setStart(struct timespec strt){
  st_owd = strt;
}

struct timespec getStart(){
  return st_owd;
}

void setMeas_Delay(uint32_t md){
  m_delay = md;
}

uint32_t getMeas_Delay(){
  return m_delay;
}

ssize_t
microtcp_recv (microtcp_sock_t *socket, void *buffer, size_t length, int flags)
{
  microtcp_header_t client, server; // Headers
  client.window = htons(MICROTCP_WIN_SIZE);
  struct sockaddr_in address = socket->address; 
  socklen_t address_len = socket->address_len;
  int received = -1, remaining_bytes = length, received_total = 0, checksum, buffer_index = 0;
  uint8_t *recvbuf;
  uint8_t *buf;
  struct timespec prev_time, curr_time;
  double inter_arrival_time, inter_arrival_sum = 0;
  clock_gettime (CLOCK_MONOTONIC_RAW, &prev_time);
  clock_gettime (CLOCK_MONOTONIC_RAW, &curr_time);


  /* If connection is closed, exit with -1 */
  if(socket->state == CLOSED){ 
    return -1;
  }
  else{
    recvbuf = socket->recvbuf;
    buf = (uint8_t*)malloc(sizeof(microtcp_header_t) + MICROTCP_MSS);
  }

  if(PRINTS) printf("ACK %ld\n", socket->ack_number);
  memset(&client, 0, sizeof(microtcp_header_t));
  memset(&server, 0, sizeof(microtcp_header_t));

/*paranoma dioti o buf einai uniti
  setStart(((microtcp_header_t*)buf)->timestamp);
  setMeas_Delay(((microtcp_header_t*)buf)->meas_delay);
*/
  /* If a packet is recieved */
  while(remaining_bytes > 0){
    prev_time = curr_time;
    memset(buf, 0, sizeof(microtcp_header_t) + MICROTCP_MSS);
    received = recvfrom(socket->sd,
      buf,
      sizeof(microtcp_header_t) + MICROTCP_MSS,  
      0,
      (struct sockaddr *)&address, 
      &address_len
    );    

    clock_gettime (CLOCK_MONOTONIC_RAW, &curr_time);
    inter_arrival_time = curr_time.tv_sec - prev_time.tv_sec + (curr_time.tv_nsec - prev_time.tv_nsec) * 1e-9;
    inter_arrival_sum += inter_arrival_time;

    if(PRINTS) printf("\nInter-Arrival time = %f seconds\n", inter_arrival_time);

    if(received < 0){//simple check
      continue;
    }

    memset(&server, 0, sizeof(microtcp_header_t));

    if(PRINTS) printf("Expected: %ld Received: %d\n", socket->ack_number, ntohl(((microtcp_header_t*)buf)->seq_number));

    /* Check FINACK for shutdown order */
    if(ntohs(((microtcp_header_t*)buf)->control) == FINACK){
      if(PRINTS) printf("Was finack %d %d\n", ntohs(((microtcp_header_t*)buf)->control), FINACK);

      /* Sends an ACK to the FIN ... */
      server.ack_number = htonl(ntohl(((microtcp_header_t*)buf)->seq_number) + 1);
      server.control = htons(ACK);
      sendto(socket->sd,(const void *)&server,sizeof(microtcp_header_t),0,(struct sockaddr *)&address,address_len);      
      socket->state = CLOSING_BY_PEER;

      /* Shutdown by server... */
      if(microtcp_shutdown(socket,0) == 0){
        socket->state = CLOSED;
        if(PRINTS) printf("Server closed connection\n");

        /* Purge buffer */
        memcpy(buffer, recvbuf, socket->buf_fill_level);
        socket->buf_fill_level = 0;
        socket->curr_win_size = socket->init_win_size;

        free(buf);
        free(recvbuf);
        return received_total;//old receieved_total
      }
    }

    if(PRINTS) printf("Received : %d\n", received);
    checksum = ntohl(((microtcp_header_t*)buf)->checksum);
    ((microtcp_header_t*)buf)->checksum = 0;

    /* Checksum check */
    if(checksum != crc32(buf, received) || ntohl(((microtcp_header_t*)buf)->seq_number) != socket->ack_number){
      /* Sends dup ACK */
      memset(&server, 0, sizeof(microtcp_header_t));
      server.ack_number = htonl(socket->ack_number);

      sendto(socket->sd, &server, sizeof(microtcp_header_t), 0, (struct sockaddr *)&address, address_len);
      socket->packets_send++;
      socket->bytes_send += sizeof(microtcp_header_t);

      socket->packets_lost++;
      socket->bytes_lost += received;
      continue;
    }

    /* Packet correction */
    socket->ack_number = socket->ack_number + received - sizeof(microtcp_header_t);
    socket->bytes_received += received;
    socket->packets_received++;

    /* Decrease win size */
    socket->curr_win_size = socket->curr_win_size - received - sizeof(microtcp_header_t);
    if((int)(socket->curr_win_size) - (int)ntohl(((microtcp_header_t*)buf)->data_len) < 0){
      socket->curr_win_size = 0;
    }
    received_total += received - sizeof(microtcp_header_t);
    remaining_bytes -= received - sizeof(microtcp_header_t);

    /* Case were the buffer receiver is full if yes it's cleared */
    if(socket->buf_fill_level > 0.85 * MICROTCP_RECVBUF_LEN){
      memcpy(buffer + buffer_index, recvbuf, socket->buf_fill_level);
      buffer_index += socket->buf_fill_level;
      socket->buf_fill_level = 0;
      socket->curr_win_size = socket->init_win_size;
    }

    /* Update socket info */
    memcpy(recvbuf + buffer_index + socket->buf_fill_level, buf + sizeof(microtcp_header_t), received - sizeof(microtcp_header_t));
    socket->buf_fill_level += received - sizeof(microtcp_header_t);

    /* Prepare header and send response */
    server.control = htons(ACK);
    server.ack_number = htonl(socket->ack_number);
    server.window = htons(socket->curr_win_size);
    sendto(socket->sd, &server, sizeof(microtcp_header_t), 0, (struct sockaddr *)&address, address_len);
    socket->packets_send++;
    socket->bytes_send += sizeof(microtcp_header_t);
    if(PRINTS) printf("%d\n", buffer_index);
  }

  /* Clean receiver buffer */
  buffer_index += socket->buf_fill_level;
  socket->buf_fill_level = 0;
  socket->curr_win_size = socket->init_win_size;

  free(buf);
  return received_total;
}
