/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __LVB_TABLE_DOT_H__
#define __LVB_TABLE_DOT_H__

/*
 * This defines the direction of transfer of LVB data.
 * Granted mode is the row; requested mode is the column.
 * Usage: matrix[grmode+1][rqmode+1]
 * 1 = LVB is returned to the caller
 * 0 = LVB is written to the resource
 * -1 = nothing happens to the LVB
 */

const int dlm_lvb_operations[8][8] = {
        /* UN   NL  CR  CW  PR  PW  EX  PD*/
        {  -1,  1,  1,  1,  1,  1,  1, -1 }, /* UN */
        {  -1,  1,  1,  1,  1,  1,  1,  0 }, /* NL */
        {  -1, -1,  1,  1,  1,  1,  1,  0 }, /* CR */
        {  -1, -1, -1,  1,  1,  1,  1,  0 }, /* CW */
        {  -1, -1, -1, -1,  1,  1,  1,  0 }, /* PR */
        {  -1,  0,  0,  0,  0,  0,  1,  0 }, /* PW */
        {  -1,  0,  0,  0,  0,  0,  0,  0 }, /* EX */
        {  -1,  0,  0,  0,  0,  0,  0,  0 }  /* PD */
};

#endif

