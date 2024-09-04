#include "openssl_gcm_encrypt.h"
#include "hashmap.h"
#include "internal_statistics.h"

uint8_t *key;
uint8_t *iv_init;
size_t iv_len;

/*
 * Encryption setup:
 * Args: key, key_len, iv, iv_len
 */
void encryption_setup(uint8_t *key_setup, size_t key_len, uint8_t *iv_setup,
                      size_t iv_len_setup) {
  // set up the key and initialization vector
  key = (uint8_t *)malloc(key_len * sizeof(uint8_t));
  memcpy(key, key_setup, key_len);
  iv_init = (uint8_t *)malloc(iv_len_setup * sizeof(uint8_t));
  memcpy(iv_init, iv_setup, iv_len_setup);
  iv_len = iv_len_setup;
}

/*
 * Deletes encryption setup:
 * free the malloc'd uint8_t streams for iv_init and key
 */
void delete_encryption_setup() {
  free(key);
  free(iv_init);
}

void handleErrors(void) {
  ERR_print_errors_fp(stderr);
  abort();
}

/*
 * Encrypts the input based on the key and IV of the set up and sets the tag
 * with the HMAC value Returns the ciphertext in success or NULL in case of
 * failure
 */
uint8_t *encrypt_final(uint8_t *input, size_t plain_size, uint8_t *tag,
                       unsigned char *aad, size_t aad_size, uint8_t *iv) {
#ifdef STATISTICS
  stats_measure_start(ENCRYPTION_COST);
#endif
  if (aad == NULL) aad = (unsigned char *)"";

  uint8_t *ciphertext = (uint8_t *)malloc(plain_size * sizeof(uint8_t));

#ifdef ENCR_OFF
  memcpy(ciphertext, input, plain_size);
#else
  int ciphertext_len;
  ciphertext_len = gcm_encrypt(input, plain_size, aad, aad_size, key, iv,
                               iv_len, ciphertext, tag);
  if (ciphertext_len != plain_size) {
    printf("Encrypt : Warning: size differs\n");
    free(ciphertext);
    return NULL;
  }
#endif
#ifdef STATISTICS
  stats_measure_end(ENCRYPTION_COST);
#endif
  return ciphertext;
}

/*
 * Encrypts the input based on the key and IV of the set up and sets the tag
 * with the HMAC value Places ciphertext in specified address
 */
void encrypt_final_direct(uint8_t *input, size_t plain_size, uint8_t *tag,
                          unsigned char *aad, size_t aad_size, uint8_t *iv,
                          uint8_t *cipher_addr) {
#ifdef STATISTICS
  stats_measure_start(ENCRYPTION_COST);
#endif
  if (aad == NULL) aad = (unsigned char *)"";

#ifdef ENCR_OFF
  memcpy(cipher_addr, input, plain_size);
#else
  int ciphertext_len;
  ciphertext_len = gcm_encrypt(input, plain_size, aad, aad_size, key, iv,
                               iv_len, cipher_addr, tag);
  if (ciphertext_len != plain_size) {
    printf("%s Encrypt : Warning: size differs\n", __func__);
    return;
  }
#endif
#ifdef STATISTICS
  stats_measure_end(ENCRYPTION_COST);
#endif
  return;
}

/*
 * Encrypts the inputs based on the key and IV of the set up and sets the tag
 * with the HMAC value Returns the ciphertext in success or NULL in case of
 * failure
 */
uint8_t *encrypt_final_two_parts(uint8_t *input1, size_t plain_size1,
                                 uint8_t *input2, size_t plain_size2,
                                 uint8_t *tag, unsigned char *aad,
                                 size_t aad_size, uint8_t *iv) {
#ifdef STATISTICS
  stats_measure_start(ENCRYPTION_COST);
#endif
  if (aad == NULL) aad = (unsigned char *)"";

  uint8_t *ciphertext =
      (uint8_t *)malloc((plain_size1 + plain_size2) * sizeof(uint8_t));

#ifdef ENCR_OFF
  memcpy(ciphertext, input1, plain_size1);
  memcpy(ciphertext + plain_size1, input2, plain_size2);
#else
  int ciphertext_len;
  ciphertext_len =
      gcm_encrypt_two_parts(input1, plain_size1, input2, plain_size2, aad,
                            aad_size, key, iv, iv_len, ciphertext, tag);
  if (ciphertext_len != (plain_size1 + plain_size2)) {
    printf("Encrypt : Warning: size differs\n");
    free(ciphertext);
    return NULL;
  }
#endif
#ifdef STATISTICS
  stats_measure_end(ENCRYPTION_COST);
#endif
  return ciphertext;
}

