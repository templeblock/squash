/* Copyright (c) 2013 The Squash Authors
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *   Evan Nemerson <evan@nemerson.com>
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>

#include <squash/squash.h>
#include <lz4.h>
#include <lz4hc.h>

typedef struct SquashLZ4Options_s {
  SquashOptions base_object;

  bool hc;
} SquashLZ4Options;

SquashStatus squash_plugin_init_codec (SquashCodec* codec, SquashCodecFuncs* funcs);

static void              squash_lz4_options_init    (SquashLZ4Options* options, SquashCodec* codec, SquashDestroyNotify destroy_notify);
static SquashLZ4Options* squash_lz4_options_new     (SquashCodec* codec);
static void              squash_lz4_options_destroy (void* options);
static void              squash_lz4_options_free    (void* options);

static void
squash_lz4_options_init (SquashLZ4Options* options, SquashCodec* codec, SquashDestroyNotify destroy_notify) {
  assert (options != NULL);

  squash_options_init ((SquashOptions*) options, codec, destroy_notify);

  options->hc = false;
}

static SquashLZ4Options*
squash_lz4_options_new (SquashCodec* codec) {
  SquashLZ4Options* options;

  options = (SquashLZ4Options*) malloc (sizeof (SquashLZ4Options));
  squash_lz4_options_init (options, codec, squash_lz4_options_free);

  return options;
}

static void
squash_lz4_options_destroy (void* options) {
  squash_options_destroy ((SquashOptions*) options);
}

static void
squash_lz4_options_free (void* options) {
  squash_lz4_options_destroy ((SquashLZ4Options*) options);
  free (options);
}

static SquashOptions*
squash_lz4_create_options (SquashCodec* codec) {
  return (SquashOptions*) squash_lz4_options_new (codec);
}

static SquashStatus
squash_lz4_parse_option (SquashOptions* options, const char* key, const char* value) {
  SquashLZ4Options* opts = (SquashLZ4Options*) options;
  char* endptr = NULL;

  assert (opts != NULL);

  if (strcasecmp (key, "level") == 0) {
    const int level = (int) strtol (value, &endptr, 0);
    if ( *endptr == '\0' && (level == 1 || level == 9) ) {
      opts->hc = level == 9;
    } else {
      return SQUASH_BAD_VALUE;
    }
  } else {
    return SQUASH_BAD_PARAM;
  }

  return SQUASH_OK;
}

static size_t
squash_lz4_get_max_compressed_size (SquashCodec* codec, size_t uncompressed_length) {
  return LZ4_COMPRESSBOUND(uncompressed_length);
}

static SquashStatus
squash_lz4_decompress_buffer (SquashCodec* codec,
                              uint8_t* decompressed, size_t* decompressed_length,
                              const uint8_t* compressed, size_t compressed_length,
                              SquashOptions* options) {
  int lz4_e = LZ4_decompress_safe ((char*) compressed,
                                   (char*) decompressed,
                                   (int) compressed_length,
                                   (int) *decompressed_length);

  if (lz4_e < 0) {
    return SQUASH_FAILED;
  } else {
    *decompressed_length = (size_t) lz4_e;
    return SQUASH_OK;
  }
}

static SquashStatus
squash_lz4_compress_buffer (SquashCodec* codec,
                            uint8_t* compressed, size_t* compressed_length,
                            const uint8_t* uncompressed, size_t uncompressed_length,
                            SquashOptions* options) {
  if (options == NULL || !((SquashLZ4Options*) options)->hc) {
    *compressed_length = LZ4_compress_limitedOutput ((char*) uncompressed,
                                                     (char*) compressed,
                                                     (int) uncompressed_length,
                                                     (int) *compressed_length);
  } else {
    *compressed_length = LZ4_compressHC_limitedOutput ((char*) uncompressed,
                                                       (char*) compressed,
                                                       (int) uncompressed_length,
                                                       (int) *compressed_length);
  }

  return (*compressed_length == 0) ? SQUASH_BUFFER_FULL : SQUASH_OK;
}

static SquashStatus
squash_lz4_compress_buffer_unsafe (SquashCodec* codec,
                                   uint8_t* compressed, size_t* compressed_length,
                                   const uint8_t* uncompressed, size_t uncompressed_length,
                                   SquashOptions* options) {
  assert (*compressed_length >= LZ4_COMPRESSBOUND(uncompressed_length));

  if (options == NULL || !((SquashLZ4Options*) options)->hc) {
    *compressed_length = LZ4_compress ((char*) uncompressed,
                                       (char*) compressed,
                                       (int) uncompressed_length);
  } else {
    *compressed_length = LZ4_compressHC ((char*) uncompressed,
                                         (char*) compressed,
                                         (int) uncompressed_length);
  }

  return (*compressed_length == 0) ? SQUASH_BUFFER_FULL : SQUASH_OK;
}

SquashStatus
squash_plugin_init_codec (SquashCodec* codec, SquashCodecFuncs* funcs) {
  const char* name = squash_codec_get_name (codec);

  if (strcmp ("lz4", name) == 0) {
    funcs->create_options = squash_lz4_create_options;
    funcs->parse_option = squash_lz4_parse_option;
    funcs->get_max_compressed_size = squash_lz4_get_max_compressed_size;
    funcs->decompress_buffer = squash_lz4_decompress_buffer;
    funcs->compress_buffer = squash_lz4_compress_buffer;
    funcs->compress_buffer_unsafe = squash_lz4_compress_buffer_unsafe;
  } else {
    return SQUASH_UNABLE_TO_LOAD;
  }

  return SQUASH_OK;
}
