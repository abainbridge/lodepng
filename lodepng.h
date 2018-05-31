// LodePNG version 20180326
// 
// Copyright (c) 2005-2018 Lode Vandevenne
// 
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
// 
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
// 
//     1. The origin of this software must not be misrepresented; you must not
//     claim that you wrote the original software. If you use this software
//     in a product, an acknowledgment in the product documentation would be
//     appreciated but is not required.
// 
//     2. Altered source versions must be plainly marked as such, and must not be
//     misrepresented as being the original software.
// 
//     3. This notice may not be removed or altered from any source
//     distribution.

#pragma once

#include <string.h> // for size_t

// The following #defines are used to create code sections. They can be disabled
// to disable code sections, which can give faster compile time and smaller binary.

// ability to convert error numerical codes to English text string
#define LODEPNG_COMPILE_ERROR_TEXT

// The PNG color types (also used for raw).
typedef enum LodePNGColorType
{
  LCT_GREY = 0, // greyscale: 1,2,4,8,16 bit
  LCT_RGB = 2, // RGB: 8,16 bit
  LCT_PALETTE = 3, // palette: 1,2,4,8 bit
  LCT_GREY_ALPHA = 4, // greyscale with alpha: 8,16 bit
  LCT_RGBA = 6 // RGB with alpha: 8,16 bit
} LodePNGColorType;

// Converts PNG data in memory to raw pixel data.
// out: Output parameter. Pointer to buffer that will contain the raw pixel data.
//      After decoding, its size is w * h * (bytes per pixel) bytes larger than
//      initially. Bytes per pixel depends on colortype and bitdepth.
//      Must be freed after usage with free(*out).
//      Note: for 16-bit per channel colors, uses big endian format like PNG does.
// w: Output parameter. Pointer to width of pixel data.
// h: Output parameter. Pointer to height of pixel data.
// in: Memory buffer with the PNG file.
// insize: size of the in buffer.
// colortype: the desired color type for the raw output image. See explanation on PNG color types.
// bitdepth: the desired bit depth for the raw output image. See explanation on PNG color types.
// Return value: LodePNG error code (0 means no error).
unsigned lodepng_decode_memory(unsigned char** out, unsigned* w, unsigned* h,
                               const unsigned char* in, size_t insize,
                               LodePNGColorType colortype, unsigned bitdepth);

// Load PNG from disk, from file with given name.
// Same as the other decode functions, but instead takes a filename as input.
unsigned lodepng_decode_file(unsigned char** out, unsigned* w, unsigned* h,
                             const char* filename,
                             LodePNGColorType colortype, unsigned bitdepth);

// Same as lodepng_decode_file, but always decodes to 32-bit RGBA raw image.
unsigned lodepng_decode32_file(unsigned char** out, unsigned* w, unsigned* h,
                               const char* filename);



// Converts raw pixel data into a PNG image in memory. The colortype and bitdepth
//   of the output PNG image cannot be chosen, they are automatically determined
//   by the colortype, bitdepth and content of the input pixel data.
//   Note: for 16-bit per channel colors, needs big endian format like PNG does.
// out: Output parameter. Pointer to buffer that will contain the PNG image data.
//      Must be freed after usage with free(*out).
// outsize: Output parameter. Pointer to the size in bytes of the out buffer.
// image: The raw pixel data to encode. The size of this buffer should be
//        w * h * (bytes per pixel), bytes per pixel depends on colortype and bitdepth.
// w: width of the raw pixel data in pixels.
// h: height of the raw pixel data in pixels.
// colortype: the color type of the raw input image. See explanation on PNG color types.
// bitdepth: the bit depth of the raw input image. See explanation on PNG color types.
// Return value: LodePNG error code (0 means no error).
unsigned lodepng_encode_memory(unsigned char** out, size_t* outsize,
                               const unsigned char* image, unsigned w, unsigned h,
                               LodePNGColorType colortype, unsigned bitdepth);

// Same as lodepng_encode_memory, but always encodes from 32-bit RGBA raw image.
unsigned lodepng_encode32(unsigned char** out, size_t* outsize,
                          const unsigned char* image, unsigned w, unsigned h);

// Converts raw pixel data into a PNG file on disk.
// Same as the other encode functions, but instead takes a filename as output.
// NOTE: This overwrites existing files without warning!
unsigned lodepng_encode_file(const char* filename,
                             const unsigned char* image, unsigned w, unsigned h,
                             LodePNGColorType colortype, unsigned bitdepth);

// Same as lodepng_encode_file, but always encodes from 32-bit RGBA raw image.
unsigned lodepng_encode32_file(const char* filename,
                               const unsigned char* image, unsigned w, unsigned h);


#ifdef LODEPNG_COMPILE_ERROR_TEXT
// Returns an English description of the numerical error code.
const char* lodepng_error_text(unsigned code);
#endif // LODEPNG_COMPILE_ERROR_TEXT

// Settings for zlib decompression
typedef struct LodePNGDecompressSettings LodePNGDecompressSettings;
struct LodePNGDecompressSettings
{
  //  Check LodePNGDecoderSettings for more ignorable errors 
  unsigned ignore_adler32; // if 1, continue and don't give an error message if the Adler32 checksum is corrupted

  // use custom zlib decoder instead of built in one (default: null)
  unsigned (*custom_zlib)(unsigned char**, size_t*,
                          const unsigned char*, size_t,
                          const LodePNGDecompressSettings*);
  /*use custom deflate decoder instead of built in one (default: null)
  if custom_zlib is used, custom_deflate is ignored since only the built in
  zlib function will call custom_deflate*/
  unsigned (*custom_inflate)(unsigned char**, size_t*,
                             const unsigned char*, size_t,
                             const LodePNGDecompressSettings*);

  const void* custom_context; // optional custom settings for custom functions
};

extern const LodePNGDecompressSettings lodepng_default_decompress_settings;
void lodepng_decompress_settings_init(LodePNGDecompressSettings* settings);


