/*
 * extra_writes.c
 */
#ifdef STATISTICS
#ifdef WRITE_AMPL
#include <assert.h>
#include <errno.h>
#include <ex_common.h>
#include <fcntl.h>
#include <libpmemobj.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include "../hashmap.h"
#include "../internal_statistics.h"
#include "../manifest_operations.h"
#include "../metadata_operations.h"
#include "../openssl_gcm_encrypt.h"
#include "../tx_user_operations.h"
#include "../user_operations.h"
extern const char *data_str[];

#define MAX_BUF_LEN_ROOT 32  /* maximum length of our root buffer */
#define MAX_BUF_LEN_OBJ 1000 /* maximum length of our object buffer */
#define POOL_SIZE 200 * PMEMOBJ_MIN_POOL
#define MB (1024 * 1024)

#ifdef SCONE
#define SYS_untrusted_mmap 1025
void *scone_kernel_mmap(void *addr, size_t length, int prot, int flags, int fd,
                        off_t offset) {
  return (void *)syscall(SYS_untrusted_mmap, addr, length, prot, flags, fd,
                         offset);
  // printf("scone mmap syscall number : %d\n", SYS_untrusted_mmap);
}
void *mmap_helper(size_t size) {
  return scone_kernel_mmap(NULL, size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANON, -1, 0);
}
#else
void *mmap_helper(size_t size) {
  return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1,
              0);
}
#endif

void munmap_helper(void *addr, size_t size) { munmap(addr, size); }

struct my_root {
  size_t len; /* = strlen(buf) */
  char buf[MAX_BUF_LEN_ROOT];
  PMEMoid first_object;
};

struct my_object {
  size_t len; /* = strlen(buf) */
  char buf[MAX_BUF_LEN_OBJ];
  PMEMoid next_object;
};

/* Default values */
int objects_per_tx = 20;
int tx_number = 1;

int safe_remove(const char *path);
void print_results(const char *type, uint64_t *res);

int main(int argc, char *argv[]) {
  if (argc < 2 || strcmp(argv[1], "extra_writes") != 0) {
    printf(
        "Usage : ./extra_writes extra_writes [objects_per_tx] "
        "[number_of_tx]\n");
    return 0;
  }

  if (argc >= 3) {
    objects_per_tx = atoi(argv[2]);
  }

  if (argc >= 4) {
    tx_number = atoi(argv[3]);
  }

  /* user operations check */
  PMEMobjpool *pop_v = NULL;
  const char *pool_path = "/dev/shm/arraypool";
  const char *manifest_path = "/dev/hugepages/Manifest";
  const char *counters_path = "/dev/shm/amcs";
  const char *metadata_log_path = "/dev/shm/Metadata_log";

  if (file_exists(pool_path) == 0) {
    safe_remove(pool_path);
  }
  if (file_exists(manifest_path) == 0) {
    safe_remove(manifest_path);
  }
  if (file_exists(counters_path) == 0) {
    safe_remove(counters_path);
  }
  if (file_exists(metadata_log_path) == 0) {
    safe_remove(metadata_log_path);
  }
  int errnum __attribute__((unused)) = 1024;

  /* A 128 bit key */
  uint8_t *key = (uint8_t *)"012345678901234";

  /* A 128 bit IV */
  uint8_t *iv = (uint8_t *)"012345678901234";
  size_t iv_len = 16;

  if (file_exists(pool_path) != 0) {
    if ((pop_v = spool_create(pool_path, POBJ_LAYOUT_NAME(microbenchmarks),
                              POOL_SIZE, CREATE_MODE_RW, manifest_path,
                              counters_path, metadata_log_path, key, 16, iv,
                              iv_len)) == NULL) {
      printf("failed to create pool\n");
      return 1;
    }
  } else {
    if ((pop_v = spool_open(pool_path, POBJ_LAYOUT_NAME(microbenchmarks),
                            manifest_path, counters_path, metadata_log_path,
                            key, 16, iv, iv_len)) == NULL) {
      printf("failed to open vol pool\n");
      return 1;
    }
  }

  PMEMoid *oid =
      (PMEMoid *)mmap_helper(objects_per_tx * tx_number * sizeof(PMEMoid));
  uint64_t *res;
  sobj_stats_init();
  sobj_stats_on();
  for (int i = 0; i < tx_number; i++) {
    TX_BEGIN(pop_v) {
      for (int j = 0; j < objects_per_tx; j++) {
        oid[i * objects_per_tx + j] =
            sobj_tx_alloc(sizeof(struct my_object), 0);
      }
    }
    TX_ONABORT { printf("transaction aborted\n"); }
    TX_END
  }
  sobj_stats_off();
  res = sobj_get_bytes_written();
  print_results("alloc", res);
  sobj_stats_clear();

  void *obj_ptr;
  sobj_stats_on();
  for (int i = 0; i < tx_number; i++) {
    TX_BEGIN(pop_v) {
      for (int j = 0; j < objects_per_tx; j++) {
        if (sobj_tx_add_range(oid[i * objects_per_tx + j], 0,
                              sizeof(struct my_object)) != 0) {
          printf("error in adding range\n");
        }
        obj_ptr = sobj_direct(pop_v, oid[i * objects_per_tx + j]);
        assert(obj_ptr != NULL);
      }
    }
    TX_ONABORT { printf("transaction aborted\n"); }
    TX_END
  }
  sobj_stats_off();
  res = sobj_get_bytes_written();
  print_results("update", res);
  sobj_stats_clear();

  sobj_stats_on();
  for (int i = 0; i < tx_number; i++) {
    TX_BEGIN(pop_v) {
      for (int j = 0; j < objects_per_tx; j++) {
        sobj_tx_free(oid[i * objects_per_tx + j]);
      }
    }
    TX_ONABORT { printf("transaction aborted\n"); }
    TX_END
  }
  sobj_stats_off();
  res = sobj_get_bytes_written();
  print_results("free", res);
  sobj_stats_clear();

  munmap_helper(oid, objects_per_tx * tx_number * sizeof(PMEMoid));
  spool_close(pop_v);

  return 0;
}

int safe_remove(const char *path) {
  int del = remove(path);
  if (del) printf("the file %s is not Deleted\n", path);
  return del;
}

void print_results(const char *type, uint64_t *bytes_written) {
  for (int j = 0; j < MAX_WRITE_CAT; j++) {
    if (j == 0)
      printf("op_type;%s", data_str[j]);
    else
      printf(";%s", data_str[j]);
  }
  printf(";tx_number;objects_per_tx");
  printf("\n");
  printf("%s", type);
  for (size_t k = 0; k < MAX_WRITE_CAT; k++) {
    printf(";%ld", bytes_written[k]);
  }
  printf(";%d;%d", tx_number, objects_per_tx);
  printf("\n");
  return;
}
#endif
#endif