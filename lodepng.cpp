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

#include "lodepng.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>



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

#ifdef LODEPNG_COMPILE_ERROR_TEXT
// Returns an English description of the numerical error code.
const char* lodepng_error_text(unsigned code);
#endif // LODEPNG_COMPILE_ERROR_TEXT

// Settings for zlib compression. Tweaking these settings tweaks the balance
// between speed and compression ratio.
typedef struct // deflate = compress
{
    // LZ77 related settings
    unsigned windowsize; // must be a power of two <= 32768. higher compresses more but is slower. Default value: 2048.
    unsigned minmatch; // minimum lz77 length. 3 is normally best, 6 can be better for some PNGs. Default: 0
    unsigned nicematch; // stop searching if >= this length found. Set to 258 for best compression. Default: 128
    unsigned lazymatching; // use lazy matching: better compression but a bit slower. Default: true
} LodePNGCompressSettings;

void lodepng_compress_settings_init(LodePNGCompressSettings* settings);

// Color mode of an image. Contains all information required to decode the pixel
// bits to RGBA colors. This information is the same as used in the PNG file
// format, and is used both for PNG and raw image data in LodePNG.
typedef struct LodePNGColorMode
{
    // header (IHDR)
    LodePNGColorType colortype; // color type, see PNG standard or documentation further in this header file
    unsigned bitdepth;  // bits per sample, see PNG standard or documentation further in this header file

                        // palette (PLTE and tRNS)
                        // 
                        // Dynamically allocated with the colors of the palette, including alpha.
                        // When encoding a PNG, to store your colors in the palette of the LodePNGColorMode, first use
                        // lodepng_palette_clear, then for each color use lodepng_palette_add.
                        // If you encode an image without alpha with palette, don't forget to put value 255 in each A byte of the palette.
                        // 
                        // When decoding, by default you can ignore this palette, since LodePNG already
                        // fills the palette colors in the pixels of the raw RGBA output.
                        // 
                        // The palette is only supported for color type 3.
    unsigned char* palette; // palette in RGBARGBA... order. When allocated, must be either 0, or have size 1024
    size_t palettesize; // palette size in number of colors (amount of bytes is 4 * palettesize)

                        // transparent color key (tRNS)
                        // 
                        // This color uses the same bit depth as the bitdepth value in this struct, which can be 1-bit to 16-bit.
                        // For greyscale PNGs, r, g and b will all 3 be set to the same.
                        // 
                        // When decoding, by default you can ignore this information, since LodePNG sets
                        // pixels with this key to transparent already in the raw RGBA output.
                        // 
                        // The color key is only supported for color types 0 and 2.
    unsigned key_defined; // is a transparent color key given? 0 = false, 1 = true
    unsigned key_r;       // red/greyscale component of color key
    unsigned key_g;       // green component of color key
    unsigned key_b;       // blue component of color key
} LodePNGColorMode;

void lodepng_palette_clear(LodePNGColorMode* info);
// add 1 color to the palette

// Information about the PNG image, except pixels, width and height.
typedef struct LodePNGInfo
{
    // header (IHDR), palette (PLTE) and transparency (tRNS) chunks
    unsigned interlace_method;  // interlace method of the original file
    LodePNGColorMode color;     // color type and bits, palette and transparency of the PNG file
} LodePNGInfo;

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

typedef enum LodePNGFilterStrategy
{
    // every filter at zero
    LFS_ZERO,
    // Use filter that gives minimum sum, as described in the official PNG filter heuristic.
    LFS_MINSUM,
    // Use the filter type that gives smallest Shannon entropy for this scanline. Depending
    // on the image, this is better or worse than minsum.
    LFS_ENTROPY
} LodePNGFilterStrategy;

// Gives characteristics about the colors of the image, which helps decide which color model to use for encoding.
// Used internally by default if "auto_convert" is enabled. Public because it's useful for custom algorithms.
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

    /*force creating a PLTE chunk if colortype is 2 or 6 (= a suggested palette).
    If colortype is 3, PLTE is _always_ created.*/
    unsigned force_palette;
} LodePNGEncoderSettings;

void lodepng_encoder_settings_init(LodePNGEncoderSettings* settings);


// The settings, state and information for extended encoding and decoding.
typedef struct LodePNGState
{
    LodePNGEncoderSettings encoder; // the encoding settings
    LodePNGColorMode info_raw; // specifies the format in which you would like to get the raw pixel buffer
    LodePNGInfo info_png; // info of the PNG image obtained after decoding
    unsigned error;
} LodePNGState;

// init, cleanup and copy functions to use with this struct
void lodepng_state_init(LodePNGState* state);

// Same as lodepng_decode_memory, but uses a LodePNGState to allow custom settings and
// getting much more information about the PNG image and color mode.
unsigned lodepng_decode(unsigned char** out, unsigned* w, unsigned* h,
    LodePNGState* state,
    const unsigned char* in, size_t insize);

// Save a file from buffer to disk. Warning, if it exists, this function overwrites
// the file without warning!
// buffer: the buffer to write
// buffersize: size of the buffer to write
// filename: the path to the file to save to
// return value: error code (0 means ok)
unsigned lodepng_save_file(const unsigned char* buffer, size_t buffersize, const char* filename);


////////////////////////////////////////////////////////////////////////////

// This source file is built up in the following large parts.
// -Tools for C and common code for PNG and Zlib
// -C Code for Zlib (huffman, deflate, ...)
// -C Code for PNG (file format chunks, adam7, PNG filters, color conversions, ...)


//////////////////////////////////////////////////////////////////////////// 
//////////////////////////////////////////////////////////////////////////// 
//// Tools for C, and common code for PNG and Zlib.                       // 
//////////////////////////////////////////////////////////////////////////// 
//////////////////////////////////////////////////////////////////////////// 

// Often in case of an error a value is assigned to a variable and then it breaks
// out of a loop (to go to the cleanup phase of a function). This macro does that.
// It makes the error handling code shorter and more readable.
//
// Example: if(!uivector_resizev(&frequencies_ll, 286, 0)) ERROR_BREAK(83);
#define CERROR_BREAK(errorvar, code)\
{\
  errorvar = code;\
  break;\
}

//version of CERROR_BREAK that assumes the common case where the error variable is named "error"
#define ERROR_BREAK(code) CERROR_BREAK(error, code)

//Set error var to the error code, and return it.
#define CERROR_RETURN_ERROR(errorvar, code)\
{\
  errorvar = code;\
  return code;\
}

// Try the code, if it returns error, also return the error.
#define CERROR_TRY_RETURN(call)\
{\
  unsigned error = call;\
  if(error) return error;\
}

// Set error var to the error code, and return from the void function.
#define CERROR_RETURN(errorvar, code)\
{\
  errorvar = code;\
  return;\
}

// About uivector, ucvector and string:
// -All of them wrap dynamic arrays or text strings in a similar way.
// -LodePNG was originally written in C++. The vectors replace the std::vectors that were used in the C++ version.
// -The string tools are made to avoid problems with compilers that declare things like strncat as deprecated.
// -They're not used in the interface, only internally in this file as static functions.
// -As with many other structs in this file, the init and cleanup functions serve as ctor and dtor.

// dynamic vector of unsigned ints
typedef struct uivector
{
  unsigned* data;
  size_t size; // size in number of unsigned longs
  size_t allocsize; // allocated size in bytes
} uivector;

static void uivector_cleanup(void* p)
{
  ((uivector*)p)->size = ((uivector*)p)->allocsize = 0;
  free(((uivector*)p)->data);
  ((uivector*)p)->data = NULL;
}

// returns 1 if success, 0 if failure ==> nothing done
static unsigned uivector_reserve(uivector* p, size_t allocsize)
{
  if(allocsize > p->allocsize)
  {
    size_t newsize = (allocsize > p->allocsize * 2) ? allocsize : (allocsize * 3 / 2);
    void* data = realloc(p->data, newsize);
    if(data)
    {
      p->allocsize = newsize;
      p->data = (unsigned*)data;
    }
    else return 0; // error: not enough memory
  }
  return 1;
}

// returns 1 if success, 0 if failure ==> nothing done
static unsigned uivector_resize(uivector* p, size_t size)
{
  if(!uivector_reserve(p, size * sizeof(unsigned))) return 0;
  p->size = size;
  return 1; // success
}

// resize and give all new elements the value
static unsigned uivector_resizev(uivector* p, size_t size, unsigned value)
{
  size_t oldsize = p->size, i;
  if(!uivector_resize(p, size)) return 0;
  for(i = oldsize; i < size; ++i) p->data[i] = value;
  return 1;
}

static void uivector_init(uivector* p)
{
  p->data = NULL;
  p->size = p->allocsize = 0;
}

// returns 1 if success, 0 if failure ==> nothing done
static unsigned uivector_push_back(uivector* p, unsigned c)
{
  if(!uivector_resize(p, p->size + 1)) return 0;
  p->data[p->size - 1] = c;
  return 1;
}

///////////////////////////////////////////////////////////////////////////// 

// dynamic vector of unsigned chars
typedef struct ucvector
{
  unsigned char* data;
  size_t size; // used size
  size_t allocsize; // allocated size
} ucvector;

// returns 1 if success, 0 if failure ==> nothing done
static unsigned ucvector_reserve(ucvector* p, size_t allocsize)
{
  if(allocsize > p->allocsize)
  {
    size_t newsize = (allocsize > p->allocsize * 2) ? allocsize : (allocsize * 3 / 2);
    void* data = realloc(p->data, newsize);
    if(data)
    {
      p->allocsize = newsize;
      p->data = (unsigned char*)data;
    }
    else return 0; // error: not enough memory
  }
  return 1;
}

// returns 1 if success, 0 if failure ==> nothing done
static unsigned ucvector_resize(ucvector* p, size_t size)
{
  if(!ucvector_reserve(p, size * sizeof(unsigned char))) return 0;
  p->size = size;
  return 1; // success
}

static void ucvector_cleanup(void* p)
{
  ((ucvector*)p)->size = ((ucvector*)p)->allocsize = 0;
  free(((ucvector*)p)->data);
  ((ucvector*)p)->data = NULL;
}

static void ucvector_init(ucvector* p)
{
  p->data = NULL;
  p->size = p->allocsize = 0;
}

/*you can both convert from vector to buffer&size and vica versa. If you use
init_buffer to take over a buffer and size, it is not needed to use cleanup*/
static void ucvector_init_buffer(ucvector* p, unsigned char* buffer, size_t size)
{
  p->data = buffer;
  p->allocsize = p->size = size;
}

// returns 1 if success, 0 if failure ==> nothing done
static unsigned ucvector_push_back(ucvector* p, unsigned char c)
{
  if(!ucvector_resize(p, p->size + 1)) return 0;
  p->data[p->size - 1] = c;
  return 1;
}


//////////////////////////////////////////////////////////////////////////// 

unsigned lodepng_read32bitInt(const unsigned char* buffer)
{
  return (unsigned)((buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3]);
}

// buffer must have at least 4 allocated bytes available
static void lodepng_set32bitInt(unsigned char* buffer, unsigned value)
{
  buffer[0] = (unsigned char)((value >> 24) & 0xff);
  buffer[1] = (unsigned char)((value >> 16) & 0xff);
  buffer[2] = (unsigned char)((value >>  8) & 0xff);
  buffer[3] = (unsigned char)((value      ) & 0xff);
}

static void lodepng_add32bitInt(ucvector* buffer, unsigned value)
{
  ucvector_resize(buffer, buffer->size + 4); // todo: give error if resize failed
  lodepng_set32bitInt(&buffer->data[buffer->size - 4], value);
}

//////////////////////////////////////////////////////////////////////////// 
/// File IO                                                                / 
//////////////////////////////////////////////////////////////////////////// 

//  returns negative value on error. This should be pure C compatible, so no fstat. 
static long lodepng_filesize(const char* filename)
{
  FILE *file = fopen(filename, "rb");
  if(!file) return -1;

  if(fseek(file, 0, SEEK_END) != 0)
  {
    fclose(file);
    return -1;
  }

  long size = ftell(file);
  //  It may give LONG_MAX as directory size, this is invalid for us. 
  if(size == LONG_MAX) size = -1;

  fclose(file);
  return size;
}

//  load file into buffer that already has the correct allocated size. Returns error code.
static unsigned lodepng_buffer_file(unsigned char* out, size_t size, const char* filename)
{
  FILE *file = fopen(filename, "rb");
  if(!file) return 78;

  size_t readsize = fread(out, 1, size, file);
  fclose(file);

  if (readsize != size) return 78;
  return 0;
}

// Load a file from disk into buffer. The function allocates the out buffer, and
// after usage you should free it.
// out: output parameter, contains pointer to loaded buffer.
// outsize: output parameter, size of the allocated out buffer
// filename: the path to the file to load
// return value: error code (0 means ok)
unsigned lodepng_load_file(unsigned char** out, size_t* outsize, const char* filename)
{
  long size = lodepng_filesize(filename);
  if (size < 0) return 78;
  *outsize = (size_t)size;

  *out = (unsigned char*)malloc((size_t)size);
  if(!(*out) && size > 0) return 83; // the above malloc failed

  return lodepng_buffer_file(*out, (size_t)size, filename);
}

// write given buffer to the file, overwriting the file, it doesn't append to it.
unsigned lodepng_save_file(const unsigned char* buffer, size_t buffersize, const char* filename)
{
  FILE* file = fopen(filename, "wb");
  if(!file) return 79;
  fwrite((char*)buffer, 1, buffersize, file);
  fclose(file);
  return 0;
}

//////////////////////////////////////////////////////////////////////////// 
//////////////////////////////////////////////////////////////////////////// 
//// End of common code and tools. Begin of Zlib related code.            // 
//////////////////////////////////////////////////////////////////////////// 
//////////////////////////////////////////////////////////////////////////// 

// This zlib part can be used independently to zlib compress and decompress a
// buffer. It cannot be used to create gzip files however, and it only supports the
// part of zlib that is required for PNG, it does not support dictionaries.

// TODO: this ignores potential out of memory errors
#define addBitToStream(/*size_t**/ bitpointer, /*ucvector**/ bitstream, /*unsigned char*/ bit)\
{\
  /*add a new byte at the end*/\
  if(((*bitpointer) & 7) == 0) ucvector_push_back(bitstream, (unsigned char)0);\
  /*earlier bit of huffman code is in a lesser significant bit of an earlier byte*/\
  (bitstream->data[bitstream->size - 1]) |= (bit << ((*bitpointer) & 0x7));\
  ++(*bitpointer);\
}

static void addBitsToStream(size_t* bitpointer, ucvector* bitstream, unsigned value, size_t nbits)
{
  for(size_t i = 0; i != nbits; ++i) addBitToStream(bitpointer, bitstream, (unsigned char)((value >> i) & 1));
}

static void addBitsToStreamReversed(size_t* bitpointer, ucvector* bitstream, unsigned value, size_t nbits)
{
  for(size_t i = 0; i != nbits; ++i) addBitToStream(bitpointer, bitstream, (unsigned char)((value >> (nbits - 1 - i)) & 1));
}

#define READBIT(bitpointer, bitstream) ((bitstream[bitpointer >> 3] >> (bitpointer & 0x7)) & (unsigned char)1)

static unsigned char readBitFromStream(size_t* bitpointer, const unsigned char* bitstream)
{
  unsigned char result = (unsigned char)(READBIT(*bitpointer, bitstream));
  ++(*bitpointer);
  return result;
}

static unsigned readBitsFromStream(size_t* bitpointer, const unsigned char* bitstream, size_t nbits)
{
  unsigned result = 0, i;
  for(i = 0; i != nbits; ++i)
  {
    result += ((unsigned)READBIT(*bitpointer, bitstream)) << i;
    ++(*bitpointer);
  }
  return result;
}

//////////////////////////////////////////////////////////////////////////// 
/// Deflate - Huffman                                                      / 
//////////////////////////////////////////////////////////////////////////// 

#define FIRST_LENGTH_CODE_INDEX 257
#define LAST_LENGTH_CODE_INDEX 285
// 256 literals, the end code, some length codes, and 2 unused codes
#define NUM_DEFLATE_CODE_SYMBOLS 288
// the distance codes have their own symbols, 30 used, 2 unused
#define NUM_DISTANCE_SYMBOLS 32
// the code length codes. 0-15: code lengths, 16: copy previous 3-6 times, 17: 3-10 zeros, 18: 11-138 zeros
#define NUM_CODE_LENGTH_CODES 19

// the base lengths represented by codes 257-285
static const unsigned LENGTHBASE[29]
  = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
     67, 83, 99, 115, 131, 163, 195, 227, 258};

// the extra bits used by codes 257-285 (added to base length)
static const unsigned LENGTHEXTRA[29]
  = {0, 0, 0, 0, 0, 0, 0,  0,  1,  1,  1,  1,  2,  2,  2,  2,  3,  3,  3,  3,
      4,  4,  4,   4,   5,   5,   5,   5,   0};

// the base backwards distances (the bits of distance codes appear after length codes and use their own huffman tree)
static const unsigned DISTANCEBASE[30]
  = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513,
     769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};

// the extra bits of backwards distances (added to base)
static const unsigned DISTANCEEXTRA[30]
  = {0, 0, 0, 0, 1, 1, 2,  2,  3,  3,  4,  4,  5,  5,   6,   6,   7,   7,   8,
       8,    9,    9,   10,   10,   11,   11,   12,    12,    13,    13};

/*the order in which "code length alphabet code lengths" are stored, out of this
the Huffman tree of the dynamic Huffman tree lengths is generated*/
static const unsigned CLCL_ORDER[NUM_CODE_LENGTH_CODES]
  = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

//////////////////////////////////////////////////////////////////////////// 

// Huffman tree struct, containing multiple representations of the tree
typedef struct HuffmanTree
{
  unsigned* tree2d;
  unsigned* tree1d;
  unsigned* lengths; // the lengths of the codes of the 1d-tree
  unsigned maxbitlen; // maximum number of bits a single code can get
  unsigned numcodes; // number of symbols in the alphabet = number of codes
} HuffmanTree;

// function used for debug purposes to draw the tree in ascii art with C++
/*
static void HuffmanTree_draw(HuffmanTree* tree)
{
  std::cout << "tree. length: " << tree->numcodes << " maxbitlen: " << tree->maxbitlen << std::endl;
  for(size_t i = 0; i != tree->tree1d.size; ++i)
  {
    if(tree->lengths.data[i])
      std::cout << i << " " << tree->tree1d.data[i] << " " << tree->lengths.data[i] << std::endl;
  }
  std::cout << std::endl;
}*/

static void HuffmanTree_init(HuffmanTree* tree)
{
  tree->tree2d = 0;
  tree->tree1d = 0;
  tree->lengths = 0;
}

static void HuffmanTree_cleanup(HuffmanTree* tree)
{
  free(tree->tree2d);
  free(tree->tree1d);
  free(tree->lengths);
}

// the tree representation used by the decoder. return value is error
static unsigned HuffmanTree_make2DTree(HuffmanTree* tree)
{
  unsigned nodefilled = 0; // up to which node it is filled
  unsigned treepos = 0; // position in the tree (1 of the numcodes columns)
  unsigned n, i;

  tree->tree2d = (unsigned*)malloc(tree->numcodes * 2 * sizeof(unsigned));
  if(!tree->tree2d) return 83; // alloc fail

  // convert tree1d[] to tree2d[][]. In the 2D array, a value of 32767 means
  // uninited, a value >= numcodes is an address to another bit, a value < numcodes
  // is a code. The 2 rows are the 2 possible bit values (0 or 1), there are as
  // many columns as codes - 1.
  // A good huffman tree has N * 2 - 1 nodes, of which N - 1 are internal nodes.
  // Here, the internal nodes are stored (what their 0 and 1 option point to).
  // There is only memory for such good tree currently, if there are more nodes
  // (due to too long length codes), error 55 will happen
  for(n = 0; n < tree->numcodes * 2; ++n)
  {
    tree->tree2d[n] = 32767; // 32767 here means the tree2d isn't filled there yet
  }

  for(n = 0; n < tree->numcodes; ++n) // the codes
  {
    for(i = 0; i != tree->lengths[n]; ++i) // the bits for this code
    {
      unsigned char bit = (unsigned char)((tree->tree1d[n] >> (tree->lengths[n] - i - 1)) & 1);
      // oversubscribed, see comment in lodepng_error_text
      if(treepos > 2147483647 || treepos + 2 > tree->numcodes) return 55;
      if(tree->tree2d[2 * treepos + bit] == 32767) // not yet filled in
      {
        if(i + 1 == tree->lengths[n]) // last bit
        {
          tree->tree2d[2 * treepos + bit] = n; // put the current code in it
          treepos = 0;
        }
        else
        {
          // put address of the next step in here, first that address has to be found of course
          // (it's just nodefilled + 1)...
          ++nodefilled;
          // addresses encoded with numcodes added to it
          tree->tree2d[2 * treepos + bit] = nodefilled + tree->numcodes;
          treepos = nodefilled;
        }
      }
      else treepos = tree->tree2d[2 * treepos + bit] - tree->numcodes;
    }
  }

  for(n = 0; n < tree->numcodes * 2; ++n)
  {
    if(tree->tree2d[n] == 32767) tree->tree2d[n] = 0; // remove possible remaining 32767's
  }

  return 0;
}

// Second step for the ...makeFromLengths and ...makeFromFrequencies functions.
// numcodes, lengths and maxbitlen must already be filled in correctly. return
// value is error.
static unsigned HuffmanTree_makeFromLengths2(HuffmanTree* tree)
{
  uivector blcount;
  uivector nextcode;
  unsigned error = 0;

  uivector_init(&blcount);
  uivector_init(&nextcode);

  tree->tree1d = (unsigned*)malloc(tree->numcodes * sizeof(unsigned));
  if(!tree->tree1d) error = 83; // alloc fail

  if(!uivector_resizev(&blcount, tree->maxbitlen + 1, 0)
  || !uivector_resizev(&nextcode, tree->maxbitlen + 1, 0))
    error = 83; // alloc fail

  if(!error)
  {
    // step 1: count number of instances of each code length
    for(unsigned bits = 0; bits != tree->numcodes; ++bits) ++blcount.data[tree->lengths[bits]];
    // step 2: generate the nextcode values
    for(unsigned bits = 1; bits <= tree->maxbitlen; ++bits)
    {
      nextcode.data[bits] = (nextcode.data[bits - 1] + blcount.data[bits - 1]) << 1;
    }
    // step 3: generate all the codes
    for(unsigned n = 0; n != tree->numcodes; ++n)
    {
      if(tree->lengths[n] != 0) tree->tree1d[n] = nextcode.data[tree->lengths[n]]++;
    }
  }

  uivector_cleanup(&blcount);
  uivector_cleanup(&nextcode);

  if(!error) return HuffmanTree_make2DTree(tree);
  else return error;
}