// Settings for zlib compression. Tweaking these settings tweaks the balance
// between speed and compression ratio.
typedef struct LodePNGCompressSettings LodePNGCompressSettings;
struct LodePNGCompressSettings // deflate = compress
{
  // LZ77 related settings
  unsigned btype; // the block type for LZ (0, 1, 2 or 3, see zlib standard). Should be 2 for proper compression.
  unsigned use_lz77; // whether or not to use LZ77. Should be 1 for proper compression.
  unsigned windowsize; // must be a power of two <= 32768. higher compresses more but is slower. Default value: 2048.
  unsigned minmatch; // minimum lz77 length. 3 is normally best, 6 can be better for some PNGs. Default: 0
  unsigned nicematch; // stop searching if >= this length found. Set to 258 for best compression. Default: 128
  unsigned lazymatching; // use lazy matching: better compression but a bit slower. Default: true

  // use custom zlib encoder instead of built in one (default: null)
  unsigned (*custom_zlib)(unsigned char**, size_t*,
                          const unsigned char*, size_t,
                          const LodePNGCompressSettings*);
  /*use custom deflate encoder instead of built in one (default: null)
  if custom_zlib is used, custom_deflate is ignored since only the built in
  zlib function will call custom_deflate*/
  unsigned (*custom_deflate)(unsigned char**, size_t*,
                             const unsigned char*, size_t,
                             const LodePNGCompressSettings*);

  const void* custom_context; // optional custom settings for custom functions
};

extern const LodePNGCompressSettings lodepng_default_compress_settings;
void lodepng_compress_settings_init(LodePNGCompressSettings* settings);

// Color mode of an image. Contains all information required to decode the pixel
// bits to RGBA colors. This information is the same as used in the PNG file
// format, and is used both for PNG and raw image data in LodePNG.
typedef struct LodePNGColorMode
{
  // header (IHDR)
  LodePNGColorType colortype; // color type, see PNG standard or documentation further in this header file
  unsigned bitdepth;  // bits per sample, see PNG standard or documentation further in this header file

  /*
  palette (PLTE and tRNS)

  Dynamically allocated with the colors of the palette, including alpha.
  When encoding a PNG, to store your colors in the palette of the LodePNGColorMode, first use
  lodepng_palette_clear, then for each color use lodepng_palette_add.
  If you encode an image without alpha with palette, don't forget to put value 255 in each A byte of the palette.

  When decoding, by default you can ignore this palette, since LodePNG already
  fills the palette colors in the pixels of the raw RGBA output.

  The palette is only supported for color type 3.
  */
  unsigned char* palette; // palette in RGBARGBA... order. When allocated, must be either 0, or have size 1024
  size_t palettesize; // palette size in number of colors (amount of bytes is 4 * palettesize)
  
  /*
  transparent color key (tRNS)

  This color uses the same bit depth as the bitdepth value in this struct, which can be 1-bit to 16-bit.
  For greyscale PNGs, r, g and b will all 3 be set to the same.

  When decoding, by default you can ignore this information, since LodePNG sets
  pixels with this key to transparent already in the raw RGBA output.

  The color key is only supported for color types 0 and 2.
  */
  unsigned key_defined; // is a transparent color key given? 0 = false, 1 = true
  unsigned key_r;       // red/greyscale component of color key
  unsigned key_g;       // green component of color key
  unsigned key_b;       // blue component of color key
} LodePNGColorMode;

// init, cleanup and copy functions to use with this struct
void lodepng_color_mode_init(LodePNGColorMode* info);
void lodepng_color_mode_cleanup(LodePNGColorMode* info);
// return value is error code (0 means no error)
unsigned lodepng_color_mode_copy(LodePNGColorMode* dest, const LodePNGColorMode* source);

void lodepng_palette_clear(LodePNGColorMode* info);
// add 1 color to the palette
unsigned lodepng_palette_add(LodePNGColorMode* info,
                             unsigned char r, unsigned char g, unsigned char b, unsigned char a);

// get the total amount of bits per pixel, based on colortype and bitdepth in the struct
unsigned lodepng_get_bpp(const LodePNGColorMode* info);
// get the amount of color channels used, based on colortype in the struct.
// If a palette is used, it counts as 1 channel.
unsigned lodepng_get_channels(const LodePNGColorMode* info);
// is it a greyscale type? (only colortype 0 or 4)
unsigned lodepng_is_greyscale_type(const LodePNGColorMode* info);
// has it got an alpha channel? (only colortype 2 or 6)
unsigned lodepng_is_alpha_type(const LodePNGColorMode* info);
// has it got a palette? (only colortype 3)
unsigned lodepng_is_palette_type(const LodePNGColorMode* info);
// only returns true if there is a palette and there is a value in the palette with alpha < 255.
// Loops through the palette to check this.
unsigned lodepng_has_palette_alpha(const LodePNGColorMode* info);
// Check if the given color info indicates the possibility of having non-opaque pixels in the PNG image.
// Returns true if the image can have translucent or invisible pixels (it still be opaque if it doesn't use such pixels).
// Returns false if the image can only have opaque pixels.
// In detail, it returns true only if it's a color type with alpha, or has a palette with non-opaque values,
// or if "key_defined" is true.
unsigned lodepng_can_have_alpha(const LodePNGColorMode* info);
// Returns the byte size of a raw image buffer with given width, height and color mode
size_t lodepng_get_raw_size(unsigned w, unsigned h, const LodePNGColorMode* color);


// Information about the PNG image, except pixels, width and height.
typedef struct LodePNGInfo
{
  // header (IHDR), palette (PLTE) and transparency (tRNS) chunks
  unsigned compression_method;// compression method of the original file. Always 0.
  unsigned filter_method;     // filter method of the original file
  unsigned interlace_method;  // interlace method of the original file
  LodePNGColorMode color;     // color type and bits, palette and transparency of the PNG file
} LodePNGInfo;

// init, cleanup and copy functions to use with this struct
void lodepng_info_init(LodePNGInfo* info);
void lodepng_info_cleanup(LodePNGInfo* info);
// return value is error code (0 means no error)
unsigned lodepng_info_copy(LodePNGInfo* dest, const LodePNGInfo* source);

