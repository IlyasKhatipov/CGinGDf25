#include "dx12_renderer.h"

#include "utils/com_error_handler.h"
#include "utils/window.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <filesystem>


void cg::renderer::dx12_renderer::init()
{
	cg::renderer::renderer::load_camera();
	cg::renderer::renderer::load_model();

	view_port = CD3DX12_VIEWPORT(0.f, 0.f, static_cast<float>(settings->width), static_cast<float>(settings->height));
	scissor_rect = CD3DX12_RECT(0, 0, static_cast<LONG>(settings->width), static_cast<LONG>(settings->height));
	load_pipeline();
	load_assets();
}

void cg::renderer::dx12_renderer::destroy()
{
	wait_for_gpu();
	CloseHandle(fence_event);
}

void cg::renderer::dx12_renderer::update()
{
	// TODO Lab: 3.08 Implement `update` method of `dx12_renderer`
}

void cg::renderer::dx12_renderer::render()
{
	// TODO Lab: 3.06 Implement `render` method
}

ComPtr<IDXGIFactory4> cg::renderer::dx12_renderer::get_dxgi_factory()
{
	UINT dxgi_factory_flags = 0;
	#ifdef _DEBUG
		ComPtr<ID3D12Debug> debug_controller;
		if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller))))
		{
			debug_controller->EnableDebugLayer();
			dxgi_factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	#endif

	ComPtr<IDXGIFactory4> dxgi_factory;
	THROW_IF_FAILED(CreateDXGIFactory2(dxgi_factory_flags, IID_PPV_ARGS(&dxgi_factory)));
	return dxgi_factory;
}

void cg::renderer::dx12_renderer::initialize_device(ComPtr<IDXGIFactory4>& dxgi_factory)
{
	ComPtr<IDXGIAdapter1> hardware_adapter;
	THROW_IF_FAILED(dxgi_factory->EnumAdapters1(0, &hardware_adapter));
	#ifdef _DEBUG
		DXGI_ADAPTER_DESC desc{};
		hardware_adapter->GetDesc(&desc);
		OutputDebugString(desc.Description);
		OutputDebugString(L"\n");
	#endif
	THROW_IF_FAILED(D3D12CreateDevice(hardware_adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
}

void cg::renderer::dx12_renderer::create_direct_command_queue()
{
	D3D12_COMMAND_QUEUE_DESC desc{};
	desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	THROW_IF_FAILED(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&command_queue)));
}

void cg::renderer::dx12_renderer::create_swap_chain(ComPtr<IDXGIFactory4>& dxgi_factory)
{
	DXGI_SWAP_CHAIN_DESC1 desc{};
	desc.BufferCount = frame_number;
	desc.Height = settings->height;
	desc.Width = settings->width;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	desc.SampleDesc.Count = 1;
	ComPtr<IDXGISwapChain1> temp_swap_chain;
	THROW_IF_FAILED(dxgi_factory->CreateSwapChainForHwnd(
		command_queue.Get(),cg::utils::window::get_hwnd(),&desc, nullptr, nullptr, &temp_swap_chain
	));
	dxgi_factory->MakeWindowAssociation(cg::utils::window::get_hwnd(), DXGI_MWA_NO_ALT_ENTER);
	temp_swap_chain.As(&swap_chain);
	frame_index = swap_chain->GetCurrentBackBufferIndex();

}

void cg::renderer::dx12_renderer::create_render_target_views()
{
	// TODO Lab: 3.04 Create a descriptor heap for render targets
	// TODO Lab: 3.04 Create render target views
}

void cg::renderer::dx12_renderer::create_depth_buffer()
{
}

void cg::renderer::dx12_renderer::create_command_allocators()
{
	// TODO Lab: 3.06 Create command allocators and a command list
}

void cg::renderer::dx12_renderer::create_command_list()
{
	// TODO Lab: 3.06 Create command allocators and a command list
}


