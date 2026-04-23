/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : WW3D                                                         *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/surfaceclass.cpp                       $*
 *                                                                                             *
 *              Original Author:: Nathaniel Hoffman                                            *
 *                                                                                             *
 *                      $Author:: Greg_h2                                                     $*
 *                                                                                             *
 *                     $Modtime:: 8/30/01 2:01p                                               $*
 *                                                                                             *
 *                    $Revision:: 25                                                          $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   SurfaceClass::Clear -- Clears a surface to 0                                              *
 *   SurfaceClass::Copy -- Copies a region from one surface to another of the same format      *
 *   SurfaceClass::FindBBAlpha -- Finds the bounding box of non zero pixels in the region (x0, *
 *   SurfaceClass::Is_Transparent_Column -- Tests to see if the column is transparent or not   *
 *   SurfaceClass::Copy -- Copies from a byte array to the surface                             *
 *   SurfaceClass::CreateCopy -- Creates a byte array copy of the surface                      *
 *   SurfaceClass::DrawHLine -- draws a horizontal line                                        *
 *   SurfaceClass::DrawPixel -- draws a pixel                                                  *
 *   SurfaceClass::Copy -- Copies a block of system ram to the surface                         *
 *   SurfaceClass::Hue_Shift -- changes the hue of the surface                                 *
 *   SurfaceClass::Is_Monochrome -- Checks if surface is monochrome or not                     *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "surfaceclass.h"
#ifdef RTS_RENDERER_DX8
#include "formconv.h"
#include "dx8wrapper.h"
#include "vector2i.h"
#include "colorspace.h"
#include "bound.h"
#include <d3dx8.h>

void Convert_Pixel(Vector3 &rgb, const SurfaceClass::SurfaceDescription &sd, const unsigned char * pixel)
{
	const float scale=1/255.0f;
	switch (sd.Format)
	{
	case WW3D_FORMAT_A8R8G8B8:
	case WW3D_FORMAT_X8R8G8B8:
	case WW3D_FORMAT_R8G8B8:
		{
			rgb.X=pixel[2]; // R
			rgb.Y=pixel[1]; // G
			rgb.Z=pixel[0]; // B
		}
		break;
	case WW3D_FORMAT_A4R4G4B4:
		{
			unsigned short tmp;
			tmp=*(unsigned short*)&pixel[0];
			rgb.X=((tmp&0x0f00)>>4);   // R
			rgb.Y=((tmp&0x00f0));		// G
			rgb.Z=((tmp&0x000f)<<4);	// B
		}
		break;
	case WW3D_FORMAT_A1R5G5B5:
		{
			unsigned short tmp;
			tmp=*(unsigned short*)&pixel[0];
			rgb.X=(tmp>>7)&0xf8; // R
			rgb.Y=(tmp>>2)&0xf8; // G
			rgb.Z=(tmp<<3)&0xf8; // B
		}
		break;
	case WW3D_FORMAT_R5G6B5:
		{
			unsigned short tmp;
			tmp=*(unsigned short*)&pixel[0];
			rgb.X=(tmp>>8)&0xf8;
			rgb.Y=(tmp>>3)&0xfc;
			rgb.Z=(tmp<<3)&0xf8;
		}
		break;

	default:
		// TODO: Implement other pixel formats
		WWASSERT(0);
	}
	rgb*=scale;
}

// Note: This function must never overwrite the original alpha
void Convert_Pixel(unsigned char * pixel,const SurfaceClass::SurfaceDescription &sd, const Vector3 &rgb)
{
	unsigned char r,g,b;
	r=(unsigned char) (rgb.X*255.0f);
	g=(unsigned char) (rgb.Y*255.0f);
	b=(unsigned char) (rgb.Z*255.0f);
	switch (sd.Format)
	{
	case WW3D_FORMAT_A8R8G8B8:
	case WW3D_FORMAT_X8R8G8B8:
	case WW3D_FORMAT_R8G8B8:
		pixel[0]=b;
		pixel[1]=g;
		pixel[2]=r;
		break;
	case WW3D_FORMAT_A4R4G4B4:
		{
			unsigned short tmp;
			tmp=*(unsigned short*)&pixel[0];
			tmp&=0xF000;
			tmp|=(r&0xF0) << 4;
			tmp|=(g&0xF0);
			tmp|=(b&0xF0) >> 4;
			*(unsigned short*)&pixel[0]=tmp;
		}
		break;
	case WW3D_FORMAT_A1R5G5B5:
		{
			unsigned short tmp;
			tmp=*(unsigned short*)&pixel[0];
			tmp&=0x8000;
			tmp|=(r&0xF8) << 7;
			tmp|=(g&0xF8) << 2;
			tmp|=(b&0xF8) >> 3;
			*(unsigned short*)&pixel[0]=tmp;
		}
		break;
	case WW3D_FORMAT_R5G6B5:
		{
			unsigned short tmp;
			tmp=(r&0xf8) << 8;
			tmp|=(g&0xfc) << 3;
			tmp|=(b&0xf8) >> 3;
			*(unsigned short*)&pixel[0]=tmp;
		}
		break;
	default:
		// TODO: Implement other pixel formats
		WWASSERT(0);
	}
}

/*************************************************************************
**                             SurfaceClass
*************************************************************************/
SurfaceClass::SurfaceClass(unsigned width, unsigned height, WW3DFormat format):
	D3DSurface(nullptr),
	SurfaceFormat(format)
{
	WWASSERT(width);
	WWASSERT(height);
	D3DSurface = DX8Wrapper::_Create_DX8_Surface(width, height, format);
}

SurfaceClass::SurfaceClass(const char *filename):
	D3DSurface(nullptr)
{
	D3DSurface = DX8Wrapper::_Create_DX8_Surface(filename);
	SurfaceDescription desc;
	Get_Description(desc);
	SurfaceFormat=desc.Format;
}

SurfaceClass::SurfaceClass(IDirect3DSurface8 *d3d_surface)	:
	D3DSurface (nullptr)
{
	Attach (d3d_surface);
	SurfaceDescription desc;
	Get_Description(desc);
	SurfaceFormat=desc.Format;
}

SurfaceClass::~SurfaceClass()
{
	if (D3DSurface) {
		D3DSurface->Release();
		D3DSurface = nullptr;
	}
}

void SurfaceClass::Get_Description(SurfaceDescription &surface_desc)
{
	D3DSURFACE_DESC d3d_desc;
	::ZeroMemory(&d3d_desc, sizeof(D3DSURFACE_DESC));
	DX8_ErrorCode(D3DSurface->GetDesc(&d3d_desc));
	surface_desc.Format = D3DFormat_To_WW3DFormat(d3d_desc.Format);
	surface_desc.Height = d3d_desc.Height;
	surface_desc.Width = d3d_desc.Width;
}

unsigned int SurfaceClass::Get_Bytes_Per_Pixel()
{
	SurfaceDescription surfaceDesc;
	Get_Description(surfaceDesc);
	return ::Get_Bytes_Per_Pixel(surfaceDesc.Format);
}

SurfaceClass::LockedSurfacePtr SurfaceClass::Lock(int *pitch)
{
	D3DLOCKED_RECT lock_rect;
	::ZeroMemory(&lock_rect, sizeof(D3DLOCKED_RECT));
	DX8_ErrorCode(D3DSurface->LockRect(&lock_rect, nullptr, 0));
	*pitch = lock_rect.Pitch;
	return static_cast<LockedSurfacePtr>(lock_rect.pBits);
}

SurfaceClass::LockedSurfacePtr SurfaceClass::Lock(int *pitch, const Vector2i &min, const Vector2i &max)
{
	D3DLOCKED_RECT lock_rect;
	::ZeroMemory(&lock_rect, sizeof(D3DLOCKED_RECT));

	RECT rect;
	rect.left = min.I;
	rect.top = min.J;
	rect.right = max.I;
	rect.bottom = max.J;
	DX8_ErrorCode(D3DSurface->LockRect(&lock_rect, &rect, 0));

	*pitch = lock_rect.Pitch;
	return static_cast<LockedSurfacePtr>(lock_rect.pBits);
}

void SurfaceClass::Unlock()
{
	DX8_ErrorCode(D3DSurface->UnlockRect());
}

/***********************************************************************************************
 * SurfaceClass::Clear -- Clears a surface to 0                                                *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/13/2001  hy : Created.                                                                  *
 *=============================================================================================*/
void SurfaceClass::Clear()
{
	SurfaceDescription sd;
	Get_Description(sd);

	// size of each pixel in bytes
	unsigned int size=::Get_Bytes_Per_Pixel(sd.Format);

	D3DLOCKED_RECT lock_rect;
	::ZeroMemory(&lock_rect, sizeof(D3DLOCKED_RECT));
	DX8_ErrorCode(D3DSurface->LockRect(&lock_rect,nullptr,0));
	unsigned int i;
	unsigned char *mem=(unsigned char *) lock_rect.pBits;

	for (i=0; i<sd.Height; i++)
	{
		memset(mem,0,size*sd.Width);
		mem+=lock_rect.Pitch;
	}

	DX8_ErrorCode(D3DSurface->UnlockRect());
}


/***********************************************************************************************
 * SurfaceClass::Copy -- Copies from a byte array to the surface                               *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/15/2001  hy : Created.                                                                  *
 *=============================================================================================*/
void SurfaceClass::Copy(const unsigned char *other)
{
	SurfaceDescription sd;
	Get_Description(sd);

	// size of each pixel in bytes
	unsigned int size=::Get_Bytes_Per_Pixel(sd.Format);

	D3DLOCKED_RECT lock_rect;
	::ZeroMemory(&lock_rect, sizeof(D3DLOCKED_RECT));
	DX8_ErrorCode(D3DSurface->LockRect(&lock_rect,nullptr,0));
	unsigned int i;
	unsigned char *mem=(unsigned char *) lock_rect.pBits;

	for (i=0; i<sd.Height; i++)
	{
		memcpy(mem,&other[i*sd.Width*size],size*sd.Width);
		mem+=lock_rect.Pitch;
	}

	DX8_ErrorCode(D3DSurface->UnlockRect());
}


/***********************************************************************************************
 * SurfaceClass::Copy -- Copies a block of system ram to the surface                           *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   5/2/2001   hy : Created.                                                                  *
 *=============================================================================================*/
void SurfaceClass::Copy(const Vector2i &min, const Vector2i &max, const unsigned char *other)
{
	SurfaceDescription sd;
	Get_Description(sd);

	// size of each pixel in bytes
	unsigned int size=::Get_Bytes_Per_Pixel(sd.Format);

	D3DLOCKED_RECT lock_rect;
	::ZeroMemory(&lock_rect, sizeof(D3DLOCKED_RECT));
	RECT rect;
	rect.left=min.I;
	rect.right=max.I;
	rect.top=min.J;
	rect.bottom=max.J;
	DX8_ErrorCode(D3DSurface->LockRect(&lock_rect,&rect,0));
	int i;
	unsigned char *mem=(unsigned char *) lock_rect.pBits;
	int dx=max.I-min.I;

	for (i=min.J; i<max.J; i++)
	{
		memcpy(mem,&other[(i*sd.Width+min.I)*size],size*dx);
		mem+=lock_rect.Pitch;
	}

	DX8_ErrorCode(D3DSurface->UnlockRect());
}


/***********************************************************************************************
 * SurfaceClass::CreateCopy -- Creates a byte array copy of the surface                        *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/16/2001  hy : Created.                                                                  *
 *=============================================================================================*/
unsigned char *SurfaceClass::CreateCopy(int *width,int *height,int*size,bool flip)
{
	SurfaceDescription sd;
	Get_Description(sd);

	// size of each pixel in bytes
	unsigned int mysize=::Get_Bytes_Per_Pixel(sd.Format);

	*width=sd.Width;
	*height=sd.Height;
	*size=mysize;

	unsigned char *other=W3DNEWARRAY unsigned char [sd.Height*sd.Width*mysize];

	D3DLOCKED_RECT lock_rect;
	::ZeroMemory(&lock_rect, sizeof(D3DLOCKED_RECT));
	DX8_ErrorCode(D3DSurface->LockRect(&lock_rect,nullptr,D3DLOCK_READONLY));
	unsigned int i;
	unsigned char *mem=(unsigned char *) lock_rect.pBits;

	for (i=0; i<sd.Height; i++)
	{
		if (flip)
		{
			memcpy(&other[(sd.Height-i-1)*sd.Width*mysize],mem,mysize*sd.Width);
		} else
		{
			memcpy(&other[i*sd.Width*mysize],mem,mysize*sd.Width);
		}
		mem+=lock_rect.Pitch;
	}

	DX8_ErrorCode(D3DSurface->UnlockRect());

	return other;
}


/***********************************************************************************************
 * SurfaceClass::Copy -- Copies a region from one surface to another                           *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/13/2001  hy : Created.                                                                  *
 *=============================================================================================*/
void SurfaceClass::Copy(
	unsigned int dstx, unsigned int dsty,
	unsigned int srcx, unsigned int srcy,
	unsigned int width, unsigned int height,
	const SurfaceClass *other)
{
	WWASSERT(other);
	WWASSERT(width);
	WWASSERT(height);

	SurfaceDescription sd,osd;
	Get_Description(sd);
	const_cast <SurfaceClass*>(other)->Get_Description(osd);

	RECT src;
	src.left=srcx;
	src.right=srcx+width;
	src.top=srcy;
	src.bottom=srcy+height;

	if (src.right>int(osd.Width)) src.right=int(osd.Width);
	if (src.bottom>int(osd.Height)) src.bottom=int(osd.Height);

	if (sd.Format==osd.Format && sd.Width==osd.Width && sd.Height==osd.Height)
	{
		POINT dst;
		dst.x=dstx;
		dst.y=dsty;
		DX8Wrapper::_Copy_DX8_Rects(other->D3DSurface,&src,1,D3DSurface,&dst);
	}
	else
	{
		RECT dest;
		dest.left=dstx;
		dest.right=dstx+width;
		dest.top=dsty;
		dest.bottom=dsty+height;

		if (dest.right>int(sd.Width)) dest.right=int(sd.Width);
		if (dest.bottom>int(sd.Height)) dest.bottom=int(sd.Height);

		DX8_ErrorCode(D3DXLoadSurfaceFromSurface(D3DSurface,nullptr,&dest,other->D3DSurface,nullptr,&src,D3DX_FILTER_NONE,0));
	}
}

/***********************************************************************************************
 * SurfaceClass::Copy -- Copies a region from one surface to another                           *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/13/2001  hy : Created.                                                                  *
 *=============================================================================================*/
void SurfaceClass::Stretch_Copy(
	unsigned int dstx, unsigned int dsty, unsigned int dstwidth, unsigned int dstheight,
	unsigned int srcx, unsigned int srcy, unsigned int srcwidth, unsigned int srcheight,
	const SurfaceClass *other)
{
	WWASSERT(other);

	SurfaceDescription sd,osd;
	Get_Description(sd);
	const_cast <SurfaceClass*>(other)->Get_Description(osd);

	RECT src;
	src.left=srcx;
	src.right=srcx+srcwidth;
	src.top=srcy;
	src.bottom=srcy+srcheight;

	RECT dest;
	dest.left=dstx;
	dest.right=dstx+dstwidth;
	dest.top=dsty;
	dest.bottom=dsty+dstheight;

	DX8_ErrorCode(D3DXLoadSurfaceFromSurface(D3DSurface,nullptr,&dest,other->D3DSurface,nullptr,&src,D3DX_FILTER_TRIANGLE ,0));
}

/***********************************************************************************************
 * SurfaceClass::FindBB -- Finds the bounding box of non zero pixels in the region             *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/13/2001  hy : Created.                                                                  *
 *=============================================================================================*/
void SurfaceClass::FindBB(Vector2i *min,Vector2i*max)
{
	SurfaceDescription sd;
	Get_Description(sd);

	WWASSERT(Has_Alpha(sd.Format));

	int alphabits=Alpha_Bits(sd.Format);
	int mask=0;
	switch (alphabits)
	{
	case 1: mask=1;
		break;
	case 4: mask=0xf;
		break;
	case 8: mask=0xff;
		break;
	}

	D3DLOCKED_RECT lock_rect;
	::ZeroMemory(&lock_rect, sizeof(D3DLOCKED_RECT));
	RECT rect;
	::ZeroMemory(&rect, sizeof(RECT));

	rect.bottom=max->J;
	rect.top=min->J;
	rect.left=min->I;
	rect.right=max->I;

	DX8_ErrorCode(D3DSurface->LockRect(&lock_rect,&rect,D3DLOCK_READONLY));

	int x,y;
	unsigned int size=::Get_Bytes_Per_Pixel(sd.Format);
	Vector2i realmin=*max;
	Vector2i realmax=*min;

	// the assumption here is that whenever a pixel has alpha it's in the MSB
	for (y = min->J; y < max->J; y++) {
		for (x = min->I; x < max->I; x++) {

			// HY - this is not endian safe
			unsigned char *alpha=(unsigned char*) ((unsigned int)lock_rect.pBits+(y-min->J)*lock_rect.Pitch+(x-min->I)*size);
			unsigned char myalpha=alpha[size-1];
			myalpha=(myalpha>>(8-alphabits)) & mask;
			if (myalpha) {
				realmin.I = MIN(realmin.I, x);
				realmax.I = MAX(realmax.I, x);
				realmin.J = MIN(realmin.J, y);
				realmax.J = MAX(realmax.J, y);
			}
		}
	}

	DX8_ErrorCode(D3DSurface->UnlockRect());

	*max=realmax;
	*min=realmin;
}


/***********************************************************************************************
 * SurfaceClass::Is_Transparent_Column -- Tests to see if the column is transparent or not     *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/13/2001  hy : Created.                                                                  *
 *=============================================================================================*/
bool SurfaceClass::Is_Transparent_Column(unsigned int column)
{
	SurfaceDescription sd;
	Get_Description(sd);

	WWASSERT(column<sd.Width);
	WWASSERT(Has_Alpha(sd.Format));

	int alphabits=Alpha_Bits(sd.Format);
	int mask=0;
	switch (alphabits)
	{
	case 1: mask=1;
		break;
	case 4: mask=0xf;
		break;
	case 8: mask=0xff;
		break;
	}

	unsigned int size=::Get_Bytes_Per_Pixel(sd.Format);

	D3DLOCKED_RECT lock_rect;
	::ZeroMemory(&lock_rect, sizeof(D3DLOCKED_RECT));
	RECT rect;
	::ZeroMemory(&rect, sizeof(RECT));

	rect.bottom=sd.Height;
	rect.top=0;
	rect.left=column;
	rect.right=column+1;

	DX8_ErrorCode(D3DSurface->LockRect(&lock_rect,&rect,D3DLOCK_READONLY));

	int y;

	// the assumption here is that whenever a pixel has alpha it's in the MSB
	for (y = 0; y < (int) sd.Height; y++)
	{
		// HY - this is not endian safe
		unsigned char *alpha=(unsigned char*) ((unsigned int)lock_rect.pBits+y*lock_rect.Pitch);
		unsigned char myalpha=alpha[size-1];
		myalpha=(myalpha>>(8-alphabits)) & mask;
		if (myalpha) {
			DX8_ErrorCode(D3DSurface->UnlockRect());
			return false;
		}
	}

	DX8_ErrorCode(D3DSurface->UnlockRect());
	return true;
}

/***********************************************************************************************
 * SurfaceClass::Get_Pixel -- Returns the pixel's RGB valus to the caller                      *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/13/2001  hy : Created.                                                                  *
 *   1/10/2025  TheSuperHackers : Added bits and pitch to argument list for better performance *
 *=============================================================================================*/
void SurfaceClass::Get_Pixel(Vector3 &rgb, int x, int y, LockedSurfacePtr pBits, int pitch)
{
	SurfaceDescription sd;
	Get_Description(sd);

	unsigned int bytesPerPixel = ::Get_Bytes_Per_Pixel(sd.Format);
	unsigned char* dst = static_cast<unsigned char *>(pBits) + y * pitch + x * bytesPerPixel;
	Convert_Pixel(rgb,sd,dst);
}

/***********************************************************************************************
 * SurfaceClass::Attach -- Attaches a surface pointer to the object, releasing the current ptr.*
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/27/2001  pds : Created.                                                                 *
 *=============================================================================================*/
void SurfaceClass::Attach (IDirect3DSurface8 *surface)
{
	Detach ();
	D3DSurface = surface;

	//
	//	Lock a reference onto the object
	//
	if (D3DSurface != nullptr) {
		D3DSurface->AddRef ();
	}

	return ;
}


/***********************************************************************************************
 * SurfaceClass::Detach -- Releases the reference on the internal surface ptr, and NULLs it.	 .*
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/27/2001  pds : Created.                                                                 *
 *=============================================================================================*/
void SurfaceClass::Detach ()
{
	//
	//	Release the hold we have on the D3D object
	//
	if (D3DSurface != nullptr) {
		D3DSurface->Release ();
	}

	D3DSurface = nullptr;
	return ;
}


/***********************************************************************************************
 * SurfaceClass::DrawPixel -- draws a pixel                                                    *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   1/10/2025  TheSuperHackers : Added bits and pitch to argument list for better performance *
 *=============================================================================================*/
void SurfaceClass::Draw_Pixel(const unsigned int x, const unsigned int y, unsigned int color,
	unsigned int bytesPerPixel, LockedSurfacePtr pBits, int pitch)
{
	unsigned char* dst = static_cast<unsigned char*>(pBits) + y * pitch + x * bytesPerPixel;
	memcpy(dst, &color, bytesPerPixel);
}



/***********************************************************************************************
 * SurfaceClass::DrawHLine -- draws a horizontal line                                          *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   4/9/2001   hy : Created.                                                                  *
 *   1/10/2025  TheSuperHackers : Added bits and pitch to argument list for better performance *
 *=============================================================================================*/
void SurfaceClass::Draw_H_Line(const unsigned int y, const unsigned int x1, const unsigned int x2,
	unsigned int color, unsigned int bytesPerPixel, LockedSurfacePtr pBits, int pitch)
{
	unsigned char* row = static_cast<unsigned char*>(pBits) + y * pitch;

	for (unsigned int x = x1; x <= x2; ++x)
	{
		unsigned char* dst = row + x * bytesPerPixel;
		memcpy(dst, &color, bytesPerPixel);
	}
}


/***********************************************************************************************
 * SurfaceClass::Is_Monochrome -- Checks if surface is monochrome or not                       *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   7/5/2001   hy : Created.                                                                  *
 *=============================================================================================*/
bool SurfaceClass::Is_Monochrome()
{
	unsigned int x,y;
	SurfaceDescription sd;
	Get_Description(sd);
	bool is_compressed = false;

	switch (sd.Format)
	{
		// these formats are always monochrome
		case WW3D_FORMAT_A8L8:
		case WW3D_FORMAT_A8:
		case WW3D_FORMAT_L8:
		case WW3D_FORMAT_A4L4:
			return true;
		break;
		// these formats cannot be determined to be monochrome or not
		case WW3D_FORMAT_UNKNOWN:
		case WW3D_FORMAT_A8P8:
		case WW3D_FORMAT_P8:
		case WW3D_FORMAT_U8V8:		// Bumpmap
		case WW3D_FORMAT_L6V5U5:	// Bumpmap
		case WW3D_FORMAT_X8L8V8U8:	// Bumpmap
			return false;
		break;
		// these formats need decompression first
		case WW3D_FORMAT_DXT1:
		case WW3D_FORMAT_DXT2:
		case WW3D_FORMAT_DXT3:
		case WW3D_FORMAT_DXT4:
		case WW3D_FORMAT_DXT5:
			is_compressed = true;
		break;
	}

	// if it's in some compressed texture format, be sure to decompress first
	if (is_compressed) {
		WW3DFormat new_format = Get_Valid_Texture_Format(sd.Format, false);
		SurfaceClass *new_surf = NEW_REF( SurfaceClass, (sd.Width, sd.Height, new_format) );
		new_surf->Copy(0, 0, 0, 0, sd.Width, sd.Height, this);
		bool result = new_surf->Is_Monochrome();
		REF_PTR_RELEASE(new_surf);
		return result;
	}

	int pitch,size;

	size=::Get_Bytes_Per_Pixel(sd.Format);
	unsigned char *bits=(unsigned char*) Lock(&pitch);

	Vector3 rgb;
	bool mono=true;

	for (y=0; y<sd.Height; y++)
	{
		for (x=0; x<sd.Width; x++)
		{
			Convert_Pixel(rgb,sd,&bits[x*size]);
			mono&=(rgb.X==rgb.Y);
			mono&=(rgb.X==rgb.Z);
			mono&=(rgb.Z==rgb.Y);
			if (!mono)
			{
				Unlock();
				return false;
			}
		}
		bits+=pitch;
	}

	Unlock();

	return true;
}

/***********************************************************************************************
 * SurfaceClass::Hue_Shift -- changes the hue of the surface                                   *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   7/3/2001   hy : Created.                                                                  *
 *=============================================================================================*/
void SurfaceClass::Hue_Shift(const Vector3 &hsv_shift)
{
	unsigned int x,y;
	SurfaceDescription sd;
	Get_Description(sd);
	int pitch,size;

	size=::Get_Bytes_Per_Pixel(sd.Format);
	unsigned char *bits=(unsigned char*) Lock(&pitch);

	Vector3 rgb;

	for (y=0; y<sd.Height; y++)
	{
		for (x=0; x<sd.Width; x++)
		{
			Convert_Pixel(rgb,sd,&bits[x*size]);
			Recolor(rgb,hsv_shift);
			rgb.X=Bound(rgb.X,0.0f,1.0f);
			rgb.Y=Bound(rgb.Y,0.0f,1.0f);
			rgb.Z=Bound(rgb.Z,0.0f,1.0f);
			Convert_Pixel(&bits[x*size],sd,rgb);
		}
		bits+=pitch;
	}

	Unlock();
}
#else // RTS_RENDERER_DX8

// =============================================================================
// Phase 5h.31 — bgfx-mode SurfaceClass
// =============================================================================
//
// A bgfx-mode surface is a CPU-side pixel buffer that optionally mirrors to a
// bgfx texture on Unlock(). There is no D3DSurface; D3DSurface stays nullptr
// so Peek_D3D_Surface()/Attach/Detach behave as no-ops. The CPU buffer is the
// sole source of truth. Callers that want the contents to reach the GPU call
// Set_Associated_Texture(handle) on a backend-allocated texture; Unlock()
// then routes the buffer through IRenderBackend::Update_Texture_RGBA8.
//
// The DX8 path (above) is deep — D3DX blits, LockRect, surface-to-surface
// copies — and is not reproduced here. The bgfx branch covers the operations
// that have actual bgfx-mode callers today (Lock/Unlock, Clear, byte-array
// copies, CreateCopy, Get_Description) plus the format-agnostic helpers that
// operate on a caller-supplied pointer (Draw_Pixel, Draw_H_Line, Get_Pixel).
// More involved paths — Stretch_Copy via D3DX, Hue_Shift, Is_Monochrome,
// FindBB, Is_Transparent_Column, surface-to-surface Copy — are provided as
// CPU implementations where the work is straightforward, or as explicit
// no-ops with a one-time log where a real D3DX dependency is implicit.

#include "ww3dformat.h"
#include "vector3.h"
#include "vector2i.h"
#include "IRenderBackend.h"
#include "RenderBackendRuntime.h"
#include <cstring>
#include <cstdio>

namespace {

// Local copy of Get_Bytes_Per_Pixel to keep this TU free of the ww3dformat.cpp
// dependency chain (which pulls dx8wrapper.h transitively). Identical table
// to ww3dformat.cpp::Get_Bytes_Per_Pixel; update in lockstep if formats gain
// new entries.
unsigned Surface_BPP(WW3DFormat format)
{
	switch (format) {
	case WW3D_FORMAT_A8R8G8B8:
	case WW3D_FORMAT_X8R8G8B8:
	case WW3D_FORMAT_X8L8V8U8:
		return 4;
	case WW3D_FORMAT_R8G8B8:
		return 3;
	case WW3D_FORMAT_R5G6B5:
	case WW3D_FORMAT_X1R5G5B5:
	case WW3D_FORMAT_A1R5G5B5:
	case WW3D_FORMAT_A4R4G4B4:
	case WW3D_FORMAT_A8R3G3B2:
	case WW3D_FORMAT_X4R4G4B4:
	case WW3D_FORMAT_A8P8:
	case WW3D_FORMAT_A8L8:
	case WW3D_FORMAT_U8V8:
	case WW3D_FORMAT_L6V5U5:
		return 2;
	case WW3D_FORMAT_R3G3B2:
	case WW3D_FORMAT_A8:
	case WW3D_FORMAT_P8:
	case WW3D_FORMAT_L8:
	case WW3D_FORMAT_A4L4:
		return 1;
	default: break;
	}
	return 0;
}

// Single-shot warn helper so missing bgfx-mode paths don't spam logs but stay
// visible during bring-up.
void Surface_Warn_Once(const char* tag)
{
	static const char* seen[8] = {};
	for (const char* s : seen) if (s == tag) return;
	for (auto& slot : seen) if (slot == nullptr) { slot = tag; break; }
	std::fprintf(stderr, "[SurfaceClass:bgfx] %s is a stub — no bgfx callers expected yet\n", tag);
}

// Route the CPU buffer into a bgfx texture if an association exists. Only
// RGBA8-ish 32-bit formats are uploaded; smaller/compressed formats are
// Phase 5h.35 — convert one source pixel of `fmt` (at `sp`) into RGBA8 at
// `dp`. Returns false for unsupported formats; caller falls back to skipping
// the upload.
//
// Source layouts (DX8 memory order):
//   A8R8G8B8   : B G R A   (4 bytes, little-endian 0xAARRGGBB dword)
//   X8R8G8B8   : B G R X   (4 bytes, alpha forced to 0xFF)
//   A4R4G4B4   : 2 bytes LE — bits [15..12]=A [11..8]=R [7..4]=G [3..0]=B
//   X4R4G4B4   : 2 bytes LE — alpha forced to 0xFF
//   R5G6B5     : 2 bytes LE — bits [15..11]=R [10..5]=G [4..0]=B
//   A1R5G5B5   : 2 bytes LE — bit 15=A, [14..10]=R [9..5]=G [4..0]=B
//   X1R5G5B5   : alpha forced to 0xFF
//   A8         : 1 byte — luminance forced white, alpha = byte
//   L8         : 1 byte — luminance replicated, alpha = 0xFF
//   A8L8       : 2 bytes — [0]=L, [1]=A
//   A4L4       : 1 byte — [7..4]=A, [3..0]=L (each expanded to 8 bits)
bool ConvertPixelToRGBA8(WW3DFormat fmt, const unsigned char* sp, unsigned char* dp)
{
	switch (fmt)
	{
	case WW3D_FORMAT_A8R8G8B8:
		dp[0] = sp[2]; dp[1] = sp[1]; dp[2] = sp[0]; dp[3] = sp[3];
		return true;

	case WW3D_FORMAT_X8R8G8B8:
		dp[0] = sp[2]; dp[1] = sp[1]; dp[2] = sp[0]; dp[3] = 0xFF;
		return true;

	case WW3D_FORMAT_R8G8B8:
		dp[0] = sp[2]; dp[1] = sp[1]; dp[2] = sp[0]; dp[3] = 0xFF;
		return true;

	case WW3D_FORMAT_A4R4G4B4:
	case WW3D_FORMAT_X4R4G4B4:
	{
		const uint16_t v = uint16_t(sp[0]) | (uint16_t(sp[1]) << 8);
		const uint8_t a4 = (v >> 12) & 0xF;
		const uint8_t r4 = (v >> 8)  & 0xF;
		const uint8_t g4 = (v >> 4)  & 0xF;
		const uint8_t b4 =  v        & 0xF;
		// 4-bit → 8-bit expansion: (x << 4) | x replicates the nibble so 0xF → 0xFF.
		dp[0] = (r4 << 4) | r4;
		dp[1] = (g4 << 4) | g4;
		dp[2] = (b4 << 4) | b4;
		dp[3] = (fmt == WW3D_FORMAT_X4R4G4B4) ? uint8_t(0xFF) : uint8_t((a4 << 4) | a4);
		return true;
	}

	case WW3D_FORMAT_R5G6B5:
	{
		const uint16_t v = uint16_t(sp[0]) | (uint16_t(sp[1]) << 8);
		const uint8_t r5 = (v >> 11) & 0x1F;
		const uint8_t g6 = (v >> 5)  & 0x3F;
		const uint8_t b5 =  v        & 0x1F;
		// 5-bit → 8-bit: (x << 3) | (x >> 2). 6-bit → 8-bit: (x << 2) | (x >> 4).
		dp[0] = (r5 << 3) | (r5 >> 2);
		dp[1] = (g6 << 2) | (g6 >> 4);
		dp[2] = (b5 << 3) | (b5 >> 2);
		dp[3] = 0xFF;
		return true;
	}

	case WW3D_FORMAT_A1R5G5B5:
	case WW3D_FORMAT_X1R5G5B5:
	{
		const uint16_t v = uint16_t(sp[0]) | (uint16_t(sp[1]) << 8);
		const uint8_t a1 = (v >> 15) & 0x1;
		const uint8_t r5 = (v >> 10) & 0x1F;
		const uint8_t g5 = (v >> 5)  & 0x1F;
		const uint8_t b5 =  v        & 0x1F;
		dp[0] = (r5 << 3) | (r5 >> 2);
		dp[1] = (g5 << 3) | (g5 >> 2);
		dp[2] = (b5 << 3) | (b5 >> 2);
		dp[3] = (fmt == WW3D_FORMAT_X1R5G5B5) ? uint8_t(0xFF) : uint8_t(a1 ? 0xFF : 0x00);
		return true;
	}

	case WW3D_FORMAT_A8:
		dp[0] = 0xFF; dp[1] = 0xFF; dp[2] = 0xFF; dp[3] = sp[0];
		return true;

	case WW3D_FORMAT_L8:
		dp[0] = sp[0]; dp[1] = sp[0]; dp[2] = sp[0]; dp[3] = 0xFF;
		return true;

	case WW3D_FORMAT_A8L8:
		dp[0] = sp[0]; dp[1] = sp[0]; dp[2] = sp[0]; dp[3] = sp[1];
		return true;

	case WW3D_FORMAT_A4L4:
	{
		const uint8_t a4 = (sp[0] >> 4) & 0xF;
		const uint8_t l4 =  sp[0]       & 0xF;
		const uint8_t l8 = (l4 << 4) | l4;
		dp[0] = l8; dp[1] = l8; dp[2] = l8;
		dp[3] = (a4 << 4) | a4;
		return true;
	}

	default:
		return false;
	}
}

// Bridges the CPU-side surface pixels into `IRenderBackend::Update_Texture_RGBA8`.
// All formats handled by `ConvertPixelToRGBA8` are supported; everything else
// (DXT, bumpmap, palette, R8G8B8 24-bit) is skipped with a one-shot warning.
void Upload_To_Associated_Texture(uintptr_t handle, WW3DFormat fmt,
	const unsigned char* src, unsigned w, unsigned h, unsigned pitch)
{
	if (handle == 0 || src == nullptr || w == 0 || h == 0) return;
	IRenderBackend* backend = RenderBackendRuntime::Get_Active();
	if (backend == nullptr) return;

	const unsigned bpp = Surface_BPP(fmt);
	if (bpp == 0)
	{
		Surface_Warn_Once("Upload_To_Associated_Texture: zero-bpp format (compressed/palette?)");
		return;
	}

	const unsigned byteSize = w * h * 4u;
	unsigned char* rgba = new unsigned char[byteSize];
	bool fmtSupported = true;

	for (unsigned y = 0; y < h && fmtSupported; ++y)
	{
		const unsigned char* row = src + y * pitch;
		unsigned char* drow = rgba + y * w * 4u;
		for (unsigned x = 0; x < w; ++x)
		{
			if (!ConvertPixelToRGBA8(fmt, row + x * bpp, drow + x * 4))
			{
				fmtSupported = false;
				break;
			}
		}
	}

	if (fmtSupported)
	{
		backend->Update_Texture_RGBA8(handle, rgba,
		                              static_cast<uint16_t>(w),
		                              static_cast<uint16_t>(h));
	}
	else
	{
		Surface_Warn_Once("Upload_To_Associated_Texture: unsupported format, skipped");
	}
	delete[] rgba;
}

} // namespace

SurfaceClass::SurfaceClass(unsigned width, unsigned height, WW3DFormat format) :
	D3DSurface(nullptr),
	SurfaceFormat(format)
{
	WWASSERT(width);
	WWASSERT(height);
	CpuWidth = width;
	CpuHeight = height;
	const unsigned bpp = Surface_BPP(format);
	CpuPitch = width * (bpp ? bpp : 1);
	CpuPixels = new unsigned char[CpuPitch * CpuHeight];
	std::memset(CpuPixels, 0, CpuPitch * CpuHeight);
}

SurfaceClass::SurfaceClass(const char* /*filename*/) :
	D3DSurface(nullptr),
	SurfaceFormat(WW3D_FORMAT_A8R8G8B8)
{
	// File-loaded surfaces in bgfx mode would need a bimg-backed loader.
	// Deferred to a future phase; for now allocate a 1x1 opaque-black surface
	// so ref-count/attach semantics stay sane if a caller hits this path.
	CpuWidth = 1;
	CpuHeight = 1;
	CpuPitch = 4;
	CpuPixels = new unsigned char[4];
	CpuPixels[0] = 0; CpuPixels[1] = 0; CpuPixels[2] = 0; CpuPixels[3] = 0xFF;
	Surface_Warn_Once("SurfaceClass(const char*)");
}

SurfaceClass::SurfaceClass(IDirect3DSurface8* /*d3d_surface*/) :
	D3DSurface(nullptr),
	SurfaceFormat(WW3D_FORMAT_UNKNOWN)
{
	// bgfx has no IDirect3DSurface8 analogue; caller-supplied surfaces are
	// not meaningful here. Leave CpuPixels null; any Lock() returns nullptr.
}

SurfaceClass::~SurfaceClass()
{
	delete[] CpuPixels;
	CpuPixels = nullptr;
}

void SurfaceClass::Get_Description(SurfaceDescription& surface_desc)
{
	surface_desc.Format = SurfaceFormat;
	surface_desc.Width  = CpuWidth;
	surface_desc.Height = CpuHeight;
}

unsigned int SurfaceClass::Get_Bytes_Per_Pixel()
{
	return Surface_BPP(SurfaceFormat);
}

SurfaceClass::LockedSurfacePtr SurfaceClass::Lock(int* pitch)
{
	if (pitch) *pitch = static_cast<int>(CpuPitch);
	return static_cast<LockedSurfacePtr>(CpuPixels);
}

SurfaceClass::LockedSurfacePtr SurfaceClass::Lock(int* pitch, const Vector2i& min, const Vector2i& /*max*/)
{
	if (pitch) *pitch = static_cast<int>(CpuPitch);
	if (CpuPixels == nullptr) return nullptr;
	const unsigned bpp = Surface_BPP(SurfaceFormat);
	const unsigned offset = static_cast<unsigned>(min.J) * CpuPitch
	                      + static_cast<unsigned>(min.I) * bpp;
	return static_cast<LockedSurfacePtr>(CpuPixels + offset);
}

void SurfaceClass::Unlock()
{
	Upload_To_Associated_Texture(AssociatedTextureHandle, SurfaceFormat,
	                             CpuPixels, CpuWidth, CpuHeight, CpuPitch);
}

void SurfaceClass::Clear()
{
	if (CpuPixels) std::memset(CpuPixels, 0, CpuPitch * CpuHeight);
	Upload_To_Associated_Texture(AssociatedTextureHandle, SurfaceFormat,
	                             CpuPixels, CpuWidth, CpuHeight, CpuPitch);
}

void SurfaceClass::Copy(const unsigned char* other)
{
	if (!CpuPixels || !other) return;
	const unsigned bpp = Surface_BPP(SurfaceFormat);
	const unsigned rowBytes = CpuWidth * bpp;
	for (unsigned y = 0; y < CpuHeight; ++y) {
		std::memcpy(CpuPixels + y * CpuPitch, other + y * rowBytes, rowBytes);
	}
	Upload_To_Associated_Texture(AssociatedTextureHandle, SurfaceFormat,
	                             CpuPixels, CpuWidth, CpuHeight, CpuPitch);
}

void SurfaceClass::Copy(const Vector2i& min, const Vector2i& max, const unsigned char* other)
{
	if (!CpuPixels || !other) return;
	const unsigned bpp = Surface_BPP(SurfaceFormat);
	const int dx = max.I - min.I;
	if (dx <= 0) return;
	for (int y = min.J; y < max.J; ++y) {
		unsigned char* dst = CpuPixels + y * CpuPitch + min.I * bpp;
		const unsigned char* srcRow = other + (y * CpuWidth + min.I) * bpp;
		std::memcpy(dst, srcRow, dx * bpp);
	}
	Upload_To_Associated_Texture(AssociatedTextureHandle, SurfaceFormat,
	                             CpuPixels, CpuWidth, CpuHeight, CpuPitch);
}

void SurfaceClass::Copy(
	unsigned int dstx, unsigned int dsty,
	unsigned int srcx, unsigned int srcy,
	unsigned int width, unsigned int height,
	const SurfaceClass* other)
{
	if (!other || !CpuPixels || !other->CpuPixels) return;
	if (SurfaceFormat != other->SurfaceFormat) {
		Surface_Warn_Once("Copy(surface->surface) format mismatch");
		return;
	}
	const unsigned bpp = Surface_BPP(SurfaceFormat);
	for (unsigned j = 0; j < height; ++j) {
		unsigned char* d = CpuPixels + (dsty + j) * CpuPitch + dstx * bpp;
		const unsigned char* s = other->CpuPixels + (srcy + j) * other->CpuPitch + srcx * bpp;
		std::memcpy(d, s, width * bpp);
	}
	Upload_To_Associated_Texture(AssociatedTextureHandle, SurfaceFormat,
	                             CpuPixels, CpuWidth, CpuHeight, CpuPitch);
}

void SurfaceClass::Stretch_Copy(
	unsigned int /*dstx*/, unsigned int /*dsty*/,
	unsigned int /*dstwidth*/, unsigned int /*dstheight*/,
	unsigned int /*srcx*/, unsigned int /*srcy*/,
	unsigned int /*srcwidth*/, unsigned int /*srcheight*/,
	const SurfaceClass* /*other*/)
{
	// DX8 path uses D3DXLoadSurfaceFromSurface for filtered resizing. No
	// bgfx-mode caller today; defer CPU-side resampling until one shows up.
	Surface_Warn_Once("Stretch_Copy");
}

unsigned char* SurfaceClass::CreateCopy(int* width, int* height, int* size, bool flip)
{
	const unsigned bpp = Surface_BPP(SurfaceFormat);
	if (width)  *width  = static_cast<int>(CpuWidth);
	if (height) *height = static_cast<int>(CpuHeight);
	if (size)   *size   = static_cast<int>(bpp);
	const unsigned rowBytes = CpuWidth * bpp;
	unsigned char* out = W3DNEWARRAY unsigned char[CpuHeight * rowBytes];
	if (CpuPixels == nullptr) {
		std::memset(out, 0, CpuHeight * rowBytes);
		return out;
	}
	for (unsigned y = 0; y < CpuHeight; ++y) {
		const unsigned char* srcRow = CpuPixels + (flip ? (CpuHeight - 1 - y) : y) * CpuPitch;
		std::memcpy(out + y * rowBytes, srcRow, rowBytes);
	}
	return out;
}

void SurfaceClass::FindBB(Vector2i* min, Vector2i* max)
{
	// CPU-side alpha scan. Preserves DX8 semantics but only handles the
	// BGRA8-alike paths in bgfx mode; other formats return the caller's
	// original bbox unchanged.
	if (!CpuPixels || !min || !max) return;
	if (SurfaceFormat != WW3D_FORMAT_A8R8G8B8) {
		Surface_Warn_Once("FindBB format other than A8R8G8B8");
		return;
	}
	Vector2i realmin = *max;
	Vector2i realmax = *min;
	for (int y = min->J; y < max->J; ++y) {
		for (int x = min->I; x < max->I; ++x) {
			const unsigned char a = CpuPixels[y * CpuPitch + x * 4 + 3];
			if (a) {
				if (x < realmin.I) realmin.I = x;
				if (x > realmax.I) realmax.I = x;
				if (y < realmin.J) realmin.J = y;
				if (y > realmax.J) realmax.J = y;
			}
		}
	}
	*min = realmin;
	*max = realmax;
}

bool SurfaceClass::Is_Transparent_Column(unsigned int column)
{
	if (!CpuPixels || column >= CpuWidth) return true;
	if (SurfaceFormat != WW3D_FORMAT_A8R8G8B8) {
		Surface_Warn_Once("Is_Transparent_Column format other than A8R8G8B8");
		return false;
	}
	for (unsigned y = 0; y < CpuHeight; ++y) {
		if (CpuPixels[y * CpuPitch + column * 4 + 3] != 0) return false;
	}
	return true;
}

void SurfaceClass::Get_Pixel(Vector3& rgb, int x, int y, LockedSurfacePtr pBits, int pitch)
{
	// Format-agnostic on the caller side — operates on the pointer/pitch the
	// caller has already locked. The DX8 branch uses the same Convert_Pixel
	// helper; provide a minimal BGRA8 decode here since Convert_Pixel itself
	// lives in the DX8-only part of this file.
	const unsigned char* p = static_cast<const unsigned char*>(pBits) + y * pitch + x * 4;
	if (SurfaceFormat == WW3D_FORMAT_A8R8G8B8 || SurfaceFormat == WW3D_FORMAT_X8R8G8B8) {
		rgb.X = p[2] * (1.0f / 255.0f);
		rgb.Y = p[1] * (1.0f / 255.0f);
		rgb.Z = p[0] * (1.0f / 255.0f);
	} else {
		Surface_Warn_Once("Get_Pixel non-BGRA8 format");
		rgb.X = rgb.Y = rgb.Z = 0.0f;
	}
}

void SurfaceClass::Attach(IDirect3DSurface8* /*surface*/)
{
	// bgfx has no IDirect3DSurface8; nothing to reference.
}

void SurfaceClass::Detach()
{
	// Symmetric no-op with Attach.
	D3DSurface = nullptr;
}

void SurfaceClass::Draw_Pixel(const unsigned int x, const unsigned int y, unsigned int color,
	unsigned int bytesPerPixel, LockedSurfacePtr pBits, int pitch)
{
	unsigned char* dst = static_cast<unsigned char*>(pBits) + y * pitch + x * bytesPerPixel;
	std::memcpy(dst, &color, bytesPerPixel);
}

void SurfaceClass::Draw_H_Line(const unsigned int y, const unsigned int x1, const unsigned int x2,
	unsigned int color, unsigned int bytesPerPixel, LockedSurfacePtr pBits, int pitch)
{
	unsigned char* row = static_cast<unsigned char*>(pBits) + y * pitch;
	for (unsigned int x = x1; x <= x2; ++x) {
		std::memcpy(row + x * bytesPerPixel, &color, bytesPerPixel);
	}
}

bool SurfaceClass::Is_Monochrome()
{
	// CPU scan for BGRA8 only; other formats conservatively report false.
	if (SurfaceFormat != WW3D_FORMAT_A8R8G8B8 && SurfaceFormat != WW3D_FORMAT_X8R8G8B8) {
		Surface_Warn_Once("Is_Monochrome non-BGRA8 format");
		return false;
	}
	if (!CpuPixels) return true;
	for (unsigned y = 0; y < CpuHeight; ++y) {
		const unsigned char* row = CpuPixels + y * CpuPitch;
		for (unsigned x = 0; x < CpuWidth; ++x) {
			const unsigned char b = row[x*4 + 0];
			const unsigned char g = row[x*4 + 1];
			const unsigned char r = row[x*4 + 2];
			if (r != g || g != b) return false;
		}
	}
	return true;
}

void SurfaceClass::Hue_Shift(const Vector3& /*hsv_shift*/)
{
	// Requires the full Convert_Pixel/Recolor plumbing above — that code is
	// DX8-branch-local today. No bgfx caller yet; defer until one exists.
	Surface_Warn_Once("Hue_Shift");
}

#endif // RTS_RENDERER_DX8
