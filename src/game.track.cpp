// game.track.cpp — Track data loading, conversion, and rendering.
//
// All Track data from Amiga StuntCarRacer is scaled by PC_FACTOR (2x).

#include "platform.h"
#include "game.h"

#include "render.software.h"


// ---------------------------------------------------------------------------
// Per-piece track arrays (populated by ReadAmigaTrackData)
// ---------------------------------------------------------------------------

// X/Z grid position for each piece (high nibble = Z, low nibble = X)
static char Piece_X_Z_Position[MAX_PIECES_PER_TRACK];

// Angle and template index per piece.
// Top 2 bits = rough angle (0/90/180/270). Bit 4 = extra 180-degree flip.
// Low nibble = piece template index.
static char Piece_Angle_And_Template[MAX_PIECES_PER_TRACK];

// Left Y coordinate IDs per piece. Bit 7 = coords stored as words.
static char Left_Y_Coordinate_ID[MAX_PIECES_PER_TRACK];

// Right Y coordinate IDs per piece. Bit 7 = alternate road line colour.
static char Right_Y_Coordinate_ID[MAX_PIECES_PER_TRACK];

// Overall Y shift applied to all left-side Y coords for each piece.
static int16_t Left_Overall_Y_Shift[MAX_PIECES_PER_TRACK];

// Overall Y shift applied to all right-side Y coords for each piece.
static int16_t Right_Overall_Y_Shift[MAX_PIECES_PER_TRACK];