// given the code lengths (as stored in the PNG file), generate the tree as defined
// by Deflate. maxbitlen is the maximum bits that a code in the tree can have.
// return value is error.
static unsigned HuffmanTree_makeFromLengths(HuffmanTree* tree, const unsigned* bitlen,
                                            size_t numcodes, unsigned maxbitlen)
{
  tree->lengths = (unsigned*)malloc(numcodes * sizeof(unsigned));
  if(!tree->lengths) return 83; // alloc fail
  for(unsigned i = 0; i != numcodes; ++i) tree->lengths[i] = bitlen[i];
  tree->numcodes = (unsigned)numcodes; // number of symbols
  tree->maxbitlen = maxbitlen;
  return HuffmanTree_makeFromLengths2(tree);
}

// BPM: Boundary Package Merge, see "A Fast and Space-Economical Algorithm for Length-Limited Coding",
// Jyrki Katajainen, Alistair Moffat, Andrew Turpin, 1995.

// chain node for boundary package merge
typedef struct BPMNode
{
  int weight; // the sum of all weights in this chain
  unsigned index; // index of this leaf node (called "count" in the paper)
  struct BPMNode* tail; // the next nodes in this chain (null if last)
  int in_use;
} BPMNode;

// lists of chains
typedef struct BPMLists
{
  // memory pool
  unsigned memsize;
  BPMNode* memory;
  unsigned numfree;
  unsigned nextfree;
  BPMNode** freelist;
  // two heads of lookahead chains per list
  unsigned listsize;
  BPMNode** chains0;
  BPMNode** chains1;
} BPMLists;

// creates a new chain node with the given parameters, from the memory in the lists 
static BPMNode* bpmnode_create(BPMLists* lists, int weight, unsigned index, BPMNode* tail)
{
  unsigned i;
  BPMNode* result;

  // memory full, so garbage collect
  if(lists->nextfree >= lists->numfree)
  {
    // mark only those that are in use
    for(i = 0; i != lists->memsize; ++i) lists->memory[i].in_use = 0;
    for(i = 0; i != lists->listsize; ++i)
    {
      BPMNode* node;
      for(node = lists->chains0[i]; node != 0; node = node->tail) node->in_use = 1;
      for(node = lists->chains1[i]; node != 0; node = node->tail) node->in_use = 1;
    }
    // collect those that are free
    lists->numfree = 0;
    for(i = 0; i != lists->memsize; ++i)
    {
      if(!lists->memory[i].in_use) lists->freelist[lists->numfree++] = &lists->memory[i];
    }
    lists->nextfree = 0;
  }

  result = lists->freelist[lists->nextfree++];
  result->weight = weight;
  result->index = index;
  result->tail = tail;
  return result;
}

// sort the leaves with stable mergesort
static void bpmnode_sort(BPMNode* leaves, size_t num)
{
  BPMNode* mem = (BPMNode*)malloc(sizeof(*leaves) * num);
  size_t width, counter = 0;
  for(width = 1; width < num; width *= 2)
  {
    BPMNode* a = (counter & 1) ? mem : leaves;
    BPMNode* b = (counter & 1) ? leaves : mem;
    size_t p;
    for(p = 0; p < num; p += 2 * width)
    {
      size_t q = (p + width > num) ? num : (p + width);
      size_t r = (p + 2 * width > num) ? num : (p + 2 * width);
      size_t i = p, j = q, k;
      for(k = p; k < r; k++)
      {
        if(i < q && (j >= r || a[i].weight <= a[j].weight)) b[k] = a[i++];
        else b[k] = a[j++];
      }
    }
    counter++;
  }
  if(counter & 1) memcpy(leaves, mem, sizeof(*leaves) * num);
  free(mem);
}

// Boundary Package Merge step, numpresent is the amount of leaves, and c is the current chain.
static void boundaryPM(BPMLists* lists, BPMNode* leaves, size_t numpresent, int c, int num)
{
  unsigned lastindex = lists->chains1[c]->index;

  if(c == 0)
  {
    if(lastindex >= numpresent) return;
    lists->chains0[c] = lists->chains1[c];
    lists->chains1[c] = bpmnode_create(lists, leaves[lastindex].weight, lastindex + 1, 0);
  }
  else
  {
    // sum of the weights of the head nodes of the previous lookahead chains.
    int sum = lists->chains0[c - 1]->weight + lists->chains1[c - 1]->weight;
    lists->chains0[c] = lists->chains1[c];
    if(lastindex < numpresent && sum > leaves[lastindex].weight)
    {
      lists->chains1[c] = bpmnode_create(lists, leaves[lastindex].weight, lastindex + 1, lists->chains1[c]->tail);
      return;
    }
    lists->chains1[c] = bpmnode_create(lists, sum, lastindex, lists->chains1[c - 1]);
    /*in the end we are only interested in the chain of the last list, so no
    need to recurse if we're at the last one (this gives measurable speedup)*/
    if(num + 1 < (int)(2 * numpresent - 2))
    {
      boundaryPM(lists, leaves, numpresent, c - 1, num);
      boundaryPM(lists, leaves, numpresent, c - 1, num);
    }
  }
}

// Find length-limited Huffman code for given frequencies. This function is in the
// public interface only for tests, it's used internally by lodepng_deflate.
unsigned lodepng_huffman_code_lengths(unsigned* lengths, const unsigned* frequencies,
                                      size_t numcodes, unsigned maxbitlen)
{
  unsigned error = 0;
  unsigned i;
  size_t numpresent = 0; // number of symbols with non-zero frequency
  BPMNode* leaves; // the symbols, only those with > 0 frequency

  if(numcodes == 0) return 80; // error: a tree of 0 symbols is not supposed to be made
  if((1u << maxbitlen) < (unsigned)numcodes) return 80; // error: represent all symbols

  leaves = (BPMNode*)malloc(numcodes * sizeof(*leaves));
  if(!leaves) return 83; // alloc fail

  for(i = 0; i != numcodes; ++i)
  {
    if(frequencies[i] > 0)
    {
      leaves[numpresent].weight = (int)frequencies[i];
      leaves[numpresent].index = i;
      ++numpresent;
    }
  }

  for(i = 0; i != numcodes; ++i) lengths[i] = 0;

  /*ensure at least two present symbols. There should be at least one symbol
  according to RFC 1951 section 3.2.7. Some decoders incorrectly require two. To
  make these work as well ensure there are at least two symbols. The
  Package-Merge code below also doesn't work correctly if there's only one
  symbol, it'd give it the theoretical 0 bits but in practice zlib wants 1 bit*/
  if(numpresent == 0)
  {
    lengths[0] = lengths[1] = 1; // note that for RFC 1951 section 3.2.7, only lengths[0] = 1 is needed
  }
  else if(numpresent == 1)
  {
    lengths[leaves[0].index] = 1;
    lengths[leaves[0].index == 0 ? 1 : 0] = 1;
  }
  else
  {
    BPMLists lists;
    BPMNode* node;

    bpmnode_sort(leaves, numpresent);

    lists.listsize = maxbitlen;
    lists.memsize = 2 * maxbitlen * (maxbitlen + 1);
    lists.nextfree = 0;
    lists.numfree = lists.memsize;
    lists.memory = (BPMNode*)malloc(lists.memsize * sizeof(*lists.memory));
    lists.freelist = (BPMNode**)malloc(lists.memsize * sizeof(BPMNode*));
    lists.chains0 = (BPMNode**)malloc(lists.listsize * sizeof(BPMNode*));
    lists.chains1 = (BPMNode**)malloc(lists.listsize * sizeof(BPMNode*));
    if(!lists.memory || !lists.freelist || !lists.chains0 || !lists.chains1) error = 83; // alloc fail

    if(!error)
    {
      for(i = 0; i != lists.memsize; ++i) lists.freelist[i] = &lists.memory[i];

      bpmnode_create(&lists, leaves[0].weight, 1, 0);
      bpmnode_create(&lists, leaves[1].weight, 2, 0);

      for(i = 0; i != lists.listsize; ++i)
      {
        lists.chains0[i] = &lists.memory[0];
        lists.chains1[i] = &lists.memory[1];
      }

      // each boundaryPM call adds one chain to the last list, and we need 2 * numpresent - 2 chains.
      for(i = 2; i != 2 * numpresent - 2; ++i) boundaryPM(&lists, leaves, numpresent, (int)maxbitlen - 1, (int)i);

      for(node = lists.chains1[maxbitlen - 1]; node; node = node->tail)
      {
        for(i = 0; i != node->index; ++i) ++lengths[leaves[i].index];
      }
    }

    free(lists.memory);
    free(lists.freelist);
    free(lists.chains0);
    free(lists.chains1);
  }

  free(leaves);
  return error;
}

// Create the Huffman tree given the symbol frequencies
static unsigned HuffmanTree_makeFromFrequencies(HuffmanTree* tree, const unsigned* frequencies,
                                                size_t mincodes, size_t numcodes, unsigned maxbitlen)
{
  unsigned error = 0;
  while(!frequencies[numcodes - 1] && numcodes > mincodes) --numcodes; // trim zeroes
  tree->maxbitlen = maxbitlen;
  tree->numcodes = (unsigned)numcodes; // number of symbols
  tree->lengths = (unsigned*)realloc(tree->lengths, numcodes * sizeof(unsigned));
  if(!tree->lengths) return 83; // alloc fail
  // initialize all lengths to 0
  memset(tree->lengths, 0, numcodes * sizeof(unsigned));

  error = lodepng_huffman_code_lengths(tree->lengths, frequencies, numcodes, maxbitlen);
  if(!error) error = HuffmanTree_makeFromLengths2(tree);
  return error;
}

static unsigned HuffmanTree_getCode(const HuffmanTree* tree, unsigned index)
{
  return tree->tree1d[index];
}

static unsigned HuffmanTree_getLength(const HuffmanTree* tree, unsigned index)
{
  return tree->lengths[index];
}

// get the literal and length code tree of a deflated block with fixed tree, as per the deflate specification
static unsigned generateFixedLitLenTree(HuffmanTree* tree)
{
  unsigned i, error = 0;
  unsigned* bitlen = (unsigned*)malloc(NUM_DEFLATE_CODE_SYMBOLS * sizeof(unsigned));
  if(!bitlen) return 83; // alloc fail

  // 288 possible codes: 0-255=literals, 256=endcode, 257-285=lengthcodes, 286-287=unused
  for(i =   0; i <= 143; ++i) bitlen[i] = 8;
  for(i = 144; i <= 255; ++i) bitlen[i] = 9;
  for(i = 256; i <= 279; ++i) bitlen[i] = 7;
  for(i = 280; i <= 287; ++i) bitlen[i] = 8;

  error = HuffmanTree_makeFromLengths(tree, bitlen, NUM_DEFLATE_CODE_SYMBOLS, 15);

  free(bitlen);
  return error;
}

// get the distance code tree of a deflated block with fixed tree, as specified in the deflate specification
static unsigned generateFixedDistanceTree(HuffmanTree* tree)
{
  unsigned i, error = 0;
  unsigned* bitlen = (unsigned*)malloc(NUM_DISTANCE_SYMBOLS * sizeof(unsigned));
  if(!bitlen) return 83; // alloc fail

  // there are 32 distance codes, but 30-31 are unused
  for(i = 0; i != NUM_DISTANCE_SYMBOLS; ++i) bitlen[i] = 5;
  error = HuffmanTree_makeFromLengths(tree, bitlen, NUM_DISTANCE_SYMBOLS, 15);

  free(bitlen);
  return error;
}

// returns the code, or (unsigned)(-1) if error happened
// inbitlength is the length of the complete buffer, in bits (so its byte length times 8)
static unsigned huffmanDecodeSymbol(const unsigned char* in, size_t* bp,
                                    const HuffmanTree* codetree, size_t inbitlength)
{
  unsigned treepos = 0, ct;
  for(;;)
  {
    if(*bp >= inbitlength) return (unsigned)(-1); // error: end of input memory reached without endcode
    /*
    decode the symbol from the tree. The "readBitFromStream" code is inlined in
    the expression below because this is the biggest bottleneck while decoding
    */
    ct = codetree->tree2d[(treepos << 1) + READBIT(*bp, in)];
    ++(*bp);
    if(ct < codetree->numcodes) return ct; // the symbol is decoded, return it
    else treepos = ct - codetree->numcodes; // symbol not yet decoded, instead move tree position

    if(treepos >= codetree->numcodes) return (unsigned)(-1); // error: it appeared outside the codetree
  }
}


//////////////////////////////////////////////////////////////////////////// 
/// Inflator (Decompressor)                                                / 
//////////////////////////////////////////////////////////////////////////// 

// get the tree of a deflated block with fixed tree, as specified in the deflate specification
static void getTreeInflateFixed(HuffmanTree* tree_ll, HuffmanTree* tree_d)
{
  // TODO: check for out of memory errors
  generateFixedLitLenTree(tree_ll);
  generateFixedDistanceTree(tree_d);
}

// get the tree of a deflated block with dynamic tree, the tree itself is also Huffman compressed with a known tree
static unsigned getTreeInflateDynamic(HuffmanTree* tree_ll, HuffmanTree* tree_d,
                                      const unsigned char* in, size_t* bp, size_t inlength)
{
  // make sure that length values that aren't filled in will be 0, or a wrong tree will be generated
  unsigned error = 0;
  unsigned n, HLIT, HDIST, HCLEN, i;
  size_t inbitlength = inlength * 8;

  // see comments in deflateDynamic for explanation of the context and these variables, it is analogous
  unsigned* bitlen_ll = 0; // lit,len code lengths
  unsigned* bitlen_d = 0; // dist code lengths
  // code length code lengths ("clcl"), the bit lengths of the huffman tree used to compress bitlen_ll and bitlen_d
  unsigned* bitlen_cl = 0;
  HuffmanTree tree_cl; // the code tree for code length codes (the huffman tree for compressed huffman trees)

  if((*bp) + 14 > (inlength << 3)) return 49; // error: the bit pointer is or will go past the memory

  // number of literal/length codes + 257. Unlike the spec, the value 257 is added to it here already
  HLIT =  readBitsFromStream(bp, in, 5) + 257;
  // number of distance codes. Unlike the spec, the value 1 is added to it here already
  HDIST = readBitsFromStream(bp, in, 5) + 1;
  // number of code length codes. Unlike the spec, the value 4 is added to it here already
  HCLEN = readBitsFromStream(bp, in, 4) + 4;

  if((*bp) + HCLEN * 3 > (inlength << 3)) return 50; // error: the bit pointer is or will go past the memory

  HuffmanTree_init(&tree_cl);

  while(!error)
  {
    // read the code length codes out of 3 * (amount of code length codes) bits

    bitlen_cl = (unsigned*)malloc(NUM_CODE_LENGTH_CODES * sizeof(unsigned));
    if(!bitlen_cl) ERROR_BREAK(83 /*alloc fail*/);

    for(i = 0; i != NUM_CODE_LENGTH_CODES; ++i)
    {
      if(i < HCLEN) bitlen_cl[CLCL_ORDER[i]] = readBitsFromStream(bp, in, 3);
      else bitlen_cl[CLCL_ORDER[i]] = 0; // if not, it must stay 0
    }

    error = HuffmanTree_makeFromLengths(&tree_cl, bitlen_cl, NUM_CODE_LENGTH_CODES, 7);
    if(error) break;

    // now we can use this tree to read the lengths for the tree that this function will return
    bitlen_ll = (unsigned*)malloc(NUM_DEFLATE_CODE_SYMBOLS * sizeof(unsigned));
    bitlen_d = (unsigned*)malloc(NUM_DISTANCE_SYMBOLS * sizeof(unsigned));
    if(!bitlen_ll || !bitlen_d) ERROR_BREAK(83 /*alloc fail*/);
    for(i = 0; i != NUM_DEFLATE_CODE_SYMBOLS; ++i) bitlen_ll[i] = 0;
    for(i = 0; i != NUM_DISTANCE_SYMBOLS; ++i) bitlen_d[i] = 0;

    // i is the current symbol we're reading in the part that contains the code lengths of lit/len and dist codes
    i = 0;
    while(i < HLIT + HDIST)
    {
      unsigned code = huffmanDecodeSymbol(in, bp, &tree_cl, inbitlength);
      if(code <= 15) // a length code
      {
        if(i < HLIT) bitlen_ll[i] = code;
        else bitlen_d[i - HLIT] = code;
        ++i;
      }
      else if(code == 16) // repeat previous
      {
        unsigned replength = 3; // read in the 2 bits that indicate repeat length (3-6)
        unsigned value; // set value to the previous code

        if(i == 0) ERROR_BREAK(54); // can't repeat previous if i is 0

        if((*bp + 2) > inbitlength) ERROR_BREAK(50); // error, bit pointer jumps past memory
        replength += readBitsFromStream(bp, in, 2);

        if(i < HLIT + 1) value = bitlen_ll[i - 1];
        else value = bitlen_d[i - HLIT - 1];
        // repeat this value in the next lengths
        for(n = 0; n < replength; ++n)
        {
          if(i >= HLIT + HDIST) ERROR_BREAK(13); // error: i is larger than the amount of codes
          if(i < HLIT) bitlen_ll[i] = value;
          else bitlen_d[i - HLIT] = value;
          ++i;
        }
      }
      else if(code == 17) // repeat "0" 3-10 times
      {
        unsigned replength = 3; // read in the bits that indicate repeat length
        if((*bp + 3) > inbitlength) ERROR_BREAK(50); // error, bit pointer jumps past memory
        replength += readBitsFromStream(bp, in, 3);

        // repeat this value in the next lengths
        for(n = 0; n < replength; ++n)
        {
          if(i >= HLIT + HDIST) ERROR_BREAK(14); // error: i is larger than the amount of codes

          if(i < HLIT) bitlen_ll[i] = 0;
          else bitlen_d[i - HLIT] = 0;
          ++i;
        }
      }
      else if(code == 18) // repeat "0" 11-138 times
      {
        unsigned replength = 11; // read in the bits that indicate repeat length
        if((*bp + 7) > inbitlength) ERROR_BREAK(50); // error, bit pointer jumps past memory
        replength += readBitsFromStream(bp, in, 7);

        // repeat this value in the next lengths
        for(n = 0; n < replength; ++n)
        {
          if(i >= HLIT + HDIST) ERROR_BREAK(15); // error: i is larger than the amount of codes

          if(i < HLIT) bitlen_ll[i] = 0;
          else bitlen_d[i - HLIT] = 0;
          ++i;
        }
      }
      else // if(code == (unsigned)(-1))*/ /*huffmanDecodeSymbol returns (unsigned)(-1) in case of error
      {
        if(code == (unsigned)(-1))
        {
          /*return error code 10 or 11 depending on the situation that happened in huffmanDecodeSymbol
          (10=no endcode, 11=wrong jump outside of tree)*/
          error = (*bp) > inbitlength ? 10 : 11;
        }
        else error = 16; // unexisting code, this can never happen
        break;
      }
    }
    if(error) break;

    if(bitlen_ll[256] == 0) ERROR_BREAK(64); // the length of the end code 256 must be larger than 0

    // now we've finally got HLIT and HDIST, so generate the code trees, and the function is done
    error = HuffmanTree_makeFromLengths(tree_ll, bitlen_ll, NUM_DEFLATE_CODE_SYMBOLS, 15);
    if(error) break;
    error = HuffmanTree_makeFromLengths(tree_d, bitlen_d, NUM_DISTANCE_SYMBOLS, 15);

    break; // end of error-while
  }

  free(bitlen_cl);
  free(bitlen_ll);
  free(bitlen_d);
  HuffmanTree_cleanup(&tree_cl);

  return error;
}

// inflate a block with dynamic of fixed Huffman tree
static unsigned inflateHuffmanBlock(ucvector* out, const unsigned char* in, size_t* bp,
                                    size_t* pos, size_t inlength, unsigned btype)
{
  unsigned error = 0;
  HuffmanTree tree_ll; // the huffman tree for literal and length codes
  HuffmanTree tree_d; // the huffman tree for distance codes
  size_t inbitlength = inlength * 8;

  HuffmanTree_init(&tree_ll);
  HuffmanTree_init(&tree_d);

  if(btype == 1) getTreeInflateFixed(&tree_ll, &tree_d);
  else if(btype == 2) error = getTreeInflateDynamic(&tree_ll, &tree_d, in, bp, inlength);

  while(!error) // decode all symbols until end reached, breaks at end code
  {
    // code_ll is literal, length or end code
    unsigned code_ll = huffmanDecodeSymbol(in, bp, &tree_ll, inbitlength);
    if(code_ll <= 255) // literal symbol
    {
      // ucvector_push_back would do the same, but for some reason the two lines below run 10% faster
      if(!ucvector_resize(out, (*pos) + 1)) ERROR_BREAK(83 /*alloc fail*/);
      out->data[*pos] = (unsigned char)code_ll;
      ++(*pos);
    }
    else if(code_ll >= FIRST_LENGTH_CODE_INDEX && code_ll <= LAST_LENGTH_CODE_INDEX) // length code
    {
      unsigned code_d, distance;
      unsigned numextrabits_l, numextrabits_d; // extra bits for length and distance
      size_t start, forward, backward, length;

      // part 1: get length base
      length = LENGTHBASE[code_ll - FIRST_LENGTH_CODE_INDEX];

      // part 2: get extra bits and add the value of that to length
      numextrabits_l = LENGTHEXTRA[code_ll - FIRST_LENGTH_CODE_INDEX];
      if((*bp + numextrabits_l) > inbitlength) ERROR_BREAK(51); // error, bit pointer will jump past memory
      length += readBitsFromStream(bp, in, numextrabits_l);

      // part 3: get distance code
      code_d = huffmanDecodeSymbol(in, bp, &tree_d, inbitlength);
      if(code_d > 29)
      {
        if(code_d == (unsigned)(-1)) // huffmanDecodeSymbol returns (unsigned)(-1) in case of error
        {
          /*return error code 10 or 11 depending on the situation that happened in huffmanDecodeSymbol
          (10=no endcode, 11=wrong jump outside of tree)*/
          error = (*bp) > inlength * 8 ? 10 : 11;
        }
        else error = 18; // error: invalid distance code (30-31 are never used)
        break;
      }
      distance = DISTANCEBASE[code_d];

      // part 4: get extra bits from distance
      numextrabits_d = DISTANCEEXTRA[code_d];
      if((*bp + numextrabits_d) > inbitlength) ERROR_BREAK(51); // error, bit pointer will jump past memory
      distance += readBitsFromStream(bp, in, numextrabits_d);

      // part 5: fill in all the out[n] values based on the length and dist
      start = (*pos);
      if(distance > start) ERROR_BREAK(52); // too long backward distance
      backward = start - distance;

      if(!ucvector_resize(out, (*pos) + length)) ERROR_BREAK(83 /*alloc fail*/);
      if (distance < length) {
        for(forward = 0; forward < length; ++forward)
        {
          out->data[(*pos)++] = out->data[backward++];
        }
      } else {
        memcpy(out->data + *pos, out->data + backward, length);
        *pos += length;
      }
    }
    else if(code_ll == 256)
    {
      break; // end code, break the loop
    }
    else // if(code == (unsigned)(-1))*/ /*huffmanDecodeSymbol returns (unsigned)(-1) in case of error
    {
      /*return error code 10 or 11 depending on the situation that happened in huffmanDecodeSymbol
      (10=no endcode, 11=wrong jump outside of tree)*/
      error = ((*bp) > inlength * 8) ? 10 : 11;
      break;
    }
  }

  HuffmanTree_cleanup(&tree_ll);
  HuffmanTree_cleanup(&tree_d);

  return error;
}

