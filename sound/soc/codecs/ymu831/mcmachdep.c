/****************************************************************************
 *
 *	Copyright(c) 2012-2013 Yamaha Corporation. All rights reserved.
 *
 *	Module		: mcmachdep.c
 *
 *	Description	: machine dependent part for MC Driver
 *
 *	Version		: 2.0.0	2013.03.06
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.	In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *	claim that you wrote the original software. If you use this software
 *	in a product, an acknowledgment in the product documentation would be
 *	appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *	misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 ****************************************************************************/

#include <linux/delay.h>
#include <linux/mutex.h>
#include "ymu831_priv.h"

#include "mcmachdep.h"

static struct mutex McDrv_Mutex;

/****************************************************************************
 *	machdep_SystemInit
 *
 *	Description:
 *			Initialize the system.
 *	Arguments:
 *			none
 *	Return:
 *			none
 *
 ****************************************************************************/
void	machdep_SystemInit(
	void
)
{

	/* Please implement system initialization procedure if need */
	mutex_init(&McDrv_Mutex);

}

/****************************************************************************
 *	machdep_SystemTerm
 *
 *	Description:
 *			Terminate the system.
 *	Arguments:
 *			none
 *	Return:
 *			none
 *
 ****************************************************************************/
void	machdep_SystemTerm(
	void
)
{

	/* Please implement system termination procedure if need */
	mutex_destroy(&McDrv_Mutex);

}

/****************************************************************************
 *	machdep_ClockStart
 *
 *	Description:
 *			Start clock.
 *	Arguments:
 *			none
 *	Return:
 *			none
 *
 ****************************************************************************/
void	machdep_ClockStart(
	void
)
{

	/* Please implement clock start procedure if need */
	mc_asoc_enable_clock(1);

}

/****************************************************************************
 *	machdep_ClockStop
 *
 *	Description:
 *			Stop clock.
 *	Arguments:
 *			none
 *	Return:
 *			none
 *
 ****************************************************************************/
void	machdep_ClockStop(
	void
)
{

	/* Please implement clock stop procedure if need */
	mc_asoc_enable_clock(0);

}

/***************************************************************************
 *	machdep_WriteReg
 *
 *	Function:
 *			Write data to the register.
 *	Arguments:
 *			bSlaveAdr	slave address
 *			pbData		byte data for write
 *			dSize		byte data length
 *	Return:
 *			None
 *
 ****************************************************************************/
void	machdep_WriteReg(
	UINT8	bSlaveAdr,
	const UINT8	*pbData,
	UINT32	dSize
)
{

	/* Please implement register write procedure */
	mc_asoc_write_data(bSlaveAdr, pbData, dSize);

}

/***************************************************************************
 *	machdep_ReadReg
 *
 *	Function:
 *			Read a byte data from the register.
 *	Arguments:
 *			bSlaveAdr	slave address
 *			dAddress	address of register
 *			pbData		pointer to read data buffer
 *			dSize		read count
 *	Return:
 *			none
 *
 ****************************************************************************/
void	machdep_ReadReg(
	UINT8	bSlaveAdr,
	UINT32	dAddress,
	UINT8	*pbData,
	UINT32	dSize
)
{

	/* Please implement register read procedure */
	mc_asoc_read_data(bSlaveAdr, dAddress, pbData, dSize);

}

/****************************************************************************
 *	machdep_Sleep
 *
 *	Function:
 *			Sleep for a specified interval.
 *	Arguments:
 *			dSleepTime	sleep time [us]
 *	Return:
 *			None
 *
 ****************************************************************************/
void	machdep_Sleep(
	UINT32	dSleepTime
)
{
	unsigned long ms = dSleepTime / 1000;
	unsigned long us = dSleepTime % 1000;


	/* Please implement sleep procedure */
	if (us)
		udelay(us);
	if (ms)
		msleep(ms);

}

/***************************************************************************
 *	machdep_Lock
 *
 *	Function:
 *			Lock a call of the driver.
 *	Arguments:
 *			none
 *	Return:
 *			none
 *
 ****************************************************************************/
void	machdep_Lock(
	void
)
{

	/* Please implement lock procedure */
	mutex_lock(&McDrv_Mutex);

}

/***************************************************************************
 *	machdep_Unlock
 *
 *	Function:
 *			Unlock a call of the driver.
 *	Arguments:
 *			none
 *	Return:
 *			none
 *
 ****************************************************************************/
void	machdep_Unlock(
	void
)
{

	/* Please implement unlock procedure */
	mutex_unlock(&McDrv_Mutex);

}

/***************************************************************************
 *	machdep_PreLDODStart
 *
 *	Function:
 *			.
 *	Arguments:
 *			none
 *	Return:
 *			none
 *
 ****************************************************************************/
void	machdep_PreLDODStart(
	void
)
{

	/* Please implement procedure */
	mc_asoc_set_codec_ldod(1);
	if (mc_asoc_get_bus_select() == 1) {
		UINT8	bData[2];
		bData[0]	= 0x04;
		bData[1]	= 0x01;
		mc_asoc_write_data(0, bData, 2);
	}

}

/***************************************************************************
 *	machdep_PostLDODStart
 *
 *	Function:
 *			.
 *	Arguments:
 *			none
 *	Return:
 *			none
 *
 ****************************************************************************/
void	machdep_PostLDODStart(
	void
)
{

	/* Please implement procedure */
	mc_asoc_set_codec_ldod(0);

}

/***************************************************************************
 *	machdep_GetBusSelect
 *
 *	Function:
 *			Get bus kind.
 *	Arguments:
 *			none
 *	Return:
 *			0:I2C
 *			1:SPI
 *			2:SLIMbus
 *
 ****************************************************************************/
UINT8	machdep_GetBusSelect(
	void
)
{
	/* Please implement get bus kind procedure */
	return mc_asoc_get_bus_select();
}

