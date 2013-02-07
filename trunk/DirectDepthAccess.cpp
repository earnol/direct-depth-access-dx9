//-----------------------------------------------------------------------------
// File: DirectDepthAccess.cpp
//
// author: Dmytro Shchukin
//-----------------------------------------------------------------------------
#include <Windows.h>
#include <mmsystem.h>
#include <d3dx9.h>
#pragma warning( disable : 4996 ) // disable deprecated warning 
#include <strsafe.h>
#pragma warning( default : 4996 )
#include <nvapi.h>

const int						SCREEN_WIDTH = 640;
const int						SCREEN_HEIGHT = 480;

//--------------------------------------------------------------------------------------
class DepthTexture
{
	#define FOURCC_RESZ ((D3DFORMAT)(MAKEFOURCC('R','E','S','Z')))
	#define FOURCC_INTZ ((D3DFORMAT)(MAKEFOURCC('I','N','T','Z')))
	#define FOURCC_RAWZ ((D3DFORMAT)(MAKEFOURCC('R','A','W','Z')))
	#define RESZ_CODE 0x7fa05000

	LPDIRECT3DTEXTURE9		m_depthTexture;
	bool					m_isRESZ;
	bool					m_isINTZ;
	bool					m_isRAWZ;
	bool					m_allowDirectDepthAccess;
	IDirect3DSurface9 *		m_registeredDepthStencilSurface;
public:

	//--------------------------------------------------------------------------------------
	DepthTexture(const LPDIRECT3DDEVICE9 device, const LPDIRECT3D9 d3d, int width, int height)
		: m_registeredDepthStencilSurface( NULL )
	{
		m_depthTexture = NULL;
		D3DDISPLAYMODE currentDisplayMode;
		d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &currentDisplayMode);

		// determine if RESZ is supported
		m_isRESZ = d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
			currentDisplayMode.Format, D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, FOURCC_RESZ ) == D3D_OK;

		// determine if INTZ is supported
		m_isINTZ = d3d->CheckDeviceFormat( D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
			currentDisplayMode.Format, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, FOURCC_INTZ ) == D3D_OK;

		// determine if RAWZ is supported, used in GeForce 6-7 series.
		m_isRAWZ = d3d->CheckDeviceFormat( D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
			currentDisplayMode.Format, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, FOURCC_RAWZ ) == D3D_OK;

		// determine if RESZ or NVAPI supported
		m_allowDirectDepthAccess = ( NvAPI_Initialize() == NVAPI_OK || m_isRESZ ) && ( m_isRAWZ || m_isINTZ );

		if (m_allowDirectDepthAccess)
		{
			D3DFORMAT format = m_isINTZ ? FOURCC_INTZ : FOURCC_RAWZ;

			device->CreateTexture(width, height, 1,
				D3DUSAGE_DEPTHSTENCIL, format,
				D3DPOOL_DEFAULT, &m_depthTexture,
				NULL);

			if (!m_isRESZ)
			{
				NvAPI_D3D9_RegisterResource(m_depthTexture);
			}
		}
	}

	//--------------------------------------------------------------------------------------
	~DepthTexture()
	{
		if (m_depthTexture)
		{
			if (!m_isRESZ)
			{
				NvAPI_D3D9_UnregisterResource(m_depthTexture);
			}
			m_depthTexture->Release();
		}
		if (m_registeredDepthStencilSurface != NULL)
		{
			NvAPI_D3D9_UnregisterResource(m_registeredDepthStencilSurface);
		}
	}

	//--------------------------------------------------------------------------------------
	void resolveDepth(const LPDIRECT3DDEVICE9 device)
	{
		if (m_isRESZ)
		{
			{
				device->SetVertexShader(NULL);
				device->SetPixelShader(NULL);
				device->SetFVF(D3DFVF_XYZ);
				// Bind depth stencil texture to texture sampler 0
				device->SetTexture(0, m_depthTexture);
				// Perform a dummy draw call to ensure texture sampler 0 is set before the // resolve is triggered
				// Vertex declaration and shaders may need to me adjusted to ensure no debug
				// error message is produced
				D3DXVECTOR3 vDummyPoint(0.0f, 0.0f, 0.0f);
				device->SetRenderState(D3DRS_ZENABLE, FALSE);
				device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
				device->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
				device->DrawPrimitiveUP(D3DPT_POINTLIST, 1, vDummyPoint, sizeof(D3DXVECTOR3));
				device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
				device->SetRenderState(D3DRS_ZENABLE, TRUE);
				device->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0F);

				// Trigger the depth buffer resolve; after this call texture sampler 0
				// will contain the contents of the resolve operation
				device->SetRenderState(D3DRS_POINTSIZE, RESZ_CODE);

				// This hack to fix resz hack, has been found by Maksym Bezus!!!
				// Without this line resz will be resolved only for first frame
				device->SetRenderState(D3DRS_POINTSIZE, 0); // TROLOLO!!!
			}
		}
		else
		{
			IDirect3DSurface9* pDepthStencilSurface = NULL;
			device->GetDepthStencilSurface( &pDepthStencilSurface );

			if (m_registeredDepthStencilSurface != pDepthStencilSurface)
			{
				NvAPI_D3D9_RegisterResource(pDepthStencilSurface);
				if (m_registeredDepthStencilSurface != NULL)
				{
					// Unregister old one if there is any
					NvAPI_D3D9_UnregisterResource(m_registeredDepthStencilSurface);
				}

				m_registeredDepthStencilSurface = pDepthStencilSurface;
			}
			NvAPI_D3D9_StretchRectEx(device, pDepthStencilSurface, NULL, m_depthTexture, NULL, D3DTEXF_LINEAR);

			pDepthStencilSurface->Release();
		}
	}

	LPDIRECT3DTEXTURE9	getTexture() { return m_depthTexture; }
	bool				isINTZ() { return m_isINTZ; }
};

