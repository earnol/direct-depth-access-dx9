This is Direct3D sample, showing how to resolve(get) multisampled depth buffer as texture. In DirectX 9

[Download Binaries](https://direct-depth-access-dx9.googlecode.com/files/DirectDepthAccess.zip)

[Download Sources](https://direct-depth-access-dx9.googlecode.com/files/DirectDepthAccessSource.zip)

```
void DepthTexture::resolveDepth(const LPDIRECT3DDEVICE9 device)
{
        if (m_isRESZ)
        {
                device->SetVertexShader(NULL);
                device->SetPixelShader(NULL);
                device->SetFVF(D3DFVF_XYZ);
                // Bind depth stencil texture to texture sampler 0
                device->SetTexture(0, m_pTexture);
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

                // This hack to fix resz hack, has been found by Maksym Bezus
                // Without this line resz will be resolved only for first frame
                device->SetRenderState(D3DRS_POINTSIZE, 0);
        }
        else
        {
                IDirect3DSurface9* pDSS = NULL;
                device->GetDepthStencilSurface( &pDSS );

                if (m_registeredDSS != pDSS)
                {
                        NvAPI_D3D9_RegisterResource(pDSS);
                        if (m_registeredDSS != NULL)
                        {
                                // Unregister old one if there is any
                                NvAPI_D3D9_UnregisterResource(m_registeredDSS);
                        }
                        m_registeredDSS = pDSS;
                }
                NvAPI_D3D9_StretchRectEx(device, pDSS, NULL, m_pTexture, NULL, D3DTEXF_LINEAR);

                pDSS->Release();
        }
}
```