/*
  Copyright Red Hat, Inc. 2002-2003, 2006

  The Red Hat Cluster Manager API Library is free software; you can
  redistribute it and/or modify it under the terms of the GNU Lesser
  General Public License as published by the Free Software Foundation;
  either version 2.1 of the License, or (at your option) any later
  version.

  The Red Hat Cluster Manager API Library is distributed in the hope
  that it will be useful, but WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
  PURPOSE.  See the GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
 */
/** @file
 * Bitmap and membership mask handling routines.
 */
#include <stdint.h>


/**
 * Clear a bit in a bitmap / bitmask.
 *
 * @param mask		Bitmask to modify.
 * @param bitidx	Bit to modify.
 * @param masklen	Bitmask length (in uint8_t units)
 * @return		-1 if the index exceeds the number of bits in the
 *			bitmap, otherwise 0.
 */
int
clear_bit(uint8_t *mask, uint32_t bitidx, uint32_t masklen)
{
	uint32_t idx;
	uint32_t bit;

	/* Index into array */
	idx = bitidx >> 3;
	bit = 1 << (bitidx & 0x7);

	if (idx >= masklen)
		return -1;

	mask[idx] &= ~bit;

	return 0;
}


/**
 * Set a bit in a bitmap / bitmask.
 *
 * @param mask		Bitmask to modify.
 * @param bitidx	Bit to modify.
 * @param masklen	Bitmask length (in uint8_t units).
 * @return		-1 if the index exceeds the number of bits in the
 *			bitmap, otherwise 0.
 */
int
set_bit(uint8_t *mask, uint32_t bitidx, uint32_t masklen)
{
	uint32_t idx;
	uint32_t bit;

	/* Index into array */
	idx = bitidx >> 3;
	bit = 1 << (bitidx & 0x7);

	if (idx >= masklen)
		return -1;

	mask[idx] |= bit;

	return 0;
}


/**
 * Check the status of a bit in a bitmap / bitmask.
 *
 * @param mask		Bitmask to check.
 * @param bitidx	Bit to to check.
 * @param masklen	Bitmask length (in uint8_t units).
 * @return		-1 if the index exceeds the number of bits in the
 *			bitmap, 0 if not set, or 1 if set.
 */
int
is_bit_set(uint8_t *mask, uint32_t bitidx, uint32_t masklen)
{
	uint32_t idx;
	uint32_t bit;

	/* Index into array */
	idx = bitidx >> 3;
	bit = 1 << (bitidx & 0x7);

	if (idx >= masklen)
		return -1;

	return !!(mask[idx]&bit);
}


