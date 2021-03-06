/*
 * CRC.c
 *
 *  Created on: 04.12.2017
 *      Author: LH
 *
 *  This is a generic implementation for a CRC8 generator supporting
 *  both compile-time (1) and run-time initialised Lookup tables for efficient CRC8 calculation.
 *  You can store multiple tables for different polynomials and (non-)reflected CRCs
 *  The different tables are referenced by an index, with an upper limit set at compile time (CRC_TABLE_COUNT).
 *
 *  To generate CRCs you must first generate the Lookup-table by calling fillCRCTable()
 *  with any index. CRCs can then be generated from any data buffer by calling CRC()
 *  with the same index previously given to fillCRCTable().
 *
 *  The table generation has been optimised for speed so that the runtime
 *  table generation can even be done during normal operation if required.
 *  However, as long as the required polynomials are known on initialisation,
 *  the table generation should be done at that time.
 *  On the Landungsbruecke the intialisation of a CRC table takes ~250�s. (2)
 *  Should your application still have problems with the table calculation time,
 *  this algorithm could probably be sped up by preparing a 2- or 4-bit lookup table
 *  to speed up the actual table generation.
 *
 *  (1): For compile-time CRC tables, just fill the table(s) by initialising CRCTables[] to the proper values.
 *  (2): Tested by toggling a GPIO pin, generating a table inbetween and measuring the GPIO pulse width.
 */

#include "CRC.h"

typedef struct {
	uint8 table[256];
	uint8 polynomial;
	bool isReflected;
} CRCTypeDef;

CRCTypeDef CRCTables[CRC_TABLE_COUNT] = { 0 };

static uint8 flipByte(uint8 value);
static uint32 flipBitsInBytes(uint32 value);

/* This function generates the Lookup table used for CRC calculations.
 * Arguments:
 *     uint8 polynomial: The CRC polynomial for which the table will be generated
 *     bool isReflected: Indicator whether the CRC table will be reflected or not.
 *     uint8 index: The index of the table to be filled
 *
 * How it works:
 *     A CRC calculation of a byte can be done by taking the byte to be CRC'd,
 *     shifting it left by one (appending a 0) and - if a 1 has been shifted out -
 *     XOR-ing in the CRC polynomial. After 8 iterations the result will be the
 *     CRC of the Byte.
 *
 *     The function below does this in a compact way, by using all 4 bytes of a
 *     uint32 to do 4 seperate CRC bytes at once.
 *     For this to work without the Byte shifting interfering with adjacent bytes,
 *     the polynomial has the 8th Byte (0x100) set. That way, if the shifted-out bit
 *     is 1, the following XOR-ing with the CRC polynomial will set that 1 to a 0,
 *     resulting in the shifted-in 0 for the adjacent byte.
 *     This process will go from the the lowest to the highest byte, resulting in
 *     fully independent byte-wise CRC calculations. For the highest byte, the value
 *     of the shifted-out byte needs to be stored before shifting the bytes (isMSBSet).
 *
 *     The for-loop that iterates over all uint8 values starts out with the
 *     uint8 values 3 to 0 stored in one uint32: 0x03020100
 *     for each iteration each uint8 value will increase by 4..
 *     0 -> 4 -> 8 -> C -> ...
 *     1 -> 5 -> 9 -> D -> ...
 *     2 -> 6 -> A -> E -> ...
 *     3 -> 7 -> B -> F -> ...
 *     ..resulting in an increase of the uint32 by 0x04040404:
 *     0x03020100 -> 0x07060504 -> 0x0B0A0908 -> 0x0F0E0D0C -> ...
 *     The loop ends as soon as we have iterated over all uint8 values.
 *     We detect that by looking for the byte-wise overflow into the next byte:
 *     0xFFFEFDFC                  <- last uint32 value to be calculated
 *     0xFF,  0xFE,  0xFD,  0xFC   <- the corresponding uint8 values
 *     0x103, 0x102, 0x101, 0x100  <- incremented uint8 values (overflow into the next byte!)
 *     0x04030200                  <- uint32 value with the overflowed bytes
 *
 *     We have the lower uint8 values at the lower bytes of the uint32.
 *     This allows us to simply store the lowest byte of the uint32,
 *     right-shift the uint32 by 8 and increment the table pointer.
 *     After 4 iterations of that all 4 bytes of the uint32 are stored in the table.
 */