void cg::renderer::dx12_renderer::load_pipeline()
{
	ComPtr<IDXGIFactory4> dxgi_factory = get_dxgi_factory();
	initialize_device(dxgi_factory);
	create_direct_command_queue();
	create_swap_chain(dxgi_factory);
	// TODO Lab: 3.04 Create render target views
}

D3D12_STATIC_SAMPLER_DESC cg::renderer::dx12_renderer::get_sampler_descriptor()
{
	D3D12_STATIC_SAMPLER_DESC sampler_desc{};
	return sampler_desc;
}

void cg::renderer::dx12_renderer::create_root_signature(const D3D12_STATIC_SAMPLER_DESC* sampler_descriptors, UINT num_sampler_descriptors)
{
	// TODO Lab: 3.05 Create a descriptor table and a root signature
}

std::filesystem::path cg::renderer::dx12_renderer::get_shader_path()
{
	// TODO Lab: 3.05 Compile shaders
	return "";
}

ComPtr<ID3DBlob> cg::renderer::dx12_renderer::compile_shader(const std::string& entrypoint, const std::string& target)
{
	// TODO Lab: 3.05 Compile shaders
	return nullptr;
}

void cg::renderer::dx12_renderer::create_pso()
{
	// TODO Lab: 3.05 Compile shaders
	// TODO Lab: 3.05 Setup a PSO descriptor and create a PSO
}

void cg::renderer::dx12_renderer::create_resource_on_upload_heap(ComPtr<ID3D12Resource>& resource, UINT size, const std::wstring& name)
{
	CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
	auto desc = CD3DX12_RESOURCE_DESC::Buffer(size);
	THROW_IF_FAILED(device->CreateCommittedResource(
		&hp,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&resource)
	));
	if (!name.empty())
	{
		resource->SetName(name.c_str());
	}
}

void cg::renderer::dx12_renderer::create_resource_on_default_heap(ComPtr<ID3D12Resource>& resource, UINT size, const std::wstring& name, D3D12_RESOURCE_DESC* resource_descriptor)
{
}

void cg::renderer::dx12_renderer::copy_data(const void* buffer_data, UINT buffer_size, ComPtr<ID3D12Resource>& destination_resource)
{
	UINT8* buffer_data_begin;
	CD3DX12_RANGE read_range(0, 0);
	THROW_IF_FAILED(
		destination_resource->Map(
			0, &read_range, reinterpret_cast<void**>(&buffer_data_begin)
		)
	);
	memcpy(buffer_data_begin, buffer_data, buffer_size);
	destination_resource->Unmap(0, 0);
}

void cg::renderer::dx12_renderer::copy_data(const void* buffer_data, const UINT buffer_size, ComPtr<ID3D12Resource>& destination_resource, ComPtr<ID3D12Resource>& intermediate_resource, D3D12_RESOURCE_STATES state_after, int row_pitch, int slice_pitch)
{
}

D3D12_VERTEX_BUFFER_VIEW cg::renderer::dx12_renderer::create_vertex_buffer_view(const ComPtr<ID3D12Resource>& vertex_buffer, const UINT vertex_buffer_size)
{
	// TODO Lab: 3.04 Create vertex buffer views
	return D3D12_VERTEX_BUFFER_VIEW{};
}

D3D12_INDEX_BUFFER_VIEW cg::renderer::dx12_renderer::create_index_buffer_view(const ComPtr<ID3D12Resource>& index_buffer, const UINT index_buffer_size)
{
	// TODO Lab: 3.04 Create index buffer views
	return D3D12_INDEX_BUFFER_VIEW{};
}

void cg::renderer::dx12_renderer::create_shader_resource_view(const ComPtr<ID3D12Resource>& texture, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handler)
{
}

void cg::renderer::dx12_renderer::create_constant_buffer_view(const ComPtr<ID3D12Resource>& buffer, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handler)
{
	// TODO Lab: 3.04 Create a constant buffer view
}