// Converts raw buffer from one color type to another color type, based on
// LodePNGColorMode structs to describe the input and output color type.
// See the reference manual at the end of this header file to see which color conversions are supported.
// return value = LodePNG error code (0 if all went ok, an error if the conversion isn't supported)
// The out buffer must have size (w * h * bpp + 7) / 8, where bpp is the bits per pixel
// of the output color type (lodepng_get_bpp).
// For < 8 bpp images, there should not be padding bits at the end of scanlines.
// For 16-bit per channel colors, uses big endian format like PNG does.
// Return value is LodePNG error code
unsigned lodepng_convert(unsigned char* out, const unsigned char* in,
                         const LodePNGColorMode* mode_out, const LodePNGColorMode* mode_in,
                         unsigned w, unsigned h);

// Settings for the decoder. This contains settings for the PNG and the Zlib
// decoder, but not the Info settings from the Info structs.
typedef struct LodePNGDecoderSettings
{
  LodePNGDecompressSettings zlibsettings; // in here is the setting to ignore Adler32 checksums

  //  Check LodePNGDecompressSettings for more ignorable errors 
  unsigned ignore_crc; // ignore CRC checksums
  unsigned ignore_critical; // ignore unknown critical chunks
  unsigned ignore_end; // ignore issues at end of file if possible (missing IEND chunk, too large chunk, ...)

  unsigned color_convert; // whether to convert the PNG to the color type you want. Default: yes
} LodePNGDecoderSettings;

void lodepng_decoder_settings_init(LodePNGDecoderSettings* settings);

// automatically use color type with less bits per pixel if losslessly possible. Default: AUTO
typedef enum LodePNGFilterStrategy
{
  // every filter at zero
  LFS_ZERO,
  // Use filter that gives minimum sum, as described in the official PNG filter heuristic.
  LFS_MINSUM,
  /*Use the filter type that gives smallest Shannon entropy for this scanline. Depending
  on the image, this is better or worse than minsum.*/
  LFS_ENTROPY,
  /*
  Brute-force-search PNG filters by compressing each filter for each scanline.
  Experimental, very slow, and only rarely gives better compression than MINSUM.
  */
  LFS_BRUTE_FORCE,
  // use predefined_filters buffer: you specify the filter type for each scanline
  LFS_PREDEFINED
} LodePNGFilterStrategy;

/*Gives characteristics about the colors of the image, which helps decide which color model to use for encoding.
Used internally by default if "auto_convert" is enabled. Public because it's useful for custom algorithms.*/
typedef struct LodePNGColorProfile
{
  unsigned colored; // not greyscale
  unsigned key; // image is not opaque and color key is possible instead of full alpha
  unsigned short key_r; // key values, always as 16-bit, in 8-bit case the byte is duplicated, e.g. 65535 means 255
  unsigned short key_g;
  unsigned short key_b;
  unsigned alpha; // image is not opaque and alpha channel or alpha palette required
  unsigned numcolors; // amount of colors, up to 257. Not valid if bits == 16.
  unsigned char palette[1024]; // Remembers up to the first 256 RGBA colors, in no particular order
  unsigned bits; // bits per channel (not for palette). 1,2 or 4 for greyscale only. 16 if 16-bit per channel required.
} LodePNGColorProfile;

void lodepng_color_profile_init(LodePNGColorProfile* profile);

// Get a LodePNGColorProfile of the image.
unsigned lodepng_get_color_profile(LodePNGColorProfile* profile,
                                   const unsigned char* image, unsigned w, unsigned h,
                                   const LodePNGColorMode* mode_in);
/*The function LodePNG uses internally to decide the PNG color with auto_convert.
Chooses an optimal color model, e.g. grey if only grey pixels, palette if < 256 colors, ...*/
unsigned lodepng_auto_choose_color(LodePNGColorMode* mode_out,
                                   const unsigned char* image, unsigned w, unsigned h,
                                   const LodePNGColorMode* mode_in);

// Settings for the encoder.
typedef struct LodePNGEncoderSettings
{
  LodePNGCompressSettings zlibsettings; // settings for the zlib encoder, such as window size, ...

  unsigned auto_convert; // automatically choose output PNG color type. Default: true

  /*If true, follows the official PNG heuristic: if the PNG uses a palette or lower than
  8 bit depth, set all filters to zero. Otherwise use the filter_strategy. Note that to
  completely follow the official PNG heuristic, filter_palette_zero must be true and
  filter_strategy must be LFS_MINSUM*/
  unsigned filter_palette_zero;
  /*Which filter strategy to use when not using zeroes due to filter_palette_zero.
  Set filter_palette_zero to 0 to ensure always using your chosen strategy. Default: LFS_MINSUM*/
  LodePNGFilterStrategy filter_strategy;
  /*used if filter_strategy is LFS_PREDEFINED. In that case, this must point to a buffer with
  the same length as the amount of scanlines in the image, and each value must <= 5. You
  have to cleanup this buffer, LodePNG will never free it. Don't forget that filter_palette_zero
  must be set to 0 to ensure this is also used on palette or low bitdepth images.*/
  const unsigned char* predefined_filters;

  /*force creating a PLTE chunk if colortype is 2 or 6 (= a suggested palette).
  If colortype is 3, PLTE is _always_ created.*/
  unsigned force_palette;
} LodePNGEncoderSettings;

void lodepng_encoder_settings_init(LodePNGEncoderSettings* settings);


// The settings, state and information for extended encoding and decoding.
typedef struct LodePNGState
{
  LodePNGDecoderSettings decoder; // the decoding settings
  LodePNGEncoderSettings encoder; // the encoding settings
  LodePNGColorMode info_raw; // specifies the format in which you would like to get the raw pixel buffer
  LodePNGInfo info_png; // info of the PNG image obtained after decoding
  unsigned error;
} LodePNGState;

// init, cleanup and copy functions to use with this struct
void lodepng_state_init(LodePNGState* state);
void lodepng_state_cleanup(LodePNGState* state);
void lodepng_state_copy(LodePNGState* dest, const LodePNGState* source);

// Same as lodepng_decode_memory, but uses a LodePNGState to allow custom settings and
// getting much more information about the PNG image and color mode.
unsigned lodepng_decode(unsigned char** out, unsigned* w, unsigned* h,
                        LodePNGState* state,
                        const unsigned char* in, size_t insize);

