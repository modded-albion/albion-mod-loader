/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "moa_mod/overlay/OverlayRenderer.h"

#include <cstdint>

#include <array>
#include <vector>

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_dx12.h>
#include <windows.h>
#include <wrl/client.h>

namespace moa_mod::overlay
{

using Microsoft::WRL::ComPtr;

namespace
{
[[nodiscard]] uint32_t Code(HRESULT result)
{
	return static_cast<uint32_t>(result);
}

/// Draws with Direct3D 11 on the immediate context of the game, on the render thread of the game.
class D3D11Renderer final: public OverlayRenderer
{
public:
	[[nodiscard]] static moa::Result<std::unique_ptr<OverlayRenderer>> Create(ComPtr<ID3D11Device> device)
	{
		auto renderer = std::unique_ptr<D3D11Renderer>(new D3D11Renderer());
		renderer->_device = std::move(device);
		renderer->_device->GetImmediateContext(&renderer->_context);
		if (!renderer->_context || !ImGui_ImplDX11_Init(renderer->_device.Get(), renderer->_context.Get()))
		{
			return moa::MakeError("the Direct3D 11 backend of ImGui did not start");
		}
		renderer->_started = true;
		return std::unique_ptr<OverlayRenderer>(std::move(renderer));
	}

	~D3D11Renderer() override
	{
		_target.Reset();
		if (_started)
		{
			ImGui_ImplDX11_Shutdown();
		}
	}

	D3D11Renderer(const D3D11Renderer&) = delete;
	D3D11Renderer& operator=(const D3D11Renderer&) = delete;

	[[nodiscard]] const char* Api() const override { return "Direct3D 11"; }

	void NewFrame() override { ImGui_ImplDX11_NewFrame(); }

	RenderResult Render(IDXGISwapChain* swapChain, ImDrawData* drawData) override
	{
		if (!_target)
		{
			ComPtr<ID3D11Texture2D> buffer;
			if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&buffer))) ||
			    FAILED(_device->CreateRenderTargetView(buffer.Get(), nullptr, &_target)))
			{
				return RenderResult::Failed;
			}
		}
		// Keep the targets of the game, and set them again after the menu bar. The backend keeps the other states.
		ComPtr<ID3D11RenderTargetView> gameTarget;
		ComPtr<ID3D11DepthStencilView> gameDepth;
		_context->OMGetRenderTargets(1, &gameTarget, &gameDepth);
		ID3D11RenderTargetView* targets[] = {_target.Get()};
		_context->OMSetRenderTargets(1, targets, nullptr);
		ImGui_ImplDX11_RenderDrawData(drawData);
		ID3D11RenderTargetView* restored[] = {gameTarget.Get()};
		_context->OMSetRenderTargets(1, restored, gameDepth.Get());
		return RenderResult::Drawn;
	}

	void ReleaseBackBuffers() override { _target.Reset(); }

private:
	D3D11Renderer() = default;

	ComPtr<ID3D11Device> _device;
	ComPtr<ID3D11DeviceContext> _context;
	ComPtr<ID3D11RenderTargetView> _target;
	bool _started = false;
};

/// Closes a Windows handle.
struct HandleCloser
{
	void operator()(HANDLE handle) const { CloseHandle(handle); }
};
using EventHandle = std::unique_ptr<void, HandleCloser>;

/// Draws with Direct3D 12. The menu bar records its own command list and puts it on the command queue of the swap
/// chain, after the commands of the game and before `Present`.
class D3D12Renderer final: public OverlayRenderer
{
public:
	[[nodiscard]] static moa::Result<std::unique_ptr<OverlayRenderer>> Create(ComPtr<ID3D12CommandQueue> queue,
	                                                                          IDXGISwapChain* swapChain)
	{
		auto renderer = std::unique_ptr<D3D12Renderer>(new D3D12Renderer());
		if (auto started = renderer->Start(std::move(queue), swapChain); !started)
		{
			return started.GetError();
		}
		return std::unique_ptr<OverlayRenderer>(std::move(renderer));
	}

	~D3D12Renderer() override
	{
		WaitForGpu();
		_frames.clear();
		if (_started)
		{
			ImGui_ImplDX12_Shutdown();
		}
	}

	D3D12Renderer(const D3D12Renderer&) = delete;
	D3D12Renderer& operator=(const D3D12Renderer&) = delete;

	[[nodiscard]] const char* Api() const override { return "Direct3D 12"; }

	void NewFrame() override { ImGui_ImplDX12_NewFrame(); }

