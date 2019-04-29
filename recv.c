#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include "link_emulator/lib.h"

#define min(a,b) (((a)<(b))?(a):(b))
#define max(a,b) (((a)>(b))?(a):(b))

#define HOST "127.0.0.1"
#define PORT 10001

// avanseaza start si end in functie de dimensiunea bufferului de nr ori
void advance(int *start, int *end, int nr, int dim) {
  int n = nr;
  while (n) {
    if (*start <= *end) {
      *start = *start + 1;
    }
    if (*end < dim - 1) {
      *end = *end + 1;
  }
    n--;
  }
}

// functie ajutatoare ce printeaza fereastra cu ajutorul vectorului de ack-uri
// pentru a vedea fereastra, ce s-a primit si ce nu
void printWindow(int start, int end, int *indexes, int dim) {
  printf("[");
  for (int i = start; i < min(end, dim - 1); ++i) {
    if (indexes[i] != -1) {
      printf("%d, ",indexes[i]);
    } else {
      printf("X, ");
    }
  }
  if (indexes[end] != -1) {
      printf("%d]\n",indexes[end]);
    } else {
      printf("X]\n");
    }
}
// creeaza un ack
// am ales ca mesajul ack sa contina 2 int-uri, primul sa fie 1 sau -1(ACK/NAK)
// iar al doilea numarul cadrului
void sendACK(int nr) {
  msg t;
  memset(&t, 0, sizeof(msg));
  int ack = 1;
  int id = nr;

  memcpy(t.payload, &ack, sizeof(int));
  memcpy(t.payload + sizeof(int), &id, sizeof(int));

  send_message(&t);
}

// creeaza un nak
// am ales ca mesajul ack sa contina 2 int-uri, primul sa fie 1 sau -1(ACK/NAK)
// iar al doilea numarul cadrului
void sendNAK(int nr) {
  msg t;
  memset(&t, 0, sizeof(msg));
  int ack = -1;
  int id = nr;

  memcpy(t.payload, &ack, sizeof(int));
  memcpy(t.payload + sizeof(int), &id, sizeof(int));

  send_message(&t);
}

// face xor pe octetii din payload-ul mesajului si verifica daca rezultatul e egal cu ultimul octet pentru
// a verifica daca este corupt sau nu (intoarce 1 sau -1)
int checkCorrupt(msg t) {
	char c = t.payload[0];
	for (int i = 1; i < 1399; ++i) {
		c ^= t.payload[i];
	}
	if (c == t.payload[1399]) {
		return 1;
	} else {
		return -1;
	}
}

int main(int argc,char** argv){
  msg r,t;
  init(HOST,PORT);
  
  // nume fisier + window + length + delay~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~``
  recv_message(&r);
  sendACK(-1);

  // cream fisierul cu numele primit
  char* name = malloc(10 + r.len * sizeof(char));
  name = strcpy(name, "recv_");
  int fd_received = creat(strcat(name, r.payload), 644);

  int length, WINDOW, DELAY;
  memcpy(&length, r.payload + r.len, sizeof(int));
  memcpy(&WINDOW, r.payload + sizeof(int) + r.len, sizeof(int));
  memcpy(&DELAY, r.payload + 2 * sizeof(int) + r.len, sizeof(int));
  int dim = length / 1380 + 1;

  // alocam memorie pentru bufferul de mesaje
  msg *buff = malloc(dim * sizeof(msg));

  // fereastra glisanta~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  // cream un vectori ce va salva mesajele pe care le-a primit, initial fiind pe -1
  int *indexes = malloc(dim * sizeof(int));
  for (int i = 0; i < dim; ++i) {
    indexes[i] = -1;
  }
  int start = 0;
  int end = min(WINDOW - 1, dim - 1);
  int nrMsg;
  int i = 0;
  int expected = 0;

  // cat timp nu ajungem sa sfarsitul bufferului
  while (start != end + 1) {
    // asteptam mesaj
    recv_message(&r);
    memcpy(&nrMsg, r.payload + r.len, sizeof(int));
    // verificam daca e corupt si daca da, nu facem nimic, ignoram mesajul
    if (checkCorrupt(r) == 1) {
      // daca nu e corupt avem 3 cazuri
	    if (expected == nrMsg) {
        // mesajul era cel asteptat => copiem mesajul in buffer si trimitem ack cu numarul cadrului
	      buff[nrMsg].len = r.len;
	      memcpy(buff[nrMsg].payload, r.payload, r.len);
        // modificam vectorul de mesaje primite
	      indexes[nrMsg] = nrMsg;
        // incercam sa avansam fereastra
	      while (indexes[start] != -1 && start != end + 1) {
	        advance(&start, &end, 1, dim);
	        i++; 
	      }
	      sendACK(nrMsg);
	      expected++;
	    } else if (expected < nrMsg) {
        // nr cadrului e mai mare decat cel asteptat
        // copiem mesajul in buffer
	    	buff[nrMsg].len = r.len;
	    	memcpy(buff[nrMsg].payload, r.payload, r.len);
        // modificam vectorul de mesaje primite
      	indexes[nrMsg] = nrMsg;
        // trimitem nak pentru fiecare mesaj de la cel asteptat pana la cel primit
      	for (int k = expected; k < nrMsg; ++k) {
      		sendNAK(k);
      	}
        // trimitem ack pentru mesajul primit
      	sendACK(nrMsg);
        // actualizam expected, a.i. sa ne asteptam la mesajul cu nr de cadru urmator fata de cel primit
      	expected = nrMsg + 1;
	    } else {
        // nr de cadru e mai mic decat cel asteptat

        // copiem mesajul in buffer
	    	buff[nrMsg].len = r.len;
	      memcpy(buff[nrMsg].payload, r.payload, r.len);
        // trimitem ack
	    	sendACK(nrMsg);
        // modificam vectorul de mesaje primite
	    	indexes[nrMsg] = nrMsg;
        // incercam sa avansam fereastra
	    	while (indexes[start] != -1 && start != end + 1) {
		        advance(&start, &end, 1, dim);
		        i++; 
	      	}
	    }
	}
  }
  // scriere din buffer in fisier~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  i = 0;
  int l = length;
  while (i < dim) {
    int count = 0;
    int msgDim = 1380;

    if (l < 1380) {
      while (count < l - 1) {
        int k = write(fd_received, buff[i].payload + count, l - 1 - count);
        count += k;
      }
    } else {
      
      while (count != msgDim) {
        int k = write(fd_received, buff[i].payload + count, 1380 - count);
        count += k;
      }
    }

    i++;
    l -= count;

  }
  // inchidem fisierul creat
  close(fd_received);
  // trimitem FIN pentru a finaliza conexiunea
  sprintf(t.payload,"FIN");
  t.len = strlen(t.payload + 1);
  send_message(&t);
  return 0;
}