void cg::renderer::dx12_renderer::load_assets()
{
	// TODO Lab: 3.05 Create a descriptor table and a root signature
	// TODO Lab: 3.05 Setup a PSO descriptor and create a PSO
	// TODO Lab: 3.06 Create command allocators and a command list

	// TODO Lab: 3.04 Create a descriptor heap for a constant buffer

	const size_t shape_num = model->get_index_buffers().size();

	vertex_buffers.resize(shape_num);
	index_buffers.resize(shape_num);

	for (size_t i = 0; i < shape_num ; i++)
	{
		auto vb_data = model->get_vertex_buffers()[i];
		const UINT vb_size = static_cast<UINT>(vb_data->size_bytes());
		std::wstring vb_name (L"Vertex buffer ");
		vb_name += std::to_wstring(i);
		create_resource_on_upload_heap(vertex_buffers[i], vb_size, vb_name);
		copy_data(vb_data -> get_data(), vb_size, vertex_buffers[i]);

		auto ib_data = model->get_index_buffers()[i];
		const UINT ib_size = static_cast<UINT>(ib_data->size_bytes());
		std::wstring ib_name (L"Index buffer ");
		ib_name += std::to_wstring(i);
		create_resource_on_upload_heap(index_buffers[i], ib_size, ib_name);
		copy_data(ib_data -> get_data(), ib_size, index_buffers[i]);
	}

	std::wstring cd_name(L"Constant buffer ");
	create_resource_on_upload_heap(constant_buffer, 64*1024, cd_name);
	copy_data(&cb, sizeof(cb), constant_buffer);
	CD3DX12_RANGE read_range(0, 0);
	THROW_IF_FAILED(constant_buffer->Map(0, &read_range, reinterpret_cast<void**>(&constant_buffer_data_begin)));

	
	// TODO Lab: 3.04 Create vertex buffer views
	// TODO Lab: 3.04 Create index buffer views

	// TODO Lab: 3.04 Create a constant buffer view

	// TODO Lab: 3.07 Create a fence and fence event
}


void cg::renderer::dx12_renderer::populate_command_list()
{
	// TODO Lab: 3.06 Implement `populate_command_list` method
}


void cg::renderer::dx12_renderer::move_to_next_frame()
{
	// TODO Lab: 3.07 Implement `move_to_next_frame` method
}

void cg::renderer::dx12_renderer::wait_for_gpu()
{
	// TODO Lab: 3.07 Implement `wait_for_gpu` method
}


void cg::renderer::descriptor_heap::create_heap(ComPtr<ID3D12Device>& device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT number, D3D12_DESCRIPTOR_HEAP_FLAGS flags)
{
	// TODO Lab: 3.04 Implement `create_heap`, `get_cpu_descriptor_handle`, `get_gpu_descriptor_handle`, and `get` methods of `cg::renderer::descriptor_heap`
}

D3D12_CPU_DESCRIPTOR_HANDLE cg::renderer::descriptor_heap::get_cpu_descriptor_handle(UINT index) const
{
	// TODO Lab: 3.04 Implement `create_heap`, `get_cpu_descriptor_handle`, `get_gpu_descriptor_handle`, and `get` methods of `cg::renderer::descriptor_heap`
	return D3D12_CPU_DESCRIPTOR_HANDLE{};
}

D3D12_GPU_DESCRIPTOR_HANDLE cg::renderer::descriptor_heap::get_gpu_descriptor_handle(UINT index) const
{
	// TODO Lab: 3.04 Implement `create_heap`, `get_cpu_descriptor_handle`, `get_gpu_descriptor_handle`, and `get` methods of `cg::renderer::descriptor_heap`
	return D3D12_GPU_DESCRIPTOR_HANDLE{};
}
ID3D12DescriptorHeap* cg::renderer::descriptor_heap::get() const
{
	// TODO Lab: 3.04 Implement `create_heap`, `get_cpu_descriptor_handle`, `get_gpu_descriptor_handle`, and `get` methods of `cg::renderer::descriptor_heap`
	return nullptr;
}