//--------------------------------------------------------------------------------------
// This is the vertex format used with the quad during post-process.
struct PPVERT
{
	float x, y, z, rhw;
	float tu, tv;       // Texcoord for post-process source
	float tu2, tv2;     // Texcoord for the original scene

	const static D3DVERTEXELEMENT9 Decl[4];
};

// Vertex declaration for post-processing
const D3DVERTEXELEMENT9 PPVERT::Decl[4] =
{
	{ 0, 0,  D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITIONT, 0 },
	{ 0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,  0 },
	{ 0, 24, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,  1 },
	D3DDECL_END()
};

//-----------------------------------------------------------------------------
// Global variables
//-----------------------------------------------------------------------------
LPDIRECT3D9						g_pD3D = NULL; // Used to create the D3DDevice
LPDIRECT3DDEVICE9				g_pd3dDevice = NULL; // Our rendering device

LPD3DXMESH						g_pMesh = NULL; // Our mesh object in sysmem
D3DMATERIAL9*					g_pMeshMaterials = NULL; // Materials for our mesh
LPDIRECT3DTEXTURE9*				g_pMeshTextures = NULL; // Textures for our mesh
DWORD							g_dwNumMaterials = 0L;   // Number of mesh materials

IDirect3DVertexDeclaration9*    g_pVertDeclPP = NULL; // Vertex decl for post-processing
ID3DXEffect*                    g_pEffect = NULL;        // D3DX effect interface
D3DXHANDLE                      g_hTShowUnmodified;       // Handle to ShowUnmodified technique
D3DXHANDLE                      g_hTextureDepthTexture;

DepthTexture*					g_depthTexture = NULL;