static unsigned inflateNoCompression(ucvector* out, const unsigned char* in, size_t* bp, size_t* pos, size_t inlength)
{
  unsigned LEN, NLEN, n, error = 0;

  // go to first boundary of byte
  while(((*bp) & 0x7) != 0) ++(*bp);
  size_t p = (*bp) / 8; // byte position

  // read LEN (2 bytes) and NLEN (2 bytes)
  if(p + 4 >= inlength) return 52; // error, bit pointer will jump past memory
  LEN = in[p] + 256u * in[p + 1]; p += 2;
  NLEN = in[p] + 256u * in[p + 1]; p += 2;

  // check if 16-bit NLEN is really the one's complement of LEN
  if(LEN + NLEN != 65535) return 21; // error: NLEN is not one's complement of LEN

  if(!ucvector_resize(out, (*pos) + LEN)) return 83; // alloc fail

  // read the literal data: LEN bytes are now stored in the out buffer
  if(p + LEN > inlength) return 23; // error: reading outside of in buffer
  for(n = 0; n < LEN; ++n) out->data[(*pos)++] = in[p++];

  (*bp) = p * 8;

  return error;
}

static unsigned lodepng_inflatev(ucvector* out,
                                 const unsigned char* in, size_t insize)
{
  // bit pointer in the "in" data, current byte is bp >> 3, current bit is bp & 0x7 (from lsb to msb of the byte)
  size_t bp = 0;
  unsigned BFINAL = 0;
  size_t pos = 0; // byte position in the out buffer
  unsigned error = 0;

  while(!BFINAL)
  {
    if(bp + 2 >= insize * 8) return 52; // error, bit pointer will jump past memory
    BFINAL = readBitFromStream(&bp, in);
    unsigned BTYPE = 1u * readBitFromStream(&bp, in);
    BTYPE += 2u * readBitFromStream(&bp, in);

    if(BTYPE == 3) return 20; // error: invalid BTYPE
    else if(BTYPE == 0) error = inflateNoCompression(out, in, &bp, &pos, insize); // no compression
    else error = inflateHuffmanBlock(out, in, &bp, &pos, insize, BTYPE); // compression, BTYPE 01 or 10

    if(error) return error;
  }

  return error;
}

// Inflate a buffer. Inflate is the decompression step of deflate. Out buffer must be freed after use.
unsigned lodepng_inflate(unsigned char** out, size_t* outsize,
                         const unsigned char* in, size_t insize)
{
  ucvector v;
  ucvector_init_buffer(&v, *out, *outsize);
  unsigned error = lodepng_inflatev(&v, in, insize);
  *out = v.data;
  *outsize = v.size;
  return error;
}


//////////////////////////////////////////////////////////////////////////// 
/// Deflator (Compressor)                                                  / 
//////////////////////////////////////////////////////////////////////////// 

static const size_t MAX_SUPPORTED_DEFLATE_LENGTH = 258;

// bitlen is the size in bits of the code
static void addHuffmanSymbol(size_t* bp, ucvector* compressed, unsigned code, unsigned bitlen)
{
  addBitsToStreamReversed(bp, compressed, code, bitlen);
}

/*search the index in the array, that has the largest value smaller than or equal to the given value,
given array must be sorted (if no value is smaller, it returns the size of the given array)*/
static size_t searchCodeIndex(const unsigned* array, size_t array_size, size_t value)
{
  // binary search (only small gain over linear). TODO: use CPU log2 instruction for getting symbols instead
  size_t left = 1;
  size_t right = array_size - 1;

  while(left <= right) {
    size_t mid = (left + right) >> 1;
    if (array[mid] >= value) right = mid - 1;
    else left = mid + 1;
  }
  if(left >= array_size || array[left] > value) left--;
  return left;
}

static void addLengthDistance(uivector* values, size_t length, size_t distance)
{
  /*values in encoded vector are those used by deflate:
  0-255: literal bytes
  256: end
  257-285: length/distance pair (length code, followed by extra length bits, distance code, extra distance bits)
  286-287: invalid*/

  unsigned length_code = (unsigned)searchCodeIndex(LENGTHBASE, 29, length);
  unsigned extra_length = (unsigned)(length - LENGTHBASE[length_code]);
  unsigned dist_code = (unsigned)searchCodeIndex(DISTANCEBASE, 30, distance);
  unsigned extra_distance = (unsigned)(distance - DISTANCEBASE[dist_code]);

  uivector_push_back(values, length_code + FIRST_LENGTH_CODE_INDEX);
  uivector_push_back(values, extra_length);
  uivector_push_back(values, dist_code);
  uivector_push_back(values, extra_distance);
}

/*3 bytes of data get encoded into two bytes. The hash cannot use more than 3
bytes as input because 3 is the minimum match length for deflate*/
static const unsigned HASH_NUM_VALUES = 65536;
static const unsigned HASH_BIT_MASK = 65535; // HASH_NUM_VALUES - 1, but C90 does not like that as initializer

typedef struct Hash
{
  int* head; // hash value to head circular pos - can be outdated if went around window
  // circular pos to prev circular pos
  unsigned short* chain;
  int* val; // circular pos to hash value

  /*TODO: do this not only for zeros but for any repeated byte. However for PNG
  it's always going to be the zeros that dominate, so not important for PNG*/
  int* headz; // similar to head, but for chainz
  unsigned short* chainz; // those with same amount of zeros
  unsigned short* zeros; // length of zeros streak, used as a second hash chain
} Hash;

static unsigned hash_init(Hash* hash, unsigned windowsize)
{
  unsigned i;
  hash->head = (int*)malloc(sizeof(int) * HASH_NUM_VALUES);
  hash->val = (int*)malloc(sizeof(int) * windowsize);
  hash->chain = (unsigned short*)malloc(sizeof(unsigned short) * windowsize);

  hash->zeros = (unsigned short*)malloc(sizeof(unsigned short) * windowsize);
  hash->headz = (int*)malloc(sizeof(int) * (MAX_SUPPORTED_DEFLATE_LENGTH + 1));
  hash->chainz = (unsigned short*)malloc(sizeof(unsigned short) * windowsize);

  if(!hash->head || !hash->chain || !hash->val  || !hash->headz|| !hash->chainz || !hash->zeros)
  {
    return 83; // alloc fail
  }

  // initialize hash table
  for(i = 0; i != HASH_NUM_VALUES; ++i) hash->head[i] = -1;
  for(i = 0; i != windowsize; ++i) hash->val[i] = -1;
  for(i = 0; i != windowsize; ++i) hash->chain[i] = i; // same value as index indicates uninitialized

  for(i = 0; i <= MAX_SUPPORTED_DEFLATE_LENGTH; ++i) hash->headz[i] = -1;
  for(i = 0; i != windowsize; ++i) hash->chainz[i] = i; // same value as index indicates uninitialized

  return 0;
}

static void hash_cleanup(Hash* hash)
{
  free(hash->head);
  free(hash->val);
  free(hash->chain);

  free(hash->zeros);
  free(hash->headz);
  free(hash->chainz);
}

static unsigned getHash(const unsigned char* data, size_t size, size_t pos)
{
  unsigned result = 0;
  if(pos + 2 < size)
  {
    /*A simple shift and xor hash is used. Since the data of PNGs is dominated
    by zeroes due to the filters, a better hash does not have a significant
    effect on speed in traversing the chain, and causes more time spend on
    calculating the hash.*/
    result ^= (unsigned)(data[pos + 0] << 0u);
    result ^= (unsigned)(data[pos + 1] << 4u);
    result ^= (unsigned)(data[pos + 2] << 8u);
  } else {
    size_t amount, i;
    if(pos >= size) return 0;
    amount = size - pos;
    for(i = 0; i != amount; ++i) result ^= (unsigned)(data[pos + i] << (i * 8u));
  }
  return result & HASH_BIT_MASK;
}

static unsigned countZeros(const unsigned char* data, size_t size, size_t pos)
{
  const unsigned char* start = data + pos;
  const unsigned char* end = start + MAX_SUPPORTED_DEFLATE_LENGTH;
  if(end > data + size) end = data + size;
  data = start;
  while(data != end && *data == 0) ++data;
  // subtracting two addresses returned as 32-bit number (max value is MAX_SUPPORTED_DEFLATE_LENGTH)
  return (unsigned)(data - start);
}

// wpos = pos & (windowsize - 1)
static void updateHashChain(Hash* hash, size_t wpos, unsigned hashval, unsigned short numzeros)
{
  hash->val[wpos] = (int)hashval;
  if(hash->head[hashval] != -1) hash->chain[wpos] = hash->head[hashval];
  hash->head[hashval] = (unsigned)wpos;

  hash->zeros[wpos] = numzeros;
  if(hash->headz[numzeros] != -1) hash->chainz[wpos] = hash->headz[numzeros];
  hash->headz[numzeros] = (unsigned)wpos;
}

// LZ77-encode the data. Return value is error code. The input are raw bytes, the output
// is in the form of unsigned integers with codes representing for example literal bytes, or
// length/distance pairs.
// It uses a hash table technique to let it encode faster. When doing LZ77 encoding, a
// sliding window (of windowsize) is used, and all past bytes in that window can be used as
// the "dictionary". A brute force search through all possible distances would be slow, and
// this hash technique is one out of several ways to speed this up.
static unsigned encodeLZ77(uivector* out, Hash* hash,
                           const unsigned char* in, size_t inpos, size_t insize, unsigned windowsize,
                           unsigned minmatch, unsigned nicematch, unsigned lazymatching)
{
  size_t pos;
  unsigned i, error = 0;
  // for large window lengths, assume the user wants no compression loss. Otherwise, max hash chain length speedup.
  unsigned maxchainlength = windowsize >= 8192 ? windowsize : windowsize / 8;
  unsigned maxlazymatch = windowsize >= 8192 ? MAX_SUPPORTED_DEFLATE_LENGTH : 64;

  unsigned usezeros = 1; // not sure if setting it to false for windowsize < 8192 is better or worse
  unsigned numzeros = 0;

  unsigned offset; // the offset represents the distance in LZ77 terminology
  unsigned length;
  unsigned lazy = 0;
  unsigned lazylength = 0, lazyoffset = 0;
  unsigned hashval;
  unsigned current_offset, current_length;
  unsigned prev_offset;
  const unsigned char *lastptr, *foreptr, *backptr;
  unsigned hashpos;

  if(windowsize == 0 || windowsize > 32768) return 60; // error: windowsize smaller/larger than allowed
  if((windowsize & (windowsize - 1)) != 0) return 90; // error: must be power of two

  if(nicematch > MAX_SUPPORTED_DEFLATE_LENGTH) nicematch = MAX_SUPPORTED_DEFLATE_LENGTH;

  for(pos = inpos; pos < insize; ++pos)
  {
    size_t wpos = pos & (windowsize - 1); // position for in 'circular' hash buffers
    unsigned chainlength = 0;

    hashval = getHash(in, insize, pos);

    if(usezeros && hashval == 0)
    {
      if(numzeros == 0) numzeros = countZeros(in, insize, pos);
      else if(pos + numzeros > insize || in[pos + numzeros - 1] != 0) --numzeros;
    }
    else
    {
      numzeros = 0;
    }

    updateHashChain(hash, wpos, hashval, numzeros);

    // the length and offset found for the current position
    length = 0;
    offset = 0;

    hashpos = hash->chain[wpos];

    lastptr = &in[insize < pos + MAX_SUPPORTED_DEFLATE_LENGTH ? insize : pos + MAX_SUPPORTED_DEFLATE_LENGTH];

    // search for the longest string
    prev_offset = 0;
    for(;;)
    {
      if(chainlength++ >= maxchainlength) break;
      current_offset = (unsigned)(hashpos <= wpos ? wpos - hashpos : wpos - hashpos + windowsize);

      if(current_offset < prev_offset) break; // stop when went completely around the circular buffer
      prev_offset = current_offset;
      if(current_offset > 0)
      {
        // test the next characters
        foreptr = &in[pos];
        backptr = &in[pos - current_offset];

        // common case in PNGs is lots of zeros. Quickly skip over them as a speedup
        if(numzeros >= 3)
        {
          unsigned skip = hash->zeros[hashpos];
          if(skip > numzeros) skip = numzeros;
          backptr += skip;
          foreptr += skip;
        }

        while(foreptr != lastptr && *backptr == *foreptr) // maximum supported length by deflate is max length
        {
          ++backptr;
          ++foreptr;
        }
        current_length = (unsigned)(foreptr - &in[pos]);

        if(current_length > length)
        {
          length = current_length; // the longest length
          offset = current_offset; // the offset that is related to this longest length
          /*jump out once a length of max length is found (speed gain). This also jumps
          out if length is MAX_SUPPORTED_DEFLATE_LENGTH*/
          if(current_length >= nicematch) break;
        }
      }

      if(hashpos == hash->chain[hashpos]) break;

      if(numzeros >= 3 && length > numzeros)
      {
        hashpos = hash->chainz[hashpos];
        if(hash->zeros[hashpos] != numzeros) break;
      }
      else
      {
        hashpos = hash->chain[hashpos];
        // outdated hash value, happens if particular value was not encountered in whole last window
        if(hash->val[hashpos] != (int)hashval) break;
      }
    }

    if(lazymatching)
    {
      if(!lazy && length >= 3 && length <= maxlazymatch && length < MAX_SUPPORTED_DEFLATE_LENGTH)
      {
        lazy = 1;
        lazylength = length;
        lazyoffset = offset;
        continue; // try the next byte
      }
      if(lazy)
      {
        lazy = 0;
        if(pos == 0) ERROR_BREAK(81);
        if(length > lazylength + 1)
        {
          // push the previous character as literal
          if(!uivector_push_back(out, in[pos - 1])) ERROR_BREAK(83 /*alloc fail*/);
        }
        else
        {
          length = lazylength;
          offset = lazyoffset;
          hash->head[hashval] = -1; // the same hashchain update will be done, this ensures no wrong alteration
          hash->headz[numzeros] = -1; // idem
          --pos;
        }
      }
    }
    if(length >= 3 && offset > windowsize) ERROR_BREAK(86 /*too big (or overflown negative) offset*/);

    // encode it as length/distance pair or literal value
    if(length < 3) // only lengths of 3 or higher are supported as length/distance pair
    {
      if(!uivector_push_back(out, in[pos])) ERROR_BREAK(83 /*alloc fail*/);
    }
    else if(length < minmatch || (length == 3 && offset > 4096))
    {
      /*compensate for the fact that longer offsets have more extra bits, a
      length of only 3 may be not worth it then*/
      if(!uivector_push_back(out, in[pos])) ERROR_BREAK(83 /*alloc fail*/);
    }
    else
    {
      addLengthDistance(out, length, offset);
      for(i = 1; i < length; ++i)
      {
        ++pos;
        wpos = pos & (windowsize - 1);
        hashval = getHash(in, insize, pos);
        if(usezeros && hashval == 0)
        {
          if(numzeros == 0) numzeros = countZeros(in, insize, pos);
          else if(pos + numzeros > insize || in[pos + numzeros - 1] != 0) --numzeros;
        }
        else
        {
          numzeros = 0;
        }
        updateHashChain(hash, wpos, hashval, numzeros);
      }
    }
  } // end of the loop through each character of input

  return error;
}

///////////////////////////////////////////////////////////////////////////// 

/*
write the lz77-encoded data, which has lit, len and dist codes, to compressed stream using huffman trees.
tree_ll: the tree for lit and len codes.
tree_d: the tree for distance codes.
*/
static void writeLZ77data(size_t* bp, ucvector* out, const uivector* lz77_encoded,
                          const HuffmanTree* tree_ll, const HuffmanTree* tree_d)
{
  size_t i = 0;
  for(i = 0; i != lz77_encoded->size; ++i)
  {
    unsigned val = lz77_encoded->data[i];
    addHuffmanSymbol(bp, out, HuffmanTree_getCode(tree_ll, val), HuffmanTree_getLength(tree_ll, val));
    if(val > 256) // for a length code, 3 more things have to be added
    {
      unsigned length_index = val - FIRST_LENGTH_CODE_INDEX;
      unsigned n_length_extra_bits = LENGTHEXTRA[length_index];
      unsigned length_extra_bits = lz77_encoded->data[++i];

      unsigned distance_code = lz77_encoded->data[++i];

      unsigned distance_index = distance_code;
      unsigned n_distance_extra_bits = DISTANCEEXTRA[distance_index];
      unsigned distance_extra_bits = lz77_encoded->data[++i];

      addBitsToStream(bp, out, length_extra_bits, n_length_extra_bits);
      addHuffmanSymbol(bp, out, HuffmanTree_getCode(tree_d, distance_code),
                       HuffmanTree_getLength(tree_d, distance_code));
      addBitsToStream(bp, out, distance_extra_bits, n_distance_extra_bits);
    }
  }
}

