// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Screenshot.h"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::ComPtr;

namespace
{
	bool SavePng(const FString& Path, int32 Width, int32 Height, const TArray<uint8>& Bgra)
	{
		ComPtr<IWICImagingFactory> Factory;
		if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Factory))))
		{
			return false;
		}
		ComPtr<IWICStream> Stream;
		ComPtr<IWICBitmapEncoder> Encoder;
		ComPtr<IWICBitmapFrameEncode> Frame;
		if (FAILED(Factory->CreateStream(&Stream))
			|| FAILED(Stream->InitializeFromFilename(Path.ToWide().c_str(), GENERIC_WRITE))
			|| FAILED(Factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &Encoder))
			|| FAILED(Encoder->Initialize(Stream.Get(), WICBitmapEncoderNoCache))
			|| FAILED(Encoder->CreateNewFrame(&Frame, nullptr))
			|| FAILED(Frame->Initialize(nullptr))
			|| FAILED(Frame->SetSize(static_cast<UINT>(Width), static_cast<UINT>(Height))))
		{
			return false;
		}
		// PNG has no 32bpp BGRX format; write 24bpp BGR (and check that the
		// encoder accepted it rather than substituting another format).
		WICPixelFormatGUID Format = GUID_WICPixelFormat24bppBGR;
		if (FAILED(Frame->SetPixelFormat(&Format)) || Format != GUID_WICPixelFormat24bppBGR)
		{
			return false;
		}
		const UINT Stride = (static_cast<UINT>(Width) * 3 + 3) & ~3u;
		TArray<uint8> Bgr(SIZE_T(Stride) * Height);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				memcpy(&Bgr[SIZE_T(Y) * Stride + SIZE_T(X) * 3], &Bgra[(SIZE_T(Y) * Width + X) * 4], 3);
			}
		}
		if (FAILED(Frame->WritePixels(static_cast<UINT>(Height), Stride, static_cast<UINT>(Bgr.size()), Bgr.data()))
			|| FAILED(Frame->Commit()) || FAILED(Encoder->Commit()))
		{
			return false;
		}
		return true;
	}
}

bool CaptureWindowClientToPng(void* NativeHandle, const FString& Path)
{
	const HWND Hwnd = static_cast<HWND>(NativeHandle);
	RECT Client{};
	GetClientRect(Hwnd, &Client);
	const int32 Width = Client.right - Client.left;
	const int32 Height = Client.bottom - Client.top;
	if (Width <= 0 || Height <= 0)
	{
		return false;
	}

	// Copy what is on screen: the composed result of the swapchain, exactly
	// as the user sees it. (PrintWindow cannot reliably read flip-model
	// swapchains.) The window must be visible and on top.
	POINT Origin{ 0, 0 };
	ClientToScreen(Hwnd, &Origin);
	const HDC ScreenDC = GetDC(nullptr);
	const HDC MemoryDC = CreateCompatibleDC(ScreenDC);
	const HBITMAP Bitmap = CreateCompatibleBitmap(ScreenDC, Width, Height);
	const HGDIOBJ Old = SelectObject(MemoryDC, Bitmap);
	bool bOk = BitBlt(MemoryDC, 0, 0, Width, Height, ScreenDC, Origin.x, Origin.y, SRCCOPY | CAPTUREBLT) != FALSE;

	BITMAPINFO Info{};
	Info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	Info.bmiHeader.biWidth = Width;
	Info.bmiHeader.biHeight = -Height; // top-down
	Info.bmiHeader.biPlanes = 1;
	Info.bmiHeader.biBitCount = 32;
	Info.bmiHeader.biCompression = BI_RGB;
	TArray<uint8> Pixels(SIZE_T(Width) * Height * 4);
	SelectObject(MemoryDC, Old);
	bOk = bOk && GetDIBits(MemoryDC, Bitmap, 0, static_cast<UINT>(Height), Pixels.data(), &Info, DIB_RGB_COLORS) == Height;

	DeleteObject(Bitmap);
	DeleteDC(MemoryDC);
	ReleaseDC(nullptr, ScreenDC);
	return bOk && SavePng(Path, Width, Height, Pixels);
}