//-----------------------------------------------------------------------------
// Name: InitD3D()
// Desc: Initializes Direct3D
//-----------------------------------------------------------------------------
HRESULT InitD3D( HWND hWnd )
{
	// Create the D3D object.
	if( NULL == ( g_pD3D = Direct3DCreate9( D3D_SDK_VERSION ) ) )
		return E_FAIL;

	// Set up the structure used to create the D3DDevice. Since we are now
	// using more complex geometry, we will create a device with a zbuffer.
	D3DPRESENT_PARAMETERS d3dpp;
	ZeroMemory( &d3dpp, sizeof( d3dpp ) );
	d3dpp.Windowed = TRUE;
	d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;
	d3dpp.BackBufferWidth = SCREEN_WIDTH;
	d3dpp.BackBufferHeight = SCREEN_HEIGHT;
	d3dpp.BackBufferCount = 1;
	d3dpp.MultiSampleType = D3DMULTISAMPLE_4_SAMPLES;
	d3dpp.EnableAutoDepthStencil = TRUE;
	d3dpp.AutoDepthStencilFormat = D3DFMT_D24X8; // NOTE! not all formats supported

	// Create the D3DDevice
	if( FAILED( g_pD3D->CreateDevice( D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd,
		D3DCREATE_SOFTWARE_VERTEXPROCESSING,
		&d3dpp, &g_pd3dDevice ) ) )
	{
		return E_FAIL;
	}

	// Turn on the zbuffer
	g_pd3dDevice->SetRenderState( D3DRS_ZENABLE, TRUE );
	g_pd3dDevice->SetRenderState( D3DRS_ZWRITEENABLE, TRUE);

	// Turn on ambient lighting 
	g_pd3dDevice->SetRenderState( D3DRS_AMBIENT, 0xffffffff );

	// Create vertex declaration for post-process
	if( FAILED( g_pd3dDevice->CreateVertexDeclaration( PPVERT::Decl, &g_pVertDeclPP ) ) )
	{
		return E_FAIL;
	}

	DWORD dwShaderFlags = 0;
	dwShaderFlags |= D3DXSHADER_DEBUG;

	if (FAILED( D3DXCreateEffectFromFile( g_pd3dDevice, L"..\\DirectDepthAccess.fx", NULL, NULL, dwShaderFlags, NULL, &g_pEffect, NULL ) ))
	{
		D3DXCreateEffectFromFile( g_pd3dDevice, L"DirectDepthAccess.fx", NULL, NULL, dwShaderFlags, NULL, &g_pEffect, NULL );
	}

	g_depthTexture = new DepthTexture(g_pd3dDevice, g_pD3D, SCREEN_WIDTH, SCREEN_HEIGHT);

	const char* techniqueName = g_depthTexture->isINTZ() ? "ShowUnmodified" : "ShowUnmodifiedRAWZ";
	g_hTShowUnmodified = g_pEffect->GetTechniqueByName( techniqueName );
	g_hTextureDepthTexture = g_pEffect->GetParameterByName( NULL, "DepthTargetTexture" );

	return S_OK;
}

//-----------------------------------------------------------------------------
HRESULT InitGeometry()
{
	LPD3DXBUFFER pD3DXMtrlBuffer;

	// Load the mesh from the specified file
	if( FAILED( D3DXLoadMeshFromX( L"Tiger.x", D3DXMESH_SYSTEMMEM,
		g_pd3dDevice, NULL,
		&pD3DXMtrlBuffer, NULL, &g_dwNumMaterials,
		&g_pMesh ) ) )
	{
		// If model is not in current folder, try parent folder
		if( FAILED( D3DXLoadMeshFromX( L"..\\Tiger.x", D3DXMESH_SYSTEMMEM,
			g_pd3dDevice, NULL,
			&pD3DXMtrlBuffer, NULL, &g_dwNumMaterials,
			&g_pMesh ) ) )
		{
			MessageBox( NULL, L"Could not find tiger.x", L"Meshes.exe", MB_OK );
			return E_FAIL;
		}
	}

	// We need to extract the material properties and texture names from the 
	// pD3DXMtrlBuffer
	D3DXMATERIAL* d3dxMaterials = ( D3DXMATERIAL* )pD3DXMtrlBuffer->GetBufferPointer();
	g_pMeshMaterials = new D3DMATERIAL9[g_dwNumMaterials];
	if( g_pMeshMaterials == NULL )
		return E_OUTOFMEMORY;
	g_pMeshTextures = new LPDIRECT3DTEXTURE9[g_dwNumMaterials];
	if( g_pMeshTextures == NULL )
		return E_OUTOFMEMORY;

	for( DWORD i = 0; i < g_dwNumMaterials; i++ )
	{
		// Copy the material
		g_pMeshMaterials[i] = d3dxMaterials[i].MatD3D;

		// Set the ambient color for the material (D3DX does not do this)
		g_pMeshMaterials[i].Ambient = g_pMeshMaterials[i].Diffuse;

		g_pMeshTextures[i] = NULL;
		if( d3dxMaterials[i].pTextureFilename != NULL &&
			lstrlenA( d3dxMaterials[i].pTextureFilename ) > 0 )
		{
			// Create the texture
			if( FAILED( D3DXCreateTextureFromFileA( g_pd3dDevice,
				d3dxMaterials[i].pTextureFilename,
				&g_pMeshTextures[i] ) ) )
			{
				// If texture is not in current folder, try parent folder
				const CHAR* strPrefix = "..\\";
				CHAR strTexture[MAX_PATH];
				strcpy_s( strTexture, MAX_PATH, strPrefix );
				strcat_s( strTexture, MAX_PATH, d3dxMaterials[i].pTextureFilename );
				// If texture is not in current folder, try parent folder
				if( FAILED( D3DXCreateTextureFromFileA( g_pd3dDevice,
					strTexture,
					&g_pMeshTextures[i] ) ) )
				{
					MessageBox( NULL, L"Could not find texture map", L"Meshes.exe", MB_OK );
				}
			}
		}
	}

	// Done with the material buffer
	pD3DXMtrlBuffer->Release();

	return S_OK;
}