// Deflate for a block of type "dynamic", that is, with freely, optimally, created huffman trees
static unsigned deflateDynamic(ucvector* out, size_t* bp, Hash* hash,
                               const unsigned char* data, size_t datapos, size_t dataend,
                               const LodePNGCompressSettings* settings, unsigned final)
{
  unsigned error = 0;

  /*
  A block is compressed as follows: The PNG data is lz77 encoded, resulting in
  literal bytes and length/distance pairs. This is then huffman compressed with
  two huffman trees. One huffman tree is used for the lit and len values ("ll"),
  another huffman tree is used for the dist values ("d"). These two trees are
  stored using their code lengths, and to compress even more these code lengths
  are also run-length encoded and huffman compressed. This gives a huffman tree
  of code lengths "cl". The code lenghts used to describe this third tree are
  the code length code lengths ("clcl").
  */

  // The lz77 encoded data, represented with integers since there will also be length and distance codes in it
  uivector lz77_encoded;
  HuffmanTree tree_ll; // tree for lit,len values
  HuffmanTree tree_d; // tree for distance codes
  HuffmanTree tree_cl; // tree for encoding the code lengths representing tree_ll and tree_d
  uivector frequencies_ll; // frequency of lit,len codes
  uivector frequencies_d; // frequency of dist codes
  uivector frequencies_cl; // frequency of code length codes
  uivector bitlen_lld; // lit,len,dist code lenghts (int bits), literally (without repeat codes).
  uivector bitlen_lld_e; // bitlen_lld encoded with repeat codes (this is a rudemtary run length compression)
  /*bitlen_cl is the code length code lengths ("clcl"). The bit lengths of codes to represent tree_cl
  (these are written as is in the file, it would be crazy to compress these using yet another huffman
  tree that needs to be represented by yet another set of code lengths)*/
  uivector bitlen_cl;
  size_t datasize = dataend - datapos;

  /*
  Due to the huffman compression of huffman tree representations ("two levels"), there are some anologies:
  bitlen_lld is to tree_cl what data is to tree_ll and tree_d.
  bitlen_lld_e is to bitlen_lld what lz77_encoded is to data.
  bitlen_cl is to bitlen_lld_e what bitlen_lld is to lz77_encoded.
  */

  unsigned BFINAL = final;
  size_t numcodes_ll, numcodes_d, i;
  unsigned HLIT, HDIST, HCLEN;

  uivector_init(&lz77_encoded);
  HuffmanTree_init(&tree_ll);
  HuffmanTree_init(&tree_d);
  HuffmanTree_init(&tree_cl);
  uivector_init(&frequencies_ll);
  uivector_init(&frequencies_d);
  uivector_init(&frequencies_cl);
  uivector_init(&bitlen_lld);
  uivector_init(&bitlen_lld_e);
  uivector_init(&bitlen_cl);

  /*This while loop never loops due to a break at the end, it is here to
  allow breaking out of it to the cleanup phase on error conditions.*/
  while(!error)
  {
    error = encodeLZ77(&lz77_encoded, hash, data, datapos, dataend, settings->windowsize,
                        settings->minmatch, settings->nicematch, settings->lazymatching);
    if(error) break;

    if(!uivector_resizev(&frequencies_ll, 286, 0)) ERROR_BREAK(83 /*alloc fail*/);
    if(!uivector_resizev(&frequencies_d, 30, 0)) ERROR_BREAK(83 /*alloc fail*/);

    // Count the frequencies of lit, len and dist codes
    for(i = 0; i != lz77_encoded.size; ++i)
    {
      unsigned symbol = lz77_encoded.data[i];
      ++frequencies_ll.data[symbol];
      if(symbol > 256)
      {
        unsigned dist = lz77_encoded.data[i + 2];
        ++frequencies_d.data[dist];
        i += 3;
      }
    }
    frequencies_ll.data[256] = 1; // there will be exactly 1 end code, at the end of the block

    // Make both huffman trees, one for the lit and len codes, one for the dist codes
    error = HuffmanTree_makeFromFrequencies(&tree_ll, frequencies_ll.data, 257, frequencies_ll.size, 15);
    if(error) break;
    // 2, not 1, is chosen for mincodes: some buggy PNG decoders require at least 2 symbols in the dist tree
    error = HuffmanTree_makeFromFrequencies(&tree_d, frequencies_d.data, 2, frequencies_d.size, 15);
    if(error) break;

    numcodes_ll = tree_ll.numcodes; if(numcodes_ll > 286) numcodes_ll = 286;
    numcodes_d = tree_d.numcodes; if(numcodes_d > 30) numcodes_d = 30;
    // store the code lengths of both generated trees in bitlen_lld
    for(i = 0; i != numcodes_ll; ++i) uivector_push_back(&bitlen_lld, HuffmanTree_getLength(&tree_ll, (unsigned)i));
    for(i = 0; i != numcodes_d; ++i) uivector_push_back(&bitlen_lld, HuffmanTree_getLength(&tree_d, (unsigned)i));

    /*run-length compress bitlen_ldd into bitlen_lld_e by using repeat codes 16 (copy length 3-6 times),
    17 (3-10 zeroes), 18 (11-138 zeroes)*/
    for(i = 0; i != (unsigned)bitlen_lld.size; ++i)
    {
      unsigned j = 0; // amount of repititions
      while(i + j + 1 < (unsigned)bitlen_lld.size && bitlen_lld.data[i + j + 1] == bitlen_lld.data[i]) ++j;

      if(bitlen_lld.data[i] == 0 && j >= 2) // repeat code for zeroes
      {
        ++j; // include the first zero
        if(j <= 10) // repeat code 17 supports max 10 zeroes
        {
          uivector_push_back(&bitlen_lld_e, 17);
          uivector_push_back(&bitlen_lld_e, j - 3);
        }
        else // repeat code 18 supports max 138 zeroes
        {
          if(j > 138) j = 138;
          uivector_push_back(&bitlen_lld_e, 18);
          uivector_push_back(&bitlen_lld_e, j - 11);
        }
        i += (j - 1);
      }
      else if(j >= 3) // repeat code for value other than zero
      {
        size_t k;
        unsigned num = j / 6, rest = j % 6;
        uivector_push_back(&bitlen_lld_e, bitlen_lld.data[i]);
        for(k = 0; k < num; ++k)
        {
          uivector_push_back(&bitlen_lld_e, 16);
          uivector_push_back(&bitlen_lld_e, 6 - 3);
        }
        if(rest >= 3)
        {
          uivector_push_back(&bitlen_lld_e, 16);
          uivector_push_back(&bitlen_lld_e, rest - 3);
        }
        else j -= rest;
        i += j;
      }
      else // too short to benefit from repeat code
      {
        uivector_push_back(&bitlen_lld_e, bitlen_lld.data[i]);
      }
    }

    // generate tree_cl, the huffmantree of huffmantrees

    if(!uivector_resizev(&frequencies_cl, NUM_CODE_LENGTH_CODES, 0)) ERROR_BREAK(83 /*alloc fail*/);
    for(i = 0; i != bitlen_lld_e.size; ++i)
    {
      ++frequencies_cl.data[bitlen_lld_e.data[i]];
      /*after a repeat code come the bits that specify the number of repetitions,
      those don't need to be in the frequencies_cl calculation*/
      if(bitlen_lld_e.data[i] >= 16) ++i;
    }

    error = HuffmanTree_makeFromFrequencies(&tree_cl, frequencies_cl.data,
                                            frequencies_cl.size, frequencies_cl.size, 7);
    if(error) break;

    if(!uivector_resize(&bitlen_cl, tree_cl.numcodes)) ERROR_BREAK(83 /*alloc fail*/);
    for(i = 0; i != tree_cl.numcodes; ++i)
    {
      // lenghts of code length tree is in the order as specified by deflate
      bitlen_cl.data[i] = HuffmanTree_getLength(&tree_cl, CLCL_ORDER[i]);
    }
    while(bitlen_cl.data[bitlen_cl.size - 1] == 0 && bitlen_cl.size > 4)
    {
      // remove zeros at the end, but minimum size must be 4
      if(!uivector_resize(&bitlen_cl, bitlen_cl.size - 1)) ERROR_BREAK(83 /*alloc fail*/);
    }
    if(error) break;

    /*
    Write everything into the output

    After the BFINAL and BTYPE, the dynamic block consists out of the following:
    - 5 bits HLIT, 5 bits HDIST, 4 bits HCLEN
    - (HCLEN+4)*3 bits code lengths of code length alphabet
    - HLIT + 257 code lenghts of lit/length alphabet (encoded using the code length
      alphabet, + possible repetition codes 16, 17, 18)
    - HDIST + 1 code lengths of distance alphabet (encoded using the code length
      alphabet, + possible repetition codes 16, 17, 18)
    - compressed data
    - 256 (end code)
    */

    // Write block type
    addBitToStream(bp, out, BFINAL);
    addBitToStream(bp, out, 0); // first bit of BTYPE "dynamic"
    addBitToStream(bp, out, 1); // second bit of BTYPE "dynamic"

    // write the HLIT, HDIST and HCLEN values
    HLIT = (unsigned)(numcodes_ll - 257);
    HDIST = (unsigned)(numcodes_d - 1);
    HCLEN = (unsigned)bitlen_cl.size - 4;
    // trim zeroes for HCLEN. HLIT and HDIST were already trimmed at tree creation
    while(!bitlen_cl.data[HCLEN + 4 - 1] && HCLEN > 0) --HCLEN;
    addBitsToStream(bp, out, HLIT, 5);
    addBitsToStream(bp, out, HDIST, 5);
    addBitsToStream(bp, out, HCLEN, 4);

    // write the code lenghts of the code length alphabet
    for(i = 0; i != HCLEN + 4; ++i) addBitsToStream(bp, out, bitlen_cl.data[i], 3);

    // write the lenghts of the lit/len AND the dist alphabet
    for(i = 0; i != bitlen_lld_e.size; ++i)
    {
      addHuffmanSymbol(bp, out, HuffmanTree_getCode(&tree_cl, bitlen_lld_e.data[i]),
                       HuffmanTree_getLength(&tree_cl, bitlen_lld_e.data[i]));
      // extra bits of repeat codes
      if(bitlen_lld_e.data[i] == 16) addBitsToStream(bp, out, bitlen_lld_e.data[++i], 2);
      else if(bitlen_lld_e.data[i] == 17) addBitsToStream(bp, out, bitlen_lld_e.data[++i], 3);
      else if(bitlen_lld_e.data[i] == 18) addBitsToStream(bp, out, bitlen_lld_e.data[++i], 7);
    }

    // write the compressed data symbols
    writeLZ77data(bp, out, &lz77_encoded, &tree_ll, &tree_d);
    // error: the length of the end code 256 must be larger than 0
    if(HuffmanTree_getLength(&tree_ll, 256) == 0) ERROR_BREAK(64);

    // write the end code
    addHuffmanSymbol(bp, out, HuffmanTree_getCode(&tree_ll, 256), HuffmanTree_getLength(&tree_ll, 256));

    break; // end of error-while
  }

  // cleanup
  uivector_cleanup(&lz77_encoded);
  HuffmanTree_cleanup(&tree_ll);
  HuffmanTree_cleanup(&tree_d);
  HuffmanTree_cleanup(&tree_cl);
  uivector_cleanup(&frequencies_ll);
  uivector_cleanup(&frequencies_d);
  uivector_cleanup(&frequencies_cl);
  uivector_cleanup(&bitlen_lld_e);
  uivector_cleanup(&bitlen_lld);
  uivector_cleanup(&bitlen_cl);

  return error;
}

static unsigned lodepng_deflatev(ucvector* out, const unsigned char* in, size_t insize,
                                 const LodePNGCompressSettings* settings)
{
  unsigned error = 0;
  size_t i, blocksize, numdeflateblocks;
  size_t bp = 0; // the bit pointer
  Hash hash;

  // on PNGs, deflate blocks of 65-262k seem to give most dense encoding
  blocksize = insize / 8 + 8;
  if(blocksize < 65536) blocksize = 65536;
  if(blocksize > 262144) blocksize = 262144;

  numdeflateblocks = (insize + blocksize - 1) / blocksize;
  if(numdeflateblocks == 0) numdeflateblocks = 1;

  error = hash_init(&hash, settings->windowsize);
  if(error) return error;

  for(i = 0; i != numdeflateblocks && !error; ++i)
  {
    unsigned final = (i == numdeflateblocks - 1);
    size_t start = i * blocksize;
    size_t end = start + blocksize;
    if(end > insize) end = insize;

    error = deflateDynamic(out, &bp, &hash, in, start, end, settings, final);
  }

  hash_cleanup(&hash);

  return error;
}

// Compress a buffer with deflate. See RFC 1951. Out buffer must be freed after use.
unsigned lodepng_deflate(unsigned char** out, size_t* outsize,
                         const unsigned char* in, size_t insize,
                         const LodePNGCompressSettings* settings)
{
  ucvector v;
  ucvector_init_buffer(&v, *out, *outsize);
  unsigned error = lodepng_deflatev(&v, in, insize, settings);
  *out = v.data;
  *outsize = v.size;
  return error;
}


//////////////////////////////////////////////////////////////////////////// 
/// Adler32                                                                  
//////////////////////////////////////////////////////////////////////////// 

// Return the adler32 of the bytes data[0..len-1]
static unsigned adler32(const unsigned char* data, unsigned len)
{
    unsigned adler = 1;
    unsigned s1 = adler & 0xffff;
    unsigned s2 = (adler >> 16) & 0xffff;

    while (len > 0)
    {
        // at least 5550 sums can be done before the sums overflow, saving a lot of module divisions
        unsigned amount = len > 5552 ? 5552 : len;
        len -= amount;
        while (amount > 0)
        {
            s1 += (*data++);
            s2 += s1;
            --amount;
        }
        s1 %= 65521;
        s2 %= 65521;
    }

    return (s2 << 16) | s1;
}

//////////////////////////////////////////////////////////////////////////// 
/// Zlib                                                                   / 
//////////////////////////////////////////////////////////////////////////// 

// Decompresses Zlib data. Reallocates the out buffer and appends the data. The
// data must be according to the zlib specification.
// Either, *out must be NULL and *outsize must be 0, or, *out must be a valid
// buffer and *outsize its size in bytes. out must be freed by user after usage.
unsigned lodepng_zlib_decompress(unsigned char** out, size_t* outsize, const unsigned char* in,
                                 size_t insize)
{
  unsigned error = 0;

  if(insize < 2) return 53; // error, size of zlib data too small
  // read information from zlib header
  if((in[0] * 256 + in[1]) % 31 != 0)
  {
    // error: 256 * in[0] + in[1] must be a multiple of 31, the FCHECK value is supposed to be made that way
    return 24;
  }

  unsigned CM = in[0] & 15;
  unsigned CINFO = (in[0] >> 4) & 15;
  // FCHECK = in[1] & 31;*/ /*FCHECK is already tested above
  unsigned FDICT = (in[1] >> 5) & 1;
  // FLEVEL = (in[1] >> 6) & 3;*/ /*FLEVEL is not used here

  if(CM != 8 || CINFO > 7)
  {
    // error: only compression method 8: inflate with sliding window of 32k is supported by the PNG spec
    return 25;
  }
  if(FDICT != 0)
  {
    /*error: the specification of PNG says about the zlib stream:
      "The additional flags shall not specify a preset dictionary."*/
    return 26;
  }

  error = lodepng_inflate(out, outsize, in + 2, insize - 2);
  if(error) return error;

  return 0; // no error
}

// Compresses data with Zlib. Reallocates the out buffer and appends the data.
// Zlib adds a small header and trailer around the deflate data.
// The data is output in the format of the zlib specification.
// Either, *out must be NULL and *outsize must be 0, or, *out must be a valid
// buffer and *outsize its size in bytes. out must be freed by user after usage.
unsigned lodepng_zlib_compress(unsigned char** out, size_t* outsize, const unsigned char* in,
                               size_t insize, const LodePNGCompressSettings* settings)
{
  /*initially, *out must be NULL and outsize 0, if you just give some random *out
  that's pointing to a non allocated buffer, this'll crash*/
  ucvector outv;
  size_t i;
  unsigned error;
  unsigned char* deflatedata = 0;
  size_t deflatesize = 0;

  // zlib data: 1 byte CMF (CM+CINFO), 1 byte FLG, deflate data, 4 byte ADLER32 checksum of the Decompressed data
  unsigned CMF = 120; // 0b01111000: CM 8, CINFO 7. With CINFO 7, any window size up to 32768 can be used.
  unsigned FLEVEL = 0;
  unsigned FDICT = 0;
  unsigned CMFFLG = 256 * CMF + FDICT * 32 + FLEVEL * 64;
  unsigned FCHECK = 31 - CMFFLG % 31;
  CMFFLG += FCHECK;

  // ucvector-controlled version of the output buffer, for dynamic array
  ucvector_init_buffer(&outv, *out, *outsize);

  ucvector_push_back(&outv, (unsigned char)(CMFFLG >> 8));
  ucvector_push_back(&outv, (unsigned char)(CMFFLG & 255));

  error = lodepng_deflate(&deflatedata, &deflatesize, in, insize, settings);

  if(!error)
  {
    unsigned ADLER32 = adler32(in, (unsigned)insize);
    for(i = 0; i != deflatesize; ++i) ucvector_push_back(&outv, deflatedata[i]);
    free(deflatedata);
    lodepng_add32bitInt(&outv, ADLER32);
  }

  *out = outv.data;
  *outsize = outv.size;

  return error;
}


//////////////////////////////////////////////////////////////////////////// 

// this is a good tradeoff between speed and compression ratio
#define DEFAULT_WINDOWSIZE 2048

void lodepng_compress_settings_init(LodePNGCompressSettings* settings)
{
  // compress with dynamic huffman tree (not in the mathematical sense, just not the predefined one)
  settings->windowsize = DEFAULT_WINDOWSIZE;
  settings->minmatch = 3;
  settings->nicematch = 128;
  settings->lazymatching = 1;
}


//////////////////////////////////////////////////////////////////////////// 
//////////////////////////////////////////////////////////////////////////// 
//// End of Zlib related code. Begin of PNG related code.                 // 
//////////////////////////////////////////////////////////////////////////// 
//////////////////////////////////////////////////////////////////////////// 


// compute CRC32 without lookup tables. Polynomial: 0xedb88320
// From http://create.stephan-brumme.com/crc32/#tableless
static inline unsigned lodepng_crc32(const void* data, size_t length)
{
    unsigned crc = 0xffffffffu;
    const unsigned char* current = (const unsigned char*)data;
    while (length--)
    {
        unsigned char s = unsigned char(crc) ^ *current++;
        unsigned low = (s ^ (s << 6)) & 0xFF;
        unsigned a = (low * ((1 << 23) + (1 << 14) + (1 << 2)));
        crc = (crc >> 8) ^
            (low * ((1 << 24) + (1 << 16) + (1 << 8))) ^
            a ^
            (a >> 1) ^
            (low * ((1 << 20) + (1 << 12))) ^
            (low << 19) ^
            (low << 17) ^
            (low >> 2);
    }
    return ~crc;
}


//////////////////////////////////////////////////////////////////////////// 
/// Reading and writing single bits and bytes from/to stream for LodePNG   / 
//////////////////////////////////////////////////////////////////////////// 

static unsigned char readBitFromReversedStream(size_t* bitpointer, const unsigned char* bitstream)
{
  unsigned char result = (unsigned char)((bitstream[(*bitpointer) >> 3] >> (7 - ((*bitpointer) & 0x7))) & 1);
  ++(*bitpointer);
  return result;
}

static unsigned readBitsFromReversedStream(size_t* bitpointer, const unsigned char* bitstream, size_t nbits)
{
  unsigned result = 0;
  for(size_t i = 0 ; i < nbits; ++i)
  {
    result <<= 1;
    result |= (unsigned)readBitFromReversedStream(bitpointer, bitstream);
  }
  return result;
}

static void setBitOfReversedStream0(size_t* bitpointer, unsigned char* bitstream, unsigned char bit)
{
  // the current bit in bitstream must be 0 for this to work
  if(bit)
  {
    // earlier bit of huffman code is in a lesser significant bit of an earlier byte
    bitstream[(*bitpointer) >> 3] |= (bit << (7 - ((*bitpointer) & 0x7)));
  }
  ++(*bitpointer);
}

static void setBitOfReversedStream(size_t* bitpointer, unsigned char* bitstream, unsigned char bit)
{
  // the current bit in bitstream may be 0 or 1 for this to work
  if(bit == 0) bitstream[(*bitpointer) >> 3] &=  (unsigned char)(~(1 << (7 - ((*bitpointer) & 0x7))));
  else         bitstream[(*bitpointer) >> 3] |=  (1 << (7 - ((*bitpointer) & 0x7)));
  ++(*bitpointer);
}

//////////////////////////////////////////////////////////////////////////// 
/// PNG chunks                                                             / 
//////////////////////////////////////////////////////////////////////////// 

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
unsigned lodepng_chunk_length(const unsigned char* chunk)
{
  return lodepng_read32bitInt(&chunk[0]);
}

// check if the type is the given type
unsigned char lodepng_chunk_type_equals(const unsigned char* chunk, const char* type)
{
  if(strlen(type) != 4) return 0;
  return (chunk[4] == type[0] && chunk[5] == type[1] && chunk[6] == type[2] && chunk[7] == type[3]);
}

const unsigned char* lodepng_chunk_data_const(const unsigned char* chunk)
{
  return &chunk[8];
}

// generates the correct CRC from the data and puts it in the last 4 bytes of the chunk
void lodepng_chunk_generate_crc(unsigned char* chunk)
{
  unsigned length = lodepng_chunk_length(chunk);
  unsigned CRC = lodepng_crc32(&chunk[4], length + 4);
  lodepng_set32bitInt(chunk + 8 + length, CRC);
}

const unsigned char* lodepng_chunk_next_const(const unsigned char* chunk)
{
  unsigned total_chunk_length = lodepng_chunk_length(chunk) + 12;
  return &chunk[total_chunk_length];
}

// Appends new chunk to out. The chunk to append is given by giving its length, type
// and data separately. The type is a 4-letter string.
// The out variable and outlength are updated to reflect the new reallocated buffer.
// Return error code (0 if it went ok)
unsigned lodepng_chunk_create(unsigned char** out, size_t* outlength, unsigned length,
                              const char* type, const unsigned char* data)
{
  unsigned char *chunk, *new_buffer;
  size_t new_length = (*outlength) + length + 12;
  if(new_length < length + 12 || new_length < (*outlength)) return 77; // integer overflow happened
  new_buffer = (unsigned char*)realloc(*out, new_length);
  if(!new_buffer) return 83; // alloc fail
  (*out) = new_buffer;
  (*outlength) = new_length;
  chunk = &(*out)[(*outlength) - length - 12];

  // 1: length
  lodepng_set32bitInt(chunk, (unsigned)length);

  // 2: chunk name (4 letters)
  chunk[4] = (unsigned char)type[0];
  chunk[5] = (unsigned char)type[1];
  chunk[6] = (unsigned char)type[2];
  chunk[7] = (unsigned char)type[3];

  // 3: the data
  for(unsigned i = 0; i != length; ++i) chunk[8 + i] = data[i];

  // 4: CRC (of the chunkname characters and the data)
  lodepng_chunk_generate_crc(chunk);

  return 0;
}

//////////////////////////////////////////////////////////////////////////// 
/// Color types and such                                                   / 
//////////////////////////////////////////////////////////////////////////// 

// return type is a LodePNG error code
static unsigned checkColorValidity(LodePNGColorType colortype, unsigned bd) // bd = bitdepth
{
  switch(colortype)
  {
    case 0: if(!(bd == 1 || bd == 2 || bd == 4 || bd == 8 || bd == 16)) return 37; break; // grey
    case 2: if(!(                                 bd == 8 || bd == 16)) return 37; break; // RGB
    case 3: if(!(bd == 1 || bd == 2 || bd == 4 || bd == 8            )) return 37; break; // palette
    case 4: if(!(                                 bd == 8 || bd == 16)) return 37; break; // grey + alpha
    case 6: if(!(                                 bd == 8 || bd == 16)) return 37; break; // RGBA
    default: return 31;
  }
  return 0; // allowed color type / bits combination
}

static unsigned getNumColorChannels(LodePNGColorType colortype)
{
  switch(colortype)
  {
    case 0: return 1; // grey
    case 2: return 3; // RGB
    case 3: return 1; // palette
    case 4: return 2; // grey + alpha
    case 6: return 4; // RGBA
  }
  return 0; // unexisting color type
}

static unsigned lodepng_get_bpp_lct(LodePNGColorType colortype, unsigned bitdepth)
{
  // bits per pixel is amount of channels * bits per channel
  return getNumColorChannels(colortype) * bitdepth;
}

//////////////////////////////////////////////////////////////////////////// 

void lodepng_color_mode_init(LodePNGColorMode* info)
{
  info->key_defined = 0;
  info->key_r = info->key_g = info->key_b = 0;
  info->colortype = LCT_RGBA;
  info->bitdepth = 8;
  info->palette = 0;
  info->palettesize = 0;
}

void lodepng_color_mode_cleanup(LodePNGColorMode* info)
{
  lodepng_palette_clear(info);
}

unsigned lodepng_color_mode_copy(LodePNGColorMode* dest, const LodePNGColorMode* source)
{
  size_t i;
  lodepng_color_mode_cleanup(dest);
  *dest = *source;
  if(source->palette)
  {
    dest->palette = (unsigned char*)malloc(1024);
    if(!dest->palette && source->palettesize) return 83; // alloc fail
    for(i = 0; i != source->palettesize * 4; ++i) dest->palette[i] = source->palette[i];
  }
  return 0;
}

static int lodepng_color_mode_equal(const LodePNGColorMode* a, const LodePNGColorMode* b)
{
  size_t i;
  if(a->colortype != b->colortype) return 0;
  if(a->bitdepth != b->bitdepth) return 0;
  if(a->key_defined != b->key_defined) return 0;
  if(a->key_defined)
  {
    if(a->key_r != b->key_r) return 0;
    if(a->key_g != b->key_g) return 0;
    if(a->key_b != b->key_b) return 0;
  }
  if(a->palettesize != b->palettesize) return 0;
  for(i = 0; i != a->palettesize * 4; ++i)
  {
    if(a->palette[i] != b->palette[i]) return 0;
  }
  return 1;
}

void lodepng_palette_clear(LodePNGColorMode* info)
{
  if(info->palette) free(info->palette);
  info->palette = 0;
  info->palettesize = 0;
}

unsigned lodepng_palette_add(LodePNGColorMode* info,
                             unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
  unsigned char* data;
  /*the same resize technique as C++ std::vectors is used, and here it's made so that for a palette with
  the max of 256 colors, it'll have the exact alloc size*/
  if(!info->palette) // allocate palette if empty
  {
    // room for 256 colors with 4 bytes each
    data = (unsigned char*)realloc(info->palette, 1024);
    if(!data) return 83; // alloc fail
    else info->palette = data;
  }
  info->palette[4 * info->palettesize + 0] = r;
  info->palette[4 * info->palettesize + 1] = g;
  info->palette[4 * info->palettesize + 2] = b;
  info->palette[4 * info->palettesize + 3] = a;
  ++info->palettesize;
  return 0;
}

unsigned lodepng_get_bpp(const LodePNGColorMode* info)
{
  // calculate bits per pixel out of colortype and bitdepth
  return lodepng_get_bpp_lct(info->colortype, info->bitdepth);
}

unsigned lodepng_get_channels(const LodePNGColorMode* info)
{
  return getNumColorChannels(info->colortype);
}

unsigned lodepng_is_greyscale_type(const LodePNGColorMode* info)
{
  return info->colortype == LCT_GREY || info->colortype == LCT_GREY_ALPHA;
}

unsigned lodepng_is_alpha_type(const LodePNGColorMode* info)
{
  return (info->colortype & 4) != 0; // 4 or 6
}

unsigned lodepng_is_palette_type(const LodePNGColorMode* info)
{
  return info->colortype == LCT_PALETTE;
}