// Read the PNG header, but not the actual data. This returns only the information
// that is in the header chunk of the PNG, such as width, height and color type. The
// information is placed in the info_png field of the LodePNGState.
unsigned lodepng_inspect(unsigned* w, unsigned* h,
                         LodePNGState* state,
                         const unsigned char* in, size_t insize);


// This function allocates the out buffer with standard malloc and stores the size in *outsize.
unsigned lodepng_encode(unsigned char** out, size_t* outsize,
                        const unsigned char* image, unsigned w, unsigned h,
                        LodePNGState* state);

// The lodepng_chunk functions are normally not needed, except to traverse the
// unknown chunks stored in the LodePNGInfo struct, or add new ones to it.
// It also allows traversing the chunks of an encoded PNG file yourself.
// 
// PNG standard chunk naming conventions:
// First byte: uppercase = critical, lowercase = ancillary
// Second byte: uppercase = public, lowercase = private
// Third byte: must be uppercase
// Fourth byte: uppercase = unsafe to copy, lowercase = safe to copy

// Gets the length of the data of the chunk. Total chunk length has 12 bytes more.
// There must be at least 4 bytes to read from. If the result value is too large,
// it may be corrupt data.
unsigned lodepng_chunk_length(const unsigned char* chunk);

// puts the 4-byte type in null terminated string
void lodepng_chunk_type(char type[5], const unsigned char* chunk);

// check if the type is the given type
unsigned char lodepng_chunk_type_equals(const unsigned char* chunk, const char* type);

// 0: it's one of the critical chunk types, 1: it's an ancillary chunk (see PNG standard)
unsigned char lodepng_chunk_ancillary(const unsigned char* chunk);

// 0: public, 1: private (see PNG standard)
unsigned char lodepng_chunk_private(const unsigned char* chunk);

// 0: the chunk is unsafe to copy, 1: the chunk is safe to copy (see PNG standard)
unsigned char lodepng_chunk_safetocopy(const unsigned char* chunk);

// get pointer to the data of the chunk, where the input points to the header of the chunk
unsigned char* lodepng_chunk_data(unsigned char* chunk);
const unsigned char* lodepng_chunk_data_const(const unsigned char* chunk);

// returns 0 if the crc is correct, 1 if it's incorrect (0 for OK as usual!)
unsigned lodepng_chunk_check_crc(const unsigned char* chunk);

// generates the correct CRC from the data and puts it in the last 4 bytes of the chunk
void lodepng_chunk_generate_crc(unsigned char* chunk);

// iterate to next chunks. don't use on IEND chunk, as there is no next chunk then
unsigned char* lodepng_chunk_next(unsigned char* chunk);
const unsigned char* lodepng_chunk_next_const(const unsigned char* chunk);

// Appends chunk to the data in out. The given chunk should already have its chunk header.
// The out variable and outlength are updated to reflect the new reallocated buffer.
// Returns error code (0 if it went ok)
unsigned lodepng_chunk_append(unsigned char** out, size_t* outlength, const unsigned char* chunk);

// Appends new chunk to out. The chunk to append is given by giving its length, type
// and data separately. The type is a 4-letter string.
// The out variable and outlength are updated to reflect the new reallocated buffer.
// Return error code (0 if it went ok)
unsigned lodepng_chunk_create(unsigned char** out, size_t* outlength, unsigned length,
                              const char* type, const unsigned char* data);


// Calculate CRC32 of buffer
unsigned lodepng_crc32(const unsigned char* buf, size_t len);


// This zlib part can be used independently to zlib compress and decompress a
// buffer. It cannot be used to create gzip files however, and it only supports the
// part of zlib that is required for PNG, it does not support dictionaries.

// Inflate a buffer. Inflate is the decompression step of deflate. Out buffer must be freed after use.
unsigned lodepng_inflate(unsigned char** out, size_t* outsize,
                         const unsigned char* in, size_t insize,
                         const LodePNGDecompressSettings* settings);

// Decompresses Zlib data. Reallocates the out buffer and appends the data. The
// data must be according to the zlib specification.
// Either, *out must be NULL and *outsize must be 0, or, *out must be a valid
// buffer and *outsize its size in bytes. out must be freed by user after usage.
unsigned lodepng_zlib_decompress(unsigned char** out, size_t* outsize,
                                 const unsigned char* in, size_t insize,
                                 const LodePNGDecompressSettings* settings);

// Compresses data with Zlib. Reallocates the out buffer and appends the data.
// Zlib adds a small header and trailer around the deflate data.
// The data is output in the format of the zlib specification.
// Either, *out must be NULL and *outsize must be 0, or, *out must be a valid
// buffer and *outsize its size in bytes. out must be freed by user after usage.
unsigned lodepng_zlib_compress(unsigned char** out, size_t* outsize,
                               const unsigned char* in, size_t insize,
                               const LodePNGCompressSettings* settings);

// Find length-limited Huffman code for given frequencies. This function is in the
// public interface only for tests, it's used internally by lodepng_deflate.
unsigned lodepng_huffman_code_lengths(unsigned* lengths, const unsigned* frequencies,
                                      size_t numcodes, unsigned maxbitlen);

// Compress a buffer with deflate. See RFC 1951. Out buffer must be freed after use.
unsigned lodepng_deflate(unsigned char** out, size_t* outsize,
                         const unsigned char* in, size_t insize,
                         const LodePNGCompressSettings* settings);


// Load a file from disk into buffer. The function allocates the out buffer, and
// after usage you should free it.
// out: output parameter, contains pointer to loaded buffer.
// outsize: output parameter, size of the allocated out buffer
// filename: the path to the file to load
// return value: error code (0 means ok)
unsigned lodepng_load_file(unsigned char** out, size_t* outsize, const char* filename);

// Save a file from buffer to disk. Warning, if it exists, this function overwrites
// the file without warning!
// buffer: the buffer to write
// buffersize: size of the buffer to write
// filename: the path to the file to save to
// return value: error code (0 means ok)
unsigned lodepng_save_file(const unsigned char* buffer, size_t buffersize, const char* filename);


