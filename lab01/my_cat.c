/*
    File: my_cat.c
    Author: Ashley Manson
 
    An implementation of a cat program in c
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#define BUFFER_SIZE 100

int main(int argc, char* argv[]) {
    
    if (argc < 2) {
        fprintf(stderr, "No file given, usage: %s [file]\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    int fd = open(argv[1], O_RDONLY);
	
	if (fd < 0) {
		fprintf(stderr, "Cannot open %s\n", argv[1]);
		exit(EXIT_FAILURE);
	}
	
    char buffer[BUFFER_SIZE];
    int size_read;
    int size_write;
    size_t size_remaining;
    size_t size_written;
    
restart:
    
    while ((size_read = read(fd, &buffer, BUFFER_SIZE)) > 0) {
        
        size_remaining = size_read;
        size_written = 0;
        
        while ((size_write = write(STDOUT_FILENO, &buffer[size_written], size_remaining)) < size_remaining) {
            
            if (size_write < 0) {
                if (EINTR == errno) {
                    continue;
                }
                else {
                    perror("write()");
                    exit(EXIT_FAILURE);
                }
            }
            
            else {
                size_written += size_write;
                size_remaining -= size_write;
            }
        }
    }
    
    if (size_read < 0) {
        if (EINTR == errno) {
            goto restart;
        }
        else {
            perror("read()");
            exit(EXIT_FAILURE);
        }
    }
    
    close(fd);
    
    return 0;
}