unsigned lodepng_has_palette_alpha(const LodePNGColorMode* info)
{
  for(size_t i = 0; i != info->palettesize; ++i)
  {
    if(info->palette[i * 4 + 3] < 255) return 1;
  }
  return 0;
}

unsigned lodepng_can_have_alpha(const LodePNGColorMode* info)
{
  return info->key_defined
      || lodepng_is_alpha_type(info)
      || lodepng_has_palette_alpha(info);
}

size_t lodepng_get_raw_size(unsigned w, unsigned h, const LodePNGColorMode* color)
{
  // will not overflow for any color type if roughly w * h < 268435455
  size_t bpp = lodepng_get_bpp(color);
  size_t n = w * h;
  return ((n / 8) * bpp) + ((n & 7) * bpp + 7) / 8;
}

// in an idat chunk, each scanline is a multiple of 8 bits, unlike the lodepng output buffer
static size_t lodepng_get_raw_size_idat(unsigned w, unsigned h, const LodePNGColorMode* color)
{
  // will not overflow for any color type if roughly w * h < 268435455
  size_t bpp = lodepng_get_bpp(color);
  size_t line = ((w / 8) * bpp) + ((w & 7) * bpp + 7) / 8;
  return h * line;
}

void lodepng_info_init(LodePNGInfo* info)
{
  lodepng_color_mode_init(&info->color);
  info->interlace_method = 0;
}

void lodepng_info_cleanup(LodePNGInfo* info)
{
  lodepng_color_mode_cleanup(&info->color);
}

unsigned lodepng_info_copy(LodePNGInfo* dest, const LodePNGInfo* source)
{
  lodepng_info_cleanup(dest);
  *dest = *source;
  lodepng_color_mode_init(&dest->color);
  CERROR_TRY_RETURN(lodepng_color_mode_copy(&dest->color, &source->color));

  return 0;
}

//////////////////////////////////////////////////////////////////////////// 

// index: bitgroup index, bits: bitgroup size(1, 2 or 4), in: bitgroup value, out: octet array to add bits to
static void addColorBits(unsigned char* out, size_t index, unsigned bits, unsigned in)
{
  unsigned m = bits == 1 ? 7 : bits == 2 ? 3 : 1; // 8 / bits - 1
  // p = the partial index in the byte, e.g. with 4 palettebits it is 0 for first half or 1 for second half
  unsigned p = index & m;
  in &= (1u << bits) - 1u; // filter out any other bits of the input value
  in = in << (bits * (m - p));
  if(p == 0) out[index * bits / 8] = in;
  else out[index * bits / 8] |= in;
}

typedef struct ColorTree ColorTree;

// One node of a color tree
// This is the data structure used to count the number of unique colors and to get a palette
// index for a color. It's like an octree, but because the alpha channel is used too, each
// node has 16 instead of 8 children.
struct ColorTree
{
  ColorTree* children[16]; // up to 16 pointers to ColorTree of next level
  int index; // the payload. Only has a meaningful value if this is in the last level
};

static void color_tree_init(ColorTree* tree)
{
  for(int i = 0; i != 16; ++i) tree->children[i] = 0;
  tree->index = -1;
}

static void color_tree_cleanup(ColorTree* tree)
{
  for(int i = 0; i != 16; ++i)
  {
    if(tree->children[i])
    {
      color_tree_cleanup(tree->children[i]);
      free(tree->children[i]);
    }
  }
}

// returns -1 if color not present, its index otherwise
static int color_tree_get(ColorTree* tree, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
  int bit = 0;
  for(bit = 0; bit < 8; ++bit)
  {
    int i = 8 * ((r >> bit) & 1) + 4 * ((g >> bit) & 1) + 2 * ((b >> bit) & 1) + 1 * ((a >> bit) & 1);
    if(!tree->children[i]) return -1;
    else tree = tree->children[i];
  }
  return tree ? tree->index : -1;
}

static int color_tree_has(ColorTree* tree, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
  return color_tree_get(tree, r, g, b, a) >= 0;
}

/*color is not allowed to already exist.
Index should be >= 0 (it's signed to be compatible with using -1 for "doesn't exist")*/
static void color_tree_add(ColorTree* tree,
                           unsigned char r, unsigned char g, unsigned char b, unsigned char a, unsigned index)
{
  int bit;
  for(bit = 0; bit < 8; ++bit)
  {
    int i = 8 * ((r >> bit) & 1) + 4 * ((g >> bit) & 1) + 2 * ((b >> bit) & 1) + 1 * ((a >> bit) & 1);
    if(!tree->children[i])
    {
      tree->children[i] = (ColorTree*)malloc(sizeof(ColorTree));
      color_tree_init(tree->children[i]);
    }
    tree = tree->children[i];
  }
  tree->index = (int)index;
}

// put a pixel, given its RGBA color, into image of any color type
static unsigned rgba8ToPixel(unsigned char* out, size_t i,
                             const LodePNGColorMode* mode, ColorTree* tree /*for palette*/,
                             unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
  if(mode->colortype == LCT_GREY)
  {
    unsigned char grey = r; /*((unsigned short)r + g + b) / 3*/;
    if(mode->bitdepth == 8) out[i] = grey;
    else if(mode->bitdepth == 16) out[i * 2 + 0] = out[i * 2 + 1] = grey;
    else
    {
      // take the most significant bits of grey
      grey = (grey >> (8 - mode->bitdepth)) & ((1 << mode->bitdepth) - 1);
      addColorBits(out, i, mode->bitdepth, grey);
    }
  }
  else if(mode->colortype == LCT_RGB)
  {
    if(mode->bitdepth == 8)
    {
      out[i * 3 + 0] = r;
      out[i * 3 + 1] = g;
      out[i * 3 + 2] = b;
    }
    else
    {
      out[i * 6 + 0] = out[i * 6 + 1] = r;
      out[i * 6 + 2] = out[i * 6 + 3] = g;
      out[i * 6 + 4] = out[i * 6 + 5] = b;
    }
  }
  else if(mode->colortype == LCT_PALETTE)
  {
    int index = color_tree_get(tree, r, g, b, a);
    if(index < 0) return 82; // color not in palette
    if(mode->bitdepth == 8) out[i] = index;
    else addColorBits(out, i, mode->bitdepth, (unsigned)index);
  }
  else if(mode->colortype == LCT_GREY_ALPHA)
  {
    unsigned char grey = r; /*((unsigned short)r + g + b) / 3*/;
    if(mode->bitdepth == 8)
    {
      out[i * 2 + 0] = grey;
      out[i * 2 + 1] = a;
    }
    else if(mode->bitdepth == 16)
    {
      out[i * 4 + 0] = out[i * 4 + 1] = grey;
      out[i * 4 + 2] = out[i * 4 + 3] = a;
    }
  }
  else if(mode->colortype == LCT_RGBA)
  {
    if(mode->bitdepth == 8)
    {
      out[i * 4 + 0] = r;
      out[i * 4 + 1] = g;
      out[i * 4 + 2] = b;
      out[i * 4 + 3] = a;
    }
    else
    {
      out[i * 8 + 0] = out[i * 8 + 1] = r;
      out[i * 8 + 2] = out[i * 8 + 3] = g;
      out[i * 8 + 4] = out[i * 8 + 5] = b;
      out[i * 8 + 6] = out[i * 8 + 7] = a;
    }
  }

  return 0; // no error
}

// put a pixel, given its RGBA16 color, into image of any color 16-bitdepth type
static void rgba16ToPixel(unsigned char* out, size_t i,
                         const LodePNGColorMode* mode,
                         unsigned short r, unsigned short g, unsigned short b, unsigned short a)
{
  if(mode->colortype == LCT_GREY)
  {
    unsigned short grey = r; /*((unsigned)r + g + b) / 3*/;
    out[i * 2 + 0] = (grey >> 8) & 255;
    out[i * 2 + 1] = grey & 255;
  }
  else if(mode->colortype == LCT_RGB)
  {
    out[i * 6 + 0] = (r >> 8) & 255;
    out[i * 6 + 1] = r & 255;
    out[i * 6 + 2] = (g >> 8) & 255;
    out[i * 6 + 3] = g & 255;
    out[i * 6 + 4] = (b >> 8) & 255;
    out[i * 6 + 5] = b & 255;
  }
  else if(mode->colortype == LCT_GREY_ALPHA)
  {
    unsigned short grey = r; /*((unsigned)r + g + b) / 3*/;
    out[i * 4 + 0] = (grey >> 8) & 255;
    out[i * 4 + 1] = grey & 255;
    out[i * 4 + 2] = (a >> 8) & 255;
    out[i * 4 + 3] = a & 255;
  }
  else if(mode->colortype == LCT_RGBA)
  {
    out[i * 8 + 0] = (r >> 8) & 255;
    out[i * 8 + 1] = r & 255;
    out[i * 8 + 2] = (g >> 8) & 255;
    out[i * 8 + 3] = g & 255;
    out[i * 8 + 4] = (b >> 8) & 255;
    out[i * 8 + 5] = b & 255;
    out[i * 8 + 6] = (a >> 8) & 255;
    out[i * 8 + 7] = a & 255;
  }
}

// Get RGBA8 color of pixel with index i (y * width + x) from the raw image with given color type.
static void getPixelColorRGBA8(unsigned char* r, unsigned char* g,
                               unsigned char* b, unsigned char* a,
                               const unsigned char* in, size_t i,
                               const LodePNGColorMode* mode)
{
  if(mode->colortype == LCT_GREY)
  {
    if(mode->bitdepth == 8)
    {
      *r = *g = *b = in[i];
      if(mode->key_defined && *r == mode->key_r) *a = 0;
      else *a = 255;
    }
    else if(mode->bitdepth == 16)
    {
      *r = *g = *b = in[i * 2 + 0];
      if(mode->key_defined && 256U * in[i * 2 + 0] + in[i * 2 + 1] == mode->key_r) *a = 0;
      else *a = 255;
    }
    else
    {
      unsigned highest = ((1U << mode->bitdepth) - 1U); // highest possible value for this bit depth
      size_t j = i * mode->bitdepth;
      unsigned value = readBitsFromReversedStream(&j, in, mode->bitdepth);
      *r = *g = *b = (value * 255) / highest;
      if(mode->key_defined && value == mode->key_r) *a = 0;
      else *a = 255;
    }
  }
  else if(mode->colortype == LCT_RGB)
  {
    if(mode->bitdepth == 8)
    {
      *r = in[i * 3 + 0]; *g = in[i * 3 + 1]; *b = in[i * 3 + 2];
      if(mode->key_defined && *r == mode->key_r && *g == mode->key_g && *b == mode->key_b) *a = 0;
      else *a = 255;
    }
    else
    {
      *r = in[i * 6 + 0];
      *g = in[i * 6 + 2];
      *b = in[i * 6 + 4];
      if(mode->key_defined && 256U * in[i * 6 + 0] + in[i * 6 + 1] == mode->key_r
         && 256U * in[i * 6 + 2] + in[i * 6 + 3] == mode->key_g
         && 256U * in[i * 6 + 4] + in[i * 6 + 5] == mode->key_b) *a = 0;
      else *a = 255;
    }
  }
  else if(mode->colortype == LCT_PALETTE)
  {
    unsigned index;
    if(mode->bitdepth == 8) index = in[i];
    else
    {
      size_t j = i * mode->bitdepth;
      index = readBitsFromReversedStream(&j, in, mode->bitdepth);
    }

    if(index >= mode->palettesize)
    {
      /*This is an error according to the PNG spec, but common PNG decoders make it black instead.
      Done here too, slightly faster due to no error handling needed.*/
      *r = *g = *b = 0;
      *a = 255;
    }
    else
    {
      *r = mode->palette[index * 4 + 0];
      *g = mode->palette[index * 4 + 1];
      *b = mode->palette[index * 4 + 2];
      *a = mode->palette[index * 4 + 3];
    }
  }
  else if(mode->colortype == LCT_GREY_ALPHA)
  {
    if(mode->bitdepth == 8)
    {
      *r = *g = *b = in[i * 2 + 0];
      *a = in[i * 2 + 1];
    }
    else
    {
      *r = *g = *b = in[i * 4 + 0];
      *a = in[i * 4 + 2];
    }
  }
  else if(mode->colortype == LCT_RGBA)
  {
    if(mode->bitdepth == 8)
    {
      *r = in[i * 4 + 0];
      *g = in[i * 4 + 1];
      *b = in[i * 4 + 2];
      *a = in[i * 4 + 3];
    }
    else
    {
      *r = in[i * 8 + 0];
      *g = in[i * 8 + 2];
      *b = in[i * 8 + 4];
      *a = in[i * 8 + 6];
    }
  }
}

/*Similar to getPixelColorRGBA8, but with all the for loops inside of the color
mode test cases, optimized to convert the colors much faster, when converting
to RGBA or RGB with 8 bit per channel. buffer must be RGBA or RGB output with
enough memory, if has_alpha is true the output is RGBA. mode has the color mode
of the input buffer.*/
static void getPixelColorsRGBA8(unsigned char* buffer, size_t numpixels,
                                unsigned has_alpha, const unsigned char* in,
                                const LodePNGColorMode* mode)
{
  unsigned num_channels = has_alpha ? 4 : 3;
  size_t i;
  if(mode->colortype == LCT_GREY)
  {
    if(mode->bitdepth == 8)
    {
      for(i = 0; i != numpixels; ++i, buffer += num_channels)
      {
        buffer[0] = buffer[1] = buffer[2] = in[i];
        if(has_alpha) buffer[3] = mode->key_defined && in[i] == mode->key_r ? 0 : 255;
      }
    }
    else if(mode->bitdepth == 16)
    {
      for(i = 0; i != numpixels; ++i, buffer += num_channels)
      {
        buffer[0] = buffer[1] = buffer[2] = in[i * 2];
        if(has_alpha) buffer[3] = mode->key_defined && 256U * in[i * 2 + 0] + in[i * 2 + 1] == mode->key_r ? 0 : 255;
      }
    }
    else
    {
      unsigned highest = ((1U << mode->bitdepth) - 1U); // highest possible value for this bit depth
      size_t j = 0;
      for(i = 0; i != numpixels; ++i, buffer += num_channels)
      {
        unsigned value = readBitsFromReversedStream(&j, in, mode->bitdepth);
        buffer[0] = buffer[1] = buffer[2] = (value * 255) / highest;
        if(has_alpha) buffer[3] = mode->key_defined && value == mode->key_r ? 0 : 255;
      }
    }
  }
  else if(mode->colortype == LCT_RGB)
  {
    if(mode->bitdepth == 8)
    {
      for(i = 0; i != numpixels; ++i, buffer += num_channels)
      {
        buffer[0] = in[i * 3 + 0];
        buffer[1] = in[i * 3 + 1];
        buffer[2] = in[i * 3 + 2];
        if(has_alpha) buffer[3] = mode->key_defined && buffer[0] == mode->key_r
           && buffer[1]== mode->key_g && buffer[2] == mode->key_b ? 0 : 255;
      }
    }
    else
    {
      for(i = 0; i != numpixels; ++i, buffer += num_channels)
      {
        buffer[0] = in[i * 6 + 0];
        buffer[1] = in[i * 6 + 2];
        buffer[2] = in[i * 6 + 4];
        if(has_alpha) buffer[3] = mode->key_defined
           && 256U * in[i * 6 + 0] + in[i * 6 + 1] == mode->key_r
           && 256U * in[i * 6 + 2] + in[i * 6 + 3] == mode->key_g
           && 256U * in[i * 6 + 4] + in[i * 6 + 5] == mode->key_b ? 0 : 255;
      }
    }
  }
  else if(mode->colortype == LCT_PALETTE)
  {
    unsigned index;
    size_t j = 0;
    for(i = 0; i != numpixels; ++i, buffer += num_channels)
    {
      if(mode->bitdepth == 8) index = in[i];
      else index = readBitsFromReversedStream(&j, in, mode->bitdepth);

      if(index >= mode->palettesize)
      {
        /*This is an error according to the PNG spec, but most PNG decoders make it black instead.
        Done here too, slightly faster due to no error handling needed.*/
        buffer[0] = buffer[1] = buffer[2] = 0;
        if(has_alpha) buffer[3] = 255;
      }
      else
      {
        buffer[0] = mode->palette[index * 4 + 0];
        buffer[1] = mode->palette[index * 4 + 1];
        buffer[2] = mode->palette[index * 4 + 2];
        if(has_alpha) buffer[3] = mode->palette[index * 4 + 3];
      }
    }
  }
  else if(mode->colortype == LCT_GREY_ALPHA)
  {
    if(mode->bitdepth == 8)
    {
      for(i = 0; i != numpixels; ++i, buffer += num_channels)
      {
        buffer[0] = buffer[1] = buffer[2] = in[i * 2 + 0];
        if(has_alpha) buffer[3] = in[i * 2 + 1];
      }
    }
    else
    {
      for(i = 0; i != numpixels; ++i, buffer += num_channels)
      {
        buffer[0] = buffer[1] = buffer[2] = in[i * 4 + 0];
        if(has_alpha) buffer[3] = in[i * 4 + 2];
      }
    }
  }
  else if(mode->colortype == LCT_RGBA)
  {
    if(mode->bitdepth == 8)
    {
      for(i = 0; i != numpixels; ++i, buffer += num_channels)
      {
        buffer[0] = in[i * 4 + 0];
        buffer[1] = in[i * 4 + 1];
        buffer[2] = in[i * 4 + 2];
        if(has_alpha) buffer[3] = in[i * 4 + 3];
      }
    }
    else
    {
      for(i = 0; i != numpixels; ++i, buffer += num_channels)
      {
        buffer[0] = in[i * 8 + 0];
        buffer[1] = in[i * 8 + 2];
        buffer[2] = in[i * 8 + 4];
        if(has_alpha) buffer[3] = in[i * 8 + 6];
      }
    }
  }
}

/*Get RGBA16 color of pixel with index i (y * width + x) from the raw image with
given color type, but the given color type must be 16-bit itself.*/
static void getPixelColorRGBA16(unsigned short* r, unsigned short* g, unsigned short* b, unsigned short* a,
                                const unsigned char* in, size_t i, const LodePNGColorMode* mode)
{
  if(mode->colortype == LCT_GREY)
  {
    *r = *g = *b = 256 * in[i * 2 + 0] + in[i * 2 + 1];
    if(mode->key_defined && 256U * in[i * 2 + 0] + in[i * 2 + 1] == mode->key_r) *a = 0;
    else *a = 65535;
  }
  else if(mode->colortype == LCT_RGB)
  {
    *r = 256u * in[i * 6 + 0] + in[i * 6 + 1];
    *g = 256u * in[i * 6 + 2] + in[i * 6 + 3];
    *b = 256u * in[i * 6 + 4] + in[i * 6 + 5];
    if(mode->key_defined
       && 256u * in[i * 6 + 0] + in[i * 6 + 1] == mode->key_r
       && 256u * in[i * 6 + 2] + in[i * 6 + 3] == mode->key_g
       && 256u * in[i * 6 + 4] + in[i * 6 + 5] == mode->key_b) *a = 0;
    else *a = 65535;
  }
  else if(mode->colortype == LCT_GREY_ALPHA)
  {
    *r = *g = *b = 256u * in[i * 4 + 0] + in[i * 4 + 1];
    *a = 256u * in[i * 4 + 2] + in[i * 4 + 3];
  }
  else if(mode->colortype == LCT_RGBA)
  {
    *r = 256u * in[i * 8 + 0] + in[i * 8 + 1];
    *g = 256u * in[i * 8 + 2] + in[i * 8 + 3];
    *b = 256u * in[i * 8 + 4] + in[i * 8 + 5];
    *a = 256u * in[i * 8 + 6] + in[i * 8 + 7];
  }
}

unsigned lodepng_convert(unsigned char* out, const unsigned char* in,
                         const LodePNGColorMode* mode_out, const LodePNGColorMode* mode_in,
                         unsigned w, unsigned h)
{
  size_t i;
  ColorTree tree;
  size_t numpixels = w * h;
  unsigned error = 0;

  if(lodepng_color_mode_equal(mode_out, mode_in))
  {
    size_t numbytes = lodepng_get_raw_size(w, h, mode_in);
    for(i = 0; i != numbytes; ++i) out[i] = in[i];
    return 0;
  }

  if(mode_out->colortype == LCT_PALETTE)
  {
    size_t palettesize = mode_out->palettesize;
    const unsigned char* palette = mode_out->palette;
    size_t palsize = (size_t)1u << mode_out->bitdepth;
    /*if the user specified output palette but did not give the values, assume
    they want the values of the input color type (assuming that one is palette).
    Note that we never create a new palette ourselves.*/
    if(palettesize == 0)
    {
      palettesize = mode_in->palettesize;
      palette = mode_in->palette;
      /*if the input was also palette with same bitdepth, then the color types are also
      equal, so copy literally. This to preserve the exact indices that were in the PNG
      even in case there are duplicate colors in the palette.*/
      if (mode_in->colortype == LCT_PALETTE && mode_in->bitdepth == mode_out->bitdepth)
      {
        size_t numbytes = lodepng_get_raw_size(w, h, mode_in);
        for(i = 0; i != numbytes; ++i) out[i] = in[i];
        return 0;
      }
    }
    if(palettesize < palsize) palsize = palettesize;
    color_tree_init(&tree);
    for(i = 0; i != palsize; ++i)
    {
      const unsigned char* p = &palette[i * 4];
      color_tree_add(&tree, p[0], p[1], p[2], p[3], (unsigned)i);
    }
  }

  if(mode_in->bitdepth == 16 && mode_out->bitdepth == 16)
  {
    for(i = 0; i != numpixels; ++i)
    {
      unsigned short r = 0, g = 0, b = 0, a = 0;
      getPixelColorRGBA16(&r, &g, &b, &a, in, i, mode_in);
      rgba16ToPixel(out, i, mode_out, r, g, b, a);
    }
  }
  else if(mode_out->bitdepth == 8 && mode_out->colortype == LCT_RGBA)
  {
    getPixelColorsRGBA8(out, numpixels, 1, in, mode_in);
  }
  else if(mode_out->bitdepth == 8 && mode_out->colortype == LCT_RGB)
  {
    getPixelColorsRGBA8(out, numpixels, 0, in, mode_in);
  }
  else
  {
    unsigned char r = 0, g = 0, b = 0, a = 0;
    for(i = 0; i != numpixels; ++i)
    {
      getPixelColorRGBA8(&r, &g, &b, &a, in, i, mode_in);
      error = rgba8ToPixel(out, i, mode_out, &tree, r, g, b, a);
      if (error) break;
    }
  }

  if(mode_out->colortype == LCT_PALETTE)
  {
    color_tree_cleanup(&tree);
  }

  return error;
}


