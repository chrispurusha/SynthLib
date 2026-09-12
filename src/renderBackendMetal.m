/*
 * SynthLib - common library for synthesizer editor applications.
 *
 * Copyright (C) 2026 Chris Turner <chris_purusha@icloud.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
// Notes: Docs/code-notes/renderBackendMetal.m.md - "// notes §k" refers there.

#include "renderBackendSelect.h"

// PLATFORM, NOT PREFERENCE. This used to be guarded on which backend was chosen at compile time;
// the choice is made at start-up now, so both are always built and the only thing that can exclude
// this file is being on a platform without Metal — which is every platform except Apple's.
#ifdef __APPLE__

// notes §1

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>    // implicit layer animations are turned off around geometry
#import <AppKit/AppKit.h>

#include <stdlib.h>
#include <string.h>

#include "synthlibDefs.h"
#include "renderBackend.h"

// notes §2
static NSString * const kShaderSource =
    @"#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "\n"
    "struct Vertex {\n"
    "    float2 pos;\n"
    "    float2 uv;\n"
    "    float4 colour;\n"
    "};\n"
    "\n"
    "struct Varying {\n"
    "    float4 position [[position]];\n"
    "    float2 uv;\n"
    "    float4 colour;\n"
    "};\n"
    "\n"
    "vertex Varying synthlib_vertex(uint vid [[vertex_id]],\n"
    "                               const device Vertex * verts [[buffer(0)]],\n"
    "                               constant float4 & xform [[buffer(1)]]) {\n"
    "    Varying out;\n"
    "    float2 p = verts[vid].pos;\n"
    "    // Framebuffer pixels, origin top left, to clip space, y up. ONE MULTIPLY-ADD PER AXIS,\n"
    "    // with the scale and bias computed on the CPU, because that is exactly what the\n"
    "    // fixed-function glOrtho matrix in the OpenGL backend does. Dividing here instead\n"
    "    // rounds twice, and in a different order, which moves the occasional edge pixel.\n"
    "    out.position = float4((p.x * xform.x) + xform.z,\n"
    "                          (p.y * xform.y) + xform.w,\n"
    "                          0.0, 1.0);\n"
    "    out.uv     = verts[vid].uv;\n"
    "    out.colour = verts[vid].colour;\n"
    "    return out;\n"
    "}\n"
    "\n"
    "fragment float4 synthlib_fragment(Varying in [[stage_in]],\n"
    "                                  texture2d<float> tex [[texture(0)]],\n"
    "                                  sampler smp [[sampler(0)]]) {\n"
    "    return in.colour * tex.sample(smp, in.uv);\n"
    "}\n";

// ── State ───────────────────────────────────────────────────────────────────

// notes §3
#if !__has_feature(objc_arc)
#error "renderBackendMetal.m must be compiled with -fobjc-arc"
#endif

static id<MTLDevice>              gDevice      = nil;
static id<MTLCommandQueue>        gQueue       = nil;
static id<MTLRenderPipelineState> gPipeline    = nil;
static id<MTLSamplerState>        gSampler     = nil;   // nearest
static id<MTLSamplerState>        gSamplerLin  = nil;   // linear, for the supersampled text atlas
static id<MTLTexture>             gWhite       = nil;   // 1x1 opaque white, for untextured draws

#define MAX_TEXTURES    (64)
static id<MTLTexture>  gTextures[MAX_TEXTURES]      = {nil};

// notes §4
#define MAX_METAL_WINDOWS    (8)

typedef struct {
    void *                             native;   // the NSView or NSWindow this belongs to
    CAMetalLayer *                     layer;
    id<MTLTexture>                     target;
    id<MTLTexture>                     msaaTarget;
    int                                surfaceWidth;
    int                                surfaceHeight;
    bool                               presentInTransaction;
    id<MTLCommandBuffer>               commands;
    id<MTLRenderCommandEncoder>        encoder;
    bool                               scissorOn;
    int                                scissorX;
    int                                scissorY;
    int                                scissorW;
    int                                scissorH;
} tMetalWindow;

static tMetalWindow   gWindows[MAX_METAL_WINDOWS];

// ALWAYS VALID, never NULL. It starts pointing at an empty slot whose layer is nil, so every
// existing "if (gLayer == nil) return;" guard in this file keeps working untouched before anything
// has been attached.
static tMetalWindow * gW = &gWindows[0];

#define gLayer                  (gW->layer)
#define gTarget                 (gW->target)
#define gMsaaTarget             (gW->msaaTarget)
#define gSurfaceWidth           (gW->surfaceWidth)
#define gSurfaceHeight          (gW->surfaceHeight)
#define gPresentInTransaction   (gW->presentInTransaction)
#define gCommands               (gW->commands)
#define gEncoder                (gW->encoder)
#define gScissorOn              (gW->scissorOn)
#define gScissorX               (gW->scissorX)
#define gScissorY               (gW->scissorY)
#define gScissorW               (gW->scissorW)
#define gScissorH               (gW->scissorH)

static tTextureFilter  gTextureFilter[MAX_TEXTURES] = {eTextureNearest};

// Forward declaration: mtl_texture_alloc() fills a new texture through this, and now that the
// entry points are file-local there is no header declaring them.
static void mtl_texture_write(uint32_t texture, int x, int y, int width, int height, const uint8_t * rgba);

static void metal_end_pass(void) {
    if (gEncoder != nil) {
        [gEncoder endEncoding];
        gEncoder = nil;
    }
}

// Commits whatever has been encoded and waits for the GPU. Only the read-back needs to wait; the
// window layer, when it exists, will present without one.
static void metal_commit_and_wait(void) {
    metal_end_pass();

    if (gCommands != nil) {
        [gCommands commit];
        [gCommands waitUntilCompleted];
        gCommands = nil;
    }
}

static void metal_apply_scissor(void) {
    if (gEncoder == nil) {
        return;
    }

    if (!gScissorOn) {
        [gEncoder setScissorRect:(MTLScissorRect){0, 0, (NSUInteger)gSurfaceWidth, (NSUInteger)gSurfaceHeight}];
        return;
    }
    // notes §5
    int lx = gScissorX;
    int ly = gScissorY;
    int rx = gScissorX + gScissorW;
    int ry = gScissorY + gScissorH;

    if (lx < 0) { lx = 0; }
    if (ly < 0) { ly = 0; }
    if (rx > gSurfaceWidth)  { rx = gSurfaceWidth; }
    if (ry > gSurfaceHeight) { ry = gSurfaceHeight; }
    if (rx < lx) { rx = lx; }
    if (ry < ly) { ry = ly; }

    [gEncoder setScissorRect:(MTLScissorRect){(NSUInteger)lx, (NSUInteger)ly,
                                              (NSUInteger)(rx - lx), (NSUInteger)(ry - ly)}];
}

// Opens a render pass if none is open. `clearColour` non-NULL clears, otherwise the existing
// contents are kept — which is what makes mtl_submit() able to add to a frame mtl_clear() started.
static void metal_begin_pass(const tRgb * clearColour) {
    if (gEncoder != nil) {
        return;
    }

    if ((gTarget == nil) || ((GFX_MSAA_SAMPLES > 1) && (gMsaaTarget == nil))) {
        return;
    }

    if (gCommands == nil) {
        gCommands = [gQueue commandBuffer];
    }
    MTLRenderPassDescriptor * pass = [MTLRenderPassDescriptor renderPassDescriptor];

#if GFX_MSAA_SAMPLES > 1
    // notes §6
    pass.colorAttachments[0].texture        = gMsaaTarget;
    pass.colorAttachments[0].resolveTexture = gTarget;
    pass.colorAttachments[0].storeAction    = MTLStoreActionStoreAndMultisampleResolve;
#else
    pass.colorAttachments[0].texture     = gTarget;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
#endif

    if (clearColour != NULL) {
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(clearColour->red, clearColour->green,
                                                                clearColour->blue, 1.0);
    } else {
        pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
    }
    gEncoder = [gCommands renderCommandEncoderWithDescriptor:pass];
    [gEncoder setRenderPipelineState:gPipeline];
    [gEncoder setFragmentSamplerState:gSampler atIndex:0];
    metal_apply_scissor();
}

static void mtl_init(void) {
    if (gDevice != nil) {
        return;
    }
    gDevice = MTLCreateSystemDefaultDevice();

    if (gDevice == nil) {
        LOG_ERROR("Metal: no device\n");
        return;
    }
    gQueue = [gDevice newCommandQueue];

    NSError *      error   = nil;
    id<MTLLibrary> library = [gDevice newLibraryWithSource:kShaderSource options:nil error:&error];

    if (library == nil) {
        LOG_ERROR("Metal: shader compile failed: %s\n", [[error localizedDescription] UTF8String]);
        return;
    }
    MTLRenderPipelineDescriptor * desc = [[MTLRenderPipelineDescriptor alloc] init];

    desc.vertexFunction   = [library newFunctionWithName:@"synthlib_vertex"];
    desc.fragmentFunction = [library newFunctionWithName:@"synthlib_fragment"];

    // BGRA8Unorm because that is what a CAMetalLayer drawable is, and the window layer that comes
    // later should not have to convert. Read-back swizzles instead — it happens once a screenshot.
    desc.colorAttachments[0].pixelFormat                 = MTLPixelFormatBGRA8Unorm;

#if GFX_MSAA_SAMPLES > 1
    // The pipeline's sample count must match the render pass's attachment, or every draw is a
    // validation failure. See renderBackend.h for what multisampling does and does not fix here.
    desc.rasterSampleCount                               = GFX_MSAA_SAMPLES;
#endif

    // The session-wide blend that mtl_init() promises: straight (non-premultiplied) source alpha,
    // exactly GL's glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA). There is no depth attachment
    // at all, which is this backend's way of saying what glDisable(GL_DEPTH_TEST) said.
    desc.colorAttachments[0].blendingEnabled             = YES;
    desc.colorAttachments[0].rgbBlendOperation           = MTLBlendOperationAdd;
    desc.colorAttachments[0].alphaBlendOperation         = MTLBlendOperationAdd;
    desc.colorAttachments[0].sourceRGBBlendFactor        = MTLBlendFactorSourceAlpha;
    desc.colorAttachments[0].sourceAlphaBlendFactor      = MTLBlendFactorSourceAlpha;
    desc.colorAttachments[0].destinationRGBBlendFactor   = MTLBlendFactorOneMinusSourceAlpha;
    desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    gPipeline = [gDevice newRenderPipelineStateWithDescriptor:desc error:&error];

    if (gPipeline == nil) {
        LOG_ERROR("Metal: pipeline failed: %s\n", [[error localizedDescription] UTF8String]);
        return;
    }
    MTLSamplerDescriptor * samp = [[MTLSamplerDescriptor alloc] init];

    // Nearest and clamp, matching the GL backend's texture parameters — see utilsGraphics.h for
    // why every texture in this codebase wants exactly that.
    samp.minFilter    = MTLSamplerMinMagFilterNearest;
    samp.magFilter    = MTLSamplerMinMagFilterNearest;
    samp.sAddressMode = MTLSamplerAddressModeClampToEdge;
    samp.tAddressMode = MTLSamplerAddressModeClampToEdge;
    gSampler          = [gDevice newSamplerStateWithDescriptor:samp];

    samp.minFilter    = MTLSamplerMinMagFilterLinear;
    samp.magFilter    = MTLSamplerMinMagFilterLinear;
    gSamplerLin       = [gDevice newSamplerStateWithDescriptor:samp];

    // The 1x1 white texture that makes untextured drawing need no second pipeline.
    MTLTextureDescriptor * whiteDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:1 height:1 mipmapped:NO];

    whiteDesc.usage       = MTLTextureUsageShaderRead;
    whiteDesc.storageMode = MTLStorageModeManaged;
    gWhite                = [gDevice newTextureWithDescriptor:whiteDesc];

    const uint8_t white[4] = {0xFF, 0xFF, 0xFF, 0xFF};

    [gWhite replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:white bytesPerRow:4];
}

static void mtl_set_surface(int width, int height) {
    mtl_init();

    if ((gDevice == nil) || (width <= 0) || (height <= 0)) {
        return;
    }

    if ((width == gSurfaceWidth) && (height == gSurfaceHeight) && (gTarget != nil)) {
        return;
    }
    // Anything encoded against the old size has to go before the target it names is replaced.
    metal_commit_and_wait();

    gSurfaceWidth  = width;
    gSurfaceHeight = height;

    MTLTextureDescriptor * desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:(NSUInteger)width
                                                          height:(NSUInteger)height
                                                       mipmapped:NO];

    desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;

    // MANAGED, not Shared. A Shared render target is not allowed on Intel macOS, and this ships
    // universal — so Managed plus an explicit synchronizeResource before the read-back, which
    // works on both architectures.
    desc.storageMode = MTLStorageModeManaged;
    gTarget          = [gDevice newTextureWithDescriptor:desc];

#if GFX_MSAA_SAMPLES > 1
    // A SECOND TARGET, because a multisampled texture is not readable and not presentable: it is
    // rendered into and then RESOLVED down into gTarget, which is the one mtl_read_pixels_rgb()
    // reads and mtl_present() blits. Private storage — nothing on the CPU ever touches it.
    MTLTextureDescriptor * msaa =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:(NSUInteger)width
                                                          height:(NSUInteger)height
                                                       mipmapped:NO];

    msaa.textureType = MTLTextureType2DMultisample;
    msaa.sampleCount = GFX_MSAA_SAMPLES;
    msaa.usage       = MTLTextureUsageRenderTarget;
    msaa.storageMode = MTLStorageModePrivate;
    gMsaaTarget      = [gDevice newTextureWithDescriptor:msaa];
#endif

    // The drawable has to match the target exactly, because mtl_present() blits one to the other
    // and a size mismatch is a validation failure rather than a scaled copy.
    if (gLayer != nil) {
        // notes §7
        [CATransaction begin];
        [CATransaction setDisableActions:YES];

        gLayer.drawableSize = CGSizeMake((CGFloat)width, (CGFloat)height);

        // notes §8
        if (gLayer.superlayer != nil) {
            gLayer.frame = CGRectMake(0.0, 0.0,
                                      (CGFloat)width / gLayer.contentsScale,
                                      (CGFloat)height / gLayer.contentsScale);
        }
        [CATransaction commit];
    }

    // There is no projection matrix to set: the vertex shader is handed the surface size and does
    // the mapping itself, so this is the whole of what glViewport + glOrtho did.
}

static void mtl_clear(tRgb colour) {
    if (gDevice == nil) {
        mtl_init();
    }
    // notes §9
    metal_end_pass();

    if (gCommands != nil) {
        [gCommands commit];
        gCommands = nil;
    }
    metal_begin_pass(&colour);
}

static void mtl_submit(const tVertex * verts, size_t count, uint32_t texture) {
    if ((verts == NULL) || (count == 0) || (gDevice == nil)) {
        return;
    }
    metal_begin_pass(NULL);

    if (gEncoder == nil) {
        return;
    }
    // notes §10
    size_t bytes = count * sizeof(tVertex);

    if (bytes <= 4096u) {
        [gEncoder setVertexBytes:verts length:bytes atIndex:0];
    } else {
        id<MTLBuffer> buffer = [gDevice newBufferWithBytes:verts
                                                    length:bytes
                                                   options:MTLResourceStorageModeShared];

        [gEncoder setVertexBuffer:buffer offset:0 atIndex:0];
    }

    // Exactly the matrix glOrtho(0, w, h, 0, -1, 1) builds: x scaled by 2/w and biased by -1,
    // y scaled by -2/h and biased by +1. Computed here, in float, so the shader performs the same
    // single multiply-add per axis that the fixed-function pipeline does, with the same roundings.
    float         xform[4] = {
        2.0f / (float)gSurfaceWidth,
        -2.0f / (float)gSurfaceHeight,
        -1.0f,
        1.0f
    };

    [gEncoder setVertexBytes:xform length:sizeof(xform) atIndex:1];

    id<MTLTexture> bound = gWhite;
    tTextureFilter filter = eTextureNearest;

    if ((texture != 0) && (texture < MAX_TEXTURES) && (gTextures[texture] != nil)) {
        bound  = gTextures[texture];
        filter = gTextureFilter[texture];
    }
    [gEncoder setFragmentTexture:bound atIndex:0];
    [gEncoder setFragmentSamplerState:((filter == eTextureLinear) ? gSamplerLin : gSampler) atIndex:0];

    [gEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:count];
}

static void mtl_scissor(int x, int y, int width, int height) {
    if (width < 0) {
        gScissorOn = false;
    } else {
        gScissorOn = true;
        gScissorX  = x;
        gScissorY  = y;
        gScissorW  = width;
        gScissorH  = height;
    }
    metal_apply_scissor();
}

static bool mtl_read_pixels_rgb(int x, int y, int width, int height, uint8_t * out) {
    if ((width <= 0) || (height <= 0) || (out == NULL) || (gTarget == nil)) {
        return false;
    }
    metal_end_pass();

    // Managed storage: the CPU copy is stale until the GPU's writes are synchronized down to it.
    if (gCommands == nil) {
        gCommands = [gQueue commandBuffer];
    }
    id<MTLBlitCommandEncoder> blit = [gCommands blitCommandEncoder];

    [blit synchronizeResource:gTarget];
    [blit endEncoding];

    metal_commit_and_wait();

    size_t    rowBytes = (size_t)gSurfaceWidth * 4;
    uint8_t * frame    = (uint8_t *)malloc(rowBytes * (size_t)gSurfaceHeight);

    if (frame == NULL) {
        return false;
    }
    [gTarget getBytes:frame
          bytesPerRow:rowBytes
           fromRegion:MTLRegionMake2D(0, 0, (NSUInteger)gSurfaceWidth, (NSUInteger)gSurfaceHeight)
          mipmapLevel:0];

    // notes §11
    for (int row = 0; row < height; row++) {
        int             srcRow = (gSurfaceHeight - 1) - (y + row);

        if ((srcRow < 0) || (srcRow >= gSurfaceHeight)) {
            continue;
        }
        const uint8_t * src    = frame + ((size_t)srcRow * rowBytes) + ((size_t)x * 4);
        uint8_t *       dst    = out + ((size_t)row * (size_t)width * 3);

        for (int col = 0; col < width; col++) {
            dst[(col * 3) + 0] = src[(col * 4) + 2];   // R, from BGRA
            dst[(col * 3) + 1] = src[(col * 4) + 1];   // G
            dst[(col * 3) + 2] = src[(col * 4) + 0];   // B
        }
    }

    free(frame);
    return true;
}

static uint32_t mtl_texture_alloc(int width, int height, const uint8_t * rgba, tTextureFilter filter) {
    mtl_init();

    if ((gDevice == nil) || (width <= 0) || (height <= 0)) {
        return 0;
    }
    uint32_t slot = 0;

    for (uint32_t i = 1; i < MAX_TEXTURES; i++) {
        if (gTextures[i] == nil) {
            slot = i;
            break;
        }
    }

    if (slot == 0) {
        LOG_ERROR("Metal: texture table full\n");
        return 0;
    }
    MTLTextureDescriptor * desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:(NSUInteger)width
                                                          height:(NSUInteger)height
                                                       mipmapped:NO];

    desc.usage        = MTLTextureUsageShaderRead;
    desc.storageMode  = MTLStorageModeManaged;
    gTextures[slot]   = [gDevice newTextureWithDescriptor:desc];

    if (gTextures[slot] == nil) {
        return 0;
    }

    gTextureFilter[slot] = filter;

    if (rgba != NULL) {
        mtl_texture_write(slot, 0, 0, width, height, rgba);
    }
    return slot;
}

static void mtl_texture_write(uint32_t texture, int x, int y, int width, int height, const uint8_t * rgba) {
    if ((texture == 0) || (texture >= MAX_TEXTURES) || (gTextures[texture] == nil)
       || (width <= 0) || (height <= 0) || (rgba == NULL)) {
        return;
    }
    // The caller's bytes are RGBA — the glyph atlas and the LCD both build them that way for
    // glTexImage2D — and the texture is BGRA, so the pixels are swapped on the way in. Doing it
    // here rather than at every caller is what keeps the two backends interchangeable.
    size_t    rowBytes = (size_t)width * 4;
    uint8_t * swapped  = (uint8_t *)malloc(rowBytes * (size_t)height);

    if (swapped == NULL) {
        return;
    }

    for (size_t i = 0; i < ((size_t)width * (size_t)height); i++) {
        swapped[(i * 4) + 0] = rgba[(i * 4) + 2];   // B
        swapped[(i * 4) + 1] = rgba[(i * 4) + 1];   // G
        swapped[(i * 4) + 2] = rgba[(i * 4) + 0];   // R
        swapped[(i * 4) + 3] = rgba[(i * 4) + 3];   // A
    }

    [gTextures[texture] replaceRegion:MTLRegionMake2D((NSUInteger)x, (NSUInteger)y,
                                                      (NSUInteger)width, (NSUInteger)height)
                          mipmapLevel:0
                            withBytes:swapped
                          bytesPerRow:rowBytes];
    free(swapped);
}

// Find the context for a native window, or NULL. Slots are matched on the native pointer, which is
// the only stable identity a window has here.
static tMetalWindow * metal_window_for(void * nativeWindow) {
    for (int i = 0; i < MAX_METAL_WINDOWS; i++) {
        if (gWindows[i].native == nativeWindow) {
            return &gWindows[i];
        }
    }

    return NULL;
}

// Is this slot still describing the window it was built for? Compares the LAYER rather than the
// address, because the address is what cannot be trusted — see the call site in mtl_attach_window().
static bool metal_slot_still_live(tMetalWindow * slot, void * nativeWindow) {
    if ((slot == NULL) || (slot->layer == nil)) {
        return false;
    }
    id object = (__bridge id)nativeWindow;

    if ([object isKindOfClass:[NSWindow class]]) {
        NSView * content = [(NSWindow *)object contentView];

        return (content != nil) && (content.layer == slot->layer);
    }

    if ([object isKindOfClass:[NSView class]]) {
        return (slot->layer.superlayer != nil) && (slot->layer.superlayer == ((NSView *)object).layer);
    }

    return false;
}

// Release everything a window owns. Called when its view goes away - without it a host that opens
// and closes editors would exhaust the slots, since each new view is a different pointer.
static void mtl_detach_window(void * nativeWindow) {
    tMetalWindow * window = metal_window_for(nativeWindow);

    if ((window == NULL) || (nativeWindow == NULL)) {
        return;
    }

    window->layer      = nil;
    window->target     = nil;
    window->msaaTarget = nil;
    window->commands   = nil;
    window->encoder    = nil;
    window->native     = NULL;

    window->surfaceWidth  = 0;
    window->surfaceHeight = 0;

    // If the window being torn down was the current one, fall back to any other attached window,
    // and to the empty slot 0 if there is none. gW must never dangle.
    if (gW == window) {
        gW = &gWindows[0];

        for (int i = 0; i < MAX_METAL_WINDOWS; i++) {
            if (gWindows[i].native != NULL) {
                gW = &gWindows[i];
                break;
            }
        }
    }
}

static void mtl_attach_window(void * nativeWindow) {
    mtl_init();

    if ((gDevice == nil) || (nativeWindow == NULL)) {
        return;
    }

    // ALREADY KNOWN MEANS SELECT IT, NOT REBUILD IT. Each open editor calls this before it draws,
    // so this is the hot path for switching between two of them; making a fresh CAMetalLayer every
    // time would be both wasteful and wrong, since the old one is still on screen.
    tMetalWindow * existing = metal_window_for(nativeWindow);

    if (existing != NULL) {
        // notes §12
        if (metal_slot_still_live(existing, nativeWindow)) {
            gW = existing;
            return;
        }

        mtl_detach_window(nativeWindow);
    }

    tMetalWindow * slot = metal_window_for(NULL);

    if (slot == NULL) {
        // notes §13
        gW = &gWindows[0];
        return;
    }

    gW         = slot;
    gW->native = nativeWindow;
    // notes §14
    id       object = (__bridge id)nativeWindow;
    NSView * view   = nil;
    NSWindow * window = nil;

    if ([object isKindOfClass:[NSWindow class]]) {
        window = (NSWindow *)object;
        view   = [window contentView];
    } else if ([object isKindOfClass:[NSView class]]) {
        view   = (NSView *)object;
        window = [view window];
    } else {
        return;
    }

    gLayer                  = [CAMetalLayer layer];
    gLayer.device           = gDevice;
    gLayer.pixelFormat      = MTLPixelFormatBGRA8Unorm;

    // NOT framebuffer-only: mtl_present() blits INTO the drawable rather than rendering into it,
    // and a framebuffer-only drawable cannot be a blit destination.
    gLayer.framebufferOnly   = NO;

    // notes §15
    gLayer.opaque            = YES;

    // notes §16
    gLayer.colorspace        = nil;

    // notes §17
    gLayer.displaySyncEnabled = YES;

    // notes §18
    gPresentInTransaction    = (window == nil) || ![object isKindOfClass:[NSWindow class]];
    gLayer.presentsWithTransaction = gPresentInTransaction;

    // notes §19
    if (window != nil && [object isKindOfClass:[NSWindow class]]) {
        view.layer      = gLayer;
        view.wantsLayer = YES;
    } else {
        view.wantsLayer = YES;
        [view.layer addSublayer:gLayer];
    }

    // notes §20
    [CATransaction begin];
    [CATransaction setDisableActions:YES];    // as in mtl_set_surface: geometry must not animate

    gLayer.contentsScale     = (window != nil) ? [window backingScaleFactor]
                               : [[NSScreen mainScreen] backingScaleFactor];

    // The layer fills the view. A layer-HOSTING view does not lay its layer out for you, and a
    // plug-in view is resized by its host, so this is set again from mtl_set_surface().
    gLayer.frame             = [view bounds];

    if ((gSurfaceWidth > 0) && (gSurfaceHeight > 0)) {
        gLayer.drawableSize = CGSizeMake((CGFloat)gSurfaceWidth, (CGFloat)gSurfaceHeight);
    }
    [CATransaction commit];
}

static void mtl_present(void) {
    if ((gLayer == nil) || (gTarget == nil)) {
        return;    // Offscreen-only build, or before the window exists: the frame simply stays put.
    }
    metal_end_pass();

    id<CAMetalDrawable> drawable = [gLayer nextDrawable];

    if (drawable == nil) {
        return;    // The layer had none free; dropping a frame is the right answer, not stalling.
    }

    if (gCommands == nil) {
        gCommands = [gQueue commandBuffer];
    }
    // notes §21
    id<MTLBlitCommandEncoder> blit = [gCommands blitCommandEncoder];

    [blit copyFromTexture:gTarget
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake((NSUInteger)gSurfaceWidth, (NSUInteger)gSurfaceHeight, 1)
                toTexture:[drawable texture]
         destinationSlice:0
         destinationLevel:0
        destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blit endEncoding];

    if (gPresentInTransaction) {
        // Committed first, then presented by hand once the GPU work is scheduled, so the frame
        // becomes visible as part of the same Core Animation transaction AppKit is running.
        [gCommands commit];
        [gCommands waitUntilScheduled];
        [drawable present];
    } else {
        [gCommands presentDrawable:drawable];
        [gCommands commit];
    }
    gCommands = nil;
}

static void mtl_texture_free(uint32_t texture) {
    if ((texture == 0) || (texture >= MAX_TEXTURES)) {
        return;
    }
    gTextures[texture]     = nil;
    gTextureFilter[texture] = eTextureNearest;
}

// The table — the whole of what this file offers. Everything above it is static.
static const tGfxBackend kMetalBackend = {
    .name            = "Metal",
    .init            = mtl_init,
    .set_surface     = mtl_set_surface,
    .clear           = mtl_clear,
    .submit          = mtl_submit,
    .scissor         = mtl_scissor,
    .read_pixels_rgb = mtl_read_pixels_rgb,
    .texture_alloc   = mtl_texture_alloc,
    .texture_write   = mtl_texture_write,
    .texture_free    = mtl_texture_free,
    .attach_window   = mtl_attach_window,
    .detach_window   = mtl_detach_window,
    .present         = mtl_present,
};

const tGfxBackend * gfx_backend_metal_table(void) {
    return &kMetalBackend;
}

#endif // __APPLE__
