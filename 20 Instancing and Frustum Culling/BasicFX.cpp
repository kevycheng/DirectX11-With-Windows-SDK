#include "Effects.h"
#include "EffectHelper.h"
#include "Vertex.h"
#include <d3dcompiler.h>
#include <experimental/filesystem>
using namespace DirectX;
using namespace std::experimental;

//
// ��Щ�ṹ���ӦHLSL�Ľṹ�壬�������ļ�ʹ�á���Ҫ��16�ֽڶ���
//

struct CBChangesEveryObjectDrawing
{
	DirectX::XMMATRIX world;
	DirectX::XMMATRIX worldInvTranspose;
};

struct CBChangesEveryInstanceDrawing
{
	Material material;
};

struct CBChangesEveryFrame
{
	DirectX::XMMATRIX view;
	DirectX::XMVECTOR eyePos;
};

struct CBChangesOnResize
{
	DirectX::XMMATRIX proj;
};

struct CBChangesRarely
{
	DirectionalLight dirLight[BasicFX::maxLights];
	PointLight pointLight[BasicFX::maxLights];
	SpotLight spotLight[BasicFX::maxLights];
};


//
// BasicFX::Impl ��Ҫ����BasicFX�Ķ���
//

class BasicFX::Impl : public AlignedType<BasicFX::Impl>
{
public:
	// ������ʽָ��
	Impl() = default;
	~Impl() = default;

	// objFileNameInOutΪ����õ���ɫ���������ļ�(.*so)������ָ��������Ѱ�Ҹ��ļ�����ȡ
	// hlslFileNameΪ��ɫ�����룬��δ�ҵ���ɫ���������ļ��������ɫ������
	// ����ɹ�����ָ����objFileNameInOut���򱣴����õ���ɫ����������Ϣ�����ļ�
	// ppBlobOut�����ɫ����������Ϣ
	HRESULT CreateShaderFromFile(const WCHAR* objFileNameInOut, const WCHAR* hlslFileName, LPCSTR entryPoint, LPCSTR shaderModel, ID3DBlob** ppBlobOut);

public:
	// ��Ҫ16�ֽڶ�������ȷ���ǰ��
	CBufferObject<0, CBChangesEveryObjectDrawing>	cbObjDrawing;		// ÿ�ζ�����Ƶĳ���������
	CBufferObject<1, CBChangesEveryInstanceDrawing> cbInstDrawing;		// ÿ��ʵ�����Ƶĳ���������
	CBufferObject<2, CBChangesEveryFrame>			cbFrame;			// ÿ֡���Ƶĳ���������
	CBufferObject<3, CBChangesOnResize>				cbOnResize;			// ÿ�δ��ڴ�С����ĳ���������
	CBufferObject<4, CBChangesRarely>				cbRarely;			// �����������ĳ���������
	BOOL isDirty;											// �Ƿ���ֵ���
	std::vector<CBufferBase*> cBufferPtrs;					// ͳһ�����������еĳ���������


	ComPtr<ID3D11VertexShader> basicInstanceVS;
	ComPtr<ID3D11VertexShader> basicObjectVS;

	ComPtr<ID3D11PixelShader> basicPS;

	ComPtr<ID3D11InputLayout> instancePosNormalTexLayout;	
	ComPtr<ID3D11InputLayout> instancePosColorLayout;
	ComPtr<ID3D11InputLayout> vertexPosNormalTexLayout;		
	ComPtr<ID3D11InputLayout> vertexPosColorLayout;

	ComPtr<ID3D11ShaderResourceView> textureA;				// �������Ӧʹ�õ�����
	ComPtr<ID3D11ShaderResourceView> textureD;				// ������Ӧʹ�õ�����
};

//
// BasicFX
//

namespace
{
	// BasicFX����
	static BasicFX * pInstance = nullptr;
}

BasicFX::BasicFX()
{
	if (pInstance)
		throw std::exception("BasicFX is a singleton!");
	pInstance = this;
	pImpl = std::make_unique<BasicFX::Impl>();
}

BasicFX::~BasicFX()
{
}

BasicFX::BasicFX(BasicFX && moveFrom)
{
	pImpl.swap(moveFrom.pImpl);
}

BasicFX & BasicFX::operator=(BasicFX && moveFrom)
{
	pImpl.swap(moveFrom.pImpl);
	return *this;
}

BasicFX & BasicFX::Get()
{
	if (!pInstance)
		throw std::exception("BasicFX needs an instance!");
	return *pInstance;
}