void lodepng_color_profile_init(LodePNGColorProfile* profile)
{
  profile->colored = 0;
  profile->key = 0;
  profile->key_r = profile->key_g = profile->key_b = 0;
  profile->alpha = 0;
  profile->numcolors = 0;
  profile->bits = 1;
}

// function used for debug purposes with C++
/*void printColorProfile(LodePNGColorProfile* p)
{
  std::cout << "colored: " << (int)p->colored << ", ";
  std::cout << "key: " << (int)p->key << ", ";
  std::cout << "key_r: " << (int)p->key_r << ", ";
  std::cout << "key_g: " << (int)p->key_g << ", ";
  std::cout << "key_b: " << (int)p->key_b << ", ";
  std::cout << "alpha: " << (int)p->alpha << ", ";
  std::cout << "numcolors: " << (int)p->numcolors << ", ";
  std::cout << "bits: " << (int)p->bits << std::endl;
}*/

// Returns how many bits needed to represent given value (max 8 bit)
static unsigned getValueRequiredBits(unsigned char value)
{
  if(value == 0 || value == 255) return 1;
  // The scaling of 2-bit and 4-bit values uses multiples of 85 and 17
  if(value % 17 == 0) return value % 85 == 0 ? 2 : 4;
  return 8;
}

// Get a LodePNGColorProfile of the image.
/*profile must already have been inited with mode.
It's ok to set some parameters of profile to done already.*/
unsigned lodepng_get_color_profile(LodePNGColorProfile* profile,
                                   const unsigned char* in, unsigned w, unsigned h,
                                   const LodePNGColorMode* mode)
{
  unsigned error = 0;
  size_t i;
  ColorTree tree;
  size_t numpixels = w * h;

  unsigned colored_done = lodepng_is_greyscale_type(mode) ? 1 : 0;
  unsigned alpha_done = lodepng_can_have_alpha(mode) ? 0 : 1;
  unsigned numcolors_done = 0;
  unsigned bpp = lodepng_get_bpp(mode);
  unsigned bits_done = bpp == 1 ? 1 : 0;
  unsigned maxnumcolors = 257;
  unsigned sixteen = 0;
  if(bpp <= 8) maxnumcolors = bpp == 1 ? 2 : (bpp == 2 ? 4 : (bpp == 4 ? 16 : 256));

  color_tree_init(&tree);

  // Check if the 16-bit input is truly 16-bit
  if(mode->bitdepth == 16)
  {
    unsigned short r, g, b, a;
    for(i = 0; i != numpixels; ++i)
    {
      getPixelColorRGBA16(&r, &g, &b, &a, in, i, mode);
      if((r & 255) != ((r >> 8) & 255) || (g & 255) != ((g >> 8) & 255) ||
         (b & 255) != ((b >> 8) & 255) || (a & 255) != ((a >> 8) & 255)) // first and second byte differ
      {
        sixteen = 1;
        break;
      }
    }
  }

  if(sixteen)
  {
    unsigned short r = 0, g = 0, b = 0, a = 0;
    profile->bits = 16;
    bits_done = numcolors_done = 1; // counting colors no longer useful, palette doesn't support 16-bit

    for(i = 0; i != numpixels; ++i)
    {
      getPixelColorRGBA16(&r, &g, &b, &a, in, i, mode);

      if(!colored_done && (r != g || r != b))
      {
        profile->colored = 1;
        colored_done = 1;
      }

      if(!alpha_done)
      {
        unsigned matchkey = (r == profile->key_r && g == profile->key_g && b == profile->key_b);
        if(a != 65535 && (a != 0 || (profile->key && !matchkey)))
        {
          profile->alpha = 1;
          profile->key = 0;
          alpha_done = 1;
        }
        else if(a == 0 && !profile->alpha && !profile->key)
        {
          profile->key = 1;
          profile->key_r = r;
          profile->key_g = g;
          profile->key_b = b;
        }
        else if(a == 65535 && profile->key && matchkey)
        {
          //  Color key cannot be used if an opaque pixel also has that RGB color. 
          profile->alpha = 1;
          profile->key = 0;
          alpha_done = 1;
        }
      }
      if(alpha_done && numcolors_done && colored_done && bits_done) break;
    }

    if(profile->key && !profile->alpha)
    {
      for(i = 0; i != numpixels; ++i)
      {
        getPixelColorRGBA16(&r, &g, &b, &a, in, i, mode);
        if(a != 0 && r == profile->key_r && g == profile->key_g && b == profile->key_b)
        {
          //  Color key cannot be used if an opaque pixel also has that RGB color. 
          profile->alpha = 1;
          profile->key = 0;
          alpha_done = 1;
        }
      }
    }
  }
  else //  < 16-bit 
  {
    unsigned char r = 0, g = 0, b = 0, a = 0;
    for(i = 0; i != numpixels; ++i)
    {
      getPixelColorRGBA8(&r, &g, &b, &a, in, i, mode);

      if(!bits_done && profile->bits < 8)
      {
        // only r is checked, < 8 bits is only relevant for greyscale
        unsigned bits = getValueRequiredBits(r);
        if(bits > profile->bits) profile->bits = bits;
      }
      bits_done = (profile->bits >= bpp);

      if(!colored_done && (r != g || r != b))
      {
        profile->colored = 1;
        colored_done = 1;
        if(profile->bits < 8) profile->bits = 8; // PNG has no colored modes with less than 8-bit per channel
      }

      if(!alpha_done)
      {
        unsigned matchkey = (r == profile->key_r && g == profile->key_g && b == profile->key_b);
        if(a != 255 && (a != 0 || (profile->key && !matchkey)))
        {
          profile->alpha = 1;
          profile->key = 0;
          alpha_done = 1;
          if(profile->bits < 8) profile->bits = 8; // PNG has no alphachannel modes with less than 8-bit per channel
        }
        else if(a == 0 && !profile->alpha && !profile->key)
        {
          profile->key = 1;
          profile->key_r = r;
          profile->key_g = g;
          profile->key_b = b;
        }
        else if(a == 255 && profile->key && matchkey)
        {
          //  Color key cannot be used if an opaque pixel also has that RGB color. 
          profile->alpha = 1;
          profile->key = 0;
          alpha_done = 1;
          if(profile->bits < 8) profile->bits = 8; // PNG has no alphachannel modes with less than 8-bit per channel
        }
      }

      if(!numcolors_done)
      {
        if(!color_tree_has(&tree, r, g, b, a))
        {
          color_tree_add(&tree, r, g, b, a, profile->numcolors);
          if(profile->numcolors < 256)
          {
            unsigned char* p = profile->palette;
            unsigned n = profile->numcolors;
            p[n * 4 + 0] = r;
            p[n * 4 + 1] = g;
            p[n * 4 + 2] = b;
            p[n * 4 + 3] = a;
          }
          ++profile->numcolors;
          numcolors_done = profile->numcolors >= maxnumcolors;
        }
      }

      if(alpha_done && numcolors_done && colored_done && bits_done) break;
    }

    if(profile->key && !profile->alpha)
    {
      for(i = 0; i != numpixels; ++i)
      {
        getPixelColorRGBA8(&r, &g, &b, &a, in, i, mode);
        if(a != 0 && r == profile->key_r && g == profile->key_g && b == profile->key_b)
        {
          //  Color key cannot be used if an opaque pixel also has that RGB color. 
          profile->alpha = 1;
          profile->key = 0;
          alpha_done = 1;
          if(profile->bits < 8) profile->bits = 8; // PNG has no alphachannel modes with less than 8-bit per channel
        }
      }
    }

    // make the profile's key always 16-bit for consistency - repeat each byte twice
    profile->key_r += (profile->key_r << 8);
    profile->key_g += (profile->key_g << 8);
    profile->key_b += (profile->key_b << 8);
  }

  color_tree_cleanup(&tree);
  return error;
}

/*Automatically chooses color type that gives smallest amount of bits in the
output image, e.g. grey if there are only greyscale pixels, palette if there
are less than 256 colors, ...
Updates values of mode with a potentially smaller color model. mode_out should
contain the user chosen color model, but will be overwritten with the new chosen one.*/
unsigned lodepng_auto_choose_color(LodePNGColorMode* mode_out,
                                   const unsigned char* image, unsigned w, unsigned h,
                                   const LodePNGColorMode* mode_in)
{
  LodePNGColorProfile prof;
  unsigned error = 0;
  unsigned i, n, palettebits, palette_ok;

  lodepng_color_profile_init(&prof);
  error = lodepng_get_color_profile(&prof, image, w, h, mode_in);
  if(error) return error;
  mode_out->key_defined = 0;

  if(prof.key && w * h <= 16)
  {
    prof.alpha = 1; // too few pixels to justify tRNS chunk overhead
    prof.key = 0;
    if(prof.bits < 8) prof.bits = 8; // PNG has no alphachannel modes with less than 8-bit per channel
  }
  n = prof.numcolors;
  palettebits = n <= 2 ? 1 : (n <= 4 ? 2 : (n <= 16 ? 4 : 8));
  palette_ok = n <= 256 && prof.bits <= 8;
  if(w * h < n * 2) palette_ok = 0; // don't add palette overhead if image has only a few pixels
  if(!prof.colored && prof.bits <= palettebits) palette_ok = 0; // grey is less overhead

  if(palette_ok)
  {
    unsigned char* p = prof.palette;
    lodepng_palette_clear(mode_out); // remove potential earlier palette
    for(i = 0; i != prof.numcolors; ++i)
    {
      error = lodepng_palette_add(mode_out, p[i * 4 + 0], p[i * 4 + 1], p[i * 4 + 2], p[i * 4 + 3]);
      if(error) break;
    }

    mode_out->colortype = LCT_PALETTE;
    mode_out->bitdepth = palettebits;

    if(mode_in->colortype == LCT_PALETTE && mode_in->palettesize >= mode_out->palettesize
        && mode_in->bitdepth == mode_out->bitdepth)
    {
      // If input should have same palette colors, keep original to preserve its order and prevent conversion
      lodepng_color_mode_cleanup(mode_out);
      lodepng_color_mode_copy(mode_out, mode_in);
    }
  }
  else // 8-bit or 16-bit per channel
  {
    mode_out->bitdepth = prof.bits;
    mode_out->colortype = prof.alpha ? (prof.colored ? LCT_RGBA : LCT_GREY_ALPHA)
                                     : (prof.colored ? LCT_RGB : LCT_GREY);

    if(prof.key)
    {
      unsigned mask = (1u << mode_out->bitdepth) - 1u; // profile always uses 16-bit, mask converts it
      mode_out->key_r = prof.key_r & mask;
      mode_out->key_g = prof.key_g & mask;
      mode_out->key_b = prof.key_b & mask;
      mode_out->key_defined = 1;
    }
  }

  return error;
}

// Paeth predictor, used by PNG filter type 4
// The parameters are of type short, but should come from unsigned chars, the shorts
// are only needed to make the paeth calculation correct.
static unsigned char paethPredictor(short a, short b, short c)
{
  short pa = abs(b - c);
  short pb = abs(a - c);
  short pc = abs(a + b - c - c);

  if(pc < pa && pc < pb) return (unsigned char)c;
  else if(pb < pa) return (unsigned char)b;
  else return (unsigned char)a;
}

// shared values used by multiple Adam7 related functions

static const unsigned ADAM7_IX[7] = { 0, 4, 0, 2, 0, 1, 0 }; // x start values
static const unsigned ADAM7_IY[7] = { 0, 0, 4, 0, 2, 0, 1 }; // y start values
static const unsigned ADAM7_DX[7] = { 8, 8, 4, 4, 2, 2, 1 }; // x delta values
static const unsigned ADAM7_DY[7] = { 8, 8, 8, 4, 4, 2, 2 }; // y delta values

// Outputs various dimensions and positions in the image related to the Adam7 reduced images.
// passw: output containing the width of the 7 passes
// passh: output containing the height of the 7 passes
// filter_passstart: output containing the index of the start and end of each
//  reduced image with filter bytes
// padded_passstart output containing the index of the start and end of each
//  reduced image when without filter bytes but with padded scanlines
// passstart: output containing the index of the start and end of each reduced
//  image without padding between scanlines, but still padding between the images
// w, h: width and height of non-interlaced image
// bpp: bits per pixel
// "padded" is only relevant if bpp is less than 8 and a scanline or image does not
//  end at a full byte
static void Adam7_getpassvalues(unsigned passw[7], unsigned passh[7], size_t filter_passstart[8],
                                size_t padded_passstart[8], size_t passstart[8], unsigned w, unsigned h, unsigned bpp)
{
  // the passstart values have 8 values: the 8th one indicates the byte after the end of the 7th (= last) pass
  unsigned i;

  // calculate width and height in pixels of each pass
  for(i = 0; i != 7; ++i)
  {
    passw[i] = (w + ADAM7_DX[i] - ADAM7_IX[i] - 1) / ADAM7_DX[i];
    passh[i] = (h + ADAM7_DY[i] - ADAM7_IY[i] - 1) / ADAM7_DY[i];
    if(passw[i] == 0) passh[i] = 0;
    if(passh[i] == 0) passw[i] = 0;
  }

  filter_passstart[0] = padded_passstart[0] = passstart[0] = 0;
  for(i = 0; i != 7; ++i)
  {
    // if passw[i] is 0, it's 0 bytes, not 1 (no filtertype-byte)
    filter_passstart[i + 1] = filter_passstart[i]
                            + ((passw[i] && passh[i]) ? passh[i] * (1 + (passw[i] * bpp + 7) / 8) : 0);
    // bits padded if needed to fill full byte at end of each scanline
    padded_passstart[i + 1] = padded_passstart[i] + passh[i] * ((passw[i] * bpp + 7) / 8);
    // only padded at end of reduced image
    passstart[i + 1] = passstart[i] + (passh[i] * passw[i] * bpp + 7) / 8;
  }
}


//////////////////////////////////////////////////////////////////////////// 
/// PNG Decoder                                                            / 
//////////////////////////////////////////////////////////////////////////// 

// read the information from the header and store it in the LodePNGInfo. return value is error
unsigned lodepng_inspect(unsigned* w, unsigned* h, LodePNGState* state,
                         const unsigned char* in, size_t insize)
{
  LodePNGInfo* info = &state->info_png;
  if(insize == 0 || in == 0)
  {
    CERROR_RETURN_ERROR(state->error, 48); // error: the given data is empty
  }
  if(insize < 33)
  {
    CERROR_RETURN_ERROR(state->error, 27); // error: the data length is smaller than the length of a PNG header
  }

  // when decoding a new PNG image, make sure all parameters created after previous decoding are reset
  lodepng_info_cleanup(info);
  lodepng_info_init(info);

  if(in[0] != 137 || in[1] != 80 || in[2] != 78 || in[3] != 71
     || in[4] != 13 || in[5] != 10 || in[6] != 26 || in[7] != 10)
  {
    CERROR_RETURN_ERROR(state->error, 28); // error: the first 8 bytes are not the correct PNG signature
  }
  if(lodepng_chunk_length(in + 8) != 13)
  {
    CERROR_RETURN_ERROR(state->error, 94); // error: header size must be 13 bytes
  }
  if(!lodepng_chunk_type_equals(in + 8, "IHDR"))
  {
    CERROR_RETURN_ERROR(state->error, 29); // error: it doesn't start with a IHDR chunk!
  }

  // read the values given in the header
  *w = lodepng_read32bitInt(&in[16]);
  *h = lodepng_read32bitInt(&in[20]);
  info->color.bitdepth = in[24];
  info->color.colortype = (LodePNGColorType)in[25];
  info->interlace_method = in[28];

  if(*w == 0 || *h == 0)
  {
    CERROR_RETURN_ERROR(state->error, 93);
  }

  // error: only compression method 0 is allowed in the specification
  if(in[26] != 0) CERROR_RETURN_ERROR(state->error, 32);
  // error: only filter method 0 is allowed in the specification
  if (in[27] != 0) CERROR_RETURN_ERROR(state->error, 33);
  // error: only interlace methods 0 and 1 exist in the specification
  if(info->interlace_method > 1) CERROR_RETURN_ERROR(state->error, 34);

  state->error = checkColorValidity(info->color.colortype, info->color.bitdepth);
  return state->error;
}

static unsigned unfilterScanline(unsigned char* recon, const unsigned char* scanline, const unsigned char* precon,
                                 size_t bytewidth, unsigned char filterType, size_t length)
{
  /*
  For PNG filter method 0
  unfilter a PNG image scanline by scanline. when the pixels are smaller than 1 byte,
  the filter works byte per byte (bytewidth = 1)
  precon is the previous unfiltered scanline, recon the result, scanline the current one
  the incoming scanlines do NOT include the filtertype byte, that one is given in the parameter filterType instead
  recon and scanline MAY be the same memory address! precon must be disjoint.
  */

  size_t i;
  switch(filterType)
  {
    case 0:
      for(i = 0; i != length; ++i) recon[i] = scanline[i];
      break;
    case 1:
      for(i = 0; i != bytewidth; ++i) recon[i] = scanline[i];
      for(i = bytewidth; i < length; ++i) recon[i] = scanline[i] + recon[i - bytewidth];
      break;
    case 2:
      if(precon)
      {
        for(i = 0; i != length; ++i) recon[i] = scanline[i] + precon[i];
      }
      else
      {
        for(i = 0; i != length; ++i) recon[i] = scanline[i];
      }
      break;
    case 3:
      if(precon)
      {
        for(i = 0; i != bytewidth; ++i) recon[i] = scanline[i] + (precon[i] >> 1);
        for(i = bytewidth; i < length; ++i) recon[i] = scanline[i] + ((recon[i - bytewidth] + precon[i]) >> 1);
      }
      else
      {
        for(i = 0; i != bytewidth; ++i) recon[i] = scanline[i];
        for(i = bytewidth; i < length; ++i) recon[i] = scanline[i] + (recon[i - bytewidth] >> 1);
      }
      break;
    case 4:
      if(precon)
      {
        for(i = 0; i != bytewidth; ++i)
        {
          recon[i] = (scanline[i] + precon[i]); // paethPredictor(0, precon[i], 0) is always precon[i]
        }
        for(i = bytewidth; i < length; ++i)
        {
          recon[i] = (scanline[i] + paethPredictor(recon[i - bytewidth], precon[i], precon[i - bytewidth]));
        }
      }
      else
      {
        for(i = 0; i != bytewidth; ++i)
        {
          recon[i] = scanline[i];
        }
        for(i = bytewidth; i < length; ++i)
        {
          // paethPredictor(recon[i - bytewidth], 0, 0) is always recon[i - bytewidth]
          recon[i] = (scanline[i] + recon[i - bytewidth]);
        }
      }
      break;
    default: return 36; // error: unexisting filter type given
  }
  return 0;
}

static unsigned unfilter(unsigned char* out, const unsigned char* in, unsigned w, unsigned h, unsigned bpp)
{
  /*
  For PNG filter method 0
  this function unfilters a single image (e.g. without interlacing this is called once, with Adam7 seven times)
  out must have enough bytes allocated already, in must have the scanlines + 1 filtertype byte per scanline
  w and h are image dimensions or dimensions of reduced image, bpp is bits per pixel
  in and out are allowed to be the same memory address (but aren't the same size since in has the extra filter bytes)
  */

  unsigned char* prevline = 0;

  // bytewidth is used for filtering, is 1 when bpp < 8, number of bytes per pixel otherwise
  size_t bytewidth = (bpp + 7) / 8;
  size_t linebytes = (w * bpp + 7) / 8;

  for(unsigned y = 0; y < h; ++y)
  {
    size_t outindex = linebytes * y;
    size_t inindex = (1 + linebytes) * y; // the extra filterbyte added to each row
    unsigned char filterType = in[inindex];

    CERROR_TRY_RETURN(unfilterScanline(&out[outindex], &in[inindex + 1], prevline, bytewidth, filterType, linebytes));

    prevline = &out[outindex];
  }

  return 0;
}

/*
in: Adam7 interlaced image, with no padding bits between scanlines, but between
 reduced images so that each reduced image starts at a byte.
out: the same pixels, but re-ordered so that they're now a non-interlaced image with size w*h
bpp: bits per pixel
out has the following size in bits: w * h * bpp.
in is possibly bigger due to padding bits between reduced images.
out must be big enough AND must be 0 everywhere if bpp < 8 in the current implementation
(because that's likely a little bit faster)
NOTE: comments about padding bits are only relevant if bpp < 8
*/
static void Adam7_deinterlace(unsigned char* out, const unsigned char* in, unsigned w, unsigned h, unsigned bpp)
{
  unsigned passw[7], passh[7];
  size_t filter_passstart[8], padded_passstart[8], passstart[8];
  unsigned i;

  Adam7_getpassvalues(passw, passh, filter_passstart, padded_passstart, passstart, w, h, bpp);

  if(bpp >= 8)
  {
    for(i = 0; i != 7; ++i)
    {
      unsigned x, y, b;
      size_t bytewidth = bpp / 8;
      for(y = 0; y < passh[i]; ++y)
      for(x = 0; x < passw[i]; ++x)
      {
        size_t pixelinstart = passstart[i] + (y * passw[i] + x) * bytewidth;
        size_t pixeloutstart = ((ADAM7_IY[i] + y * ADAM7_DY[i]) * w + ADAM7_IX[i] + x * ADAM7_DX[i]) * bytewidth;
        for(b = 0; b < bytewidth; ++b)
        {
          out[pixeloutstart + b] = in[pixelinstart + b];
        }
      }
    }
  }
  else // bpp < 8: Adam7 with pixels < 8 bit is a bit trickier: with bit pointers
  {
    for(i = 0; i != 7; ++i)
    {
      unsigned x, y, b;
      unsigned ilinebits = bpp * passw[i];
      unsigned olinebits = bpp * w;
      size_t obp, ibp; // bit pointers (for out and in buffer)
      for(y = 0; y < passh[i]; ++y)
      for(x = 0; x < passw[i]; ++x)
      {
        ibp = (8 * passstart[i]) + (y * ilinebits + x * bpp);
        obp = (ADAM7_IY[i] + y * ADAM7_DY[i]) * olinebits + (ADAM7_IX[i] + x * ADAM7_DX[i]) * bpp;
        for(b = 0; b < bpp; ++b)
        {
          unsigned char bit = readBitFromReversedStream(&ibp, in);
          // note that this function assumes the out buffer is completely 0, use setBitOfReversedStream otherwise
          setBitOfReversedStream0(&obp, out, bit);
        }
      }
    }
  }
}