uint8 fillCRC8Table(uint8 polynomial, bool isReflected, uint8 index)
{
	uint32 CRCdata;
	// Helper pointer for traversing the result table
	uint8 *table;
	uint32 poly = 0;
	uint8 isMSBSet = 0;
	uint32 i,j;

	if(index >= CRC_TABLE_COUNT)
		return 0;

	CRCTables[index].polynomial   = polynomial;
	CRCTables[index].isReflected  = isReflected;
	table = &CRCTables[index].table[0];

	// Extend the polynomial to correct byte MSBs shifting into next bytes
	poly = (uint32) polynomial | 0x0100;

	// Iterate over all 256 possible uint8 values, compressed into a uint32 (see detailed explanation above)
	for(i = 0x03020100; i != 0x04030200; i+=0x04040404)
	{
		// For reflected table: Flip the bits of each input byte
		CRCdata = (isReflected)? flipBitsInBytes(i) : i;

		// Iterate over 8 Bits
		for(j = 0; j < 8; j++)
		{
			// Store value of soon-to-be shifted out byte
			isMSBSet = (CRCdata & 0x80000000)? 1:0;

			// CRC Shift
			CRCdata <<= 1;

			// XOR the bytes when required, lowest to highest
			CRCdata ^= (CRCdata & 0x00000100)? (poly      ) : 0;
			CRCdata ^= (CRCdata & 0x00010000)? (poly << 8 ) : 0;
			CRCdata ^= (CRCdata & 0x01000000)? (poly << 16) : 0;
			CRCdata ^= (isMSBSet)?             (poly << 24) : 0;
		}

		// For reflected table: Flip the bits of each output byte
		CRCdata = (isReflected)? flipBitsInBytes(CRCdata) : CRCdata;
		// Store the CRC result bytes in the table array
		*table++ = (uint8) CRCdata;
		CRCdata >>= 8;
		*table++ = (uint8) CRCdata;
		CRCdata >>= 8;
		*table++ = (uint8) CRCdata;
		CRCdata >>= 8;
		*table++ = (uint8) CRCdata;
	}

	return 1;
}

/* This function calculates the CRC from a data buffer
 * Arguments:
 *     uint8 *data: A pointer to the data that will be CRC'd
 *     uint32 bytes: The length of the data buffer
 *     uint8 index: The index of the CRC table to be used
 */
uint8 CRC8(uint8 *data, uint32 bytes, uint8 index)
{
	uint8 result = 0;
	uint8 *table;

	if(index >= CRC_TABLE_COUNT)
		return 0;

	table = &CRCTables[index].table[0];

	while(bytes--)
		result = table[result ^ *data++];

	return (CRCTables[index].isReflected)? flipByte(result) : result;
}

uint8 tableGetPolynomial(uint8 index)
{
	if(index >= CRC_TABLE_COUNT)
		return 0;

	return CRCTables[index].polynomial;
}

bool tableIsReflected(uint8 index)
{
	if(index >= CRC_TABLE_COUNT)
		return FALSE;

	return CRCTables[index].isReflected;
}

// Helper functions
static uint8 flipByte(uint8 value)
{
	// swap odd and even bits
	value = ((value >> 1) & 0x55) | ((value & 0x55) << 1);
	// swap consecutive pairs
	value = ((value >> 2) & 0x33) | ((value & 0x33) << 2);
	// swap nibbles ...
	value = ((value >> 4) & 0x0F) | ((value & 0x0F) << 4);

	return value;
}

/* This helper function switches all bits within each byte.
 * The byte order remains the same:
 * [b31 b30 b29 b28 b27 b26 b25 b24 .. b7 b6 b5 b4 b3 b2 b1 b0]
 *                                  ||
 *                                 \||/
 *                                  \/
 * [b24 b25 b26 b27 b28 b29 b30 b31 .. b0 b1 b2 b3 b4 b5 b6 b7]
 */
static uint32 flipBitsInBytes(uint32 value)
{
	// swap odd and even bits
	value = ((value >> 1) & 0x55555555) | ((value & 0x55555555) << 1);
	// swap consecutive pairs
	value = ((value >> 2) & 0x33333333) | ((value & 0x33333333) << 2);
	// swap nibbles ...
	value = ((value >> 4) & 0x0F0F0F0F) | ((value & 0x0F0F0F0F) << 4);

	return value;
}
