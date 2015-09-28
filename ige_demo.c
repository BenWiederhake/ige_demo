/*
 * ige_demo.c, a small AES_IGE demo (C) Ben Wiederhake 2015
 * Released into the public domain in hope of being useful.
 * Creative Commons Public Domain Mark 1.0
 * ( https://creativecommons.org/publicdomain/mark/1.0/ )
 *
 * Compile: gcc ige_demo.c -lgcrypt -o ige_demo
 * Run: ./ige_demo
 */

/* Needed for axctual functionality */
#include <gcrypt.h>
#include <string.h> /* memcpy; could be avoided */

/* Needed for testing etc. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h> /* malloc */
#include <string.h> /* memcmp, memcpy */
// Intentionally duplicated to tell you that the tests need memcpy, too.

/* === ACTUAL FUNCTIONALITY ===
 * Only the symbol 'exposed_aes_ige_encrypt' needs to be exposed. */

// TODO: Use gcrypt's internal 'buf_xor'
static void do_xor_block (const unsigned char *in, const unsigned char *with,
    unsigned char *out) {
  for (int i = 0; i < 16; ++i) {
    *out = *in ^ *with;
    ++out;
    ++in;
    ++with;
  }
}

static gcry_error_t do_ige_encrypt (const unsigned char *in, unsigned char *out,
    unsigned long n_blocks, gcry_cipher_hd_t cipher, unsigned char *ivec) {
  const unsigned char *prev_x = ivec;
  const unsigned char *prev_y = ivec + 16;
  for (unsigned long i = 0; i < n_blocks; ++i) {
    do_xor_block (in, prev_y, out);
    gcry_error_t gcry_error = gcry_cipher_encrypt (cipher, out, 16, out, 16);
    if (gcry_error) {
      return gcry_error;
    }
    do_xor_block (out, prev_x, out);
    prev_x = in; // decrypted is in 'in'
    prev_y = out; // encrypted is in 'out'
    in += 16;
    out += 16;
  }
  if (n_blocks > 0) {
    /* OpenSSL updates the IV, so we do that, too.
     * One could avoid memcpy here, as it's only 16 bytes. */
    memcpy (ivec, prev_x, 16);
    memcpy (ivec + 16, prev_y, 16);
  }
  return 0;
}

static gcry_error_t do_ige_decrypt (const unsigned char *in, unsigned char *out,
    unsigned long n_blocks, gcry_cipher_hd_t cipher, unsigned char *ivec) {
  const unsigned char *prev_x = ivec;
  const unsigned char *prev_y = ivec + 16;
  for (unsigned long i = 0; i < n_blocks; ++i) {
    do_xor_block (in, prev_x, out);
    gcry_error_t gcry_error =
      gcry_cipher_decrypt (cipher, out, 16, out, 16);
    if (gcry_error) {
      return gcry_error;
    }
    do_xor_block (out, prev_y, out);
    prev_x = out; // decrypted is in 'out'
    prev_y = in; // encrypted is in 'in'
    in += 16;
    out += 16;
  }
  /* Do not change ivec */
  return 0;
}

/* Needs to be given an IV of length 2*16. */
gcry_error_t exposed_aes_ige_encrypt (const unsigned char *in,
    unsigned char *out, unsigned long length, const unsigned char *key,
    unsigned long key_length, unsigned char *ivec, const int enc) {
  if (length % 16) {
    return -3; // TODO: Which one?
  }
  unsigned long n_blocks = length / 16;

  /* Set it up. */
  gcry_cipher_hd_t cipher;
  gcry_error_t gcry_error = -3; // TODO: Which one?
  switch (key_length) {
  case 16:
    gcry_error =
      gcry_cipher_open (&cipher, GCRY_CIPHER_AES128, GCRY_CIPHER_MODE_ECB, 0);
    break;
  case 24:
    gcry_error =
      gcry_cipher_open (&cipher, GCRY_CIPHER_AES192, GCRY_CIPHER_MODE_ECB, 0);
    break;
  case 32:
    gcry_error =
      gcry_cipher_open (&cipher, GCRY_CIPHER_AES256, GCRY_CIPHER_MODE_ECB, 0);
    break;
  }
  if (gcry_error) {
    return gcry_error;
  }
  gcry_cipher_setkey (cipher, key, key_length);

  if (enc) {
    gcry_error = do_ige_encrypt(in, out, n_blocks, cipher, ivec);
  } else {
    gcry_error = do_ige_decrypt(in, out, n_blocks, cipher, ivec);
  }

  gcry_cipher_close(cipher);

  return gcry_error;
}

/* === HELPERS FOR TESTING === */

static void print_buf (const unsigned char* buf, unsigned long length) {
  for (unsigned long i = 0; i < length; ++i) {
    printf("%02x", *(buf + i));
  }
}