static void removePaddingBits(unsigned char* out, const unsigned char* in,
                              size_t olinebits, size_t ilinebits, unsigned h)
{
  /*
  After filtering there are still padding bits if scanlines have non multiple of 8 bit amounts. They need
  to be removed (except at last scanline of (Adam7-reduced) image) before working with pure image buffers
  for the Adam7 code, the color convert code and the output to the user.
  in and out are allowed to be the same buffer, in may also be higher but still overlapping; in must
  have >= ilinebits*h bits, out must have >= olinebits*h bits, olinebits must be <= ilinebits
  also used to move bits after earlier such operations happened, e.g. in a sequence of reduced images from Adam7
  only useful if (ilinebits - olinebits) is a value in the range 1..7
  */
  unsigned y;
  size_t diff = ilinebits - olinebits;
  size_t ibp = 0, obp = 0; // input and output bit pointers
  for(y = 0; y < h; ++y)
  {
    size_t x;
    for(x = 0; x < olinebits; ++x)
    {
      unsigned char bit = readBitFromReversedStream(&ibp, in);
      setBitOfReversedStream(&obp, out, bit);
    }
    ibp += diff;
  }
}

/*out must be buffer big enough to contain full image, and in must contain the full decompressed data from
the IDAT chunks (with filter index bytes and possible padding bits)
return value is error*/
static unsigned postProcessScanlines(unsigned char* out, unsigned char* in,
                                     unsigned w, unsigned h, const LodePNGInfo* info_png)
{
  /*
  This function converts the filtered-padded-interlaced data into pure 2D image buffer with the PNG's colortype.
  Steps:
  *) if no Adam7: 1) unfilter 2) remove padding bits (= posible extra bits per scanline if bpp < 8)
  *) if adam7: 1) 7x unfilter 2) 7x remove padding bits 3) Adam7_deinterlace
  NOTE: the in buffer will be overwritten with intermediate data!
  */
  unsigned bpp = lodepng_get_bpp(&info_png->color);
  if(bpp == 0) return 31; // error: invalid colortype

  if(info_png->interlace_method == 0)
  {
    if(bpp < 8 && w * bpp != ((w * bpp + 7) / 8) * 8)
    {
      CERROR_TRY_RETURN(unfilter(in, in, w, h, bpp));
      removePaddingBits(out, in, w * bpp, ((w * bpp + 7) / 8) * 8, h);
    }
    // we can immediately filter into the out buffer, no other steps needed
    else CERROR_TRY_RETURN(unfilter(out, in, w, h, bpp));
  }
  else // interlace_method is 1 (Adam7)
  {
    unsigned passw[7], passh[7]; size_t filter_passstart[8], padded_passstart[8], passstart[8];
    unsigned i;

    Adam7_getpassvalues(passw, passh, filter_passstart, padded_passstart, passstart, w, h, bpp);

    for(i = 0; i != 7; ++i)
    {
      CERROR_TRY_RETURN(unfilter(&in[padded_passstart[i]], &in[filter_passstart[i]], passw[i], passh[i], bpp));
      /*TODO: possible efficiency improvement: if in this reduced image the bits fit nicely in 1 scanline,
      move bytes instead of bits or move not at all*/
      if(bpp < 8)
      {
        /*remove padding bits in scanlines; after this there still may be padding
        bits between the different reduced images: each reduced image still starts nicely at a byte*/
        removePaddingBits(&in[passstart[i]], &in[padded_passstart[i]], passw[i] * bpp,
                          ((passw[i] * bpp + 7) / 8) * 8, passh[i]);
      }
    }

    Adam7_deinterlace(out, in, w, h, bpp);
  }

  return 0;
}

static unsigned readChunk_PLTE(LodePNGColorMode* color, const unsigned char* data, size_t chunkLength)
{
  unsigned pos = 0, i;
  if(color->palette) free(color->palette);
  color->palettesize = chunkLength / 3;
  color->palette = (unsigned char*)malloc(4 * color->palettesize);
  if(!color->palette && color->palettesize)
  {
    color->palettesize = 0;
    return 83; // alloc fail
  }
  if(color->palettesize > 256) return 38; // error: palette too big

  for(i = 0; i != color->palettesize; ++i)
  {
    color->palette[4 * i + 0] = data[pos++]; // R
    color->palette[4 * i + 1] = data[pos++]; // G
    color->palette[4 * i + 2] = data[pos++]; // B
    color->palette[4 * i + 3] = 255; // alpha
  }

  return 0; //  OK 
}

static unsigned readChunk_tRNS(LodePNGColorMode* color, const unsigned char* data, size_t chunkLength)
{
  unsigned i;
  if(color->colortype == LCT_PALETTE)
  {
    // error: more alpha values given than there are palette entries
    if(chunkLength > color->palettesize) return 38;

    for(i = 0; i != chunkLength; ++i) color->palette[4 * i + 3] = data[i];
  }
  else if(color->colortype == LCT_GREY)
  {
    // error: this chunk must be 2 bytes for greyscale image
    if(chunkLength != 2) return 30;

    color->key_defined = 1;
    color->key_r = color->key_g = color->key_b = 256u * data[0] + data[1];
  }
  else if(color->colortype == LCT_RGB)
  {
    // error: this chunk must be 6 bytes for RGB image
    if(chunkLength != 6) return 41;

    color->key_defined = 1;
    color->key_r = 256u * data[0] + data[1];
    color->key_g = 256u * data[2] + data[3];
    color->key_b = 256u * data[4] + data[5];
  }
  else return 42; // error: tRNS chunk not allowed for other color models

  return 0; //  OK 
}


// read a PNG, the result will be in the same color type as the PNG (hence "generic")
static void decodeGeneric(unsigned char** out, unsigned* w, unsigned* h,
                          LodePNGState* state,
                          const unsigned char* in, size_t insize)
{
  unsigned char IEND = 0;
  const unsigned char* chunk;
  size_t i;
  ucvector idat; // the data from idat chunks
  ucvector scanlines;
  size_t predict;
  size_t numpixels;
  size_t outsize = 0;

  // for unknown chunk order
  unsigned unknown = 0;

  // provide some proper output values if error will happen
  *out = 0;

  state->error = lodepng_inspect(w, h, state, in, insize); // reads header and resets other parameters in state->info_png
  if(state->error) return;

  numpixels = *w * *h;

  // multiplication overflow
  if(*h != 0 && numpixels / *h != *w) CERROR_RETURN(state->error, 92);
  /*multiplication overflow possible further below. Allows up to 2^31-1 pixel
  bytes with 16-bit RGBA, the rest is room for filter bytes.*/
  if(numpixels > 268435455) CERROR_RETURN(state->error, 92);

  ucvector_init(&idat);
  chunk = &in[33]; // first byte of the first chunk after the header

  /*loop through the chunks, ignoring unknown chunks and stopping at IEND chunk.
  IDAT data is put at the start of the in buffer*/
  while(!IEND && !state->error)
  {
    unsigned chunkLength;
    const unsigned char* data; // the data in the chunk

    // error: size of the in buffer too small to contain next chunk
    if((size_t)((chunk - in) + 12) > insize || chunk < in)
    {
      break; // other errors may still happen though
    }

    // length of the data of the chunk, excluding the length bytes, chunk type and CRC bytes
    chunkLength = lodepng_chunk_length(chunk);
    // error: chunk length larger than the max PNG chunk size
    if(chunkLength > 2147483647)
    {
      break; // other errors may still happen though
    }

    if((size_t)((chunk - in) + chunkLength + 12) > insize || (chunk + chunkLength + 12) < in)
    {
      CERROR_BREAK(state->error, 64); // error: size of the in buffer too small to contain next chunk
    }

    data = lodepng_chunk_data_const(chunk);

    // IDAT chunk, containing compressed image data
    if(lodepng_chunk_type_equals(chunk, "IDAT"))
    {
      size_t oldsize = idat.size;
      if(!ucvector_resize(&idat, oldsize + chunkLength)) CERROR_BREAK(state->error, 83 /*alloc fail*/);
      for(i = 0; i != chunkLength; ++i) idat.data[oldsize + i] = data[i];
    }
    // IEND chunk
    else if(lodepng_chunk_type_equals(chunk, "IEND"))
    {
      IEND = 1;
    }
    // palette chunk (PLTE)
    else if(lodepng_chunk_type_equals(chunk, "PLTE"))
    {
      state->error = readChunk_PLTE(&state->info_png.color, data, chunkLength);
      if(state->error) break;
    }
    // palette transparency chunk (tRNS)
    else if(lodepng_chunk_type_equals(chunk, "tRNS"))
    {
      state->error = readChunk_tRNS(&state->info_png.color, data, chunkLength);
      if(state->error) break;
    }
    else // it's not an implemented chunk type, so ignore it: skip over the data
    {
      // error: unknown critical chunk (5th bit of first byte of chunk type is 0)
      unknown = 1;
    }

    if(!IEND) chunk = lodepng_chunk_next_const(chunk);
  }

  ucvector_init(&scanlines);
  /*predict output size, to allocate exact size for output buffer to avoid more dynamic allocation.
  If the decompressed size does not match the prediction, the image must be corrupt.*/
  if(state->info_png.interlace_method == 0)
  {
    // The extra *h is added because this are the filter bytes every scanline starts with
    predict = lodepng_get_raw_size_idat(*w, *h, &state->info_png.color) + *h;
  }
  else
  {
    // Adam-7 interlaced: predicted size is the sum of the 7 sub-images sizes
    const LodePNGColorMode* color = &state->info_png.color;
    predict = 0;
    predict += lodepng_get_raw_size_idat((*w + 7) >> 3, (*h + 7) >> 3, color) + ((*h + 7) >> 3);
    if(*w > 4) predict += lodepng_get_raw_size_idat((*w + 3) >> 3, (*h + 7) >> 3, color) + ((*h + 7) >> 3);
    predict += lodepng_get_raw_size_idat((*w + 3) >> 2, (*h + 3) >> 3, color) + ((*h + 3) >> 3);
    if(*w > 2) predict += lodepng_get_raw_size_idat((*w + 1) >> 2, (*h + 3) >> 2, color) + ((*h + 3) >> 2);
    predict += lodepng_get_raw_size_idat((*w + 1) >> 1, (*h + 1) >> 2, color) + ((*h + 1) >> 2);
    if(*w > 1) predict += lodepng_get_raw_size_idat((*w + 0) >> 1, (*h + 1) >> 1, color) + ((*h + 1) >> 1);
    predict += lodepng_get_raw_size_idat((*w + 0), (*h + 0) >> 1, color) + ((*h + 0) >> 1);
  }
  if(!state->error && !ucvector_reserve(&scanlines, predict)) state->error = 83; // alloc fail
  if(!state->error)
  {
    state->error = lodepng_zlib_decompress(&scanlines.data, &scanlines.size, idat.data, idat.size);
    if(!state->error && scanlines.size != predict) state->error = 91; // decompressed size doesn't match prediction
  }
  ucvector_cleanup(&idat);

  if(!state->error)
  {
    outsize = lodepng_get_raw_size(*w, *h, &state->info_png.color);
    *out = (unsigned char*)malloc(outsize);
    if(!*out) state->error = 83; // alloc fail
  }
  if(!state->error)
  {
    for(i = 0; i < outsize; i++) (*out)[i] = 0;
    state->error = postProcessScanlines(*out, scanlines.data, *w, *h, &state->info_png);
  }
  ucvector_cleanup(&scanlines);
}

unsigned lodepng_decode(unsigned char** out, unsigned* w, unsigned* h,
                        LodePNGState* state,
                        const unsigned char* in, size_t insize)
{
  *out = 0;
  decodeGeneric(out, w, h, state, in, insize);
  if(state->error) return state->error;
  if(lodepng_color_mode_equal(&state->info_raw, &state->info_png.color))
  {
    // same color type, no copying or converting of data needed
  }
  else
  {
    // color conversion needed; sort of copy of the data
    unsigned char* data = *out;
    size_t outsize;

    /*TODO: check if this works according to the statement in the documentation: "The converter can convert
    from greyscale input color type, to 8-bit greyscale or greyscale with alpha"*/
    if(!(state->info_raw.colortype == LCT_RGB || state->info_raw.colortype == LCT_RGBA)
       && !(state->info_raw.bitdepth == 8))
    {
      return 56; // unsupported color mode conversion
    }

    outsize = lodepng_get_raw_size(*w, *h, &state->info_raw);
    *out = (unsigned char*)malloc(outsize);
    if(!(*out))
    {
      state->error = 83; // alloc fail
    }
    else state->error = lodepng_convert(*out, data, &state->info_raw,
                                        &state->info_png.color, *w, *h);
    free(data);
  }
  return state->error;
}

// Converts PNG data in memory to raw pixel data.
// out: Output parameter. Pointer to buffer that will contain the raw pixel data.
//      After decoding, its size is w * h * (bytes per pixel) bytes. Bytes per pixel 
//      depends on colortype and bitdepth.
//      Must be freed after usage with free(*out).
//      Note: for 16-bit per channel colors, uses big endian format like PNG does.
// w: Output parameter. Pointer to width of pixel data.
// h: Output parameter. Pointer to height of pixel data.
// in: Memory buffer with the PNG file.
// insize: size of the in buffer.
// colortype: the desired color type for the raw output image. See explanation on PNG color types.
// bitdepth: the desired bit depth for the raw output image. See explanation on PNG color types.
// Return value: LodePNG error code (0 means no error).
unsigned lodepng_decode_memory(unsigned char** out, unsigned* w, unsigned* h, const unsigned char* in,
                               size_t insize, LodePNGColorType colortype, unsigned bitdepth)
{
  LodePNGState state;
  lodepng_state_init(&state);
  state.info_raw.colortype = colortype;
  state.info_raw.bitdepth = bitdepth;
  return lodepng_decode(out, w, h, &state, in, insize);
}

unsigned lodepng_decode32(unsigned char** out, unsigned* w, unsigned* h, const unsigned char* in, size_t insize)
{
  return lodepng_decode_memory(out, w, h, in, insize, LCT_RGBA, 8);
}

// Load PNG from disk, from file with given name.
// Same as the other decode functions, but instead takes a filename as input.
unsigned lodepng_decode_file(unsigned char** out, unsigned* w, unsigned* h, const char* filename,
                             LodePNGColorType colortype, unsigned bitdepth)
{
  unsigned char* buffer = 0;
  size_t buffersize;
  unsigned error = lodepng_load_file(&buffer, &buffersize, filename);
  if(!error) error = lodepng_decode_memory(out, w, h, buffer, buffersize, colortype, bitdepth);
  free(buffer);
  return error;
}

unsigned lodepng_decode32_file(unsigned char** out, unsigned* w, unsigned* h, const char* filename)
{
  return lodepng_decode_file(out, w, h, filename, LCT_RGBA, 8);
}

unsigned lodepng_decode24_file(unsigned char** out, unsigned* w, unsigned* h, const char* filename)
{
  return lodepng_decode_file(out, w, h, filename, LCT_RGB, 8);
}


void lodepng_state_init(LodePNGState* state)
{
  lodepng_encoder_settings_init(&state->encoder);
  lodepng_color_mode_init(&state->info_raw);
  lodepng_info_init(&state->info_png);
  state->error = 1;
}


//////////////////////////////////////////////////////////////////////////// 
/// PNG Encoder                                                            / 
//////////////////////////////////////////////////////////////////////////// 

// chunkName must be string of 4 characters
static unsigned addChunk(ucvector* out, const char* chunkName, const unsigned char* data, size_t length)
{
  CERROR_TRY_RETURN(lodepng_chunk_create(&out->data, &out->size, (unsigned)length, chunkName, data));
  out->allocsize = out->size; // fix the allocsize again
  return 0;
}

static void writeSignature(ucvector* out)
{
  // 8 bytes PNG signature, aka the magic bytes
  ucvector_push_back(out, 137);
  ucvector_push_back(out, 80);
  ucvector_push_back(out, 78);
  ucvector_push_back(out, 71);
  ucvector_push_back(out, 13);
  ucvector_push_back(out, 10);
  ucvector_push_back(out, 26);
  ucvector_push_back(out, 10);
}

static unsigned addChunk_IHDR(ucvector* out, unsigned w, unsigned h,
                              LodePNGColorType colortype, unsigned bitdepth, unsigned interlace_method)
{
  unsigned error = 0;
  ucvector header;
  ucvector_init(&header);

  lodepng_add32bitInt(&header, w); // width
  lodepng_add32bitInt(&header, h); // height
  ucvector_push_back(&header, (unsigned char)bitdepth); // bit depth
  ucvector_push_back(&header, (unsigned char)colortype); // color type
  ucvector_push_back(&header, 0); // compression method
  ucvector_push_back(&header, 0); // filter method
  ucvector_push_back(&header, interlace_method); // interlace method

  error = addChunk(out, "IHDR", header.data, header.size);
  ucvector_cleanup(&header);

  return error;
}

static unsigned addChunk_PLTE(ucvector* out, const LodePNGColorMode* info)
{
  unsigned error = 0;
  size_t i;
  ucvector PLTE;
  ucvector_init(&PLTE);
  for(i = 0; i != info->palettesize * 4; ++i)
  {
    // add all channels except alpha channel
    if(i % 4 != 3) ucvector_push_back(&PLTE, info->palette[i]);
  }
  error = addChunk(out, "PLTE", PLTE.data, PLTE.size);
  ucvector_cleanup(&PLTE);

  return error;
}

static unsigned addChunk_tRNS(ucvector* out, const LodePNGColorMode* info)
{
  unsigned error = 0;
  size_t i;
  ucvector tRNS;
  ucvector_init(&tRNS);
  if(info->colortype == LCT_PALETTE)
  {
    size_t amount = info->palettesize;
    // the tail of palette values that all have 255 as alpha, does not have to be encoded
    for(i = info->palettesize; i != 0; --i)
    {
      if(info->palette[4 * (i - 1) + 3] == 255) --amount;
      else break;
    }
    // add only alpha channel
    for(i = 0; i != amount; ++i) ucvector_push_back(&tRNS, info->palette[4 * i + 3]);
  }
  else if(info->colortype == LCT_GREY)
  {
    if(info->key_defined)
    {
      ucvector_push_back(&tRNS, (unsigned char)(info->key_r >> 8));
      ucvector_push_back(&tRNS, (unsigned char)(info->key_r & 255));
    }
  }
  else if(info->colortype == LCT_RGB)
  {
    if(info->key_defined)
    {
      ucvector_push_back(&tRNS, (unsigned char)(info->key_r >> 8));
      ucvector_push_back(&tRNS, (unsigned char)(info->key_r & 255));
      ucvector_push_back(&tRNS, (unsigned char)(info->key_g >> 8));
      ucvector_push_back(&tRNS, (unsigned char)(info->key_g & 255));
      ucvector_push_back(&tRNS, (unsigned char)(info->key_b >> 8));
      ucvector_push_back(&tRNS, (unsigned char)(info->key_b & 255));
    }
  }

  error = addChunk(out, "tRNS", tRNS.data, tRNS.size);
  ucvector_cleanup(&tRNS);

  return error;
}

static unsigned addChunk_IDAT(ucvector* out, const unsigned char* data, size_t datasize,
                              LodePNGCompressSettings* zlibsettings)
{
  ucvector zlibdata;
  unsigned error = 0;

  // compress with the Zlib compressor
  ucvector_init(&zlibdata);
  error = lodepng_zlib_compress(&zlibdata.data, &zlibdata.size, data, datasize, zlibsettings);
  if(!error) error = addChunk(out, "IDAT", zlibdata.data, zlibdata.size);
  ucvector_cleanup(&zlibdata);

  return error;
}

static unsigned addChunk_IEND(ucvector* out)
{
  unsigned error = 0;
  error = addChunk(out, "IEND", 0, 0);
  return error;
}

static void filterScanline(unsigned char* out, const unsigned char* scanline, const unsigned char* prevline,
                           size_t length, size_t bytewidth, unsigned char filterType)
{
  size_t i;
  switch(filterType)
  {
    case 0: // None
      for(i = 0; i != length; ++i) out[i] = scanline[i];
      break;
    case 1: // Sub
      for(i = 0; i != bytewidth; ++i) out[i] = scanline[i];
      for(i = bytewidth; i < length; ++i) out[i] = scanline[i] - scanline[i - bytewidth];
      break;
    case 2: // Up
      if(prevline)
      {
        for(i = 0; i != length; ++i) out[i] = scanline[i] - prevline[i];
      }
      else
      {
        for(i = 0; i != length; ++i) out[i] = scanline[i];
      }
      break;
    case 3: // Average
      if(prevline)
      {
        for(i = 0; i != bytewidth; ++i) out[i] = scanline[i] - (prevline[i] >> 1);
        for(i = bytewidth; i < length; ++i) out[i] = scanline[i] - ((scanline[i - bytewidth] + prevline[i]) >> 1);
      }
      else
      {
        for(i = 0; i != bytewidth; ++i) out[i] = scanline[i];
        for(i = bytewidth; i < length; ++i) out[i] = scanline[i] - (scanline[i - bytewidth] >> 1);
      }
      break;
    case 4: // Paeth
      if(prevline)
      {
        // paethPredictor(0, prevline[i], 0) is always prevline[i]
        for(i = 0; i != bytewidth; ++i) out[i] = (scanline[i] - prevline[i]);
        for(i = bytewidth; i < length; ++i)
        {
          out[i] = (scanline[i] - paethPredictor(scanline[i - bytewidth], prevline[i], prevline[i - bytewidth]));
        }
      }
      else
      {
        for(i = 0; i != bytewidth; ++i) out[i] = scanline[i];
        // paethPredictor(scanline[i - bytewidth], 0, 0) is always scanline[i - bytewidth]
        for(i = bytewidth; i < length; ++i) out[i] = (scanline[i] - scanline[i - bytewidth]);
      }
      break;
    default: return; // unexisting filter type given
  }
}

//  log2 approximation. A slight bit faster than std::log. 
static float flog2(float f)
{
  float result = 0;
  while(f > 32) { result += 4; f /= 16; }
  while(f > 2) { ++result; f /= 2; }
  return result + 1.442695f * (f * f * f / 3 - 3 * f * f / 2 + 3 * f - 1.83333f);
}