bool BasicFX::InitAll(ComPtr<ID3D11Device> device)
{
	if (!device)
		return false;

	if (!pImpl->cBufferPtrs.empty())
		return true;


	ComPtr<ID3DBlob> blob;

	// ʵ�����벼��
	D3D11_INPUT_ELEMENT_DESC basicInstLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "World", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
		{ "World", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
		{ "World", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
		{ "World", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1},
		{ "WorldInvTranspose", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 64, D3D11_INPUT_PER_INSTANCE_DATA, 1},
		{ "WorldInvTranspose", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 80, D3D11_INPUT_PER_INSTANCE_DATA, 1},
		{ "WorldInvTranspose", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 96, D3D11_INPUT_PER_INSTANCE_DATA, 1},
		{ "WorldInvTranspose", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 112, D3D11_INPUT_PER_INSTANCE_DATA, 1}
	};

	//
	// ����������ɫ��
	//

	HR(pImpl->CreateShaderFromFile(L"HLSL\\BasicInstance_VS.vso", L"HLSL\\BasicInstance_VS.hlsl", "VS", "vs_5_0", blob.ReleaseAndGetAddressOf()));
	HR(device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, pImpl->basicInstanceVS.GetAddressOf()));
	// �������㲼��
	HR(device->CreateInputLayout(basicInstLayout, ARRAYSIZE(basicInstLayout),
		blob->GetBufferPointer(), blob->GetBufferSize(), pImpl->instancePosNormalTexLayout.GetAddressOf()));

	HR(pImpl->CreateShaderFromFile(L"HLSL\\BasicObject_VS.vso", L"HLSL\\BasicObject_VS.hlsl", "VS", "vs_5_0", blob.ReleaseAndGetAddressOf()));
	HR(device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, pImpl->basicObjectVS.GetAddressOf()));
	// �������㲼��
	HR(device->CreateInputLayout(VertexPosNormalTex::inputLayout, ARRAYSIZE(VertexPosNormalTex::inputLayout),
		blob->GetBufferPointer(), blob->GetBufferSize(), pImpl->vertexPosNormalTexLayout.GetAddressOf()));

	//
	// ����������ɫ��
	//

	HR(pImpl->CreateShaderFromFile(L"HLSL\\Basic_PS.pso", L"HLSL\\Basic_PS.hlsl", "PS", "ps_5_0", blob.ReleaseAndGetAddressOf()));
	HR(device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, pImpl->basicPS.GetAddressOf()));

	// ��ʼ��
	RenderStates::InitAll(device);

	pImpl->cBufferPtrs.assign({
		&pImpl->cbObjDrawing,
		&pImpl->cbInstDrawing, 
		&pImpl->cbFrame, 
		&pImpl->cbOnResize, 
		&pImpl->cbRarely});

	// ��������������
	for (auto& pBuffer : pImpl->cBufferPtrs)
	{
		pBuffer->CreateBuffer(device);
	}

	return true;
}


void BasicFX::SetRenderDefault(ComPtr<ID3D11DeviceContext> deviceContext, RenderType type)
{
	if (type == RenderInstance)
	{
		deviceContext->IASetInputLayout(pImpl->instancePosNormalTexLayout.Get());
		deviceContext->VSSetShader(pImpl->basicInstanceVS.Get(), nullptr, 0);
		deviceContext->PSSetShader(pImpl->basicPS.Get(), nullptr, 0);
	}
	else
	{
		deviceContext->IASetInputLayout(pImpl->vertexPosNormalTexLayout.Get());
		deviceContext->VSSetShader(pImpl->basicObjectVS.Get(), nullptr, 0);
		deviceContext->PSSetShader(pImpl->basicPS.Get(), nullptr, 0);
	}

	deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	
	deviceContext->GSSetShader(nullptr, nullptr, 0);
	deviceContext->RSSetState(nullptr);
	
	deviceContext->PSSetSamplers(0, 1, RenderStates::SSLinearWrap.GetAddressOf());
	deviceContext->OMSetDepthStencilState(nullptr, 0);
	deviceContext->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
}

void XM_CALLCONV BasicFX::SetWorldMatrix(DirectX::FXMMATRIX W)
{
	auto& cBuffer = pImpl->cbObjDrawing;
	cBuffer.data.world = XMMatrixTranspose(W);
	cBuffer.data.worldInvTranspose = XMMatrixInverse(nullptr, W);	// ����ת�õ���
	pImpl->isDirty = cBuffer.isDirty = true;
}

void XM_CALLCONV BasicFX::SetViewMatrix(FXMMATRIX V)
{
	auto& cBuffer = pImpl->cbFrame;
	cBuffer.data.view = XMMatrixTranspose(V);
	pImpl->isDirty = cBuffer.isDirty = true;
}

void XM_CALLCONV BasicFX::SetProjMatrix(FXMMATRIX P)
{
	auto& cBuffer = pImpl->cbOnResize;
	cBuffer.data.proj = XMMatrixTranspose(P);
	pImpl->isDirty = cBuffer.isDirty = true;
}