// ---------------------------------------------------------------------------
// Y co-ordinate data for piece templates (B = bytes, W = words)
// ---------------------------------------------------------------------------
static uint8_t B1037[] =
{
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static uint8_t B1051[] =
{
	0x00, 0x60, 0x61, 0x03, 0x44,
	0x26, 0x28, 0x2a, 0x2c
};
static uint8_t W1060[] =
{
	0x00, 0x00, 0x02, 0x00, 0x04, 0x00,
	0x06, 0x00, 0x08, 0x00, 0x0a, 0x00,
	0x0c, 0x00, 0x0e, 0x00, 0x10, 0x00
};
static uint8_t B1078[] =
{
	0x00, 0x20, 0x40, 0x60, 0x01, 0x21, 0x41,
	0x61, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02
};
static uint8_t B1092[] =
{
	0x02, 0x61, 0x41, 0x21, 0x01, 0x60, 0x40,
	0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static uint8_t B1106[] =
{
	0x00, 0x60, 0x21, 0x51, 0x02,
	0x22, 0x42, 0x62, 0x03, 0x13
};
static uint8_t B1116[] =
{
	0x00, 0x20, 0x40, 0x70, 0x21,
	0x41, 0x61, 0x02, 0x22, 0x32
};
static uint8_t B1126[] =
{
	0x00, 0x02, 0x04, 0x06, 0xe7,
	0x29, 0xca, 0x4b, 0x2c
};
static uint8_t B1135[] =
{
	0x46, 0x96, 0x55, 0x85, 0x24,
	0x33, 0xb2, 0x21, 0x00
};
static uint8_t B1144[] =
{
	0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x20,
	0x40, 0x60, 0x01, 0x21, 0x41, 0x61, 0x02
};
static uint8_t B1158[] =
{
	0x02, 0x02, 0x02, 0x02, 0x02, 0x71, 0x61,
	0x41, 0x21, 0x01, 0x60, 0x40, 0x20, 0x00
};

static uint8_t B1172[] =
{
	0x00, 0x10, 0x10, 0x10, 0x10,
	0x10, 0x10, 0x90, 0x80
};
static uint8_t B1181[] =
{
	0x10, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x80, 0x90
};

static uint8_t B1190[] =
{
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
	0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b
};
static uint8_t W1202[] =
{
	0x1b, 0x80, 0x1c, 0x80, 0x1d, 0x80,
	0x1e, 0x80, 0x1f, 0x80, 0x20, 0x80,
	0xa1, 0x80, 0x80, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static uint8_t B1226[] =
{
	0x4e, 0x1d, 0xdb, 0x0a, 0xa8,
	0x36, 0x34, 0x22, 0x00
};
static uint8_t W1235[] =
{
	0x00, 0x00, 0x9b, 0x20, 0x19, 0xe0,
	0x18, 0xa0, 0x17, 0x60, 0x16, 0x20,
	0x14, 0xe0, 0x13, 0xa0, 0x12, 0x60,
	0x11, 0x20, 0x0f, 0xe0, 0x0e, 0xa0
};
static uint8_t B1259[] =
{
	0x48, 0x27, 0x26, 0x35, 0x44, 0x63,
	0x13, 0x42, 0x71, 0x21, 0x50, 0x00
};
static uint8_t B1271[] =
{
	0x13, 0x03, 0x62, 0x42, 0x22,
	0x02, 0x51, 0x21, 0xe0, 0x80
};
static uint8_t B1281[] =
{
	0x05, 0x05, 0x85, 0x00, 0x00,
	0x85, 0x05, 0x05, 0x05
};
static uint8_t B1290[] =
{
	0x32, 0x22, 0x02, 0x61, 0x41,
	0x21, 0x70, 0x40, 0xa0, 0x80
};
static uint8_t B1300[] =
{
	0x00, 0x40, 0x01, 0x41, 0x02,
	0x42, 0x03, 0x33, 0x63
};
static uint8_t B1309[] =
{
	0x00, 0x20, 0x30, 0x30, 0x30,
	0x30, 0x30, 0x30, 0x30, 0x30
};
static uint8_t B1319[] =
{
	0x30, 0x10, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00
};
static uint8_t B1329[] =
{
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x90, 0xb0
};
static uint8_t B1338[] =
{
	0x30, 0x30, 0x30, 0x30, 0x30,
	0x30, 0x30, 0xa0, 0x80
};
static uint8_t B1347[] =
{
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x90, 0xb0
};
static uint8_t B1357[] =
{
	0x30, 0x30, 0x30, 0x30, 0x30,
	0x30, 0x30, 0x30, 0xa0, 0x80
};
static uint8_t B1367[] =
{
	0x00, 0x21, 0x42, 0x53, 0xe4,
	0x65, 0xe6, 0x57, 0x48
};
static uint8_t B1376[] =
{
	0x00, 0x60, 0x41, 0x92, 0x62,
	0xa3, 0x63, 0x14, 0x44
};
static uint8_t B1385[] =
{
	0x00, 0x20, 0x40, 0xd0, 0x60,
	0x60, 0xd0, 0x40, 0x20
};
static uint8_t B1394[] =
{
	0x04, 0x63, 0xb3, 0x03, 0x42,
	0x82, 0x31, 0x60, 0x00
};
static uint8_t B1403[] =
{
	0xa6, 0x80, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x80, 0x35
};
static uint8_t B1412[] =
{
	0x47, 0x87, 0x46, 0x75, 0x25, 0x44,
	0x63, 0x03, 0x22, 0x41, 0x60, 0x00
};
static uint8_t B1424[] =
{
	0x08, 0x27, 0x36, 0xc5, 0x44,
	0x43, 0x32, 0x21, 0x00
};
static uint8_t B1433[] =
{
	0x50, 0x50, 0x50, 0x50, 0xc0,
	0x30, 0x20, 0x10, 0x00
};
static uint8_t B1442[] =
{
	0x00, 0x00, 0x10, 0x30, 0x60,
	0x11, 0x51, 0x22, 0x72
};
static uint8_t B1451[] =
{
	0x00, 0x60, 0x41, 0xa2, 0xd2, 0x62,
	0xf2, 0x72, 0x72, 0x72, 0x72, 0x72
};
static uint8_t B1463[] =
{
	0x22, 0xb2, 0x32, 0xa2, 0x12,
	0xf1, 0x31, 0x60, 0x00
};
static uint8_t B1472[] =
{
	0x0a, 0x68, 0x47, 0x26, 0x05,
	0x63, 0x42, 0x21, 0x00
};
static uint8_t B1481[] =
{
	0x00, 0x10, 0x30, 0x60, 0x21, 0x71,
	0x42, 0x13, 0x63, 0x34, 0x05, 0x55
};
static uint8_t B1493[] =
{
	0x55, 0x26, 0x76, 0x47, 0x18, 0x68,
	0x39, 0x8a, 0x00, 0x00, 0x00, 0x00
};
static uint8_t B1505[] =
{
	0x00, 0xc7, 0x76, 0x26, 0x55, 0x05,
	0x34, 0x63, 0x13, 0x42, 0x71, 0x21
};
static uint8_t B1517[] =
{
	0x21, 0x60, 0x30, 0x10, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static uint8_t B1529[] =
{
	0x8a, 0x80, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x80, 0x4c
};
static uint8_t B1538[] =
{
	0x00, 0x41, 0x03, 0x44, 0x06,
	0x47, 0x09, 0x4a, 0x0c
};
static uint8_t B1547[] =
{
	0x70, 0x50, 0x30, 0x10, 0x00,
	0x10, 0x30, 0x50, 0x70
};
static uint8_t B1556[] =
{
	0xaa, 0x80, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x80, 0x2a
};
static uint8_t B1565[] =
{
	0x59, 0x49, 0x39, 0xa9, 0x63,
	0x63, 0x63, 0x63, 0x47
};
static uint8_t B1574[] =
{
	0x00, 0x00, 0x00, 0x10, 0x30, 0x50,
	0x01, 0x31, 0x71, 0x42, 0x23, 0x14
};
static uint8_t B1586[] =
{
	0x62, 0x62, 0x62, 0xd2, 0x42, 0xa2,
	0x02, 0x61, 0xb1, 0x01, 0x40, 0x00
};
static uint8_t B1598[] =
{
	0x00, 0x40, 0x01, 0x41, 0x02, 0x42, 0x03,
	0x43, 0x04, 0x64, 0x45, 0x26, 0x07, 0x67
};
static uint8_t B1612[] =
{
	0x00, 0x10, 0x20, 0x30, 0x40,
	0x40, 0x40, 0x40, 0x40, 0x40
};
static uint8_t B1622[] =
{
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x10, 0x30, 0x60, 0x21
};
static uint8_t B1631[] =
{
	0x8d, 0x80, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00
};
static uint8_t W1640[] =
{
	0x00, 0x00, 0x00, 0x00, 0x80, 0x00,
	0x9c, 0x80, 0x1c, 0x80, 0x9c, 0x80,
	0x80, 0x00, 0x00, 0x00, 0x00, 0x00
};
static uint8_t B1658[] =
{
	0x00, 0x00, 0x10, 0x20, 0x40,
	0x60, 0x01, 0x31, 0x71
};
static uint8_t B1667[] =
{
	0x00, 0x10, 0x30, 0x70, 0x31,
	0x71, 0xb2, 0x52, 0x62
};
static uint8_t B1676[] =
{
	0x00, 0x00, 0x00, 0x10, 0x30,
	0x60, 0x21, 0x02, 0x03
};
static uint8_t B1685[] =
{
	0x00, 0x10, 0x30, 0x60, 0x21,
	0x71, 0x62, 0x53, 0x44
};
static uint8_t B1694[] =
{
	0x00, 0x70, 0x61, 0x52, 0x43,
	0x34, 0x25, 0x16, 0x07
};
static uint8_t B1703[] =
{
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x80, 0x2e
};
static uint8_t B1712[] =
{
	0x00, 0x01, 0xf1, 0x52, 0xa3,
	0x63, 0x94, 0x34, 0x54
};
static uint8_t B1721[] =
{
	0x00, 0x30, 0xd0, 0x70, 0x11,
	0xa1, 0x31, 0x41, 0x41, 0x41
};
static uint8_t B1731[] =
{
	0x40, 0x10, 0x00, 0x00, 0x00,
	0x10, 0x40, 0x11, 0x61
};
static uint8_t B1740[] =
{
	0x40, 0x40, 0x40, 0x40, 0x40,
	0x40, 0x30, 0x20, 0x10, 0x00
};
static uint8_t W1750[] =
{
	0x9a, 0xc0, 0x80, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x80, 0x00, 0x0c, 0x80
};
static uint8_t B1768[] =
{
	0x24, 0x03, 0x02, 0x21, 0x60,
	0x30, 0x10, 0x00, 0x00
};
static uint8_t B1777[] =
{
	0x47, 0x46, 0x65, 0x25, 0x05,
	0x05, 0x15, 0x35, 0x75
};
static uint8_t B1786[] =
{
	0x80, 0xe6, 0x16, 0x45, 0x74,
	0x24, 0x53, 0x23, 0x13
};
static uint8_t B1795[] =
{
	0x46, 0x25, 0x14, 0x13, 0x22,
	0x41, 0x70, 0x30, 0x00
};
static uint8_t B1804[] =
{
	0x00, 0x01, 0x12, 0x33, 0x54,
	0x75, 0x17, 0x38, 0x59, 0x7a
};
static uint8_t B1814[] =
{
	0x02, 0x71, 0xd1, 0x21, 0x60,
	0x30, 0x10, 0x00, 0x00
};
static uint8_t B1823[] =
{
	0x00, 0x00, 0x10, 0x30, 0x60,
	0x21, 0xd1, 0x71, 0x02
};
static uint8_t B1832[] =
{
	0x00, 0x40, 0x81, 0x31, 0xd1,
	0x61, 0xf1, 0x71, 0x71
};
static uint8_t B1841[] =
{
	0x22, 0x61, 0x21, 0x60, 0x30,
	0x10, 0x00, 0x00, 0x00
};
static uint8_t B1850[] =
{
	0x00, 0x60, 0x41, 0x22, 0x03, 0x63,
	0x44, 0x25, 0x06, 0x66, 0x47, 0x28
};
static uint8_t B1862[] =
{
	0x00, 0x00, 0x10, 0x30, 0x60,
	0x21, 0x71, 0x52, 0x43
};
static uint8_t B1871[] =
{
	0x24, 0x45, 0xe6, 0x80, 0x21,
	0x42, 0x63, 0x05, 0x26
};
static uint8_t W1880[] =
{
	0x28, 0x60, 0x27, 0xc0, 0x27, 0x40,
	0x26, 0xe0, 0x26, 0xa0, 0x26, 0x80,
	0x26, 0x80, 0x26, 0xa0, 0x26, 0xe0,
	0x27, 0x20, 0xa7, 0x60, 0x00, 0x00
};
static uint8_t B1904[] =
{
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
	0x07, 0x08, 0x68, 0x49, 0x2a, 0x0b, 0x6b
};
static uint8_t B1918[] =
{
	0x00, 0x70, 0x51, 0x32, 0x13,
	0x73, 0x54, 0x35, 0x06
};
static uint8_t B1927[] =
{
	0x00, 0x50, 0x31, 0x12, 0x72,
	0x53, 0x34, 0x15, 0x06
};
static uint8_t B1936[] =
{
	0x00, 0x60, 0x41, 0x22, 0x03, 0x73, 0x64,
	0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b
};
static uint8_t B1950[] =
{
	0x00, 0x60, 0x41, 0x22, 0x03, 0x53, 0x24,
	0x64, 0x25, 0x65, 0x26, 0x66, 0x27, 0x67
};
static uint8_t B1964[] =
{
	0x00, 0x81, 0x61, 0xa2, 0x42,
	0x52, 0x52, 0x52, 0x52
};
static uint8_t B1973[] =
{
	0x00, 0x41, 0x72, 0x14, 0x35,
	0x56, 0x77, 0x19, 0x3a, 0x5b
};
static uint8_t B1983[] =
{
	0x00, 0x21, 0x42, 0x63, 0x05,
	0x26, 0x47, 0x68, 0x1a, 0x5b
};
static uint8_t B1993[] =
{
	0x64, 0x14, 0x43, 0x72, 0x22, 0x51,
	0x01, 0x40, 0x20, 0x10, 0x00, 0x00
};
static uint8_t B2005[] =
{
	0x05, 0x05, 0x05, 0x15, 0x25,
	0x45, 0xe5, 0x00, 0x00
};
static uint8_t B2014[] =
{
	0x22, 0x12, 0xf1, 0x51, 0x31,
	0x11, 0x60, 0x30, 0x00
};
static uint8_t B2023[] =
{
	0x00, 0x50, 0x31, 0x22, 0x23,
	0x34, 0x55, 0x76, 0x18
};
static uint8_t B2032[] =
{
	0x00, 0x21, 0x42, 0x63, 0x05,
	0x26, 0x47, 0x68, 0x79, 0x7a
};
static uint8_t B2042[] =
{
	0x52, 0x71, 0x21, 0x60, 0x30,
	0x10, 0x00, 0x00, 0x00
};
// 20/08/1998 - following four arrays form the animating section of the DrawBridge
// The original Amiga StuntCarRacer data for these (large spikes) has been changed to a
// set of data from the MoveDrawBridge function (height of 15), to prevent some of
// the road surfaces being made black when this is not required.
static uint8_t W2051[] =
{
	0x00, 0x00, 0x02, 0x60, 0x04, 0xc0,
	0x07, 0x20, 0x09, 0x80, 0x0b, 0xe0,
	0x0e, 0x40, 0x10, 0xa0, 0x13, 0x00
};
static uint8_t W2069[] =
{
	0x13, 0x00, 0x15, 0x60, 0x17, 0xc0,
	0x1a, 0x20, 0x1c, 0x80, 0x1e, 0xe0,
	0x21, 0x40, 0x23, 0xa0, 0x00, 0x00
};
static uint8_t W2087[] =
{
	0x00, 0x00, 0x23, 0xa0, 0x21, 0x40,
	0x1e, 0xe0, 0x1c, 0x80, 0x1a, 0x20,
	0x17, 0xc0, 0x15, 0x60, 0x13, 0x00
};
static uint8_t W2105[] =
{
	0x13, 0x00, 0x10, 0xa0, 0x0e, 0x40,
	0x0b, 0xe0, 0x09, 0x80, 0x07, 0x20,
	0x04, 0xc0, 0x02, 0x60, 0x00, 0x00
};
static uint8_t B2123[] =
{
	0x63, 0x43, 0xa3, 0xf2, 0x42,
	0x02, 0x41, 0x01, 0x40
};
static uint8_t B2132[] =
{
	0x28, 0x47, 0x66, 0x06, 0x25, 0x44,
	0x63, 0x03, 0x22, 0x41, 0x60, 0x00
};
static uint8_t B2144[] =
{
	0x14, 0x73, 0x43, 0x03, 0x42,
	0x02, 0x41, 0x01, 0x40, 0x00
};
static uint8_t B2154[] =
{
	0x74, 0x14, 0x43, 0x03, 0x42,
	0x02, 0x41, 0x01, 0x40, 0x00
};
static uint8_t B2164[] =
{
	0x14, 0x53, 0x13, 0x52, 0x12,
	0x51, 0x11, 0x50, 0xa0, 0x80
};
static uint8_t B2174[] =
{
	0x74, 0x34, 0x73, 0x33, 0x72,
	0x32, 0x71, 0x31, 0xe0, 0x80
};
static uint8_t B2184[] =
{
	0x23, 0x62, 0x22, 0x61, 0x21,
	0x70, 0x40, 0x20, 0x00
};
static uint8_t B2193[] =
{
	0x42, 0x42, 0x52, 0x72, 0x13,
	0x43, 0xf3, 0x80, 0x00
};
static uint8_t B2202[] =
{
	0x00, 0x00, 0x00, 0x80, 0x85,
	0x05, 0x05, 0x05, 0x05
};
static uint8_t B2211[] =
{
	0x0c, 0x59, 0x47, 0x55, 0x04,
	0x52, 0x41, 0x50, 0x00
};
static uint8_t B2220[] =
{
	0x00, 0x10, 0x30, 0x50, 0xe0,
	0x50, 0x30, 0x10, 0x00
};
static uint8_t B2229[] =
{
	0x00, 0x00, 0x00, 0x00, 0x80,
	0x00, 0x00, 0x00, 0x00
};
static uint8_t B2238[] =
{
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x73, 0xe3, 0x33, 0x52, 0x41, 0x00
};
static uint8_t B2250[] =
{
	0x44, 0x04, 0x43, 0x03, 0x42,
	0x02, 0x41, 0x01, 0x40, 0x00
};
static uint8_t B2260[] =
{
	0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
	0x31, 0xa1, 0x01, 0xe0, 0x30, 0x00
};
static uint8_t W2272[] =
{
	0x18, 0xc0, 0x16, 0x80, 0x14, 0x40,
	0x12, 0x00, 0x0f, 0xc0, 0x0d, 0x80,
	0x0b, 0x40, 0x09, 0x00, 0x06, 0xc0,
	0x04, 0x80, 0x02, 0x40, 0x00, 0x00
};
static uint8_t B2296[] =
{
	0x7e, 0x4c, 0x1a, 0x08, 0x16, 0x44,
	0x13, 0x02, 0x11, 0x40, 0x10, 0x00
};
static uint8_t B2308[] =
{
	0x60, 0x30, 0x10, 0x00, 0x00,
	0x10, 0x30, 0x60, 0x21
};
static uint8_t W2317[] =
{
	0x13, 0x00, 0x10, 0xa0, 0x0e, 0x40,
	0x0b, 0xe0, 0x09, 0x80, 0x07, 0x20,
	0x04, 0xc0, 0x02, 0x60, 0x00, 0x00
};
static uint8_t B2335[] =
{
	0x00, 0xe8, 0x18, 0x47, 0x76,
	0x26, 0x55, 0x05, 0x34
};
static uint8_t B2344[] =
{
	0x00, 0x00, 0x00, 0x10, 0x30,
	0x60, 0x21, 0x71, 0x42
};
static uint8_t B2353[] =
{
	0x00, 0x21, 0x42, 0x63, 0x05,
	0x26, 0x47, 0x68, 0x0a
};
static uint8_t B2362[] =
{
	0x00, 0x60, 0x31, 0x71, 0x32,
	0x72, 0x33, 0x73, 0x34, 0x74
};
static uint8_t B2372[] =
{
	0x00, 0x20, 0x50, 0x11, 0x51,
	0x12, 0x52, 0x13, 0x53, 0x14
};
static uint8_t B2382[] =
{
	0x00, 0x40, 0x01, 0x41, 0x02,
	0x42, 0x03, 0x43, 0x94, 0xf4
};
static uint8_t B2392[] =
{
	0x00, 0x40, 0x01, 0x41, 0x02,
	0x42, 0x03, 0x43, 0xf3, 0x94
};

static constexpr int32_t NUM_AMIGA_PIECE_Y = 128;
static constexpr int32_t MAX_Y_COORDS_PER_PIECE = MAX_SEGMENTS_PER_PIECE + 1;

static struct AMIGA_PIECE_Y
{
	uint8_t* amigaY;
	int32_t words; /* true/false - indicating whether or not co-ords are word sized */
	int32_t size;
} Amiga_Piece_Y[NUM_AMIGA_PIECE_Y] =
{
	{B1037, false, sizeof(B1037)},
	{B1051, false, sizeof(B1051)},
	{W1060, true, sizeof(W1060)},
	{B1078, false, sizeof(B1078)},
	{B1092, false, sizeof(B1092)},
	{B1106, false, sizeof(B1106)},
	{B1116, false, sizeof(B1116)},
	{B1126, false, sizeof(B1126)},
	{B1135, false, sizeof(B1135)},
	{B1144, false, sizeof(B1144)},
	{B1158, false, sizeof(B1158)},
	{B1172, false, sizeof(B1172)},
	{B1181, false, sizeof(B1181)},
	{B1190, false, sizeof(B1190)},
	{W1202, true, sizeof(W1202)},
	{B1226, false, sizeof(B1226)},
	{W1235, true, sizeof(W1235)},
	{B1259, false, sizeof(B1259)},
	{B1271, false, sizeof(B1271)},
	{B1281, false, sizeof(B1281)},
	{B1290, false, sizeof(B1290)},
	{B1300, false, sizeof(B1300)},
	{B1309, false, sizeof(B1309)},
	{B1319, false, sizeof(B1319)},
	{B1329, false, sizeof(B1329)},
	{B1338, false, sizeof(B1338)},
	{B1347, false, sizeof(B1347)},
	{B1357, false, sizeof(B1357)},
	{B1367, false, sizeof(B1367)},
	{B1376, false, sizeof(B1376)},
	{B1385, false, sizeof(B1385)},
	{B1394, false, sizeof(B1394)},
	{B1403, false, sizeof(B1403)},
	{B1412, false, sizeof(B1412)},
	{B1424, false, sizeof(B1424)},
	{B1433, false, sizeof(B1433)},
	{B1442, false, sizeof(B1442)},
	{B1451, false, sizeof(B1451)},
	{B1463, false, sizeof(B1463)},
	{B1472, false, sizeof(B1472)},
	{B1481, false, sizeof(B1481)},
	{B1493, false, sizeof(B1493)},
	{B1505, false, sizeof(B1505)},
	{B1517, false, sizeof(B1517)},
	{B1529, false, sizeof(B1529)},
	{B1538, false, sizeof(B1538)},
	{B1547, false, sizeof(B1547)},
	{B1556, false, sizeof(B1556)},
	{B1565, false, sizeof(B1565)},
	{B1574, false, sizeof(B1574)},
	{B1586, false, sizeof(B1586)},
	{B1598, false, sizeof(B1598)},
	{B1612, false, sizeof(B1612)},
	{B1622, false, sizeof(B1622)},
	{B1631, false, sizeof(B1631)},
	{W1640, true, sizeof(W1640)},
	{B1658, false, sizeof(B1658)},
	{B1667, false, sizeof(B1667)},
	{B1676, false, sizeof(B1676)},
	{B1685, false, sizeof(B1685)},
	{B1694, false, sizeof(B1694)},
	{B1703, false, sizeof(B1703)},
	{B1712, false, sizeof(B1712)},
	{B1721, false, sizeof(B1721)},
	{B1731, false, sizeof(B1731)},
	{B1740, false, sizeof(B1740)},
	{W1750, true, sizeof(W1750)},
	{B1768, false, sizeof(B1768)},
	{B1777, false, sizeof(B1777)},
	{B1786, false, sizeof(B1786)},
	{B1795, false, sizeof(B1795)},
	{B1804, false, sizeof(B1804)},
	{B1814, false, sizeof(B1814)},
	{B1823, false, sizeof(B1823)},
	{B1832, false, sizeof(B1832)},
	{B1841, false, sizeof(B1841)},
	{B1850, false, sizeof(B1850)},
	{B1862, false, sizeof(B1862)},
	{B1871, false, sizeof(B1871)},
	{W1880, true, sizeof(W1880)},
	{B1904, false, sizeof(B1904)},
	{nullptr, false, 0},
	{B1918, false, sizeof(B1918)},
	{B1927, false, sizeof(B1927)},
	{B1936, false, sizeof(B1936)},
	{B1950, false, sizeof(B1950)},
	{B1964, false, sizeof(B1964)},
	{B1973, false, sizeof(B1973)},
	{B1983, false, sizeof(B1983)},
	{B1993, false, sizeof(B1993)},
	{B2005, false, sizeof(B2005)},
	{B2014, false, sizeof(B2014)},
	{B2023, false, sizeof(B2023)},
	{B2032, false, sizeof(B2032)},
	{B2042, false, sizeof(B2042)},
	{W2051, true, sizeof(W2051)},
	{W2069, true, sizeof(W2069)},
	{W2087, true, sizeof(W2087)},
	{W2105, true, sizeof(W2105)},
	{B2123, false, sizeof(B2123)},
	{B2132, false, sizeof(B2132)},
	{B2144, false, sizeof(B2144)},
	{B2154, false, sizeof(B2154)},
	{B2164, false, sizeof(B2164)},
	{B2174, false, sizeof(B2174)},
	{B2184, false, sizeof(B2184)},
	{B2193, false, sizeof(B2193)},
	{B2202, false, sizeof(B2202)},
	{B2211, false, sizeof(B2211)},
	{B2220, false, sizeof(B2220)},
	{B2229, false, sizeof(B2229)},
	{B2238, false, sizeof(B2238)},
	{B2250, false, sizeof(B2250)},
	{B2260, false, sizeof(B2260)},
	{W2272, true, sizeof(W2272)},
	{B2296, false, sizeof(B2296)},
	{B2308, false, sizeof(B2308)},
	{W2317, true, sizeof(W2317)},
	{B2335, false, sizeof(B2335)},
	{B2344, false, sizeof(B2344)},
	{nullptr, false, 0},
	{nullptr, false, 0},
	{B2353, false, sizeof(B2353)},
	{nullptr, false, 0},
	{B2362, false, sizeof(B2362)},
	{B2372, false, sizeof(B2372)},
	{B2382, false, sizeof(B2382)},
	{B2392, false, sizeof(B2392)}
};

// ---------------------------------------------------------------------------
// Piece template XZ data (Amiga format, converted at startup)
// ---------------------------------------------------------------------------

// Straight 8-segment
static uint8_t Amiga_Piece0_XZ[9 * 8] =
{
	0x40, 0x03, 0x00, 0x00, 0xc0, 0x04, 0x00, 0x00,
	0x40, 0x03, 0x00, 0x01, 0xc0, 0x04, 0x00, 0x01,
	0x40, 0x03, 0x00, 0x02, 0xc0, 0x04, 0x00, 0x02,
	0x40, 0x03, 0x00, 0x03, 0xc0, 0x04, 0x00, 0x03,
	0x40, 0x03, 0x00, 0x04, 0xc0, 0x04, 0x00, 0x04,
	0x40, 0x03, 0x00, 0x05, 0xc0, 0x04, 0x00, 0x05,
	0x40, 0x03, 0x00, 0x06, 0xc0, 0x04, 0x00, 0x06,
	0x40, 0x03, 0x00, 0x07, 0xc0, 0x04, 0x00, 0x07,
	0x40, 0x03, 0x00, 0x08, 0xc0, 0x04, 0x00, 0x08
};

// Curve right 8-segment
static uint8_t Amiga_Piece1_XZ[9 * 8] =
{
	0x40, 0x03, 0x00, 0x00, 0xc0, 0x04, 0x00, 0x00,
	0x4c, 0x03, 0x05, 0x01, 0xca, 0x04, 0xdf, 0x00,
	0x73, 0x03, 0x07, 0x02, 0xeb, 0x04, 0xbc, 0x01,
	0xb2, 0x03, 0x05, 0x03, 0x22, 0x05, 0x95, 0x02,
	0x0a, 0x04, 0xfb, 0x03, 0x6d, 0x05, 0x68, 0x03,
	0x7a, 0x04, 0xe7, 0x04, 0xcd, 0x05, 0x32, 0x04,
	0x00, 0x05, 0xc8, 0x05, 0x40, 0x06, 0xf2, 0x04,
	0x9c, 0x05, 0x9a, 0x06, 0xc5, 0x06, 0xa6, 0x05,
	0x4c, 0x06, 0x5b, 0x07, 0x5b, 0x07, 0x4c, 0x06
};

// Curve left 8-segment
static uint8_t Amiga_Piece3_XZ[9 * 8] =
{
	0x3f, 0x03, 0x00, 0x00, 0xbf, 0x04, 0x00, 0x00,
	0x35, 0x03, 0xdf, 0x00, 0xb3, 0x04, 0x05, 0x01,
	0x14, 0x03, 0xbc, 0x01, 0x8c, 0x04, 0x07, 0x02,
	0xdd, 0x02, 0x95, 0x02, 0x4d, 0x04, 0x05, 0x03,
	0x92, 0x02, 0x68, 0x03, 0xf5, 0x03, 0xfb, 0x03,
	0x32, 0x02, 0x32, 0x04, 0x85, 0x03, 0xe7, 0x04,
	0xbf, 0x01, 0xf2, 0x04, 0xff, 0x02, 0xc8, 0x05,
	0x3a, 0x01, 0xa6, 0x05, 0x63, 0x02, 0x9a, 0x06,
	0xa4, 0x00, 0x4c, 0x06, 0xb3, 0x01, 0x5b, 0x07
};

// Diagonal straight 13-segment
static uint8_t Amiga_Piece4_XZ[14 * 8] =
{
	0x78, 0xff, 0x87, 0x00, 0x87, 0x00, 0x78, 0xff,
	0x2c, 0x00, 0x3c, 0x01, 0x3c, 0x01, 0x2c, 0x00,
	0xe1, 0x00, 0xf0, 0x01, 0xf0, 0x01, 0xe1, 0x00,
	0x96, 0x01, 0xa5, 0x02, 0xa5, 0x02, 0x96, 0x01,
	0x4a, 0x02, 0x5a, 0x03, 0x5a, 0x03, 0x4a, 0x02,
	0xff, 0x02, 0x0e, 0x04, 0x0e, 0x04, 0xff, 0x02,
	0xb3, 0x03, 0xc3, 0x04, 0xc3, 0x04, 0xb3, 0x03,
	0x68, 0x04, 0x77, 0x05, 0x77, 0x05, 0x68, 0x04,
	0x1d, 0x05, 0x2c, 0x06, 0x2c, 0x06, 0x1d, 0x05,
	0xd1, 0x05, 0xe1, 0x06, 0xe1, 0x06, 0xd1, 0x05,
	0x86, 0x06, 0x95, 0x07, 0x95, 0x07, 0x86, 0x06,
	0x3a, 0x07, 0x4a, 0x08, 0x4a, 0x08, 0x3a, 0x07,
	0xef, 0x07, 0xff, 0x08, 0xff, 0x08, 0xef, 0x07,
	0xa4, 0x08, 0xb3, 0x09, 0xb3, 0x09, 0xa4, 0x08
};

// Curve right 9-segment
static uint8_t Amiga_Piece6_XZ[10 * 8] =
{
	0x40, 0x03, 0x00, 0x00, 0xc0, 0x04, 0x00, 0x00,
	0x4c, 0x03, 0x1c, 0x01, 0xca, 0x04, 0xfb, 0x00,
	0x71, 0x03, 0x36, 0x02, 0xeb, 0x04, 0xf4, 0x01,
	0xaf, 0x03, 0x4c, 0x03, 0x22, 0x05, 0xe9, 0x02,
	0x04, 0x04, 0x5c, 0x04, 0x6d, 0x05, 0xd9, 0x03,
	0x71, 0x04, 0x63, 0x05, 0xcd, 0x05, 0xc1, 0x04,
	0xf5, 0x04, 0x60, 0x06, 0x41, 0x06, 0xa0, 0x05,
	0x8e, 0x05, 0x50, 0x07, 0xc8, 0x06, 0x73, 0x06,
	0x3b, 0x06, 0x32, 0x08, 0x61, 0x07, 0x3b, 0x07,
	0xfc, 0x06, 0x03, 0x09, 0x0b, 0x08, 0xf4, 0x07
};

// Curve left 9-segment
static uint8_t Amiga_Piece7_XZ[10 * 8] =
{
	0x40, 0x03, 0x00, 0x00, 0xc0, 0x04, 0x00, 0x00,
	0x35, 0x03, 0xfb, 0x00, 0xb3, 0x04, 0x1c, 0x01,
	0x14, 0x03, 0xf4, 0x01, 0x8e, 0x04, 0x36, 0x02,
	0xdd, 0x02, 0xe9, 0x02, 0x50, 0x04, 0x4c, 0x03,
	0x92, 0x02, 0xd9, 0x03, 0xfb, 0x03, 0x5c, 0x04,
	0x32, 0x02, 0xc1, 0x04, 0x8e, 0x03, 0x63, 0x05,
	0xbe, 0x01, 0xa0, 0x05, 0x0a, 0x03, 0x60, 0x06,
	0x37, 0x01, 0x73, 0x06, 0x71, 0x02, 0x50, 0x07,
	0x9e, 0x00, 0x3b, 0x07, 0xc4, 0x01, 0x32, 0x08,
	0xf4, 0xff, 0xf4, 0x07, 0x03, 0x01, 0x03, 0x09
};

// Diagonal straight 11-segment
static uint8_t Amiga_Piece10_XZ[12 * 8] =
{
	0x78, 0xff, 0x87, 0x00, 0x87, 0x00, 0x78, 0xff,
	0x32, 0x00, 0x41, 0x01, 0x41, 0x01, 0x32, 0x00,
	0xec, 0x00, 0xfc, 0x01, 0xfc, 0x01, 0xec, 0x00,
	0xa6, 0x01, 0xb6, 0x02, 0xb6, 0x02, 0xa6, 0x01,
	0x60, 0x02, 0x70, 0x03, 0x70, 0x03, 0x60, 0x02,
	0x1b, 0x03, 0x2a, 0x04, 0x2a, 0x04, 0x1b, 0x03,
	0xd5, 0x03, 0xe4, 0x04, 0xe4, 0x04, 0xd5, 0x03,
	0x8f, 0x04, 0x9f, 0x05, 0x9f, 0x05, 0x8f, 0x04,
	0x49, 0x05, 0x59, 0x06, 0x59, 0x06, 0x49, 0x05,
	0x03, 0x06, 0x13, 0x07, 0x13, 0x07, 0x03, 0x06,
	0xbe, 0x06, 0xcd, 0x07, 0xcd, 0x07, 0xbe, 0x06,
	0x78, 0x07, 0x87, 0x08, 0x87, 0x08, 0x78, 0x07
};


// piece XZs after conversion from Amiga
static COORD_XZ Piece0_XZ[9 * 2];
static COORD_XZ Piece1_XZ[9 * 2];
static COORD_XZ Piece3_XZ[9 * 2];
static COORD_XZ Piece4_XZ[14 * 2];
static COORD_XZ Piece6_XZ[10 * 2];
static COORD_XZ Piece7_XZ[10 * 2];
static COORD_XZ Piece10_XZ[12 * 2];

// only 0,1,3,4,6,7,10 used below
static struct PIECE_TEMPLATE
{
	int32_t numSegments;
	// could eventually get rid of next value and just use type
	int32_t curveToLeft; // true/false
	int32_t type; // near.section.byte1 - 0x00 STRAIGHT, 0x40 DIAGONAL (45 degrees)
	//						0x80 CURVE RIGHT, 0Xc0 CURVE LEFT
	int32_t lengthReduction;
	int32_t steeringAmount;
	COORD_XZ* pieceXZ;
} Piece_Templates[11] =
{
	{8, false, 0x00, 128, 0x20, Piece0_XZ},
	{8, false, 0x80, 135, 0x3e, Piece1_XZ},
	{0, false, 0x00, 0, 0x00, nullptr},
	{8, true, 0xc0, 135, 0x3e, Piece3_XZ},
	{13, false, 0x40, 128, 0x20, Piece4_XZ},
	{0, false, 0x00, 0, 0x00, nullptr},
	{9, false, 0x80, 122, 0x32, Piece6_XZ},
	{9, true, 0xc0, 122, 0x32, Piece7_XZ},
	{0, false, 0x00, 0, 0x00, nullptr},
	{0, false, 0x00, 0, 0x00, nullptr},
	{11, false, 0x40, 124, 0x20, Piece10_XZ}
};

// piece Ys after conversion from Amiga
// (could allocate memory individually, as some have less than MAX_Y_COORDS_PER_PIECE,
//  but this wouldn't save much memory and would have to free it before exit)
static COORD_Y Piece_Y[NUM_AMIGA_PIECE_Y][MAX_Y_COORDS_PER_PIECE];


// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

static void ConvertAmigaPieceData(void);

static COORD_XZ GetRotatedPieceXZ(COORD_XZ coord,
                                  int32_t roughAngle);

static void ConvertAmigaPieceXZ(const uint8_t* amiga,
                                COORD_XZ* dest,
                                int32_t size);

static void ConvertAmigaPieceY(const AMIGA_PIECE_Y* amiga,
                               COORD_Y* dest);

static void UpdateDrawBridgeYCoords(const TrackState& t, int32_t piece,
                                    int32_t firstCoord,
                                    int32_t lastCoord,
                                    int32_t firstYIndex,
                                    int32_t direction);

static int32_t ReadAmigaTrackData(TrackState& t, int32_t track);

// ---------------------------------------------------------------------------
// Track conversion (Amiga data -> PC format)
// ---------------------------------------------------------------------------

wchar_t* GetTrackName(const int32_t track)
{
	static wchar_t trackNames[][32] =
	{
		L"Little Ramp",
		L"Stepping Stones",
		L"Hump Back",
		L"Big Ramp",
		L"Ski Jump",
		L"Draw Bridge",
		L"High Jump",
		L"Roller Coaster"
	};

	return trackNames[track];
}

char GetPieceAngleAndTemplate(const int32_t piece)
{
	return Piece_Angle_And_Template[piece];
}


int32_t ConvertAmigaTrack(TrackState& t, const int32_t track)
{
	static int32_t first_time = true;

	if (first_time)
	{
		ConvertAmigaPieceData();
		first_time = false;
	}

	if (t.TrackID != NO_TRACK)
		FreeTrackData(t);

	// set the required track
	if (!ReadAmigaTrackData(t, track))
	{
		t.TrackID = NO_TRACK;
		return false;
	}

	// store track ID
	t.TrackID = track;

	// clear the Track Map
	for (int32_t x = 0; x < NUM_TRACK_CUBES; x++)
		for (int32_t z = 0; z < NUM_TRACK_CUBES; z++)
			t.Track_Map[x][z] = -1;

	// convert each piece of track in turn
	int32_t firstSegment = 0;
	for (int32_t piece = 0; piece < t.NumTrackPieces; piece++)
	{
		// store x,y,z of piece's cube
		const char c = Piece_X_Z_Position[piece];
		const int32_t x = static_cast<int32_t>(c) & 0x0f;
		const int32_t z = (static_cast<int32_t>(c) & 0xf0) >> 4;
		t.Track[piece].x = x;
		t.Track[piece].y = 0;
		t.Track[piece].z = z;


		// record piece within Track Map
		t.Track_Map[x][z] = piece;


		// get piece template information
		const char tc = Piece_Angle_And_Template[piece];
		const int32_t templateNum = static_cast<int32_t>(tc) & 0x0f;
		const int32_t roughAngle = static_cast<int32_t>(tc) & 0xc0;
		const int32_t reverseOrder = static_cast<int32_t>(tc) & 0x10;

		t.Track[piece].roughPieceAngle = roughAngle * MAX_ANGLE / 0x100;
		t.Track[piece].oppositeDirection = reverseOrder != 0;
		t.Track[piece].curveToLeft = Piece_Templates[templateNum].curveToLeft;
		t.Track[piece].type = Piece_Templates[templateNum].type;
		t.Track[piece].lengthReduction = Piece_Templates[templateNum].lengthReduction;
		t.Track[piece].steeringAmount = Piece_Templates[templateNum].steeringAmount;


		// store number of segments
		const int32_t numSegments = Piece_Templates[templateNum].numSegments;

		t.Track[piece].numSegments = numSegments;
		t.Track[piece].firstSegment = firstSegment;
		firstSegment += numSegments;


		// store initial road lines colour
		t.Track[piece].initialColour = (static_cast<int32_t>(Right_Y_Coordinate_ID[piece]) & 0x80) >> 7;


		// decide road/side surface colours
		uint8_t roadColour, sidesColour;
		if (piece & 1)
		{
			roadColour = SCR_BASE_COLOUR + 2;
			sidesColour = SCR_BASE_COLOUR + 10;
		}
		else
		{
			roadColour = SCR_BASE_COLOUR + 1;
			sidesColour = SCR_BASE_COLOUR + 15;
		}


		// allocate memory for unrotated co-ordinates
		const int32_t size = sizeof(COORD_3D) * (numSegments + 1) * 4;
		t.Track[piece].coords = static_cast<COORD_3D*>(malloc(size));
		t.Track[piece].coordsSize = size;
		if (t.Track[piece].coords == nullptr)
			return false;

		const COORD_XZ* pieceXZ = Piece_Templates[templateNum].pieceXZ;

		// store piece x,z using template
		const int32_t numCoords = numSegments + 1;
		int32_t j, step;

		if (reverseOrder)
		{
			j = numCoords * 2 - 1;
			step = -1;
		}
		else
		{
			j = 0;
			step = 1;
		}

		for (int32_t i = 0; i < numCoords; i++)
		{
			auto rotated = GetRotatedPieceXZ(pieceXZ[j], roughAngle);
			double rx = rotated.x * PC_FACTOR;
			double rz = rotated.z * PC_FACTOR;

			t.Track[piece].coords[(i * 4)].x = rx;
			t.Track[piece].coords[(i * 4)].z = rz;
			t.Track[piece].coords[i * 4 + 2].x = rx;
			t.Track[piece].coords[i * 4 + 2].z = rz;
			j += step;

			rotated = GetRotatedPieceXZ(pieceXZ[j], roughAngle);
			rx = rotated.x * PC_FACTOR;
			rz = rotated.z * PC_FACTOR;

			t.Track[piece].coords[i * 4 + 1].x = rx;
			t.Track[piece].coords[i * 4 + 1].z = rz;
			t.Track[piece].coords[i * 4 + 3].x = rx;
			t.Track[piece].coords[i * 4 + 3].z = rz;
			j += step;
		}


		// store piece y using IDs and overall shifts
		const int32_t leftID = static_cast<int32_t>(Left_Y_Coordinate_ID[piece]) & 0x7f;
		const int32_t rightID = static_cast<int32_t>(Right_Y_Coordinate_ID[piece]) & 0x7f;
		const int32_t leftOverallShift = Left_Overall_Y_Shift[piece];
		const int32_t rightOverallShift = Right_Overall_Y_Shift[piece];

		for (int32_t i = 0; i < numCoords; i++)
		{
			// top left y
			double y = Piece_Y[leftID][i].y + leftOverallShift;
			t.Track[piece].coords[(i * 4)].y = y * PC_FACTOR;

			// bottom left y
			t.Track[piece].coords[i * 4 + 2].y = TRACK_BOTTOM_Y;

			// top right y
			y = Piece_Y[rightID][i].y + rightOverallShift;
			t.Track[piece].coords[i * 4 + 1].y = y * PC_FACTOR;

			// bottom right y
			t.Track[piece].coords[i * 4 + 3].y = TRACK_BOTTOM_Y;
		}


		// set road/side surface colours
		t.Track[piece].sidesColour = sidesColour;

		for (int32_t i = 0; i < numSegments; i++)
		{
			const double y1 = t.Track[piece].coords[(i * 4)].y - t.Track[piece].coords[(i + 1) * 4].y;
			const double y2 = t.Track[piece].coords[i * 4 + 1].y - t.Track[piece].coords[(i + 1) * 4 + 1].y;
			const double y = abs(y1) > abs(y2) ? y1 : y2;

			if (abs(y) >= 640 * PC_FACTOR)
				t.Track[piece].roadColour[i] = SCR_BASE_COLOUR + 0;
			else
				t.Track[piece].roadColour[i] = roadColour;
		}
	}
	t.NumTrackSegments = firstSegment;


	// ensure pieces join up perfectly by copying start co-ordinates of
	// current piece to end co-ordinates of previous piece
	for (int32_t piece = 0; piece < t.NumTrackPieces; piece++)
	{
		const int32_t lastPiece = piece > 0 ? piece - 1 : t.NumTrackPieces - 1;

		// Pieces are in different cubes; account for cube position difference
		// (in render-space world units, matching the COORD_3D coords).
		const double cubeX = t.Track[piece].x * WORLD_CUBE_SIZE;
		const double cubeY = t.Track[piece].y * WORLD_CUBE_SIZE;
		const double cubeZ = t.Track[piece].z * WORLD_CUBE_SIZE;
		const double lastCubeX = t.Track[lastPiece].x * WORLD_CUBE_SIZE;
		const double lastCubeY = t.Track[lastPiece].y * WORLD_CUBE_SIZE;
		const double lastCubeZ = t.Track[lastPiece].z * WORLD_CUBE_SIZE;

		int32_t j = t.Track[lastPiece].numSegments * 4;
		for (int32_t i = 0; i < 4; i++, j++)
		{
			t.Track[lastPiece].coords[j].x = t.Track[piece].coords[i].x + cubeX - lastCubeX;
			t.Track[lastPiece].coords[j].y = t.Track[piece].coords[i].y + cubeY - lastCubeY;
			t.Track[lastPiece].coords[j].z = t.Track[piece].coords[i].z + cubeZ - lastCubeZ;
		}
	}

	return true;
}

static COORD_XZ GetRotatedPieceXZ(const COORD_XZ coord,
                                  const int32_t roughAngle)
{
	switch (roughAngle)
	{
	case 0x00: // 0 degrees
		return {coord.x, coord.z};

	case 0x40: // 90 degrees clockwise
		return {coord.z, 0x800 - coord.x};

	case 0x80: // 180 degrees
		return {0x800 - coord.x, 0x800 - coord.z};

	case 0xc0: // 270 degrees clockwise
		return {0x800 - coord.z, coord.x};

	default:
		return {coord.x, coord.z};
	}
}

void FreeTrackData(const TrackState& t)
{
	for (int32_t piece = 0; piece < t.NumTrackPieces; piece++)
	{
		free(t.Track[piece].coords);
	}
}

static void ConvertAmigaPieceData(void)
{
	// convert XZ data
	ConvertAmigaPieceXZ(Amiga_Piece0_XZ, Piece0_XZ, sizeof(Piece0_XZ));
	ConvertAmigaPieceXZ(Amiga_Piece1_XZ, Piece1_XZ, sizeof(Piece1_XZ));
	ConvertAmigaPieceXZ(Amiga_Piece3_XZ, Piece3_XZ, sizeof(Piece3_XZ));
	ConvertAmigaPieceXZ(Amiga_Piece4_XZ, Piece4_XZ, sizeof(Piece4_XZ));
	ConvertAmigaPieceXZ(Amiga_Piece6_XZ, Piece6_XZ, sizeof(Piece6_XZ));
	ConvertAmigaPieceXZ(Amiga_Piece7_XZ, Piece7_XZ, sizeof(Piece7_XZ));
	ConvertAmigaPieceXZ(Amiga_Piece10_XZ, Piece10_XZ, sizeof(Piece10_XZ));

	// convert Y data
	for (int32_t i = 0; i < NUM_AMIGA_PIECE_Y; i++)
		ConvertAmigaPieceY(&Amiga_Piece_Y[i], Piece_Y[i]);
}

static void ConvertAmigaPieceXZ(const uint8_t* amiga,
                                COORD_XZ* dest,
                                const int32_t size)
{
	const int32_t number = size / sizeof(COORD_XZ);

	for (int32_t i = 0; i < number; i++)
	{
		const uint8_t xlo = *amiga++;
		const uint8_t xhi = *amiga++;
		dest->x = static_cast<int16_t>(xhi << 8 | xlo);

		const uint8_t zlo = *amiga++;
		const uint8_t zhi = *amiga++;
		dest->z = static_cast<int16_t>(zhi << 8 | zlo);

		dest++;
	}
}

static void ConvertAmigaPieceY(const AMIGA_PIECE_Y* amiga,
                               COORD_Y* dest)
{
	int32_t number = amiga->size;
	const uint8_t* yptr = amiga->amigaY;

	if (amiga->words != 0)
	{
		number /= 2;
		for (int32_t i = 0; i < number; i++)
		{
			const uint8_t high = *yptr++;
			const uint8_t low = *yptr++;
			dest->y = static_cast<int16_t>((high & 0x7f) << 8 | low);
			dest++;
		}
	}
	else
	{
		for (int32_t i = 0; i < number; i++)
		{
			const uint8_t b = *yptr++;
			const int32_t ya = b << 1 & 0xe0;
			const int32_t yb = (b & 0x0f) << 8;
			dest->y = ya | yb;
			dest++;
		}
	}
}

// ---------------------------------------------------------------------------
// Vertex / index buffer management for track & shadow rendering
// ---------------------------------------------------------------------------

static constexpr int32_t MAX_VERTICES_PER_TRACK = MAX_PIECES_PER_TRACK * MAX_SEGMENTS_PER_PIECE * 4 * 3;
static constexpr int32_t MAX_INDICES_PER_TRACK = MAX_PIECES_PER_TRACK * MAX_SEGMENTS_PER_PIECE * 6 * 3;
static constexpr int32_t MAX_VERTICES_PER_SHADOW = 2 * 3;

using TrackFaceType = enum
{
	LEFT_SIDE = 0,
	RIGHT_SIDE,
	ROAD,
	NUM_TRACK_FACES
};

static SWVertex* pTrackVertices = nullptr;
static uint32_t* pTrackIndices = nullptr;
static SWVertex* pShadowVertices = nullptr;
static int32_t trackVertices, trackIndices, trackSegments;
static int32_t numShadowVertices;
static int32_t maxTrackVertices, maxTrackIndices, maxShadowVertices;
static int32_t PieceFirstVertex[NUM_TRACK_FACES][MAX_PIECES_PER_TRACK];
static int32_t PieceFirstIndex[NUM_TRACK_FACES][MAX_PIECES_PER_TRACK];
static int32_t SegmentRoadTexture[MAX_PIECES_PER_TRACK * MAX_SEGMENTS_PER_PIECE];


static void SetSegmentTextures(const TrackState& t)
{
	int32_t tt;

	trackSegments = 0;
	for (int32_t piece = 0; piece < t.NumTrackPieces; piece++)
	{
		const int32_t numSegments = t.Track[piece].numSegments;
		int32_t rlc = t.Track[piece].initialColour; // get initial colour of road side lines

		// loop through piece segments
		for (int32_t s = 0; s < numSegments; s++)
		{
			const uint8_t roadColourIndex = t.Track[piece].roadColour[s];
			if (piece == t.StartLinePiece && s == numSegments - 1)
			{
				tt = 5; // set colour to white for start line
			}
			else if (roadColourIndex == SCR_BASE_COLOUR + 1) // darker
			{
				if (rlc == 0)
					tt = 0;
				else
					tt = 2;
			}
			else if (roadColourIndex == SCR_BASE_COLOUR + 2) // lighter
			{
				if (rlc == 0)
					tt = 1;
				else
					tt = 3;
			}
			else // if (roadColourIndex == SCR_BASE_COLOUR)	// black
			{
				tt = 4;
			}
			SegmentRoadTexture[trackSegments++] = tt;

			// switch to other road side lines colour
			rlc = rlc == 0 ? 1 : 0;
		}
	}
}


static Vec3 GetPieceVertex(const TrackState& t, const int32_t piece, const double piece_x, const double piece_y,
                           const double piece_z,
                           const int32_t offset)
{
	double x = t.Track[piece].coords[offset].x;
	double y = t.Track[piece].coords[offset].y;
	// y co-ordinates need to be divided by 4 for display
	y = y / 4;
	double z = t.Track[piece].coords[offset].z;

	x += piece_x;
	y += piece_y;
	z += piece_z;

	return Vec3(x, y, z);
}


static void StorePieceVertex(const TrackState& t, const int32_t piece, const double piece_x, const double piece_y,
                             const double piece_z,
                             const int32_t coordOffset, SWVertex* pVertices, const uint32_t colour, const double tu,
                             const double tv)
{
	const Vec3 v = GetPieceVertex(t, piece, piece_x, piece_y, piece_z, coordOffset);
	pVertices[trackVertices].pos = v;
	pVertices[trackVertices].color = colour;
	pVertices[trackVertices].tu = tu;
	pVertices[trackVertices].tv = tv;
	++trackVertices;
}


static void StoreQuadIndices(uint32_t* pIndices, const int32_t baseVert, const int i0, const int i1, const int i2,
                             const int i3, const int i4, const int i5)
{
	pIndices[trackIndices++] = baseVert + i0;
	pIndices[trackIndices++] = baseVert + i1;
	pIndices[trackIndices++] = baseVert + i2;
	pIndices[trackIndices++] = baseVert + i3;
	pIndices[trackIndices++] = baseVert + i4;
	pIndices[trackIndices++] = baseVert + i5;
}


void RemoveShadowTriangles(void)
{
	numShadowVertices = 0;
}

void StoreShadowTriangle(const Vec3& v1, const Vec3& v2, const Vec3& v3, const int32_t other_colour)
{
	if (!pShadowVertices) return;

	const uint32_t colour = SCRGB(other_colour ? SCR_BASE_COLOUR + 15 : SCR_BASE_COLOUR + 5);
	const Vec3 verts[] = {v1, v2, v3};
	for (const auto& v : verts)
	{
		SWVertex& sv = pShadowVertices[numShadowVertices];
		sv.pos = v;
		sv.color = colour;
		sv.tu = 0;
		sv.tv = 0;
		++numShadowVertices;
	}
}


// Create/update piece in vertex and index buffers using indexed triangle list
static void CreateUpdatePieceInVB(const TrackState& t, const int32_t piece, const int32_t face, SWVertex* pVertices,
                                  uint32_t* pIndices,
                                  const bool create)
{
	const int32_t numSegments = t.Track[piece].numSegments;

	// Store index of piece's first vertex/index when creating (used by MoveDrawBridge updates)
	if (create)
	{
		PieceFirstVertex[face][piece] = trackVertices;
		PieceFirstIndex[face][piece] = trackIndices;
	}

	// Calculate position of piece's bottom front left corner, within world
	// (in render-space world units, matching the COORD_3D coords).
	const double piece_x = t.Track[piece].x * WORLD_CUBE_SIZE;
	const double piece_y = t.Track[piece].y * WORLD_CUBE_SIZE;
	const double piece_z = t.Track[piece].z * WORLD_CUBE_SIZE;

	if (face == ROAD)
	{
		for (int32_t s = 0; s < numSegments; s++)
		{
			const int32_t offset = s * 4;
			uint8_t roadColourIndex = t.Track[piece].roadColour[s];
			if (piece == t.StartLinePiece && s == numSegments - 1)
				roadColourIndex = SCR_BASE_COLOUR + 15; // white for start line

			const uint32_t colour = SCRGB(roadColourIndex);
			const int32_t baseVert = trackVertices;
			StorePieceVertex(t, piece, piece_x, piece_y, piece_z, offset, pVertices, colour, 0.0f, 0.0f);
			StorePieceVertex(t, piece, piece_x, piece_y, piece_z, offset + 1, pVertices, colour, 1.0f, 0.0f);
			StorePieceVertex(t, piece, piece_x, piece_y, piece_z, offset + 4, pVertices, colour, 0.0f, 0.0f);
			StorePieceVertex(t, piece, piece_x, piece_y, piece_z, offset + 5, pVertices, colour, 1.0f, 0.0f);
			StoreQuadIndices(pIndices, baseVert, 0, 2, 3, 0, 3, 1);
		}
	}
	else if (face == LEFT_SIDE)
	{
		const uint32_t colour = SCRGB(t.Track[piece].sidesColour);
		for (int32_t s = 0; s < numSegments; s++)
		{
			const int32_t offset = s * 4;
			const int32_t baseVert = trackVertices;
			StorePieceVertex(t, piece, piece_x, piece_y, piece_z, offset, pVertices, colour, 0.0f, 0.0f);
			StorePieceVertex(t, piece, piece_x, piece_y, piece_z, offset + 2, pVertices, colour, 0.0f, 0.0f);
			StorePieceVertex(t, piece, piece_x, piece_y, piece_z, offset + 4, pVertices, colour, 0.0f, 0.0f);
			StorePieceVertex(t, piece, piece_x, piece_y, piece_z, offset + 6, pVertices, colour, 0.0f, 0.0f);
			StoreQuadIndices(pIndices, baseVert, 0, 1, 3, 0, 3, 2);
		}
	}
	else // RIGHT_SIDE
	{
		const uint32_t colour = SCRGB(t.Track[piece].sidesColour);
		for (int32_t s = 0; s < numSegments; s++)
		{
			const int32_t offset = s * 4;
			const int32_t baseVert = trackVertices;
			StorePieceVertex(t, piece, piece_x, piece_y, piece_z, offset + 1, pVertices, colour, 0.0f, 0.0f);
			StorePieceVertex(t, piece, piece_x, piece_y, piece_z, offset + 3, pVertices, colour, 0.0f, 0.0f);
			StorePieceVertex(t, piece, piece_x, piece_y, piece_z, offset + 5, pVertices, colour, 0.0f, 0.0f);
			StorePieceVertex(t, piece, piece_x, piece_y, piece_z, offset + 7, pVertices, colour, 0.0f, 0.0f);
			StoreQuadIndices(pIndices, baseVert, 2, 3, 1, 2, 1, 0);
		}
	}
}


// Shadow rendering (on the track surface)
void CreateShadowVertexBuffer(void)
{
	if (pShadowVertices == nullptr)
	{
		maxShadowVertices = MAX_VERTICES_PER_SHADOW;
		pShadowVertices = new SWVertex[maxShadowVertices];
	}
	numShadowVertices = 0;
}


void FreeShadowVertexBuffer(void)
{
	delete[] pShadowVertices;
	pShadowVertices = nullptr;
}

void CreateTrackVertexBuffer(const TrackState& t)
{
	if (pTrackVertices == nullptr)
	{
		maxTrackVertices = MAX_VERTICES_PER_TRACK;
		pTrackVertices = new SWVertex[maxTrackVertices];
	}
	if (pTrackIndices == nullptr)
	{
		maxTrackIndices = MAX_INDICES_PER_TRACK;
		pTrackIndices = new uint32_t[maxTrackIndices];
	}

	SetSegmentTextures(t);

	// convert each piece of track in turn
	trackVertices = 0;
	trackIndices = 0;
	for (int32_t face = LEFT_SIDE; face < NUM_TRACK_FACES; face++)
	{
		for (int32_t piece = 0; piece < t.NumTrackPieces; piece++)
		{
			CreateUpdatePieceInVB(t, piece, face, pTrackVertices, pTrackIndices, true);
		}
	}
}


void FreeTrackVertexBuffer(void)
{
	delete[] pTrackVertices;
	pTrackVertices = nullptr;
	delete[] pTrackIndices;
	pTrackIndices = nullptr;
}


// Used to update vertices and indices, e.g. by MoveDrawBridge
static void UpdatePieceInVB(const TrackState& t, const int32_t piece)
{
	const int32_t savedTrackVertices = trackVertices;
	const int32_t savedTrackIndices = trackIndices;

	if (pTrackVertices == nullptr || pTrackIndices == nullptr)
	{
		return;
	}

	for (int32_t face = LEFT_SIDE; face < NUM_TRACK_FACES; face++)
	{
		trackVertices = PieceFirstVertex[face][piece];
		trackIndices = PieceFirstIndex[face][piece];

		CreateUpdatePieceInVB(t, piece, face, pTrackVertices, pTrackIndices, false);
	}

	trackVertices = savedTrackVertices;
	trackIndices = savedTrackIndices;
}

// ---------------------------------------------------------------------------
// Track rendering
// ---------------------------------------------------------------------------

static constexpr int32_t TEXTURED_SEGMENTS_AROUND_PLAYER = 11;


void DrawTrack(const TrackState& t, const GameModeType GameMode, SoftwareRenderer& r, const int32_t playerCurrentPiece,
               const int32_t playerCurrentSegment, const std::vector<SWTexture>& roadTextures)
{
	int32_t segmentsRendered = 0;

	if (t.TrackID == NO_TRACK)
		return;

	if (!pTrackVertices || !pTrackIndices) return;

	if (GameMode == TRACK_MENU || GameMode == TRACK_PREVIEW)
	{
		// Draw track without road lines
		r.DrawIndexedTriangleList(pTrackVertices, pTrackIndices, 0, trackIndices / 3, {});
	}
	else //	GAME_IN_PROGRESS or GAME_OVER
	{
		// Draw track with road lines
		int32_t lastTexturedSegment;
		constexpr int32_t indicesPerSegment = 6; // 2 triangles × 3 indices

		// 1) Draw left and right sides untextured
		int32_t v = PieceFirstIndex[LEFT_SIDE][0];
		r.DrawIndexedTriangleList(pTrackVertices, pTrackIndices, v, t.NumTrackSegments * 2, {});

		v = PieceFirstIndex[RIGHT_SIDE][0];
		r.DrawIndexedTriangleList(pTrackVertices, pTrackIndices, v, t.NumTrackSegments * 2, {});

		// 2) Draw first part of road untextured
		int32_t firstTexturedSegment = lastTexturedSegment = t.Track[playerCurrentPiece].firstSegment +
			playerCurrentSegment;
		firstTexturedSegment -= TEXTURED_SEGMENTS_AROUND_PLAYER;
		lastTexturedSegment += TEXTURED_SEGMENTS_AROUND_PLAYER;

		v = PieceFirstIndex[ROAD][0]; // first road index
		if (firstTexturedSegment > 0)
		{
			r.DrawIndexedTriangleList(pTrackVertices, pTrackIndices, v, firstTexturedSegment * 2, {});
			segmentsRendered += firstTexturedSegment;
		}


		const int32_t count = lastTexturedSegment - firstTexturedSegment + 1;

		// Limit first and last to track boundaries
		if (firstTexturedSegment < 0)
			firstTexturedSegment += t.NumTrackSegments;

		if (lastTexturedSegment >= t.NumTrackSegments)
			lastTexturedSegment -= t.NumTrackSegments;

		int32_t s = firstTexturedSegment;
		v += s * indicesPerSegment;
		for (int32_t i = 0; i < count; i++, s++, v += indicesPerSegment)
		{
			if (s == t.NumTrackSegments)
			{
				s = 0;
				v = PieceFirstIndex[ROAD][0];
			}

			// Setup texture
			r.DrawIndexedTriangleList(pTrackVertices, pTrackIndices, v, 2, &roadTextures[SegmentRoadTexture[s]]);
			segmentsRendered++;
		}


		if (segmentsRendered < t.NumTrackSegments)
		{
			s = t.NumTrackSegments - segmentsRendered;
			r.DrawIndexedTriangleList(pTrackVertices, pTrackIndices, v, s * 2, {});
		}
	}

	/* Finally draw the opponent's car shadow */
	if (GameMode != TRACK_MENU && numShadowVertices > 0)
	{
		r.DrawTriangleList(pShadowVertices, 0, numShadowVertices / 3, {});
	}
}

// ---------------------------------------------------------------------------
// Draw Bridge animation
// ---------------------------------------------------------------------------

static constexpr int32_t NUM_DRAW_BRIDGE_Y_VALUES = 15;


static int32_t on_draw_bridge_offset = 0;
static int32_t draw_bridge_frame_count = 0;
static int32_t draw_bridge_y_list[NUM_DRAW_BRIDGE_Y_VALUES];


// Opponent's speed values for driving up the Draw Bridge (one value for each height)
static uint8_t TAB5a996[16] = {
	0xd2, 0xbb, 0xb7, 0xb3, 0xb1, 0xad, 0xab, 0xa7, 0xa6, 0xa4, 0xa2, 0xa1, 0x9f, 0x9f, 0x9f, 0x9e
};
// Opponent's speed values for approaching the Draw Bridge
static uint8_t TAB5a9a6[16] = {
	0xf7, 0xf7, 0xf6, 0xf6, 0xf5, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfb, 0xfd, 0xff, 0x02, 0x05, 0xfd
};

extern uint8_t opponents_speed_values[NUM_TRACKS][MAX_PIECES_PER_TRACK];


void MoveDrawBridge(const TrackState& t, const GameState& game)
{
	int32_t f, i, yinc;

	if (t.TrackID != DRAW_BRIDGE)
		return;

	const bool playerOnBridge = game.player_current_piece >= 51 && game.player_current_piece < 56;
	const bool opponentOnBridge = game.opponents_current_piece >= 51 && game.opponents_current_piece < 56;
	const bool opponentApproaching = game.opponents_current_piece >= 48 && game.opponents_current_piece < 51 &&
		on_draw_bridge_offset != 0;

	if (playerOnBridge || opponentOnBridge || opponentApproaching)
	{
		// Player or opponent are on Draw Bridge section, or opponent is approaching it (is on piece 48 to 50)
		// NOTE: draw bridge doesn't move in this case
		on_draw_bridge_offset = 12;
		f = on_draw_bridge_offset + draw_bridge_frame_count;
	}
	else
	{
		// Neither player nor opponent are on Draw Bridge section
		draw_bridge_frame_count++; // draw bridge does move in this case
		on_draw_bridge_offset = 0;

		// get height value between 0 and 15
		int32_t height = (draw_bridge_frame_count & 0x1f) - 0x10;
		if (height < 0) height = abs(height) - 1;

		// Set opponent's required speed values for driving up the Draw Bridge
		opponents_speed_values[DRAW_BRIDGE][51] = TAB5a996[height];
		opponents_speed_values[DRAW_BRIDGE][52] = TAB5a996[height];

		// populate y value array for current height
		int32_t y = yinc = (height + 4) << 5;
		for (i = 0; i < NUM_DRAW_BRIDGE_Y_VALUES; i++)
		{
			draw_bridge_y_list[i] = y;
			y += yinc;
		}

		// update piece 51
		UpdateDrawBridgeYCoords(t, 51, 1, 8, 0, 1);

		// update piece 52
		UpdateDrawBridgeYCoords(t, 52, 0, 7, 7, 1);

		// update piece 54
		UpdateDrawBridgeYCoords(t, 54, 1, 8, NUM_DRAW_BRIDGE_Y_VALUES - 1, -1);

		// update piece 55
		UpdateDrawBridgeYCoords(t, 55, 0, 7, NUM_DRAW_BRIDGE_Y_VALUES - 1 - 7, -1);

		// 29/06/2007 also update pieces in vertex buffer
		UpdatePieceInVB(t, 51);
		UpdatePieceInVB(t, 52);
		UpdatePieceInVB(t, 54);
		UpdatePieceInVB(t, 55);

		if (game.opponents_current_piece != 47)
			return;

		f = draw_bridge_frame_count;
	}

	// Set opponent's required speed values for approaching the Draw Bridge
	const int32_t idx = (f & 0x1f) >> 1;
	int32_t speed = 0xc6; // -'ve to cause double acceleration, to change speed more quickly
	for (i = 0; i < 3; i++)
	{
		speed += TAB5a9a6[idx];
		opponents_speed_values[DRAW_BRIDGE][48 + i] = static_cast<uint8_t>(speed);
	}
}


static void UpdateDrawBridgeYCoords(const TrackState& t, const int32_t piece,
                                    const int32_t firstCoord,
                                    const int32_t lastCoord,
                                    const int32_t firstYIndex,
                                    const int32_t direction) // 1 or -1
{
	int32_t i, j;

	const int32_t leftOverallShift = Left_Overall_Y_Shift[piece];
	const int32_t rightOverallShift = Right_Overall_Y_Shift[piece];

	for (i = firstCoord, j = firstYIndex; i <= lastCoord; i++, j += direction)
	{
		// top left y
		int32_t y = draw_bridge_y_list[j];
		y += leftOverallShift;
		t.Track[piece].coords[(i * 4)].y = y * PC_FACTOR;

		// top right y
		y = draw_bridge_y_list[j];
		y += rightOverallShift;
		t.Track[piece].coords[i * 4 + 1].y = y * PC_FACTOR;
	}
}


void ResetDrawBridge(const TrackState& t, GameState& p)
{
	on_draw_bridge_offset = 0;
	draw_bridge_frame_count = 0;

	// Set car's position to start piece, so that Draw Bridge will move
	p.player_current_piece = p.opponents_current_piece = t.PlayersStartPiece;

	MoveDrawBridge(t, p);
}


// ---------------------------------------------------------------------------
// Track data loading from Amiga resource files
// ---------------------------------------------------------------------------


static int32_t ReadAmigaTrackData(TrackState& t, const int32_t track)
{
	static wchar_t track_resource_names[NUM_TRACKS][32] =
	{
		L"LittleRamp",
		L"SteppingStones",
		L"HumpBack",
		L"BigRamp",
		L"SkiJump",
		L"DrawBridge",
		L"HighJump",
		L"RollerCoaster"
	};
	static char* track_buffer_ptrs[NUM_TRACKS];
	static int32_t first_time = true;

	// read all tracks on first call
	if (first_time)
	{
		first_time = false;

		for (int32_t n = 0; n < NUM_TRACKS; n++)
		{
			const auto buf = static_cast<char*>(PlatformLoadResource(track_resource_names[n], L"TRACK"));
			if (buf == nullptr)
				return false;

			track_buffer_ptrs[n] = buf;
		}
	}

	const auto* buffer = reinterpret_cast<const uint8_t*>(track_buffer_ptrs[track]);

	// transfer track data into final locations
	int32_t i = 0;
	t.NumTrackPieces = buffer[i++];
	t.PlayersStartPiece = buffer[i++];
	t.StartLinePiece = t.PlayersStartPiece;

	t.HalfALapPiece = t.StartLinePiece + t.NumTrackPieces / 2;
	if (t.HalfALapPiece >= t.NumTrackPieces) t.HalfALapPiece -= t.NumTrackPieces;

	for (int32_t j = 0; j < MAX_PIECES_PER_TRACK; i++, j++)
		Piece_X_Z_Position[j] = static_cast<char>(buffer[i]);

	for (int32_t j = 0; j < MAX_PIECES_PER_TRACK; i++, j++)
		Piece_Angle_And_Template[j] = static_cast<char>(buffer[i]);

	for (int32_t j = 0; j < MAX_PIECES_PER_TRACK; i++, j++)
		Left_Y_Coordinate_ID[j] = static_cast<char>(buffer[i]);

	for (int32_t j = 0; j < MAX_PIECES_PER_TRACK; i++, j++)
		Right_Y_Coordinate_ID[j] = static_cast<char>(buffer[i]);

	for (int32_t j = 0; j < MAX_PIECES_PER_TRACK; j++)
	{
		const uint8_t hi = buffer[i++];
		const uint8_t lo = buffer[i++];
		Left_Overall_Y_Shift[j] = static_cast<int16_t>(hi << 8 | lo);
	}

	for (int32_t j = 0; j < MAX_PIECES_PER_TRACK; j++)
	{
		const uint8_t hi = buffer[i++];
		const uint8_t lo = buffer[i++];
		Right_Overall_Y_Shift[j] = static_cast<int16_t>(hi << 8 | lo);
	}

	t.StandardBoost = buffer[i++];
	t.SuperBoost = buffer[i++];
	return true;
}