/*
 * Encrypts the inputs based on the key and IV of the set up and sets the tag
 * with the HMAC value Places ciphertext in specified address
 */
void encrypt_final_two_parts_direct(uint8_t *input1, size_t plain_size1,
                                    uint8_t *input2, size_t plain_size2,
                                    uint8_t *tag, unsigned char *aad,
                                    size_t aad_size, uint8_t *iv,
                                    uint8_t *cipher_addr) {
#ifdef STATISTICS
  stats_measure_start(ENCRYPTION_COST);
#endif
  if (aad == NULL) aad = (unsigned char *)"";

#ifdef ENCR_OFF
  memcpy(cipher_addr, input1, plain_size1);
  memcpy(cipher_addr + plain_size1, input2, plain_size2);
#else
  int ciphertext_len;
  ciphertext_len =
      gcm_encrypt_two_parts(input1, plain_size1, input2, plain_size2, aad,
                            aad_size, key, iv, iv_len, cipher_addr, tag);
  if (ciphertext_len != (plain_size1 + plain_size2)) {
    printf("%s Encrypt : Warning: size differs\n", __func__);
    return;
  }
#endif
#ifdef STATISTICS
  stats_measure_end(ENCRYPTION_COST);
#endif
  return;
}

/*
 * Decrypts the input based on the key and IV of the set up and the provided
 * HMAC tag Reutrns the decrypted text in success or NULL in case of failure
 */
uint8_t *decrypt_final(uint8_t *input, size_t cipher_size, uint8_t *tag,
                       unsigned char *aad, size_t aad_size, uint8_t *iv) {
#ifdef STATISTICS
  stats_measure_start(ENCRYPTION_COST);
#endif
  if (aad == NULL) aad = (unsigned char *)"";

  uint8_t *decryptedtext = (uint8_t *)malloc(cipher_size * sizeof(uint8_t));

#ifdef ENCR_OFF
  memcpy(decryptedtext, input, cipher_size);
#else
  int decryptedtext_len;
  /* Decrypt the ciphertext */
  decryptedtext_len = gcm_decrypt(
      (unsigned char *)input, cipher_size, aad, aad_size, (unsigned char *)tag,
      key, (unsigned char *)iv, iv_len, (unsigned char *)decryptedtext);
  if (decryptedtext_len < 0) {
    free(decryptedtext);
#ifdef DEBUG
    printf("%s Decryption : Corrupted Object/Log Entry \n", __func__);
#endif
    return NULL;
  }
#endif
#ifdef STATISTICS
  stats_measure_end(ENCRYPTION_COST);
#endif
  return decryptedtext;
}

/*
 * Decrypts the input based on the key and IV of the set up and the provided
 * HMAC tag Reutrns the decrypted text in success or NULL in case of failure
 */
uint8_t *decrypt_final_two_parts(uint8_t *input1, size_t cipher_size1,
                                 uint8_t *input2, size_t cipher_size2,
                                 uint8_t *tag, unsigned char *aad,
                                 size_t aad_size, uint8_t *iv) {
#ifdef STATISTICS
  stats_measure_start(ENCRYPTION_COST);
#endif
  if (aad == NULL) aad = (unsigned char *)"";

  uint8_t *decryptedtext =
      (uint8_t *)malloc((cipher_size1 + cipher_size2) * sizeof(uint8_t));

#ifdef ENCR_OFF
  memcpy(decryptedtext, input1, cipher_size1);
  memcpy(decryptedtext + cipher_size1, input2, cipher_size2);
#else
  int decryptedtext_len;
  /* Decrypt the ciphertext */
  decryptedtext_len = gcm_decrypt_two_parts(
      (unsigned char *)input1, cipher_size1, (unsigned char *)input2,
      cipher_size2, aad, aad_size, (unsigned char *)tag, key,
      (unsigned char *)iv, iv_len, (unsigned char *)decryptedtext);
  if (decryptedtext_len < 0) {
#ifdef DEBUG
    printf("%s Decryption : Corrupted Object/Log Entry \n", __func__);
#endif
    return NULL;
  }
#endif
#ifdef STATISTICS
  stats_measure_end(ENCRYPTION_COST);
#endif
  return decryptedtext;
}

