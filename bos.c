#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <memory.h>

const int INIT_SIZE = 64 * 1024;

#pragma pack(1)
struct bos_hdr {
    int magic; // must be set to 0xb055;
    union {
        char padding[16];
    } u;
};

struct bos_entry {
    int allocated:1; // will be non-zero if allocated
    int size; // includes the size of bos_entry
    union {
        char padding[8];
    } u;
};

#pragma pack()

int main(int argc, char **argv) {
    int fd;
    void *base;
    if (argc == 2) {
        fd = open(argv[1], O_CREAT | O_RDWR, 0777);
        if (fd == -1) {
            perror(argv[1]);
            exit(2);
        }
        base = mmap(NULL, 1024*1024, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
    }
    else if (argc == 3){ // Implementing the -t flag
        fd = open(argv[2], O_CREAT | O_RDWR, 0777);
        if (fd == -1) {
            perror(argv[2]);
            exit(2);
        }
        if (strcmp(argv[1], "-t") == 0)
            base = mmap(NULL, 1024*1024, PROT_WRITE | PROT_READ, MAP_PRIVATE, fd, 0);
        else {
            printf("Only -t flag has been implemented\n");
            exit(3);
        }
    }
    else {
        printf("USAGE: %s bos_filename OR %s -t bos_filename\n", argv[0], argv[0]);
        exit(4);
    }
    void *end;
    struct bos_hdr *hdr = base;
    struct bos_entry *first_entry = (struct bos_entry *)(hdr+1);

    struct stat stat;
    if (fstat(fd, &stat) == -1) {
        perror("fstat");
        exit(5);
    }
    if (stat.st_size == 0) {
        ftruncate(fd, INIT_SIZE);
        memset(hdr, 0, sizeof *hdr);
        hdr->magic = 0xb055;
        memset(first_entry, 0, sizeof *first_entry);
        first_entry->size = INIT_SIZE - sizeof *hdr;
        end = (char*)base + INIT_SIZE;
    } else {
        end = (char*)base + stat.st_size;
    }

    int file_size = INIT_SIZE;
    size_t n;
    char *line = NULL;
    while (getline(&line, &n, stdin) > 0) {
        // remove the newline
        size_t endi = strlen(line) - 1;
        if (line[endi] == '\n') {
            line[endi] = '\0';
        }
        switch(line[0]) {
            // Deletion
            case 'd': {
                int deleted = 0;
                char *str = &line[2];
                struct bos_entry *entry = first_entry;
                struct bos_entry *previous_entry;
                while ((void *) entry < end) {
                    if (entry->allocated) {
                        if (strcmp((char *) &entry[1], str) == 0) {
                            entry->allocated = 0;
                            deleted = 1;
                            break;
                        }
                    }
                    previous_entry = entry;
                    entry = (struct bos_entry *) ((char *) entry + entry->size);
                }
                // Coalesce
                if (deleted) {
                    struct bos_entry *next_entry = (struct bos_entry *) ((char *) entry + entry->size);
                    if (!next_entry->allocated) {
                        entry->size = entry->size + next_entry->size;
                    }
                    if (!previous_entry->allocated) {
                        previous_entry->size = previous_entry->size + entry->size;
                    }
                }
            }
            break;
            case 'l': {
                struct bos_entry *entry = first_entry;
                int i = 0;
                while ((void*)entry < end) {
                    if (entry->allocated) {
                        printf("%s\n", (char*)&entry[1]);
                    }
                    entry = (struct bos_entry *) ((char *) entry + entry->size);
                }
            }
            break; // break from the switch statement
            case 'a': {
                int added = 0;
                char *str = &line[2];
                struct bos_entry *entry = first_entry, *best_fit;
                int foundAnEntry = 0, duplicate = 0;
                int needed_size = sizeof(*entry) + strlen(str) + 1;
                while ((void*)entry < end) {
                    // Checking for duplicates
                    if (entry->allocated) {
                        if (strcmp((char *)&entry[1], str) == 0) {
                            printf("Trying to add a duplicate\n");
                            duplicate = 1;  // The string is already in the bag
                            break;
                        }
                    }
                    // Searching for the best fit
                    if (!foundAnEntry) {
                        // First, set the best_fit entry as the first entry big enough to hold the string
                        if (!entry->allocated && entry->size >= needed_size) {
                            best_fit = entry;
                            foundAnEntry = 1;
                        }
                    } else { // Once best_fit has been initialized we can check the current entry against best_fit's size
                        if (!entry->allocated) {
                            if (entry->size >= needed_size && entry->size < best_fit->size)
                                best_fit = entry;
                        }
                    }
                    entry = (struct bos_entry *) ((char *) entry + entry->size);
                }
                if (!duplicate) {
                    if (foundAnEntry) {
                        // split it
                        entry = best_fit;
                        int left_over = entry->size - needed_size;
                        if (left_over < sizeof(*entry)) {
                            // if we don't have enough left over for an entry struct, we just
                            // use it here and it will be internal fragmentation
                            needed_size = entry->size;
                            left_over = 0;
                        }
                        entry->size = needed_size;
                        entry->allocated = 1;
                        strcpy((char *) &entry[1], str);
                        if (left_over > 0) {
                            entry = (struct bos_entry *) ((char *) entry + entry->size);
                            memset(entry, 0, sizeof *entry);
                            entry->size = left_over;
                        }
                    }
                    // If there was no entry large enough to add the string, then grow the file
                    else {
                        entry = end;
                        file_size += INIT_SIZE;
                        ftruncate(fd, file_size);
                        entry->size = entry->size + INIT_SIZE;
                        end = (char *) end + INIT_SIZE;
                        // Then add the string
                        // Duplicate detection not necessary because we're looking through new space
                        // that hasn't been added to yet

                        // split it
                        int left_over = entry->size - needed_size;
                        if (left_over < sizeof(*entry)) {
                            // if we don't have enough left over for an entry struct, we just
                            // use it here and it will be internal fragmentation
                            needed_size = entry->size;
                            left_over = 0;
                        }
                        entry->size = needed_size;
                        entry->allocated = 1;
                        strcpy((char *) &entry[1], str);
                        if (left_over > 0) {
                            entry = (struct bos_entry *) ((char *) entry + entry->size);
                            memset(entry, 0, sizeof *entry);
                            entry->size = left_over;
                        }
                    }
                }
            }
        }
        free(line);
        n = 0;
        line = 0;
    }
    return 0;
}
