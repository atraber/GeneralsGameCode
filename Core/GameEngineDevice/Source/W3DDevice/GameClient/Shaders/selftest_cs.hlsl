// The compute stage's positive control (compute, Shader Model 5).
//
// THROWAWAY. It exists for stage C1 of the clustered-lighting plan and for nothing else:
// it is the one measurement that says the whole new path -- compile a cs_5_0 blob, load
// it, create buffers with unordered-access views, clear one on the GPU, bind them,
// dispatch, copy the results back and read them on the CPU -- works end to end. Every
// stage after this one depends on all of those and none of them can be seen in a frame.
//
// Delete it, its entry in cmake/shaders.cmake, and W3DShaderManager::runComputeSelfTest
// once C4 has its own control (a cluster grid compared against a CPU-built reference).
// A control that outlives the thing it was controlling for is a cost with no reader.
//
// The pattern is `id.x * 7 + 3`. Deliberately not `id.x`, and not a constant: a buffer
// that was never written reads as zero and an off-by-one in the dispatch or the view
// reads as a shifted index, and neither of those can be mistaken for 7n+3. The multiply
// also makes the element stride visible -- a view built with the wrong stride returns
// values from the wrong element, which shows up as the wrong arithmetic progression
// rather than as plausible-looking numbers.
//
// Two buffers because the clustered path needs two kinds and they are not the same
// resource under D3D11. The light list is a StructuredBuffer of 48-byte records; the
// cluster grid is an array of 32-bit words, and only the second kind can be cleared with
// ClearUnorderedAccessViewUint. Testing one would leave the other's whole creation and
// binding path unexercised until something depended on it.
#include "shadermodel.hlsli"

RWStructuredBuffer<uint> SelfTestStructured : register(u0);
RWBuffer<uint>           SelfTestTyped      : register(u1);

// 64 is one wavefront on AMD and two warps on NVIDIA, which is the ordinary choice; the
// CPU side divides the element count by exactly this number, so the two have to agree.
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    // No bounds check, and the CPU side is what makes that safe: it dispatches exactly
    // element_count / 64 groups and the count is a multiple of 64. If that ever stops
    // being true this writes past the end, which D3D11 discards -- so the failure would
    // be silent here and loud in the compare.
    const uint value = id.x * 7 + 3;
    SelfTestStructured[id.x] = value;
    SelfTestTyped[id.x] = value;
}