static unsigned filter(unsigned char* out, const unsigned char* in, unsigned w, unsigned h,
                       const LodePNGColorMode* info, const LodePNGEncoderSettings* settings)
{
  /*
  For PNG filter method 0
  out must be a buffer with as size: h + (w * h * bpp + 7) / 8, because there are
  the scanlines with 1 extra byte per scanline
  */

  unsigned bpp = lodepng_get_bpp(info);
  // the width of a scanline in bytes, not including the filter type
  size_t linebytes = (w * bpp + 7) / 8;
  // bytewidth is used for filtering, is 1 when bpp < 8, number of bytes per pixel otherwise
  size_t bytewidth = (bpp + 7) / 8;
  const unsigned char* prevline = 0;
  unsigned x, y;
  unsigned error = 0;
  LodePNGFilterStrategy strategy = settings->filter_strategy;

  /*
  There is a heuristic called the minimum sum of absolute differences heuristic, suggested by the PNG standard:
   *  If the image type is Palette, or the bit depth is smaller than 8, then do not filter the image (i.e.
      use fixed filtering, with the filter None).
   * (The other case) If the image type is Grayscale or RGB (with or without Alpha), and the bit depth is
     not smaller than 8, then use adaptive filtering heuristic as follows: independently for each row, apply
     all five filters and select the filter that produces the smallest sum of absolute values per row.
  This heuristic is used if filter strategy is LFS_MINSUM and filter_palette_zero is true.

  If filter_palette_zero is true and filter_strategy is not LFS_MINSUM, the above heuristic is followed,
  but for "the other case", whatever strategy filter_strategy is set to instead of the minimum sum
  heuristic is used.
  */
  if(settings->filter_palette_zero &&
     (info->colortype == LCT_PALETTE || info->bitdepth < 8)) strategy = LFS_ZERO;

  if(bpp == 0) return 31; // error: invalid color type

  if(strategy == LFS_ZERO)
  {
    for(y = 0; y != h; ++y)
    {
      size_t outindex = (1 + linebytes) * y; // the extra filterbyte added to each row
      size_t inindex = linebytes * y;
      out[outindex] = 0; // filter type byte
      filterScanline(&out[outindex + 1], &in[inindex], prevline, linebytes, bytewidth, 0);
      prevline = &in[inindex];
    }
  }
  else if(strategy == LFS_MINSUM)
  {
    // adaptive filtering
    size_t sum[5];
    unsigned char* attempt[5]; // five filtering attempts, one for each filter type
    size_t smallest = 0;
    unsigned char type, bestType = 0;

    for(type = 0; type != 5; ++type)
    {
      attempt[type] = (unsigned char*)malloc(linebytes);
      if(!attempt[type]) return 83; // alloc fail
    }

    if(!error)
    {
      for(y = 0; y != h; ++y)
      {
        // try the 5 filter types
        for(type = 0; type != 5; ++type)
        {
          filterScanline(attempt[type], &in[y * linebytes], prevline, linebytes, bytewidth, type);

          // calculate the sum of the result
          sum[type] = 0;
          if(type == 0)
          {
            for(x = 0; x != linebytes; ++x) sum[type] += (unsigned char)(attempt[type][x]);
          }
          else
          {
            for(x = 0; x != linebytes; ++x)
            {
              /*For differences, each byte should be treated as signed, values above 127 are negative
              (converted to signed char). Filtertype 0 isn't a difference though, so use unsigned there.
              This means filtertype 0 is almost never chosen, but that is justified.*/
              unsigned char s = attempt[type][x];
              sum[type] += s < 128 ? s : (255U - s);
            }
          }

          // check if this is smallest sum (or if type == 0 it's the first case so always store the values)
          if(type == 0 || sum[type] < smallest)
          {
            bestType = type;
            smallest = sum[type];
          }
        }

        prevline = &in[y * linebytes];

        // now fill the out values
        out[y * (linebytes + 1)] = bestType; // the first byte of a scanline will be the filter type
        for(x = 0; x != linebytes; ++x) out[y * (linebytes + 1) + 1 + x] = attempt[bestType][x];
      }
    }

    for(type = 0; type != 5; ++type) free(attempt[type]);
  }
  else if(strategy == LFS_ENTROPY)
  {
    float sum[5];
    unsigned char* attempt[5]; // five filtering attempts, one for each filter type
    float smallest = 0;
    unsigned type, bestType = 0;
    unsigned count[256];

    for(type = 0; type != 5; ++type)
    {
      attempt[type] = (unsigned char*)malloc(linebytes);
      if(!attempt[type]) return 83; // alloc fail
    }

    for(y = 0; y != h; ++y)
    {
      // try the 5 filter types
      for(type = 0; type != 5; ++type)
      {
        filterScanline(attempt[type], &in[y * linebytes], prevline, linebytes, bytewidth, type);
        for(x = 0; x != 256; ++x) count[x] = 0;
        for(x = 0; x != linebytes; ++x) ++count[attempt[type][x]];
        ++count[type]; // the filter type itself is part of the scanline
        sum[type] = 0;
        for(x = 0; x != 256; ++x)
        {
          float p = count[x] / (float)(linebytes + 1);
          sum[type] += count[x] == 0 ? 0 : flog2(1 / p) * p;
        }
        // check if this is smallest sum (or if type == 0 it's the first case so always store the values)
        if(type == 0 || sum[type] < smallest)
        {
          bestType = type;
          smallest = sum[type];
        }
      }

      prevline = &in[y * linebytes];

      // now fill the out values
      out[y * (linebytes + 1)] = bestType; // the first byte of a scanline will be the filter type
      for(x = 0; x != linebytes; ++x) out[y * (linebytes + 1) + 1 + x] = attempt[bestType][x];
    }

    for(type = 0; type != 5; ++type) free(attempt[type]);
  }
  else return 88; //  unknown filter strategy 

  return error;
}

static void addPaddingBits(unsigned char* out, const unsigned char* in,
                           size_t olinebits, size_t ilinebits, unsigned h)
{
  /*The opposite of the removePaddingBits function
  olinebits must be >= ilinebits*/
  unsigned y;
  size_t diff = olinebits - ilinebits;
  size_t obp = 0, ibp = 0; // bit pointers
  for(y = 0; y != h; ++y)
  {
    size_t x;
    for(x = 0; x < ilinebits; ++x)
    {
      unsigned char bit = readBitFromReversedStream(&ibp, in);
      setBitOfReversedStream(&obp, out, bit);
    }
    /*obp += diff; --> no, fill in some value in the padding bits too, to avoid
    "Use of uninitialised value of size ###" warning from valgrind*/
    for(x = 0; x != diff; ++x) setBitOfReversedStream(&obp, out, 0);
  }
}

/*
in: non-interlaced image with size w*h
out: the same pixels, but re-ordered according to PNG's Adam7 interlacing, with
 no padding bits between scanlines, but between reduced images so that each
 reduced image starts at a byte.
bpp: bits per pixel
there are no padding bits, not between scanlines, not between reduced images
in has the following size in bits: w * h * bpp.
out is possibly bigger due to padding bits between reduced images
NOTE: comments about padding bits are only relevant if bpp < 8
*/
static void Adam7_interlace(unsigned char* out, const unsigned char* in, unsigned w, unsigned h, unsigned bpp)
{
  unsigned passw[7], passh[7];
  size_t filter_passstart[8], padded_passstart[8], passstart[8];
  unsigned i;

  Adam7_getpassvalues(passw, passh, filter_passstart, padded_passstart, passstart, w, h, bpp);

  if(bpp >= 8)
  {
    for(i = 0; i != 7; ++i)
    {
      unsigned x, y, b;
      size_t bytewidth = bpp / 8;
      for(y = 0; y < passh[i]; ++y)
      for(x = 0; x < passw[i]; ++x)
      {
        size_t pixelinstart = ((ADAM7_IY[i] + y * ADAM7_DY[i]) * w + ADAM7_IX[i] + x * ADAM7_DX[i]) * bytewidth;
        size_t pixeloutstart = passstart[i] + (y * passw[i] + x) * bytewidth;
        for(b = 0; b < bytewidth; ++b)
        {
          out[pixeloutstart + b] = in[pixelinstart + b];
        }
      }
    }
  }
  else // bpp < 8: Adam7 with pixels < 8 bit is a bit trickier: with bit pointers
  {
    for(i = 0; i != 7; ++i)
    {
      unsigned x, y, b;
      unsigned ilinebits = bpp * passw[i];
      unsigned olinebits = bpp * w;
      size_t obp, ibp; // bit pointers (for out and in buffer)
      for(y = 0; y < passh[i]; ++y)
      for(x = 0; x < passw[i]; ++x)
      {
        ibp = (ADAM7_IY[i] + y * ADAM7_DY[i]) * olinebits + (ADAM7_IX[i] + x * ADAM7_DX[i]) * bpp;
        obp = (8 * passstart[i]) + (y * ilinebits + x * bpp);
        for(b = 0; b < bpp; ++b)
        {
          unsigned char bit = readBitFromReversedStream(&ibp, in);
          setBitOfReversedStream(&obp, out, bit);
        }
      }
    }
  }
}

/*out must be buffer big enough to contain uncompressed IDAT chunk data, and in must contain the full image.
return value is error**/
static unsigned preProcessScanlines(unsigned char** out, size_t* outsize, const unsigned char* in,
                                    unsigned w, unsigned h,
                                    const LodePNGInfo* info_png, const LodePNGEncoderSettings* settings)
{
  /*
  This function converts the pure 2D image with the PNG's colortype, into filtered-padded-interlaced data. Steps:
  *) if no Adam7: 1) add padding bits (= possible extra bits per scanline if bpp < 8) 2) filter
  *) if adam7: 1) Adam7_interlace 2) 7x add padding bits 3) 7x filter
  */
  unsigned bpp = lodepng_get_bpp(&info_png->color);
  unsigned error = 0;

  if(info_png->interlace_method == 0)
  {
    *outsize = h + (h * ((w * bpp + 7) / 8)); // image size plus an extra byte per scanline + possible padding bits
    *out = (unsigned char*)malloc(*outsize);
    if(!(*out) && (*outsize)) error = 83; // alloc fail

    if(!error)
    {
      // non multiple of 8 bits per scanline, padding bits needed per scanline
      if(bpp < 8 && w * bpp != ((w * bpp + 7) / 8) * 8)
      {
        unsigned char* padded = (unsigned char*)malloc(h * ((w * bpp + 7) / 8));
        if(!padded) error = 83; // alloc fail
        if(!error)
        {
          addPaddingBits(padded, in, ((w * bpp + 7) / 8) * 8, w * bpp, h);
          error = filter(*out, padded, w, h, &info_png->color, settings);
        }
        free(padded);
      }
      else
      {
        // we can immediately filter into the out buffer, no other steps needed
        error = filter(*out, in, w, h, &info_png->color, settings);
      }
    }
  }
  else // interlace_method is 1 (Adam7)
  {
    unsigned passw[7], passh[7];
    size_t filter_passstart[8], padded_passstart[8], passstart[8];
    unsigned char* adam7;

    Adam7_getpassvalues(passw, passh, filter_passstart, padded_passstart, passstart, w, h, bpp);

    *outsize = filter_passstart[7]; // image size plus an extra byte per scanline + possible padding bits
    *out = (unsigned char*)malloc(*outsize);
    if(!(*out)) error = 83; // alloc fail

    adam7 = (unsigned char*)malloc(passstart[7]);
    if(!adam7 && passstart[7]) error = 83; // alloc fail

    if(!error)
    {
      unsigned i;

      Adam7_interlace(adam7, in, w, h, bpp);
      for(i = 0; i != 7; ++i)
      {
        if(bpp < 8)
        {
          unsigned char* padded = (unsigned char*)malloc(padded_passstart[i + 1] - padded_passstart[i]);
          if(!padded) ERROR_BREAK(83); // alloc fail
          addPaddingBits(padded, &adam7[passstart[i]],
                         ((passw[i] * bpp + 7) / 8) * 8, passw[i] * bpp, passh[i]);
          error = filter(&(*out)[filter_passstart[i]], padded,
                         passw[i], passh[i], &info_png->color, settings);
          free(padded);
        }
        else
        {
          error = filter(&(*out)[filter_passstart[i]], &adam7[padded_passstart[i]],
                         passw[i], passh[i], &info_png->color, settings);
        }

        if(error) break;
      }
    }

    free(adam7);
  }

  return error;
}

/*
palette must have 4 * palettesize bytes allocated, and given in format RGBARGBARGBARGBA...
returns 0 if the palette is opaque,
returns 1 if the palette has a single color with alpha 0 ==> color key
returns 2 if the palette is semi-translucent.
*/
static unsigned getPaletteTranslucency(const unsigned char* palette, size_t palettesize)
{
  size_t i;
  unsigned key = 0;
  unsigned r = 0, g = 0, b = 0; // the value of the color with alpha 0, so long as color keying is possible
  for(i = 0; i != palettesize; ++i)
  {
    if(!key && palette[4 * i + 3] == 0)
    {
      r = palette[4 * i + 0]; g = palette[4 * i + 1]; b = palette[4 * i + 2];
      key = 1;
      i = (size_t)(-1); // restart from beginning, to detect earlier opaque colors with key's value
    }
    else if(palette[4 * i + 3] != 255) return 2;
    // when key, no opaque RGB may have key's RGB
    else if(key && r == palette[i * 4 + 0] && g == palette[i * 4 + 1] && b == palette[i * 4 + 2]) return 2;
  }
  return key;
}

// This function allocates the out buffer with standard malloc and stores the size in *outsize.
unsigned lodepng_encode(unsigned char** out, size_t* outsize,
                        const unsigned char* image, unsigned w, unsigned h,
                        LodePNGState* state)
{
  LodePNGInfo info;
  ucvector outv;
  unsigned char* data = 0; // uncompressed version of the IDAT chunk data
  size_t datasize = 0;

  // provide some proper output values if error will happen
  *out = 0;
  *outsize = 0;
  state->error = 0;

  // check input values validity
  if((state->info_png.color.colortype == LCT_PALETTE || state->encoder.force_palette)
      && (state->info_png.color.palettesize == 0 || state->info_png.color.palettesize > 256))
  {
    CERROR_RETURN_ERROR(state->error, 68); // invalid palette size, it is only allowed to be 1-256
  }
  if(state->info_png.interlace_method > 1)
  {
    CERROR_RETURN_ERROR(state->error, 71); // error: unexisting interlace mode
  }
  state->error = checkColorValidity(state->info_png.color.colortype, state->info_png.color.bitdepth);
  if(state->error) return state->error; // error: unexisting color type given
  state->error = checkColorValidity(state->info_raw.colortype, state->info_raw.bitdepth);
  if(state->error) return state->error; // error: unexisting color type given

  //  color convert and compute scanline filter types 
  lodepng_info_init(&info);
  lodepng_info_copy(&info, &state->info_png);
  if(state->encoder.auto_convert)
  {
    state->error = lodepng_auto_choose_color(&info.color, image, w, h, &state->info_raw);
  }
  if (!state->error)
  {
    if(!lodepng_color_mode_equal(&state->info_raw, &info.color))
    {
      unsigned char* converted;
      size_t size = (w * h * (size_t)lodepng_get_bpp(&info.color) + 7) / 8;

      converted = (unsigned char*)malloc(size);
      if(!converted && size) state->error = 83; // alloc fail
      if(!state->error)
      {
        state->error = lodepng_convert(converted, image, &info.color, &state->info_raw, w, h);
      }
      if(!state->error) preProcessScanlines(&data, &datasize, converted, w, h, &info, &state->encoder);
      free(converted);
    }
    else preProcessScanlines(&data, &datasize, image, w, h, &info, &state->encoder);
  }

  //  output all PNG chunks 
  ucvector_init(&outv);
  while(!state->error) // while only executed once, to break on error
  {
    // write signature and chunks
    writeSignature(&outv);
    // IHDR
    addChunk_IHDR(&outv, w, h, info.color.colortype, info.color.bitdepth, info.interlace_method);
    // PLTE
    if(info.color.colortype == LCT_PALETTE)
    {
      addChunk_PLTE(&outv, &info.color);
    }
    if(state->encoder.force_palette && (info.color.colortype == LCT_RGB || info.color.colortype == LCT_RGBA))
    {
      addChunk_PLTE(&outv, &info.color);
    }
    // tRNS
    if(info.color.colortype == LCT_PALETTE && getPaletteTranslucency(info.color.palette, info.color.palettesize) != 0)
    {
      addChunk_tRNS(&outv, &info.color);
    }
    if((info.color.colortype == LCT_GREY || info.color.colortype == LCT_RGB) && info.color.key_defined)
    {
      addChunk_tRNS(&outv, &info.color);
    }
    // IDAT (multiple IDAT chunks must be consecutive)
    state->error = addChunk_IDAT(&outv, data, datasize, &state->encoder.zlibsettings);
    if(state->error) break;
    addChunk_IEND(&outv);

    break; // this isn't really a while loop; no error happened so break out now!
  }

  lodepng_info_cleanup(&info);
  free(data);
  // instead of cleaning the vector up, give it to the output
  *out = outv.data;
  *outsize = outv.size;

  return state->error;
}

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
unsigned lodepng_encode_memory(unsigned char** out, size_t* outsize, const unsigned char* image,
                               unsigned w, unsigned h, LodePNGColorType colortype, unsigned bitdepth)
{
  LodePNGState state;
  lodepng_state_init(&state);
  state.info_raw.colortype = colortype;
  state.info_raw.bitdepth = bitdepth;
  state.info_png.color.colortype = colortype;
  state.info_png.color.bitdepth = bitdepth;
  lodepng_encode(out, outsize, image, w, h, &state);
  return state.error;
}

// Same as lodepng_encode_memory, but always encodes from 32-bit RGBA raw image.
unsigned lodepng_encode32(unsigned char** out, size_t* outsize, const unsigned char* image, unsigned w, unsigned h)
{
  return lodepng_encode_memory(out, outsize, image, w, h, LCT_RGBA, 8);
}

unsigned lodepng_encode24(unsigned char** out, size_t* outsize, const unsigned char* image, unsigned w, unsigned h)
{
  return lodepng_encode_memory(out, outsize, image, w, h, LCT_RGB, 8);
}

// Converts raw pixel data into a PNG file on disk.
// Same as the other encode functions, but instead takes a filename as output.
// NOTE: This overwrites existing files without warning!
unsigned lodepng_encode_file(const char* filename, const unsigned char* image, unsigned w, unsigned h,
                             LodePNGColorType colortype, unsigned bitdepth)
{
  unsigned char* buffer;
  size_t buffersize;
  unsigned error = lodepng_encode_memory(&buffer, &buffersize, image, w, h, colortype, bitdepth);
  if(!error) error = lodepng_save_file(buffer, buffersize, filename);
  free(buffer);
  return error;
}

unsigned lodepng_encode32_file(const char* filename, const unsigned char* image, unsigned w, unsigned h)
{
  return lodepng_encode_file(filename, image, w, h, LCT_RGBA, 8);
}

unsigned lodepng_encode24_file(const char* filename, const unsigned char* image, unsigned w, unsigned h)
{
  return lodepng_encode_file(filename, image, w, h, LCT_RGB, 8);
}

void lodepng_encoder_settings_init(LodePNGEncoderSettings* settings)
{
  lodepng_compress_settings_init(&settings->zlibsettings);
  settings->filter_palette_zero = 1;
  settings->filter_strategy = LFS_MINSUM;
  settings->auto_convert = 1;
  settings->force_palette = 0;
}

#ifdef LODEPNG_COMPILE_ERROR_TEXT
// This returns the description of a numerical error code in English. This is also
// the documentation of all the error codes.
const char* lodepng_error_text(unsigned code)
{
  switch(code)
  {
    case 0: return "no error";
    case 1: return "nothing done yet"; // the Encoder/Decoder has done nothing yet, error checking makes no sense yet
    case 10: return "end of input memory reached without huffman end code"; // while huffman decoding
    case 11: return "error in code tree made it jump outside of huffman tree"; // while huffman decoding
    case 13:
    case 14:
    case 15: return "problem while processing dynamic deflate block";
    case 16: return "unexisting code while processing dynamic deflate block";
    case 18: return "invalid distance code while inflating";
    case 20: return "invalid deflate block BTYPE encountered while decoding";
    case 21: return "NLEN is not ones complement of LEN in a deflate block";
     /*end of out buffer memory reached while inflating:
     This can happen if the inflated deflate data is longer than the amount of bytes required to fill up
     all the pixels of the image, given the color depth and image dimensions. Something that doesn't
     happen in a normal, well encoded, PNG image.*/
    case 23: return "end of in buffer memory reached while inflating";
    case 24: return "invalid FCHECK in zlib header";
    case 25: return "invalid compression method in zlib header";
    case 26: return "FDICT encountered in zlib header while it's not used for PNG";
    case 27: return "PNG file is smaller than a PNG header";
    // Checks the magic file header, the first 8 bytes of the PNG file
    case 28: return "incorrect PNG signature, it's no PNG or corrupted";
    case 29: return "first chunk is not the header chunk";
    case 30: return "chunk length too large, chunk broken off at end of file";
    case 31: return "illegal PNG color type or bpp";
    case 32: return "illegal PNG compression method";
    case 33: return "illegal PNG filter method";
    case 34: return "illegal PNG interlace method";
    case 36: return "illegal PNG filter type encountered";
    case 37: return "illegal bit depth for this color type given";
    case 38: return "the palette is too big"; // more than 256 colors
    case 41: return "tRNS chunk has wrong size for RGB image";
    case 42: return "tRNS chunk appeared while it was not allowed for this color type";
    case 48: return "empty input buffer given to decoder. Maybe caused by non-existing file?";
    case 49: return "jumped past memory while generating dynamic huffman tree";
    case 50: return "jumped past memory while generating dynamic huffman tree";
    case 51: return "jumped past memory while inflating huffman block";
    case 52: return "jumped past memory while inflating";
    case 53: return "size of zlib data too small";
    case 54: return "repeat symbol in tree while there was no value symbol yet";
    /*jumped past tree while generating huffman tree, this could be when the
    tree will have more leaves than symbols after generating it out of the
    given lenghts. They call this an oversubscribed dynamic bit lengths tree in zlib.*/
    case 55: return "jumped past tree while generating huffman tree";
    case 56: return "given output image colortype or bitdepth not supported for color conversion";
    case 60: return "invalid window size given in the settings of the encoder (must be 0-32768)";
    // LodePNG leaves the choice of RGB to greyscale conversion formula to the user.
    // this would result in the inability of a deflated block to ever contain an end code. It must be at least 1.
    case 64: return "the length of the END symbol 256 in the Huffman tree is 0";
    case 68: return "tried to encode a PLTE chunk with a palette that has less than 1 or more than 256 colors";
    case 71: return "unexisting interlace mode given to encoder (must be 0 or 1)";
    // length could be wrong, or data chopped off
    case 77: return "integer overflow in buffer size";
    case 78:
    case 79: return "failed to open file";
    case 80: return "tried creating a tree of 0 symbols";
    case 81: return "lazy matching at pos 0 is impossible";
    case 82: return "color conversion to palette requested while a color isn't in palette";
    case 83: return "out of mem";
    case 84: return "given image too small to contain all pixels to be encoded";
    case 86: return "impossible offset in lz77 encoding (internal bug)";
    case 88: return "invalid filter strategy given for LodePNGEncoderSettings.filter_strategy";
    // the windowsize in the LodePNGCompressSettings. Requiring POT(==> & instead of %) makes encoding 12% faster.
    case 90: return "windowsize must be a power of two";
    case 91: return "invalid decompressed idat size";
    case 92: return "too many pixels, not supported";
    case 93: return "zero width or height is invalid";
    case 94: return "header chunk must have a size of 13 bytes";
  }
  return "unknown error code";
}
#endif // LODEPNG_COMPILE_ERROR_TEXT