/*
LodePNG Documentation
---------------------

0. table of contents
--------------------

  1. about
   1.1. supported features
   1.2. features not supported
  2. C and C++ version
  3. security
  4. decoding
  5. encoding
  6. color conversions
    6.1. PNG color types
    6.2. color conversions
    6.3. padding bits
    6.4. A note about 16-bits per channel and endianness
  7. error values
  8. chunks and PNG editing
  9. compiler support
  10. examples
   10.1. decoder C++ example
   10.2. decoder C example
  11. state settings reference
  12. changes
  13. contact information


1. about
--------

LodePNG is simple but only supports the basic requirements. To achieve
simplicity, the following design choices were made: There are no dependencies
on any external library. There are functions to decode and encode a PNG with
a single function call, and extended versions of these functions taking a
LodePNGState struct allowing to specify or get more information. By default
the colors of the raw image are always RGB or RGBA, no matter what color type
the PNG file uses. To read and write files, there are simple functions to
convert the files to/from buffers in memory.

This all makes LodePNG suitable for loading textures in games, demos and small
programs, ... It's less suitable for full fledged image editors, loading PNGs
over network (it requires all the image data to be available before decoding can
begin), life-critical systems, ...

1.1. supported features
-----------------------

The following features are supported by the decoder:

*) decoding of PNGs with any color type, bit depth and interlace mode, to a 24- or 32-bit color raw image,
   or the same color type as the PNG
*) encoding of PNGs, from any raw image to 24- or 32-bit color, or the same color type as the raw image
*) Adam7 interlace and deinterlace for any color type
*) loading the image from harddisk or decoding it from a buffer from other sources than harddisk
*) support for alpha channels, including RGBA color model, translucent palettes and color keying
*) zlib decompression (inflate)
*) zlib compression (deflate)
*) CRC32 and ADLER32 checksums
*) handling of unknown chunks, allowing making a PNG editor that stores custom and unknown chunks.
*) the following chunks are supported (generated/interpreted) by both encoder and decoder:
    IHDR: header information
    PLTE: color palette
    IDAT: pixel data
    IEND: the final chunk
    tRNS: transparency for palettized images
    tEXt: textual information
    zTXt: compressed textual information
    iTXt: international textual information
    bKGD: suggested background color
    pHYs: physical dimensions
    tIME: modification time

1.2. features not supported
---------------------------

The following features are _not_ supported:

*) some features needed to make a conformant PNG-Editor might be still missing.
*) partial loading/stream processing. All data must be available and is processed in one call.
*) The following public chunks are not supported but treated as unknown chunks by LodePNG
    cHRM, gAMA, iCCP, sRGB, sBIT, hIST, sPLT
   Some of these are not supported on purpose: LodePNG wants to provide the RGB values
   stored in the pixels, not values modified by system dependent gamma or color models.

   
3. Security
-----------

When using LodePNG, care has to be taken with the C version of LodePNG, as well
as the C-style structs when working with C++. The following conventions are used
for all C-style structs:

-if a struct has a corresponding init function, always call the init function when making a new one
-if a struct has a corresponding cleanup function, call it before the struct disappears to avoid memory leaks
-if a struct has a corresponding copy function, use the copy function instead of "=".
 The destination must also be inited already.


4. Decoding
-----------

Most documentation on using the decoder is at its declarations in the header
above. For C, simple decoding can be done with functions such as
lodepng_decode32, and more advanced decoding can be done with the struct
LodePNGState and lodepng_decode. For C++, all decoding can be done with the
various lodepng::decode functions, and lodepng::State can be used for advanced
features.

When using the LodePNGState, it uses the following fields for decoding:
*) LodePNGInfo info_png: it stores extra information about the PNG (the input) in here
*) LodePNGColorMode info_raw: here you can say what color mode of the raw image (the output) you want to get
*) LodePNGDecoderSettings decoder: you can specify a few extra settings for the decoder to use

LodePNGInfo info_png
--------------------

After decoding, this contains extra information of the PNG image, except the actual
pixels, width and height because these are already gotten directly from the decoder
functions.

It contains for example the original color type of the PNG image, text comments,
suggested background color, etc... More details about the LodePNGInfo struct are
at its declaration documentation.

LodePNGColorMode info_raw
-------------------------

When decoding, here you can specify which color type you want
the resulting raw image to be. If this is different from the colortype of the
PNG, then the decoder will automatically convert the result. This conversion
always works, except if you want it to convert a color PNG to greyscale or to
a palette with missing colors.

By default, 32-bit color is used for the result.

LodePNGDecoderSettings decoder
------------------------------

The settings can be used to ignore the errors created by invalid CRC and Adler32
chunks, and to disable the decoding of tEXt chunks.

There's also a setting color_convert, true by default. If false, no conversion
is done, the resulting data will be as it was in the PNG (after decompression)
and you'll have to puzzle the colors of the pixels together yourself using the
color type information in the LodePNGInfo.


5. Encoding
-----------

Encoding converts a raw pixel buffer to a PNG compressed image.

Most documentation on using the encoder is at its declarations in the header
above. For C, simple encoding can be done with functions such as
lodepng_encode32, and more advanced decoding can be done with the struct
LodePNGState and lodepng_encode. For C++, all encoding can be done with the
various lodepng::encode functions, and lodepng::State can be used for advanced
features.

Like the decoder, the encoder can also give errors. However it gives less errors
since the encoder input is trusted, the decoder input (a PNG image that could
be forged by anyone) is not trusted.

When using the LodePNGState, it uses the following fields for encoding:
*) LodePNGInfo info_png: here you specify how you want the PNG (the output) to be.
*) LodePNGColorMode info_raw: here you say what color type of the raw image (the input) has
*) LodePNGEncoderSettings encoder: you can specify a few settings for the encoder to use

LodePNGInfo info_png
--------------------

When encoding, you use this the opposite way as when decoding: for encoding,
you fill in the values you want the PNG to have before encoding. By default it's
not needed to specify a color type for the PNG since it's automatically chosen,
but it's possible to choose it yourself given the right settings.

The encoder will not always exactly match the LodePNGInfo struct you give,
it tries as close as possible. Some things are ignored by the encoder. The
encoder uses, for example, the following settings from it when applicable:
colortype and bitdepth, text chunks, time chunk, the color key, the palette, the
background color, the interlace method, unknown chunks, ...

When encoding to a PNG with colortype 3, the encoder will generate a PLTE chunk.
If the palette contains any colors for which the alpha channel is not 255 (so
there are translucent colors in the palette), it'll add a tRNS chunk.

LodePNGColorMode info_raw
-------------------------

You specify the color type of the raw image that you give to the input here,
including a possible transparent color key and palette you happen to be using in
your raw image data.

By default, 32-bit color is assumed, meaning your input has to be in RGBA
format with 4 bytes (unsigned chars) per pixel.

LodePNGEncoderSettings encoder
------------------------------

The following settings are supported (some are in sub-structs):
*) auto_convert: when this option is enabled, the encoder will
automatically choose the smallest possible color mode (including color key) that
can encode the colors of all pixels without information loss.
*) btype: the block type for LZ77. 0 = uncompressed, 1 = fixed huffman tree,
   2 = dynamic huffman tree (best compression). Should be 2 for proper
   compression.
*) use_lz77: whether or not to use LZ77 for compressed block types. Should be
   true for proper compression.
*) windowsize: the window size used by the LZ77 encoder (1 - 32768). Has value
   2048 by default, but can be set to 32768 for better, but slow, compression.
*) force_palette: if colortype is 2 or 6, you can make the encoder write a PLTE
   chunk if force_palette is true. This can used as suggested palette to convert
   to by viewers that don't support more than 256 colors (if those still exist)
*) add_id: add text chunk "Encoder: LodePNG <version>" to the image.
*) text_compression: default 1. If 1, it'll store texts as zTXt instead of tEXt chunks.
  zTXt chunks use zlib compression on the text. This gives a smaller result on
  large texts but a larger result on small texts (such as a single program name).
  It's all tEXt or all zTXt though, there's no separate setting per text yet.


6. color conversions
--------------------

An important thing to note about LodePNG, is that the color type of the PNG, and
the color type of the raw image, are completely independent. By default, when
you decode a PNG, you get the result as a raw image in the color type you want,
no matter whether the PNG was encoded with a palette, greyscale or RGBA color.
And if you encode an image, by default LodePNG will automatically choose the PNG
color type that gives good compression based on the values of colors and amount
of colors in the image. It can be configured to let you control it instead as
well, though.

To be able to do this, LodePNG does conversions from one color mode to another.
It can convert from almost any color type to any other color type, except the
following conversions: RGB to greyscale is not supported, and converting to a
palette when the palette doesn't have a required color is not supported. This is
not supported on purpose: this is information loss which requires a color
reduction algorithm that is beyong the scope of a PNG encoder (yes, RGB to grey
is easy, but there are multiple ways if you want to give some channels more
weight).

By default, when decoding, you get the raw image in 32-bit RGBA or 24-bit RGB
color, no matter what color type the PNG has. And by default when encoding,
LodePNG automatically picks the best color model for the output PNG, and expects
the input image to be 32-bit RGBA or 24-bit RGB. So, unless you want to control
the color format of the images yourself, you can skip this chapter.

6.1. PNG color types
--------------------

A PNG image can have many color types, ranging from 1-bit color to 64-bit color,
as well as palettized color modes. After the zlib decompression and unfiltering
in the PNG image is done, the raw pixel data will have that color type and thus
a certain amount of bits per pixel. If you want the output raw image after
decoding to have another color type, a conversion is done by LodePNG.

The PNG specification gives the following color types:

0: greyscale, bit depths 1, 2, 4, 8, 16
2: RGB, bit depths 8 and 16
3: palette, bit depths 1, 2, 4 and 8
4: greyscale with alpha, bit depths 8 and 16
6: RGBA, bit depths 8 and 16

Bit depth is the amount of bits per pixel per color channel. So the total amount
of bits per pixel is: amount of channels * bitdepth.

6.2. color conversions
----------------------

As explained in the sections about the encoder and decoder, you can specify
color types and bit depths in info_png and info_raw to change the default
behaviour.

If, when decoding, you want the raw image to be something else than the default,
you need to set the color type and bit depth you want in the LodePNGColorMode,
or the parameters colortype and bitdepth of the simple decoding function.

If, when encoding, you use another color type than the default in the raw input
image, you need to specify its color type and bit depth in the LodePNGColorMode
of the raw image, or use the parameters colortype and bitdepth of the simple
encoding function.

If, when encoding, you don't want LodePNG to choose the output PNG color type
but control it yourself, you need to set auto_convert in the encoder settings
to false, and specify the color type you want in the LodePNGInfo of the
encoder (including palette: it can generate a palette if auto_convert is true,
otherwise not).

If the input and output color type differ (whether user chosen or auto chosen),
LodePNG will do a color conversion, which follows the rules below, and may
sometimes result in an error.

To avoid some confusion:
-the decoder converts from PNG to raw image
-the encoder converts from raw image to PNG
-the colortype and bitdepth in LodePNGColorMode info_raw, are those of the raw image
-the colortype and bitdepth in the color field of LodePNGInfo info_png, are those of the PNG
-when encoding, the color type in LodePNGInfo is ignored if auto_convert
 is enabled, it is automatically generated instead
-when decoding, the color type in LodePNGInfo is set by the decoder to that of the original
 PNG image, but it can be ignored since the raw image has the color type you requested instead
-if the color type of the LodePNGColorMode and PNG image aren't the same, a conversion
 between the color types is done if the color types are supported. If it is not
 supported, an error is returned. If the types are the same, no conversion is done.
-even though some conversions aren't supported, LodePNG supports loading PNGs from any
 colortype and saving PNGs to any colortype, sometimes it just requires preparing
 the raw image correctly before encoding.
-both encoder and decoder use the same color converter.

Non supported color conversions:
-color to greyscale: no error is thrown, but the result will look ugly because
only the red channel is taken
-anything to palette when that palette does not have that color in it: in this
case an error is thrown

Supported color conversions:
-anything to 8-bit RGB, 8-bit RGBA, 16-bit RGB, 16-bit RGBA
-any grey or grey+alpha, to grey or grey+alpha
-anything to a palette, as long as the palette has the requested colors in it
-removing alpha channel
-higher to smaller bitdepth, and vice versa

If you want no color conversion to be done (e.g. for speed or control):
-In the encoder, you can make it save a PNG with any color type by giving the
raw color mode and LodePNGInfo the same color mode, and setting auto_convert to
false.
-In the decoder, you can make it store the pixel data in the same color type
as the PNG has, by setting the color_convert setting to false. Settings in
info_raw are then ignored.

The function lodepng_convert does the color conversion. It is available in the
interface but normally isn't needed since the encoder and decoder already call
it.

6.3. padding bits
-----------------

In the PNG file format, if a less than 8-bit per pixel color type is used and the scanlines
have a bit amount that isn't a multiple of 8, then padding bits are used so that each
scanline starts at a fresh byte. But that is NOT true for the LodePNG raw input and output.
The raw input image you give to the encoder, and the raw output image you get from the decoder
will NOT have these padding bits, e.g. in the case of a 1-bit image with a width
of 7 pixels, the first pixel of the second scanline will the the 8th bit of the first byte,
not the first bit of a new byte.

6.4. A note about 16-bits per channel and endianness
----------------------------------------------------

LodePNG uses unsigned char arrays for 16-bit per channel colors too, just like
for any other color format. The 16-bit values are stored in big endian (most
significant byte first) in these arrays. This is the opposite order of the
little endian used by x86 CPU's.

LodePNG always uses big endian because the PNG file format does so internally.
Conversions to other formats than PNG uses internally are not supported by
LodePNG on purpose, there are myriads of formats, including endianness of 16-bit
colors, the order in which you store R, G, B and A, and so on. Supporting and
converting to/from all that is outside the scope of LodePNG.

This may mean that, depending on your use case, you may want to convert the big
endian output of LodePNG to little endian with a for loop. This is certainly not
always needed, many applications and libraries support big endian 16-bit colors
anyway, but it means you cannot simply cast the unsigned char* buffer to an
unsigned short* buffer on x86 CPUs.


7. error values
---------------

All functions in LodePNG that return an error code, return 0 if everything went
OK, or a non-zero code if there was an error.

The meaning of the LodePNG error values can be retrieved with the function
lodepng_error_text: given the numerical error code, it returns a description
of the error in English as a string.

Check the implementation of lodepng_error_text to see the meaning of each code.


8. chunks and PNG editing
-------------------------

If you want to add extra chunks to a PNG you encode, or use LodePNG for a PNG
editor that should follow the rules about handling of unknown chunks, or if your
program is able to read other types of chunks than the ones handled by LodePNG,
then that's possible with the chunk functions of LodePNG.

A PNG chunk has the following layout:

4 bytes length
4 bytes type name
length bytes data
4 bytes CRC

8.1. iterating through chunks
-----------------------------

If you have a buffer containing the PNG image data, then the first chunk (the
IHDR chunk) starts at byte number 8 of that buffer. The first 8 bytes are the
signature of the PNG and are not part of a chunk. But if you start at byte 8
then you have a chunk, and can check the following things of it.

NOTE: none of these functions check for memory buffer boundaries. To avoid
exploits, always make sure the buffer contains all the data of the chunks.
When using lodepng_chunk_next, make sure the returned value is within the
allocated memory.

unsigned lodepng_chunk_length(const unsigned char* chunk):

Get the length of the chunk's data. The total chunk length is this length + 12.

void lodepng_chunk_type(char type[5], const unsigned char* chunk):
unsigned char lodepng_chunk_type_equals(const unsigned char* chunk, const char* type):

Get the type of the chunk or compare if it's a certain type

unsigned char lodepng_chunk_critical(const unsigned char* chunk):
unsigned char lodepng_chunk_private(const unsigned char* chunk):
unsigned char lodepng_chunk_safetocopy(const unsigned char* chunk):

Check if the chunk is critical in the PNG standard (only IHDR, PLTE, IDAT and IEND are).
Check if the chunk is private (public chunks are part of the standard, private ones not).
Check if the chunk is safe to copy. If it's not, then, when modifying data in a critical
chunk, unsafe to copy chunks of the old image may NOT be saved in the new one if your
program doesn't handle that type of unknown chunk.

unsigned char* lodepng_chunk_data(unsigned char* chunk):
const unsigned char* lodepng_chunk_data_const(const unsigned char* chunk):

Get a pointer to the start of the data of the chunk.

unsigned lodepng_chunk_check_crc(const unsigned char* chunk):
void lodepng_chunk_generate_crc(unsigned char* chunk):

Check if the crc is correct or generate a correct one.

unsigned char* lodepng_chunk_next(unsigned char* chunk):
const unsigned char* lodepng_chunk_next_const(const unsigned char* chunk):

Iterate to the next chunk. This works if you have a buffer with consecutive chunks. Note that these
functions do no boundary checking of the allocated data whatsoever, so make sure there is enough
data available in the buffer to be able to go to the next chunk.

unsigned lodepng_chunk_append(unsigned char** out, size_t* outlength, const unsigned char* chunk):
unsigned lodepng_chunk_create(unsigned char** out, size_t* outlength, unsigned length,
                              const char* type, const unsigned char* data):

These functions are used to create new chunks that are appended to the data in *out that has
length *outlength. The append function appends an existing chunk to the new data. The create
function creates a new chunk with the given parameters and appends it. Type is the 4-letter
name of the chunk.

8.2. chunks in info_png
-----------------------

The LodePNGInfo struct contains fields with the unknown chunk in it. It has 3
buffers (each with size) to contain 3 types of unknown chunks:
the ones that come before the PLTE chunk, the ones that come between the PLTE
and the IDAT chunks, and the ones that come after the IDAT chunks.
It's necessary to make the distionction between these 3 cases because the PNG
standard forces to keep the ordering of unknown chunks compared to the critical
chunks, but does not force any other ordering rules.

info_png.unknown_chunks_data[0] is the chunks before PLTE
info_png.unknown_chunks_data[1] is the chunks after PLTE, before IDAT
info_png.unknown_chunks_data[2] is the chunks after IDAT

The chunks in these 3 buffers can be iterated through and read by using the same
way described in the previous subchapter.

When using the decoder to decode a PNG, you can make it store all unknown chunks
if you set the option settings.remember_unknown_chunks to 1. By default, this
option is off (0).

The encoder will always encode unknown chunks that are stored in the info_png.
If you need it to add a particular chunk that isn't known by LodePNG, you can
use lodepng_chunk_append or lodepng_chunk_create to the chunk data in
info_png.unknown_chunks_data[x].

Chunks that are known by LodePNG should not be added in that way. E.g. to make
LodePNG add a bKGD chunk, set background_defined to true and add the correct
parameters there instead.


9. compiler support
-------------------

No libraries other than the current standard C library are needed to compile
LodePNG. For the C++ version, only the standard C++ library is needed on top.
Add the files lodepng.c(pp) and lodepng.h to your project, include
lodepng.h where needed, and your program can read/write PNG files.

It is compatible with C90 and up, and C++03 and up.

If performance is important, use optimization when compiling! For both the
encoder and decoder, this makes a large difference.

Make sure that LodePNG is compiled with the same compiler of the same version
and with the same settings as the rest of the program, or the interfaces with
std::vectors and std::strings in C++ can be incompatible.

CHAR_BITS must be 8 or higher, because LodePNG uses unsigned chars for octets.

*) gcc and g++

LodePNG is developed in gcc so this compiler is natively supported. It gives no
warnings with compiler options "-Wall -Wextra -pedantic -ansi", with gcc and g++
version 4.7.1 on Linux, 32-bit and 64-bit.

*) Clang

Fully supported and warning-free.

*) Mingw

The Mingw compiler (a port of gcc for Windows) should be fully supported by
LodePNG.

*) Visual Studio and Visual C++ Express Edition

LodePNG should be warning-free with warning level W4. Two warnings were disabled
with pragmas though: warning 4244 about implicit conversions, and warning 4996
where it wants to use a non-standard function fopen_s instead of the standard C
fopen.

Visual Studio may want "stdafx.h" files to be included in each source file and
give an error "unexpected end of file while looking for precompiled header".
This is not standard C++ and will not be added to the stock LodePNG. You can
disable it for lodepng.cpp only by right clicking it, Properties, C/C++,
Precompiled Headers, and set it to Not Using Precompiled Headers there.

NOTE: Modern versions of VS should be fully supported, but old versions, e.g.
VS6, are not guaranteed to work.

*) Compilers on Macintosh

LodePNG has been reported to work both with gcc and LLVM for Macintosh, both for
C and C++.

*) Other Compilers

If you encounter problems on any compilers, feel free to let me know and I may
try to fix it if the compiler is modern and standards complient.


10. examples
------------

This decoder example shows the most basic usage of LodePNG. More complex
examples can be found on the LodePNG website.

10.1. decoder C++ example
-------------------------

#include "lodepng.h"
#include <iostream>

int main(int argc, char *argv[])
{
  const char* filename = argc > 1 ? argv[1] : "test.png";

  //load and decode
  std::vector<unsigned char> image;
  unsigned width, height;
  unsigned error = lodepng::decode(image, width, height, filename);

  //if there's an error, display it
  if(error) std::cout << "decoder error " << error << ": " << lodepng_error_text(error) << std::endl;

  //the pixels are now in the vector "image", 4 bytes per pixel, ordered RGBARGBA..., use it as texture, draw it, ...
}

10.2. decoder C example
-----------------------

#include "lodepng.h"

int main(int argc, char *argv[])
{
  unsigned error;
  unsigned char* image;
  size_t width, height;
  const char* filename = argc > 1 ? argv[1] : "test.png";

  error = lodepng_decode32_file(&image, &width, &height, filename);

  if(error) printf("decoder error %u: %s\n", error, lodepng_error_text(error));

  / * use image here * /

  free(image);
  return 0;
}

11. state settings reference
----------------------------

A quick reference of some settings to set on the LodePNGState

For decoding:

state.decoder.zlibsettings.ignore_adler32: ignore ADLER32 checksums
state.decoder.zlibsettings.custom_...: use custom inflate function
state.decoder.ignore_crc: ignore CRC checksums
state.decoder.ignore_critical: ignore unknown critical chunks
state.decoder.ignore_end: ignore missing IEND chunk. May fail if this corruption causes other errors
state.decoder.color_convert: convert internal PNG color to chosen one
state.decoder.read_text_chunks: whether to read in text metadata chunks
state.decoder.remember_unknown_chunks: whether to read in unknown chunks
state.info_raw.colortype: desired color type for decoded image
state.info_raw.bitdepth: desired bit depth for decoded image
state.info_raw....: more color settings, see struct LodePNGColorMode
state.info_png....: no settings for decoder but ouput, see struct LodePNGInfo

For encoding:

state.encoder.zlibsettings.btype: disable compression by setting it to 0
state.encoder.zlibsettings.use_lz77: use LZ77 in compression
state.encoder.zlibsettings.windowsize: tweak LZ77 windowsize
state.encoder.zlibsettings.minmatch: tweak min LZ77 length to match
state.encoder.zlibsettings.nicematch: tweak LZ77 match where to stop searching
state.encoder.zlibsettings.lazymatching: try one more LZ77 matching
state.encoder.zlibsettings.custom_...: use custom deflate function
state.encoder.auto_convert: choose optimal PNG color type, if 0 uses info_png
state.encoder.filter_palette_zero: PNG filter strategy for palette
state.encoder.filter_strategy: PNG filter strategy to encode with
state.encoder.force_palette: add palette even if not encoding to one
state.encoder.add_id: add LodePNG identifier and version as a text chunk
state.encoder.text_compression: use compressed text chunks for metadata
state.info_raw.colortype: color type of raw input image you provide
state.info_raw.bitdepth: bit depth of raw input image you provide
state.info_raw: more color settings, see struct LodePNGColorMode
state.info_png.color.colortype: desired color type if auto_convert is false
state.info_png.color.bitdepth: desired bit depth if auto_convert is false
state.info_png.color....: more color settings, see struct LodePNGColorMode
state.info_png....: more PNG related settings, see struct LodePNGInfo


13. contact information
-----------------------

Feel free to contact me with suggestions, problems, comments, ... concerning
LodePNG. If you encounter a PNG image that doesn't work properly with this
decoder, feel free to send it and I'll use it to find and fix the problem.

My email address is (puzzle the account and domain together with an @ symbol):
Domain: gmail dot com.
Account: lode dot vandevenne.


Copyright (c) 2005-2017 Lode Vandevenne
*/
