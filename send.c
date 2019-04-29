#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "link_emulator/lib.h"

#define min(a,b) (((a)<(b))?(a):(b))
#define max(a,b) (((a)>(b))?(a):(b))

#define HOST "127.0.0.1"
#define PORT 10000

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
// verifica daca un mesaj e ack
// am ales ca mesajul ack sa contina 2 int-uri, primul sa fie 1 sau -1(ACK/NAK)
// iar al doilea numarul cadrului
int checkACK(msg t) {
	int ack;
	memcpy(&ack, t.payload, sizeof(int));
	if (ack == 1) {
		return 1;
	} else {
		return -1;
	}
}
// functie ajutatoare ce printeaza fereastra cu ajutorul vectorului de ack-uri
// pentru a vedea fereastra, ce s-a primit si ce nu
void printWindow(int start, int end, int *indexes) {
	printf("[");
	for (int i = start; i < end; ++i) {
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

// face xor pe octetii din payload si pune rezultatul pe ultimul octet din payload pentru verificarea coruperii
void xorBytes(msg* t) {
	char c = t->payload[0];
	for (int i = 1; i < 1399; ++i) {
		c ^= t->payload[i];
	}
	t->payload[1399] = c;
	// memcpy(t->payload + 1399, &c, sizeof(char));
}

int main(int argc,char** argv){
  init(HOST,PORT);
  msg t, r;

  // trimitere nume + WINDOW + lungime + DELAY~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  
  memset(&t, 0, sizeof(msg));
  memcpy(t.payload, argv[1], strlen(argv[1]));
  t.len = strlen(t.payload)+1;

  int SPEED = atoi(argv[2]);
  int DELAY = atoi(argv[3]);
  int WINDOW = SPEED * DELAY * 1000 / (1404 * 8);

  int fd = open(argv[1], O_RDONLY);

  int end = lseek(fd, 0, SEEK_END);

  if (end < 0) {
    printf("error end");
    return -1;
  }

  int start = lseek (fd, 0, SEEK_SET);

  if (start < 0) {
    printf("error start");
    return -1;
  }
  
  int length = end - start + 1;
  int dim = length / 1380 + 1;

  memcpy(t.payload + t.len, &length, sizeof(int));
  memcpy(t.payload + sizeof(int) + t.len, &WINDOW, sizeof(int));
  memcpy(t.payload + 2 * sizeof(int) + t.len, &DELAY, sizeof(int));
  send_message(&t);
  int res = recv_message_timeout(&r, DELAY + 2);
  
  while (res == -1 || checkACK(r) != 1) {
	  send_message(&t);
	  res = recv_message_timeout(&r, DELAY + 2);
  }

  // creare buffer de cadre~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  // alocam memorie pentru bufferul de mesaje
  msg *buff = malloc(dim * sizeof(msg));
  int count = 0;
  int i = 0;
  int l = length;
  
  // setam memoria pe 0
  memset(buff, 0, dim * sizeof(msg));

  // citim din fisier si adaugam in bufferul de mesaje
  while (i < dim) {
    count = 0;
    int msgDim = 1380;

    if (l < 1380) {
      
      while (count < l - 1) {
        int k = read(fd, buff[i].payload + count, l - count);
        count += k;
      }
      buff[i].len = l - 1;
      memcpy(buff[i].payload + count, &i, sizeof(int));
      // adaugam rezultatul xor-ului pe octeti pentru verificarea coruperii
      xorBytes(&buff[i]);
    } else {
      
      while (count != msgDim) {
        int k = read(fd, buff[i].payload + count, 1380 - count);
        count += k;
      }

      buff[i].len = 1380;
      memcpy(buff[i].payload + count, &i, sizeof(int));
      xorBytes(&buff[i]);
    }

    i++;
    l -= count;

  }

  // fereastra glisanta~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // cream un vectori ce va salva mesajele la care a primit ack, initial fiind pe -1
  int *indexes = malloc(dim * sizeof(int));
  for (int i = 0; i < dim; ++i) {
  	indexes[i] = -1;
  }
  start = 0;
  end = min(WINDOW - 1, dim - 1);

  // daca dimensiunea fisierului este mai mare decat marimea ferestrei
  if (dim > WINDOW) {
  	// trimitem window mesaje pentru a incarca reteaua
  	for (int i = 0; i < WINDOW; ++i) {
  		send_message(&buff[i]);
  	}
  	int i = WINDOW;
  	while (i != dim) {
  		// asteptam ack/nak	
  		res = recv_message_timeout(&r, 2 + DELAY);

  		// daca nu am primit timeout
  		if (res != -1) {
  			// daca e ACK
  			if (checkACK(r) == 1) {
  				int nr_cadru;
  				// scoatem numarul cadrului pe care am primit ack
  				memcpy(&nr_cadru, r.payload + sizeof(int), sizeof(int));
  				// adaugam cadrul in vectorul de ack-uri
  				indexes[nr_cadru] = nr_cadru;
  				// incercam sa avansam fereastra, trimitand mesaj cu fiecare mesaj cu care avanseaza
  				while (indexes[start] != -1 && start != end + 1) {
  					advance(&start, &end, 1, dim);
  					if (i < dim) {
	  					send_message(&buff[i]);
	  					i++;
  					}
  				}
  			} else { 
  			// daca e NAK retrimitem mesajul
  				int nr_cadru;
  				memcpy(&nr_cadru, r.payload + sizeof(int), sizeof(int));
  				send_message(&buff[nr_cadru]);
  			}
  		} else {
  			// daca am primit timeout retrimitem toate mesajele din fereastra pentru care nu am primit ack
  			for (int j = start; j <= end ; ++j) {
	  			if (indexes[j] == -1) {
	  				send_message(&buff[j]);
	  			}
  			}
  		}
  	}

  	i = 0;
  	while (start != end + 1) {
  		// asteptam ack/nak
  		res = recv_message_timeout(&r, 2 + DELAY);
  		// daca nu primeste timeout
  		if (res != -1) {
  			// daca e ACK
  			if (checkACK(r) == 1) {
  				// adaugam ack-ul in vectorul de ack-uri
  				int nr_cadru;
  				memcpy(&nr_cadru, r.payload + sizeof(int), sizeof(int));
  				indexes[nr_cadru] = nr_cadru;
  				// incercam sa avansam
  				while (indexes[start] != -1 && start != end + 1) {
  					advance(&start, &end, 1, dim);
  					i++;
  				}
  			} else { 
  				// daca e NAK, retrimitem acel cadru
  				int nr_cadru;
  				memcpy(&nr_cadru, r.payload + sizeof(int), sizeof(int));
  				send_message(&buff[nr_cadru]);
  			}
  		} else {
  			// daca a primit timeout
  			if (start == end + 1) {
  				break;
  			}
  			// daca nu s-a ajuns la sfarsitul bufferului de mesaje
  			// retrimitem toate mesajele din fereastra pe care nu s-a primit ack
  			for (int j = start; j <= end ; ++j) {
	  			if (indexes[j] == -1) {
	  				send_message(&buff[j]);
	  			}
  			}

  		}
  	}

  } else { 
  // fereastra mai mare decat numarul de cadre
  	for (int i = 0; i < dim; ++i) {
  		send_message(&buff[i]);
  	}
  	for (int i = 0; i < dim; ++i) {
  		res = recv_message_timeout(&r, 2 + DELAY);
  		if (res != -1) {
  			if (checkACK(r) == 1) {
  				int nr_cadru;
  				memcpy(&nr_cadru, r.payload + sizeof(int), sizeof(int));
  				indexes[nr_cadru] = nr_cadru;
  				printWindow(start, end, indexes);
  				advance(&start, &end, 1, dim);
  			}
  		} else {
  			//printf("S-a atins timeout\n");
  		}
  	}
  }
  // inchidem fisierul
  close(fd);
  // asteptam FIN pentru a finaliza conexiunea
  res = recv_message_timeout(&t, 2 + DELAY);
  while (strstr(t.payload, "FIN") == NULL || res == -1) {
  	res = recv_message_timeout(&t, 2 * DELAY);
  }
  if (res < 0) {
    perror("[SENDER] Receive error. Exiting.\n");
    return -1;
  }
  return 0;
}
