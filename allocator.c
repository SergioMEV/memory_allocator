#define _GNU_SOURCE

#include <assert.h>
#include <malloc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

// test the program by running LD_PRELOAD=./myallocator.so test/malloc-test

// The minimum size returned by malloc
#define MIN_MALLOC_SIZE 16

// The size of a single page of memory, in bytes
#define PAGE_SIZE 0x1000

// This is the magic number
#define MAGIC_NUMBER 10230829

// Round a value x up to the next multiple of y
#define ROUND_UP(x, y) ((x) % (y) == 0 ? (x) : (x) + ((y) - (x) % (y)))

// The struct for storing chunk_size and magic_number in the header of a page.
typedef struct Chunk_header {
  size_t chunk_size;
  size_t magic_number;
} Chunk_header;

// The global array to store the headers of free lists.
intptr_t free_list_headers[] = {0, 0, 0, 0, 0, 0, 0, 0};

// A utility function to get the available size of an allocated object.
size_t xxmalloc_usable_size(void* ptr);

// A utility logging function that definitely does not call malloc or free
void log_message(char* message);

/**
 * Map chunck size to the index of that chunk size in free_list_headers.
 * \param chunk_size the size of free chunk
 * \returns  the index of that chunk size in free list headers.
 *
 */
int search_free_list_index(int chunk_size) {
  if (chunk_size == 16)
    return 0;
  else if (chunk_size == 32)
    return 1;
  else if (chunk_size == 64)
    return 2;
  else if (chunk_size == 128)
    return 3;
  else if (chunk_size == 256)
    return 4;
  else if (chunk_size == 512)
    return 5;
  else if (chunk_size == 1024)
    return 6;
  else if (chunk_size == 2048)
    return 7;
  else
    return chunk_size;
}

/**
 * Round up some size to the multiplication of MIN_MALLOC_SIZE and exponents of 2.
 *        (e.g. 16, 32, 64, 128, 256, 512, 1024, 2048)
 * \param size the size of chunk.
 * \returns    The round up size.
 *
 */
int round_byte(int size) {
  int chunk_size = MIN_MALLOC_SIZE;
  while (chunk_size <= 2048) {
    if (size <= chunk_size) return chunk_size;
    chunk_size *= 2;
  }
  return size;
}

/**
 * Allocate space on the heap.
 * \param size  The minimium number of bytes that must be allocated
 * \returns     A pointer to the beginning of the allocated space.
 *              This function may return NULL when an error occurs.
 */
void* xxmalloc(size_t size) {
  int chunk_size = round_byte(size);
  // if chunk_size is larger than 2048, index will just be chunk_size,
  //    else it will be the index of that chunck_size in the free list.
  int free_list_index = search_free_list_index(chunk_size);

  if (free_list_index > 7) {
    // size larger than 2048
    // Round the size up to the next multiple of the page size
    int size_large = ROUND_UP(size, PAGE_SIZE);
    void* page = mmap(NULL, size_large, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    // Check for errors when allocating pages.
    if (page == MAP_FAILED) {
      log_message("mmap failed! Giving up.\n");
      exit(2);
    }
    return page;
  } else {
    // size smaller or equal to 2048
    if (free_list_headers[free_list_index] == 0) {
      // we do not have free list to allocate, lets create a new page.
      Chunk_header* page_header_ptr = (Chunk_header*)mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
                                                          MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
      // Check for errorsLD_PRELOAD=./myallocator.so malloc-test/malloc-test
      if (page_header_ptr == MAP_FAILED) {
        log_message("mmap failed! Giving up.\n");
        exit(2);
      }
      // write the size and magic number in the header
      page_header_ptr->chunk_size = chunk_size;
      page_header_ptr->magic_number = MAGIC_NUMBER;
      // write the head of linked list into free list
      intptr_t linked_list_header = (intptr_t)page_header_ptr + chunk_size;
      free_list_headers[free_list_index] = linked_list_header;
      // write the linked list inside our page
      intptr_t linked_list_current = linked_list_header;
      intptr_t linked_list_next = linked_list_header + chunk_size;
      for (int chunk_index = 0; chunk_index < (PAGE_SIZE / chunk_size) - 2; chunk_index++) {
        *(intptr_t*)linked_list_current = linked_list_next;
        linked_list_current += chunk_size;
        linked_list_next += chunk_size;
      }
      // write the last chunk of page
      *(intptr_t*)linked_list_current = 0;
    }
    // now, we are sure that we have free list to allocate
    intptr_t free_list = free_list_headers[free_list_index];
    free_list_headers[free_list_index] = (intptr_t) * (intptr_t*)free_list;
    return (void*)free_list;
  }
  return NULL;
}

/**
 * Free space occupied by a heap object.
 * \param ptr   A pointer somewhere inside the object that is being freed
 */
void xxfree(void* ptr) {
  // Don't free NULL!
  if (ptr == NULL) return;

  size_t chunk_size = xxmalloc_usable_size(ptr);

  // if magic number does not match or ptr is NULL, size will be zero.
  if (chunk_size == 0) return;

  // Treat the freed pointer as an integer
  intptr_t free_address = (intptr_t)ptr;

  // Round down to the beginning of a chunk
  intptr_t chunk_start = free_address - (free_address % chunk_size);

  int free_list_index = search_free_list_index(chunk_size);
  // Put available chunk in the free list headers.
  intptr_t free_list_head = free_list_headers[free_list_index];
  *(intptr_t*)free_address = free_list_head;
  free_list_headers[free_list_index] = chunk_start;
}

/**
 * Get the available size of an allocated object. This function should return the amount of space
 * that was actually allocated by malloc, not the amount that was requested.
 * \param ptr   A pointer somewhere inside the allocated object
 * \returns     The number of bytes available for use in this object
 */
size_t xxmalloc_usable_size(void* ptr) {
  // If ptr is NULL always return zero
  if (ptr == NULL) {
    return 0;
  }

  // Treat the freed pointer as an integer
  intptr_t free_address = (intptr_t)ptr;

  // Round down to the beginning of a page
  intptr_t page_start = free_address - (free_address % PAGE_SIZE);

  Chunk_header* page_header_ptr = (Chunk_header*)page_start;

  // If magic number does not match, return 0.
  if (page_header_ptr->magic_number != MAGIC_NUMBER) return 0;

  return page_header_ptr->chunk_size;
}

/**
 * Print a message directly to standard error without invoking malloc or free.
 * \param message   A null-terminated string that contains the message to be printed
 */
void log_message(char* message) {
  // Get the message length
  size_t len = 0;
  while (message[len] != '\0') {
    len++;
  }

  // Write the message
  if (write(STDERR_FILENO, message, len) != len) {
    // Write failed. Try to write an error message, then exit
    char fail_msg[] = "logging failed\n";
    write(STDERR_FILENO, fail_msg, sizeof(fail_msg));
    exit(2);
  }
}
