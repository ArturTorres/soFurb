//Artur Lian Fernandes Torres e Kevin Cavalheiro

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <dirent.h>
#include <time.h>
#include <stdatomic.h>
#include <limits.h>
#include <sys/mman.h>

#define NUM_WRITERS 6
#define NUM_READERS 4
#define NUM_RANDOM_IO 4
#define FILE_SIZE_MB 500
#define WORK_DIR "/tmp/vm_disk_stress"
#define BLOCK_SIZE (4 * 1024)

volatile sig_atomic_t stop = 0;

// Estrutura para contadores compartilhados
typedef struct {
    atomic_ullong total_bytes_written;
    atomic_ullong total_bytes_read;
    atomic_ullong total_files_created;
    atomic_ullong total_files_deleted;
} shared_counters_t;

shared_counters_t *shared_counters;

void handler(int sig) {
    stop = 1;
}

void init_counters() {
    atomic_init(&shared_counters->total_bytes_written, 0);
    atomic_init(&shared_counters->total_bytes_read, 0);
    atomic_init(&shared_counters->total_files_created, 0);
    atomic_init(&shared_counters->total_files_deleted, 0);
}

char* create_temp_file(const char* prefix, int id) {
    char* filename = malloc(PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/%s_%d_%ld.dat", WORK_DIR, prefix, id, time(NULL));
    return filename;
}

void writer_worker(int id) {
    char* buffer = malloc(BLOCK_SIZE);
    memset(buffer, id, BLOCK_SIZE);

    while (!stop) {
        char* filename = create_temp_file("writer", id);
        int flags = O_WRONLY | O_CREAT | O_TRUNC | O_SYNC;
        
        int fd = open(filename, flags, 0644);
        if (fd == -1) {
            free(filename);
            continue;
        }

        size_t total_written = 0;
        while (total_written < FILE_SIZE_MB * 1024 * 1024 && !stop) {
            ssize_t written = write(fd, buffer, BLOCK_SIZE);
            if (written <= 0) break;
            total_written += written;
            atomic_fetch_add(&shared_counters->total_bytes_written, written);
        }

        close(fd);
        free(filename);
        atomic_fetch_add(&shared_counters->total_files_created, 1);
        
        if (!stop) {
            usleep(10000);
        }
    }
    free(buffer);
}

void reader_worker() {
    char* buffer = malloc(BLOCK_SIZE);
    DIR* dir;
    struct dirent* ent;

    while (!stop) {
        if ((dir = opendir(WORK_DIR)) != NULL) {
            while ((ent = readdir(dir)) != NULL && !stop) {
                if (strstr(ent->d_name, "writer")) {
                    char path[PATH_MAX];
                    snprintf(path, sizeof(path), "%s/%s", WORK_DIR, ent->d_name);
                    
                    int fd = open(path, O_RDONLY);
                    if (fd != -1) {
                        ssize_t bytes_read;
                        while ((bytes_read = read(fd, buffer, BLOCK_SIZE)) > 0 && !stop) {
                            atomic_fetch_add(&shared_counters->total_bytes_read, bytes_read);
                        }
                        close(fd);
                    }
                }
            }
            closedir(dir);
        }
        
        if (!stop) {
            usleep(50000);
        }
    }
    free(buffer);
}

void random_io_worker(int id) {
    char buffer[512];
    memset(buffer, id, sizeof(buffer));

    while (!stop) {
        char* filename = create_temp_file("random", id);
        
        // Escrita
        int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, 0644);
        if (fd != -1) {
            for (int i = 0; i < 50 && !stop; i++) {
                ssize_t written = write(fd, buffer, sizeof(buffer));
                if (written > 0) {
                    atomic_fetch_add(&shared_counters->total_bytes_written, written);
                }
            }
            close(fd);
            atomic_fetch_add(&shared_counters->total_files_created, 1);
        }
        
        // Leitura
        fd = open(filename, O_RDONLY);
        if (fd != -1) {
            ssize_t bytes_read;
            while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0 && !stop) {
                atomic_fetch_add(&shared_counters->total_bytes_read, bytes_read);
            }
            close(fd);
        }
        
        // Remoção
        if (!stop) {
            unlink(filename);
            atomic_fetch_add(&shared_counters->total_files_deleted, 1);
            free(filename);
            usleep(1000 + (rand() % 1000));
        } else {
            free(filename);
        }
    }
}