void XM_CALLCONV BasicFX::SetWorldViewProjMatrix(FXMMATRIX W, CXMMATRIX V, CXMMATRIX P)
{
	pImpl->cbObjDrawing.data.world = XMMatrixTranspose(W);
	pImpl->cbObjDrawing.data.worldInvTranspose = XMMatrixInverse(nullptr, W);	// ����ת�õ���
	pImpl->cbFrame.data.view = XMMatrixTranspose(V);
	pImpl->cbOnResize.data.proj = XMMatrixTranspose(P);

	auto& pCBuffers = pImpl->cBufferPtrs;
	pCBuffers[0]->isDirty = pCBuffers[2]->isDirty = pCBuffers[3]->isDirty = true;
	pImpl->isDirty = true;
}

void BasicFX::SetDirLight(size_t pos, const DirectionalLight & dirLight)
{
	auto& cBuffer = pImpl->cbRarely;
	cBuffer.data.dirLight[pos] = dirLight;
	pImpl->isDirty = cBuffer.isDirty = true;
}

void BasicFX::SetPointLight(size_t pos, const PointLight & pointLight)
{
	auto& cBuffer = pImpl->cbRarely;
	cBuffer.data.pointLight[pos] = pointLight;
	pImpl->isDirty = cBuffer.isDirty = true;
}

void BasicFX::SetSpotLight(size_t pos, const SpotLight & spotLight)
{
	auto& cBuffer = pImpl->cbRarely;
	cBuffer.data.spotLight[pos] = spotLight;
	pImpl->isDirty = cBuffer.isDirty = true;
}

void BasicFX::SetMaterial(const Material & material)
{
	auto& cBuffer = pImpl->cbInstDrawing;
	cBuffer.data.material = material;
	pImpl->isDirty = cBuffer.isDirty = true;
}

void BasicFX::SetTextureAmbient(ComPtr<ID3D11ShaderResourceView> texture)
{
	pImpl->textureA = texture;
}

void BasicFX::SetTextureDiffuse(ComPtr<ID3D11ShaderResourceView> texture)
{
	pImpl->textureD = texture;
}

void XM_CALLCONV BasicFX::SetEyePos(FXMVECTOR eyePos)
{
	auto& cBuffer = pImpl->cbFrame;
	cBuffer.data.eyePos = eyePos;
	pImpl->isDirty = cBuffer.isDirty = true;
}

void BasicFX::Apply(ComPtr<ID3D11DeviceContext> deviceContext)
{
	auto& pCBuffers = pImpl->cBufferPtrs;
	// ���������󶨵���Ⱦ������
	pCBuffers[0]->BindVS(deviceContext);
	pCBuffers[2]->BindVS(deviceContext);
	pCBuffers[3]->BindVS(deviceContext);

	pCBuffers[1]->BindPS(deviceContext);
	pCBuffers[2]->BindPS(deviceContext);
	pCBuffers[4]->BindPS(deviceContext);

	// ��������
	deviceContext->PSSetShaderResources(0, 1, pImpl->textureA.GetAddressOf());
	deviceContext->PSSetShaderResources(1, 1, pImpl->textureD.GetAddressOf());

	if (pImpl->isDirty)
	{
		pImpl->isDirty = false;
		for (auto& pCBuffer : pCBuffers)
		{
			pCBuffer->UpdateBuffer(deviceContext);
		}
	}
}

//
// BasicFX::Implʵ�ֲ���
//


HRESULT BasicFX::Impl::CreateShaderFromFile(const WCHAR * objFileNameInOut, const WCHAR * hlslFileName, LPCSTR entryPoint, LPCSTR shaderModel, ID3DBlob ** ppBlobOut)
{
	HRESULT hr = S_OK;

	// Ѱ���Ƿ����Ѿ�����õĶ�����ɫ��
	if (objFileNameInOut && filesystem::exists(objFileNameInOut))
	{
		HR(D3DReadFileToBlob(objFileNameInOut, ppBlobOut));
	}
	else
	{
		DWORD dwShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
		// ���� D3DCOMPILE_DEBUG ��־���ڻ�ȡ��ɫ��������Ϣ���ñ�־���������������飬
		// ����Ȼ������ɫ�������Ż�����
		dwShaderFlags |= D3DCOMPILE_DEBUG;

		// ��Debug�����½����Ż��Ա������һЩ�����������
		dwShaderFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
		ComPtr<ID3DBlob> errorBlob = nullptr;
		hr = D3DCompileFromFile(hlslFileName, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entryPoint, shaderModel,
			dwShaderFlags, 0, ppBlobOut, errorBlob.GetAddressOf());
		if (FAILED(hr))
		{
			if (errorBlob != nullptr)
			{
				OutputDebugStringA(reinterpret_cast<const char*>(errorBlob->GetBufferPointer()));
			}
			return hr;
		}

		// ��ָ��������ļ���������ɫ����������Ϣ���
		if (objFileNameInOut)
		{
			HR(D3DWriteBlobToFile(*ppBlobOut, objFileNameInOut, FALSE));
		}
	}

	return hr;
}

