#include <openssl/conf.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HMAC_SIZE 16
#define IV_SIZE 16
#define IV_SIZE_UINT64 IV_SIZE / sizeof(uint64_t)

void handleErrors(void);

void encryption_setup(uint8_t* key_setup, size_t key_len, uint8_t* iv_setup,
                      size_t iv_len_setup);
void delete_encryption_setup();
uint8_t* encrypt_final(uint8_t* input, size_t plain_size, uint8_t* tag,
                       unsigned char* aad, size_t aad_size, uint8_t* iv);
uint8_t* encrypt_final_two_parts(uint8_t* input1, size_t plain_size1,
                                 uint8_t* input2, size_t plain_size2,
                                 uint8_t* tag, unsigned char* aad,
                                 size_t aad_size, uint8_t* iv);
uint8_t* decrypt_final(uint8_t* input, size_t cipher_size, uint8_t* tag,
                       unsigned char* aad, size_t aad_size, uint8_t* iv);
uint8_t* decrypt_final_two_parts(uint8_t* input1, size_t cipher_size1,
                                 uint8_t* input2, size_t cipher_size2,
                                 uint8_t* tag, unsigned char* aad,
                                 size_t aad_size, uint8_t* iv);

void encrypt_final_direct(uint8_t* input, size_t plain_size, uint8_t* tag,
                          unsigned char* aad, size_t aad_size, uint8_t* iv,
                          uint8_t* cipher_addr);
void encrypt_final_two_parts_direct(uint8_t* input1, size_t plain_size1,
                                    uint8_t* input2, size_t plain_size2,
                                    uint8_t* tag, unsigned char* aad,
                                    size_t aad_size, uint8_t* iv,
                                    uint8_t* cipher_addr);
void direct_encrypt_final_two_parts(uint8_t* input1, size_t plain_size1,
                                    uint8_t* input2, size_t plain_size2,
                                    uint8_t* tag, unsigned char* aad,
                                    size_t aad_size, uint8_t* iv,
                                    uint8_t* cipher_addr);

int gcm_encrypt(unsigned char* plaintext, int plaintext_len, unsigned char* aad,
                int aad_len, unsigned char* key, unsigned char* iv, int iv_len,
                unsigned char* ciphertext, unsigned char* tag);
int gcm_decrypt(unsigned char* ciphertext, int ciphertext_len,
                unsigned char* aad, int aad_len, unsigned char* tag,
                unsigned char* key, unsigned char* iv, int iv_len,
                unsigned char* plaintext);
int gcm_encrypt_two_parts(unsigned char* plaintext1, int plaintext_len1,
                          unsigned char* plaintext2, int plaintext_len2,
                          unsigned char* aad, int aad_len, unsigned char* key,
                          unsigned char* iv, int iv_len,
                          unsigned char* ciphertext, unsigned char* tag);
int gcm_decrypt_two_parts(unsigned char* ciphertext1, int ciphertext_len1,
                          unsigned char* ciphertext2, int ciphertext_len2,
                          unsigned char* aad, int aad_len, unsigned char* tag,
                          unsigned char* key, unsigned char* iv, int iv_len,
                          unsigned char* plaintext);

#ifdef __cplusplus
}
#endif