	RenderResult Render(IDXGISwapChain* swapChain, ImDrawData* drawData) override
	{
		ComPtr<IDXGISwapChain3> swapChain3;
		DXGI_SWAP_CHAIN_DESC1 description {};
		if (FAILED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain3))) || FAILED(swapChain3->GetDesc1(&description)))
		{
			return RenderResult::Failed;
		}
		if (description.BufferCount != _frames.size() || description.Format != _format)
		{
			return RenderResult::Rebuild;
		}
		const auto index = swapChain3->GetCurrentBackBufferIndex();
		if (index >= _frames.size())
		{
			return RenderResult::Rebuild;
		}
		auto& frame = _frames[index];
		if (!WaitForFence(frame.fenceValue))
		{
			return RenderResult::Skipped;
		}
		if (!frame.buffer)
		{
			if (FAILED(swapChain->GetBuffer(index, IID_PPV_ARGS(&frame.buffer))))
			{
				return RenderResult::Failed;
			}
			_device->CreateRenderTargetView(frame.buffer.Get(), nullptr, frame.target);
		}
		if (FAILED(frame.allocator->Reset()) || FAILED(_list->Reset(frame.allocator.Get(), nullptr)))
		{
			return RenderResult::Failed;
		}
		D3D12_RESOURCE_BARRIER barrier {.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION};
		barrier.Transition = {.pResource = frame.buffer.Get(),
		                      .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
		                      .StateBefore = D3D12_RESOURCE_STATE_PRESENT,
		                      .StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET};
		_list->ResourceBarrier(1, &barrier);
		_list->OMSetRenderTargets(1, &frame.target, FALSE, nullptr);
		ID3D12DescriptorHeap* heaps[] = {_srvHeap.Get()};
		_list->SetDescriptorHeaps(1, heaps);
		ImGui_ImplDX12_RenderDrawData(drawData, _list.Get());
		std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
		_list->ResourceBarrier(1, &barrier);
		if (FAILED(_list->Close()))
		{
			return RenderResult::Failed;
		}
		ID3D12CommandList* lists[] = {_list.Get()};
		_queue->ExecuteCommandLists(1, lists);
		if (FAILED(_queue->Signal(_fence.Get(), ++_fenceValue)))
		{
			return RenderResult::Failed;
		}
		frame.fenceValue = _fenceValue;
		return RenderResult::Drawn;
	}

	void ReleaseBackBuffers() override
	{
		WaitForGpu();
		for (auto& frame : _frames)
		{
			frame.buffer.Reset();
		}
	}

