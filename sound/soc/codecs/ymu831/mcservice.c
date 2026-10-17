/****************************************************************************
 *
 *	Copyright(c) 2012 Yamaha Corporation. All rights reserved.
 *
 *	Module		: mcservice.c
 *
 *	Description	: MC Driver service routine
 *
 *	Version		: 1.0.0	2012.12.13
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


#include "mcdefs.h"
#include "mcservice.h"
#include "mcmachdep.h"
#include "mcresctrl.h"
#include "mcdevprof.h"


/****************************************************************************
 *	McSrv_SystemInit
 *
 *	Description:
 *			Initialize the system.
 *	Arguments:
 *			none
 *	Return:
 *			none
 *
 ****************************************************************************/
void	McSrv_SystemInit(
	void
)
{

	machdep_SystemInit();

}

/****************************************************************************
 *	McSrv_SystemTerm
 *
 *	Description:
 *			Terminate the system.
 *	Arguments:
 *			none
 *	Return:
 *			none
 *
 ****************************************************************************/
void	McSrv_SystemTerm(
	void
)
{

	machdep_SystemTerm();

}

/****************************************************************************
 *	McSrv_ClockStart
 *
 *	Description:
 *			Start clock.
 *	Arguments:
 *			none
 *	Return:
 *			none
 *
 ****************************************************************************/
void	McSrv_ClockStart(
	void
)
{

	machdep_ClockStart();

}

/****************************************************************************
 *	McSrv_ClockStop
 *
 *	Description:
 *			Stop clock.
 *	Arguments:
 *			none
 *	Return:
 *			none
 *
 ****************************************************************************/
void	McSrv_ClockStop(
	void
)
{

	machdep_ClockStop();

}

/****************************************************************************
 *	McSrv_WriteReg
 *
 *	Description:
 *			Write data to register.
 *	Arguments:
 *			bSlaveAddr	slave address
 *			pbData		data
 *			dSize		data size
 *	Return:
 *			none
 *
 ****************************************************************************/
void	McSrv_WriteReg(
	UINT8	bSlaveAddr,
	UINT8	*pbData,
	UINT32	dSize
)
{

	McSrv_DisableIrq();
	machdep_WriteReg(bSlaveAddr, pbData, dSize);
	McSrv_EnableIrq();

}

/****************************************************************************
 *	McSrv_ReadReg
 *
 *	Function:
 *			Read a byte data from the register.
 *	Arguments:
 *			bSlaveAddr	slave address
 *			dRegAddr	address of register
 *	Return:
 *			read data
 *
 ****************************************************************************/
UINT8	McSrv_ReadReg(
	UINT8	bSlaveAddr,
	UINT32	dRegAddr
)
{
	UINT8	bReg;


	McSrv_DisableIrq();
	machdep_ReadReg(bSlaveAddr, dRegAddr, &bReg, 1);
	McSrv_EnableIrq();


	return bReg;
}

/****************************************************************************
 *	McSrv_ReadRegN
 *
 *	Function:
 *			Read a byte data from the register.
 *	Arguments:
 *			bSlaveAddr	slave address
 *			dRegAddr	address of register
 *			pbData		pointer to read data buffer
 *			dSize		read count
 *	Return:
 *			none
 *
 ****************************************************************************/
void	McSrv_ReadRegN(
	UINT8	bSlaveAddr,
	UINT32	dRegAddr,
	UINT8	*pbData,
	UINT32	dSize
)
{
	McSrv_DisableIrq();
	machdep_ReadReg(bSlaveAddr, dRegAddr, pbData, dSize);
	McSrv_EnableIrq();

}

/***************************************************************************
 *	McSrv_Sleep
 *
 *	Function:
 *			Sleep for a specified interval.
 *	Arguments:
 *			dSleepTime	sleep time [us]
 *	Return:
 *			none
 *
 ****************************************************************************/
void	McSrv_Sleep(
	UINT32	dSleepTime
)
{

	machdep_Sleep(dSleepTime);

}

/****************************************************************************
 *	McSrv_Lock
 *
 *	Description:
 *			Lock a call of the driver.
 *	Arguments:
 *			none
 *	Return:
 *			none
 *
 ****************************************************************************/
void	McSrv_Lock(
	void
)
{

	machdep_Lock();

}

/***************************************************************************
 *	McSrv_Unlock
 *
 *	Function:
 *			Unlock a call of the driver.
 *	Arguments:
 *			none
 *	Return:
 *			none
 *
 ****************************************************************************/
void	McSrv_Unlock(
	void
)
{

	machdep_Unlock();

}

/***************************************************************************
 *	McSrv_MemCopy
 *
 *	Function:
 *			Copy memory.
 *	Arguments:
 *			pbSrc	copy source
 *			pbDest	copy destination
 *			dSize	size
 *	Return:
 *			none
 *
 ****************************************************************************/
void	McSrv_MemCopy(
	const UINT8	*pbSrc,
	UINT8		*pbDest,
	UINT32		dSize
)
{
	UINT32	i;


	for (i = 0; i < dSize; i++) {
		;
		pbDest[i] = pbSrc[i];
	}

}

/***************************************************************************
 *	McSrv_DisableIrq
 *
 *	Function:
 *			Disable interrupt.
 *	Arguments:
 *			none
 *	Return:
 *			none
 *
 ****************************************************************************/
void	McSrv_DisableIrq(
	void
)
{

}

/***************************************************************************
 *	McSrv_EnableIrq
 *
 *	Function:
 *			Enable interrupt.
 *	Arguments:
 *			none
 *	Return:
 *			none
 *
 ****************************************************************************/
void	McSrv_EnableIrq(
	void
)
{

}