void cleanup_workspace() {
    DIR* dir;
    struct dirent* ent;
    if ((dir = opendir(WORK_DIR)) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
            char path[PATH_MAX];
            snprintf(path, sizeof(path), "%s/%s", WORK_DIR, ent->d_name);
            unlink(path);
        }
        closedir(dir);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <duracao_em_segundos>\n", argv[0]);
        return 1;
    }

    int duration_sec = atoi(argv[1]);
    if (duration_sec <= 0) {
        fprintf(stderr, "A duração deve ser um número inteiro positivo.\n");
        return 1;
    }

    printf("Iniciando o Estressador de Disco por %d segundos\n", duration_sec);
    
    // Aloca memória compartilhada para os contadores
    shared_counters = mmap(NULL, sizeof(shared_counters_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shared_counters == MAP_FAILED) {
        perror("mmap failed");
        return 1;
    }

    init_counters();
    signal(SIGINT, handler);
    signal(SIGTERM, handler);
    
    mkdir(WORK_DIR, 0777);
    cleanup_workspace();
    
    time_t start_time = time(NULL);
    time_t end_time = start_time + duration_sec;
    
    // Cria workers
    pid_t children[NUM_WRITERS + NUM_READERS + NUM_RANDOM_IO];
    int child_count = 0;
    
    for (int i = 0; i < NUM_WRITERS; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            writer_worker(i);
            exit(0);
        } else {
            children[child_count++] = pid;
        }
    }
    
    for (int i = 0; i < NUM_READERS; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            reader_worker();
            exit(0);
        } else {
            children[child_count++] = pid;
        }
    }
    
    for (int i = 0; i < NUM_RANDOM_IO; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            random_io_worker(i);
            exit(0);
        } else {
            children[child_count++] = pid;
        }
    }
    
    // Loop principal
    while (!stop && time(NULL) < end_time) {
        sleep(1);
    }
    
    // Finalização controlada
    stop = 1;
    
    // Dá tempo para os processos filhos finalizarem
    sleep(2);
    
    // Força término se necessário
    for (int i = 0; i < child_count; i++) {
        kill(children[i], SIGTERM);
    }
    
    // Espera todos os filhos
    int status;
    while (wait(&status) > 0);
    
    // Limpeza final
    cleanup_workspace();
    rmdir(WORK_DIR);
    
    // Estatísticas
    double elapsed = difftime(time(NULL), start_time);
    double finalTime = elapsed - 2;
    double throughput_write = (double)atomic_load(&shared_counters->total_bytes_written) / (1024 * 1024) / elapsed;
    double throughput_read = (double)atomic_load(&shared_counters->total_bytes_read) / (1024 * 1024) / elapsed;
    
    printf("\n------ Estatísticas Finais ------\n");
    printf("Tempo decorrido: %.2f segundos\n", finalTime);
    printf("Dados escritos: %.2f MB\n", (double)atomic_load(&shared_counters->total_bytes_written) / (1024 * 1024));
    printf("Dados lidos: %.2f MB\n", (double)atomic_load(&shared_counters->total_bytes_read) / (1024 * 1024));
    printf("Taxa de escrita: %.2f MB/s\n", throughput_write);
    printf("Taxa de leitura: %.2f MB/s\n", throughput_read);
    printf("Arquivos criados: %llu\n", (unsigned long long)atomic_load(&shared_counters->total_files_created));
    printf("Arquivos deletados: %llu\n", (unsigned long long)atomic_load(&shared_counters->total_files_deleted));
    printf("--------------------------------\n");

    // Desmapear memória compartilhada
    if (munmap(shared_counters, sizeof(shared_counters_t)) == -1) {
        perror("munmap failed");
    }
    
    return 0;
}