int gcm_encrypt(unsigned char *plaintext, int plaintext_len, unsigned char *aad,
                int aad_len, unsigned char *key, unsigned char *iv, int iv_len,
                unsigned char *ciphertext, unsigned char *tag) {
  EVP_CIPHER_CTX *ctx;

  int len;

  int ciphertext_len;

  /* Create and initialise the context */
  if (!(ctx = EVP_CIPHER_CTX_new())) handleErrors();

  /* Initialise the encryption operation. */
  if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_128_gcm(), NULL, NULL, NULL))
    handleErrors();

  /*
   * Set IV length if default 12 bytes (96 bits) is not appropriate
   */
  if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv_len, NULL))
    handleErrors();

  /* Initialise key and IV */
  if (1 != EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv)) handleErrors();

  /*
   * Provide any AAD data. This can be called zero or more times as
   * required
   */
  if (1 != EVP_EncryptUpdate(ctx, NULL, &len, aad, aad_len)) handleErrors();

  /*
   * Provide the message to be encrypted, and obtain the encrypted output.
   * EVP_EncryptUpdate can be called multiple times if necessary
   */
  if (1 != EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext, plaintext_len))
    handleErrors();
  ciphertext_len = len;

  /*
   * Finalise the encryption. Normally ciphertext bytes may be written at
   * this stage, but this does not occur in GCM mode
   */
  if (1 != EVP_EncryptFinal_ex(ctx, ciphertext + len, &len)) handleErrors();
  ciphertext_len += len;

  /* Get the tag */
  if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag))
    handleErrors();

  /* Clean up */
  EVP_CIPHER_CTX_free(ctx);

  return ciphertext_len;
}

int gcm_decrypt(unsigned char *ciphertext, int ciphertext_len,
                unsigned char *aad, int aad_len, unsigned char *tag,
                unsigned char *key, unsigned char *iv, int iv_len,
                unsigned char *plaintext) {
  EVP_CIPHER_CTX *ctx;
  int len = 0;
  int plaintext_len = 0;
  int ret;

  /* Create and initialise the context */
  if (!(ctx = EVP_CIPHER_CTX_new())) handleErrors();

  /* Initialise the decryption operation. */
  if (!EVP_DecryptInit_ex(ctx, EVP_aes_128_gcm(), NULL, NULL, NULL))
    handleErrors();

  /* Set IV length. Not necessary if this is 12 bytes (96 bits) */
  if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv_len, NULL))
    handleErrors();

  /* Initialise key and IV */
  if (!EVP_DecryptInit_ex(ctx, NULL, NULL, key, iv)) handleErrors();

  /*
   * Provide any AAD data. This can be called zero or more times as
   * required
   */
  if (!EVP_DecryptUpdate(ctx, NULL, &len, aad, aad_len)) handleErrors();

  /*
   * Provide the message to be decrypted, and obtain the plaintext output.
   * EVP_DecryptUpdate can be called multiple times if necessary
   */
  if (!EVP_DecryptUpdate(ctx, plaintext, &len, ciphertext, ciphertext_len))
    handleErrors();
  plaintext_len = len;

  /* Set expected tag value. Works in OpenSSL 1.0.1d and later */
  if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, tag)) handleErrors();

  /*
   * Finalise the decryption. A positive return value indicates success,
   * anything else is a failure - the plaintext is not trustworthy.
   */
  ret = EVP_DecryptFinal_ex(ctx, plaintext + len, &len);

  /* Clean up */
  EVP_CIPHER_CTX_free(ctx);

  if (ret > 0) {
    /* Success */
    plaintext_len += len;
    return plaintext_len;
  } else {
    /* Verify failed */
    return -1;
  }
}