static unsigned int failures = 0;

static void verify_buf (const unsigned char* expected,
    const unsigned char* actual, unsigned long length) {
  if (memcmp (expected, actual, length)) {
    printf ("mismatch\nExpected: ");
    print_buf (expected, length);
    printf ("\nActual:   ");
    print_buf (actual, length);
    printf ("\n");
    ++failures;
  } else {
    printf ("OK\n");
  }
}

static void verify (const unsigned char* in, unsigned char* out,
    unsigned long length, const unsigned char *key, unsigned long key_length,
    unsigned char *ivec) {
  gcry_error_t gcry_error;
  unsigned char* actual = malloc (length);
  unsigned char* orig_iv = malloc (32);
  assert (actual && orig_iv);

  memcpy(orig_iv, ivec, 2 * 16);
  printf ("Encryption ... ");
  gcry_error =
    exposed_aes_ige_encrypt (in, actual, length, key, key_length, ivec, 1);
  if (gcry_error) {
    printf ("failed with error: %d\n", gcry_error);
    ++failures;
  } else {
    verify_buf (out, actual, length);
  }

  /* We're updating the IV during encryption. */
  memcpy(ivec, orig_iv, 2 * 16);
  printf ("Decryption ... ");
  gcry_error =
    exposed_aes_ige_encrypt (out, actual, length, key, key_length, ivec, 0);
  if (gcry_error) {
    printf ("failed with error: %d\n", gcry_error);
    ++failures;
  } else {
    verify_buf (in, actual, length);
  }
}

/* === ACTUAL TESTS === */

void test_vect_1 () {
  unsigned char key[] =
    {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
     0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
  unsigned char ivec[] =
    {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
     0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
     0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
     0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
  unsigned char in[] =
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  unsigned char out[] =
    {0x1a, 0x85, 0x19, 0xa6, 0x55, 0x7b, 0xe6, 0x52,
     0xe9, 0xda, 0x8e, 0x43, 0xda, 0x4e, 0xf4, 0x45,
     0x3c, 0xf4, 0x56, 0xb4, 0xca, 0x48, 0x8a, 0xa3,
     0x83, 0xc7, 0x9c, 0x98, 0xb3, 0x47, 0x97, 0xcb};
  assert (sizeof (in) == sizeof (out));
  assert (2 * 16 == sizeof (ivec));
  verify (in, out, sizeof (in), key, sizeof (key), ivec);
}

void test_vect_2 () {
  /*
   * It says "This is an imple", "mentation of IGE mode for OpenSS",
   * and "L. Let's hope Ben got it right!\n", which seems to be an
   * easter egg from Ben Laurie: http://www.links.org/files/openssl-ige.pdf
   */
  unsigned char key[] =
    {0x54, 0x68, 0x69, 0x73, 0x20, 0x69, 0x73, 0x20,
     0x61, 0x6e, 0x20, 0x69, 0x6d, 0x70, 0x6c, 0x65};
  unsigned char ivec[] =
    {0x6d, 0x65, 0x6e, 0x74, 0x61, 0x74, 0x69, 0x6f,
     0x6e, 0x20, 0x6f, 0x66, 0x20, 0x49, 0x47, 0x45,
     0x20, 0x6d, 0x6f, 0x64, 0x65, 0x20, 0x66, 0x6f,
     0x72, 0x20, 0x4f, 0x70, 0x65, 0x6e, 0x53, 0x53};
  unsigned char in[] =
    {0x99, 0x70, 0x64, 0x87, 0xa1, 0xcd, 0xe6, 0x13,
     0xbc, 0x6d, 0xe0, 0xb6, 0xf2, 0x4b, 0x1c, 0x7a,
     0xa4, 0x48, 0xc8, 0xb9, 0xc3, 0x40, 0x3e, 0x34,
     0x67, 0xa8, 0xca, 0xd8, 0x93, 0x40, 0xf5, 0x3b};
  unsigned char out[] =
    {0x4c, 0x2e, 0x20, 0x4c, 0x65, 0x74, 0x27, 0x73,
     0x20, 0x68, 0x6f, 0x70, 0x65, 0x20, 0x42, 0x65,
     0x6e, 0x20, 0x67, 0x6f, 0x74, 0x20, 0x69, 0x74,
     0x20, 0x72, 0x69, 0x67, 0x68, 0x74, 0x21, 0x0a};
  assert (sizeof (in) == sizeof (out));
  assert (2 * 16 == sizeof (ivec));
  verify (in, out, sizeof (in), key, sizeof (key), ivec);
}

int main () {
  test_vect_1 ();
  test_vect_2 ();
  printf("Had %d failure(s).\n", failures);
  return failures != 0;
}
