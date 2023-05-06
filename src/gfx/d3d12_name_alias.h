#pragma once
struct IDXGIFactory7;
struct IDXGIAdapter4;
struct ID3D12Device10;
using DxgiFactory = IDXGIFactory7;
using DxgiAdapter = IDXGIAdapter4;
using D3d12Device = ID3D12Device10;
using D3d12Fence = ID3D12Fence1;
using D3d12CommandAllocator = ID3D12CommandAllocator;
using D3d12CommandList = ID3D12GraphicsCommandList7;
