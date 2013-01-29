//--------------------------------------------------------------------------------------
// File: DirectDepthAccess.fx
//
// The effect file for the DirectDepthAccess sample.  
//--------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------
// Global variables
//--------------------------------------------------------------------------------------
texture DepthTargetTexture;

sampler DepthSampler = 
sampler_state
{
    Texture = <DepthTargetTexture>;
    MinFilter = LINEAR;
    MagFilter = LINEAR;

    AddressU = Clamp;
    AddressV = Clamp;
};


float4 RenderUnmodified( in float2 OriginalUV : TEXCOORD0 ) : COLOR 
{
    return tex2D(DepthSampler, OriginalUV);
}

technique ShowUnmodified
{
    pass P0
    {        
        PixelShader = compile ps_2_0 RenderUnmodified();
    }
}