private:
	/// The descriptors of the textures of ImGui (the font atlas and its updates).
	static constexpr UINT k_SrvDescriptors = 64;
	/// The longest wait for the GPU. A longer wait skips the frame, so that the game does not stop.
	static constexpr DWORD k_GpuWaitMilliseconds = 1000;

	struct Frame
	{
		ComPtr<ID3D12CommandAllocator> allocator;
		ComPtr<ID3D12Resource> buffer;
		D3D12_CPU_DESCRIPTOR_HANDLE target {};
		UINT64 fenceValue = 0;
	};

	D3D12Renderer() = default;

	[[nodiscard]] moa::Result<void> Start(ComPtr<ID3D12CommandQueue> queue, IDXGISwapChain* swapChain)
	{
		_queue = std::move(queue);
		if (_queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
		{
			return moa::MakeError("the command queue of the swap chain is not a direct queue");
		}
		if (auto result = _queue->GetDevice(IID_PPV_ARGS(&_device)); FAILED(result))
		{
			return moa::MakeError("the command queue gave no Direct3D 12 device (0x{:08X})", Code(result));
		}
		ComPtr<IDXGISwapChain1> swapChain1;
		DXGI_SWAP_CHAIN_DESC1 description {};
		if (FAILED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain1))) || FAILED(swapChain1->GetDesc1(&description)) ||
		    description.BufferCount == 0)
		{
			return moa::MakeError("the Direct3D 12 swap chain gave no description");
		}
		_format = description.Format;
		const D3D12_DESCRIPTOR_HEAP_DESC targetHeap {.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
		                                             .NumDescriptors = description.BufferCount};
		const D3D12_DESCRIPTOR_HEAP_DESC srvHeap {.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		                                          .NumDescriptors = k_SrvDescriptors,
		                                          .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
		if (FAILED(_device->CreateDescriptorHeap(&targetHeap, IID_PPV_ARGS(&_targetHeap))) ||
		    FAILED(_device->CreateDescriptorHeap(&srvHeap, IID_PPV_ARGS(&_srvHeap))))
		{
			return moa::MakeError("the menu bar could not make its descriptor heaps");
		}
		const auto targetSize = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		_srvSize = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		_frames.resize(description.BufferCount);
		auto target = _targetHeap->GetCPUDescriptorHandleForHeapStart();
		for (auto& frame : _frames)
		{
			if (FAILED(_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.allocator))))
			{
				return moa::MakeError("the menu bar could not make a command allocator");
			}
			frame.target = target;
			target.ptr += targetSize;
		}
		if (FAILED(_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _frames[0].allocator.Get(), nullptr,
		                                      IID_PPV_ARGS(&_list))) ||
		    FAILED(_list->Close()) || FAILED(_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence))))
		{
			return moa::MakeError("the menu bar could not make its command list or fence");
		}
		_event.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
		if (!_event)
		{
			return moa::MakeError("the menu bar could not make an event");
		}
		for (UINT i = 0; i < k_SrvDescriptors; ++i)
		{
			_freeSrv.push_back(k_SrvDescriptors - 1 - i);
		}
		ImGui_ImplDX12_InitInfo info;
		info.Device = _device.Get();
		info.CommandQueue = _queue.Get();
		info.NumFramesInFlight = static_cast<int>(description.BufferCount);
		info.RTVFormat = _format;
		info.DSVFormat = DXGI_FORMAT_UNKNOWN;
		info.UserData = this;
		info.SrvDescriptorHeap = _srvHeap.Get();
		info.SrvDescriptorAllocFn = &AllocateSrv;
		info.SrvDescriptorFreeFn = &FreeSrv;
		if (!ImGui_ImplDX12_Init(&info))
		{
			return moa::MakeError("the Direct3D 12 backend of ImGui did not start");
		}
		_started = true;
		return {};
	}

	static void AllocateSrv(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu)
	{
		auto* self = static_cast<D3D12Renderer*>(info->UserData);
		// With no free descriptor, the backend gets a null handle and draws no texture. It does not crash.
		if (self->_freeSrv.empty())
		{
			*cpu = {};
			*gpu = {};
			return;
		}
		const auto index = self->_freeSrv.back();
		self->_freeSrv.pop_back();
		cpu->ptr = self->_srvHeap->GetCPUDescriptorHandleForHeapStart().ptr + static_cast<SIZE_T>(index) * self->_srvSize;
		gpu->ptr = self->_srvHeap->GetGPUDescriptorHandleForHeapStart().ptr + static_cast<UINT64>(index) * self->_srvSize;
	}

	static void FreeSrv(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE /*gpu*/)
	{
		auto* self = static_cast<D3D12Renderer*>(info->UserData);
		if (cpu.ptr == 0)
		{
			return;
		}
		const auto start = self->_srvHeap->GetCPUDescriptorHandleForHeapStart().ptr;
		self->_freeSrv.push_back(static_cast<UINT>((cpu.ptr - start) / self->_srvSize));
	}

	/// Wait until the GPU did the commands up to `value`. Return false after the longest wait.
	[[nodiscard]] bool WaitForFence(UINT64 value)
	{
		if (!_fence || _fence->GetCompletedValue() >= value)
		{
			return true;
		}
		if (FAILED(_fence->SetEventOnCompletion(value, _event.get())))
		{
			return false;
		}
		return WaitForSingleObject(_event.get(), k_GpuWaitMilliseconds) == WAIT_OBJECT_0;
	}

	void WaitForGpu()
	{
		if (_queue && _fence && SUCCEEDED(_queue->Signal(_fence.Get(), ++_fenceValue)))
		{
			(void)WaitForFence(_fenceValue);
		}
	}

	ComPtr<ID3D12CommandQueue> _queue;
	ComPtr<ID3D12Device> _device;
	ComPtr<ID3D12DescriptorHeap> _targetHeap;
	ComPtr<ID3D12DescriptorHeap> _srvHeap;
	ComPtr<ID3D12GraphicsCommandList> _list;
	ComPtr<ID3D12Fence> _fence;
	EventHandle _event;
	UINT64 _fenceValue = 0;
	UINT _srvSize = 0;
	std::vector<UINT> _freeSrv;
	std::vector<Frame> _frames;
	DXGI_FORMAT _format = DXGI_FORMAT_UNKNOWN;
	bool _started = false;
};
} // namespace

const char* SwapChainApi(IDXGISwapChain* swapChain)
{
	ComPtr<ID3D11Device> device11;
	if (SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(&device11))))
	{
		return "Direct3D 11";
	}
	ComPtr<ID3D12CommandQueue> queue;
	if (SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(&queue))))
	{
		return "Direct3D 12";
	}
	return "an unknown API";
}

moa::Result<std::unique_ptr<OverlayRenderer>> CreateRenderer(IDXGISwapChain* swapChain)
{
	// The device of a Direct3D 11 swap chain is the Direct3D 11 device. The device of a Direct3D 12 swap chain is the
	// command queue that presents.
	ComPtr<ID3D11Device> device11;
	if (SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(&device11))))
	{
		return D3D11Renderer::Create(std::move(device11));
	}
	ComPtr<ID3D12CommandQueue> queue;
	if (SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(&queue))))
	{
		return D3D12Renderer::Create(std::move(queue), swapChain);
	}
	return moa::MakeError("the swap chain has no Direct3D 11 device and no Direct3D 12 command queue");
}

} // namespace moa_mod::overlay