//-----------------------------------------------------------------------------
VOID Cleanup()
{
	if( g_pMeshMaterials != NULL )
		delete[] g_pMeshMaterials;

	if( g_pMeshTextures )
	{
		for( DWORD i = 0; i < g_dwNumMaterials; i++ )
		{
			if( g_pMeshTextures[i] )
				g_pMeshTextures[i]->Release();
		}
		delete[] g_pMeshTextures;
	}
	if( g_pMesh != NULL )
		g_pMesh->Release();

	if( g_pd3dDevice != NULL )
		g_pd3dDevice->Release();

	if( g_pD3D != NULL )
		g_pD3D->Release();

	if (g_pVertDeclPP != NULL )
		g_pVertDeclPP->Release();

	if (g_pEffect != NULL )
		g_pEffect->Release();

	delete g_depthTexture;
	g_depthTexture = NULL;
}

//-----------------------------------------------------------------------------
VOID SetupMatrices()
{
	// Set up world matrix
	D3DXMATRIXA16 matWorld;
	D3DXMatrixRotationY( &matWorld, timeGetTime() / 1000.0f );
	g_pd3dDevice->SetTransform( D3DTS_WORLD, &matWorld );

	// Set up our view matrix. A view matrix can be defined given an eye point,
	// a point to lookat, and a direction for which way is up. Here, we set the
	// eye five units back along the z-axis and up three units, look at the 
	// origin, and define "up" to be in the y-direction.
	D3DXVECTOR3 vEyePt( 0.0f, 3.0f,-5.0f );
	D3DXVECTOR3 vLookatPt( 0.0f, 0.0f, 0.0f );
	D3DXVECTOR3 vUpVec( 0.0f, 1.0f, 0.0f );
	D3DXMATRIXA16 matView;
	D3DXMatrixLookAtLH( &matView, &vEyePt, &vLookatPt, &vUpVec );
	g_pd3dDevice->SetTransform( D3DTS_VIEW, &matView );

	// For the projection matrix, we set up a perspective transform (which
	// transforms geometry from 3D view space to 2D viewport space, with
	// a perspective divide making objects smaller in the distance). To build
	// a perpsective transform, we need the field of view (1/4 pi is common),
	// the aspect ratio, and the near and far clipping planes (which define at
	// what distances geometry should be no longer be rendered).
	D3DXMATRIXA16 matProj;
	D3DXMatrixPerspectiveFovLH( &matProj, D3DX_PI / 4, 1.0f, 1.0f, 100.0f );
	g_pd3dDevice->SetTransform( D3DTS_PROJECTION, &matProj );
}