int gcm_encrypt_two_parts(unsigned char *plaintext1, int plaintext_len1,
                          unsigned char *plaintext2, int plaintext_len2,
                          unsigned char *aad, int aad_len, unsigned char *key,
                          unsigned char *iv, int iv_len,
                          unsigned char *ciphertext, unsigned char *tag) {
  EVP_CIPHER_CTX *ctx;

  int len;

  int ciphertext_len;

  /* Create and initialise the context */
  if (!(ctx = EVP_CIPHER_CTX_new())) handleErrors();

  /* Initialise the encryption operation. */
  if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_128_gcm(), NULL, NULL, NULL))
    handleErrors();

  /*
   * Set IV length if default 12 bytes (96 bits) is not appropriate
   */
  if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv_len, NULL))
    handleErrors();

  /* Initialise key and IV */
  if (1 != EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv)) handleErrors();

  /*
   * Provide any AAD data. This can be called zero or more times as
   * required
   */
  if (1 != EVP_EncryptUpdate(ctx, NULL, &len, aad, aad_len)) handleErrors();

  /*
   * Provide the message to be encrypted, and obtain the encrypted output.
   * EVP_EncryptUpdate can be called multiple times if necessary
   */
  if (1 != EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext1, plaintext_len1))
    handleErrors();
  ciphertext_len = len;

  if (1 != EVP_EncryptUpdate(ctx, ciphertext + len, &len, plaintext2,
                             plaintext_len2))
    handleErrors();
  ciphertext_len += len;
  /*
   * Finalise the encryption. Normally ciphertext bytes may be written at
   * this stage, but this does not occur in GCM mode
   */
  if (1 != EVP_EncryptFinal_ex(ctx, ciphertext + len, &len)) handleErrors();
  ciphertext_len += len;

  /* Get the tag */
  if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag))
    handleErrors();

  /* Clean up */
  EVP_CIPHER_CTX_free(ctx);

  return ciphertext_len;
}

int gcm_decrypt_two_parts(unsigned char *ciphertext1, int ciphertext_len1,
                          unsigned char *ciphertext2, int ciphertext_len2,
                          unsigned char *aad, int aad_len, unsigned char *tag,
                          unsigned char *key, unsigned char *iv, int iv_len,
                          unsigned char *plaintext) {
  EVP_CIPHER_CTX *ctx;
  int len;
  int plaintext_len;
  int ret;

  /* Create and initialise the context */
  if (!(ctx = EVP_CIPHER_CTX_new())) handleErrors();

  /* Initialise the decryption operation. */
  if (!EVP_DecryptInit_ex(ctx, EVP_aes_128_gcm(), NULL, NULL, NULL))
    handleErrors();

  /* Set IV length. Not necessary if this is 12 bytes (96 bits) */
  if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv_len, NULL))
    handleErrors();

  /* Initialise key and IV */
  if (!EVP_DecryptInit_ex(ctx, NULL, NULL, key, iv)) handleErrors();

  /*
   * Provide any AAD data. This can be called zero or more times as
   * required
   */
  if (!EVP_DecryptUpdate(ctx, NULL, &len, aad, aad_len)) handleErrors();

  /*
   * Provide the message to be decrypted, and obtain the plaintext output.
   * EVP_DecryptUpdate can be called multiple times if necessary
   */
  if (!EVP_DecryptUpdate(ctx, plaintext, &len, ciphertext1, ciphertext_len1))
    handleErrors();
  plaintext_len = len;

  if (!EVP_DecryptUpdate(ctx, plaintext + len, &len, ciphertext2,
                         ciphertext_len2))
    handleErrors();
  plaintext_len += len;

  /* Set expected tag value. Works in OpenSSL 1.0.1d and later */
  if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, tag)) handleErrors();

  /*
   * Finalise the decryption. A positive return value indicates success,
   * anything else is a failure - the plaintext is not trustworthy.
   */
  ret = EVP_DecryptFinal_ex(ctx, plaintext + len, &len);

  /* Clean up */
  EVP_CIPHER_CTX_free(ctx);

  if (ret > 0) {
    /* Success */
    plaintext_len += len;
    return plaintext_len;
  } else {
    /* Verify failed */
    return -1;
  }
}