//-----------------------------------------------------------------------------
VOID Render()
{
	// Clear the backbuffer and the zbuffer
	g_pd3dDevice->Clear( 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
		D3DCOLOR_XRGB( 0, 0, 255 ), 1.0f, 0 );

	// Begin the scene
	if( SUCCEEDED( g_pd3dDevice->BeginScene() ) )
	{
		// Setup the world, view, and projection matrices
		SetupMatrices();

		// Meshes are divided into subsets, one for each material. Render them in
		// a loop
		for( DWORD i = 0; i < g_dwNumMaterials; i++ )
		{
			// Set the material and texture for this subset
			g_pd3dDevice->SetMaterial( &g_pMeshMaterials[i] );
			g_pd3dDevice->SetTexture( 0, g_pMeshTextures[i] );

			// Draw the mesh subset
			g_pMesh->DrawSubset( i );
		}

		// Resolve depth
		g_depthTexture->resolveDepth(g_pd3dDevice);

		// Render a screen-sized quad
		{
			int width = SCREEN_WIDTH * 0.35f;
			int height = SCREEN_HEIGHT * 0.35f;
			PPVERT quad[4] =
			{
				{ -0.5f,		-0.5f,          0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f },
				{ width - 0.5f, -0.5f,			0.5f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f },
				{ -0.5f,		height - 0.5f,	0.5f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f },
				{ width - 0.5f, height - 0.5f,	0.5f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f }
			};

			g_pd3dDevice->SetVertexDeclaration( g_pVertDeclPP );
			g_pEffect->SetTechnique( g_hTShowUnmodified );
			g_pEffect->SetTexture( g_hTextureDepthTexture, g_depthTexture->getTexture() );
			UINT cPasses;
			g_pEffect->Begin( &cPasses, 0 );
			for( size_t p = 0; p < cPasses; ++p )
			{
				g_pEffect->BeginPass( p );
				g_pd3dDevice->DrawPrimitiveUP( D3DPT_TRIANGLESTRIP, 2, quad, sizeof( PPVERT ) );
				g_pEffect->EndPass();
			}
			g_pEffect->End();
		}

		// End the scene
		g_pd3dDevice->EndScene();
	}

	// Present the backbuffer contents to the display
	g_pd3dDevice->Present( NULL, NULL, NULL, NULL );
}

//-----------------------------------------------------------------------------
LRESULT WINAPI MsgProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
	switch( msg )
	{
	case WM_DESTROY:
		Cleanup();
		PostQuitMessage( 0 );
		return 0;
	}

	return DefWindowProc( hWnd, msg, wParam, lParam );
}

//-----------------------------------------------------------------------------
INT WINAPI wWinMain( HINSTANCE hInst, HINSTANCE, LPWSTR, INT )
{
	UNREFERENCED_PARAMETER( hInst );

	// Register the window class
	WNDCLASSEX wc =
	{
		sizeof( WNDCLASSEX ), CS_CLASSDC, MsgProc, 0L, 0L,
		GetModuleHandle( NULL ), NULL, NULL, NULL, NULL,
		L"D3D Tutorial", NULL
	};
	RegisterClassEx( &wc );

	// Create the application's window
	HWND hWnd = CreateWindow( L"D3D Tutorial", L"Direct Depth Access",
		WS_OVERLAPPEDWINDOW, 100, 100, SCREEN_WIDTH, SCREEN_HEIGHT,
		NULL, NULL, wc.hInstance, NULL );

	// Initialize Direct3D
	if( SUCCEEDED( InitD3D( hWnd ) ) )
	{
		// Create the scene geometry
		if( SUCCEEDED( InitGeometry() ) )
		{
			// Show the window
			ShowWindow( hWnd, SW_SHOWDEFAULT );
			UpdateWindow( hWnd );

			// Enter the message loop
			MSG msg;
			ZeroMemory( &msg, sizeof( msg ) );
			while( msg.message != WM_QUIT )
			{
				if( PeekMessage( &msg, NULL, 0U, 0U, PM_REMOVE ) )
				{
					TranslateMessage( &msg );
					DispatchMessage( &msg );
				}
				else
					Render();
			}
		}
	}

	UnregisterClass( L"D3D Tutorial", wc.hInstance );
	return 0;
}