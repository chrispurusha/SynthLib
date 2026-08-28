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

#include "renderBackendSelect.h"

#if RENDER_BACKEND == RENDER_BACKEND_METAL

// ── The Metal backend ───────────────────────────────────────────────────────────────────────────
//
// The same nine gfx_* calls renderBackendGL.c implements, on Metal. Nothing above this file knows
// which one it is talking to.
//
// IT RENDERS OFFSCREEN, and that is deliberate rather than a stepping stone left half-built. The
// frame is drawn into a texture this file owns; a window layer will later blit that texture to a
// CAMetalLayer drawable and present it. Two reasons it is built this way round:
//   - gfx_read_pixels_rgb() — the backdoor SCREENSHOT, which is how every rendering change in this
//     project gets proved — cannot read a drawable back reliably once it has been presented. An
//     offscreen target makes the read trivial and keeps the verification method intact.
//   - it means the DRAWING can be proved correct against the OpenGL backend, on the same machine,
//     with the same patch, before any windowing code exists to argue with.
// So a Metal build today renders correct frames and PRESENTS NOTHING: the window stays blank while
// SCREENSHOT returns exactly what the GL build returns. That is the intended intermediate state.
//
// THE THREE COORDINATE FACTS, which is where this normally goes wrong:
//   - Metal's clip space has y going UP, tVertex has y going DOWN. The vertex shader flips it —
//     the job glOrtho(0, w, h, 0, ...) was doing in the GL backend.
//   - Metal's SCISSOR and viewport origin is the TOP LEFT, which is already the tVertex origin, so
//     gfx_scissor() needs no flip here at all. The GL backend needs one. Getting both right at the
//     same time is the trap: they are opposite, in the same file position, in the two backends.
//   - a scissor rect that leaves the render target is a Metal VALIDATION FAILURE, where GL quietly
//     clamped it. So it is clamped here, explicitly.

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <AppKit/AppKit.h>

#include <stdlib.h>
#include <string.h>

#include "synthlibDefs.h"
#include "renderBackend.h"

// ── Shaders ─────────────────────────────────────────────────────────────────
//
// Compiled at RUNTIME from this string rather than built into a .metallib. A .metallib would mean
// a build step in do-vst3 (which is a shell script, not an Xcode target) and a bundle-resource
// lookup that differs between the application and a plug-in a host may have sandboxed. A string
// costs a few tens of milliseconds once, at start-up, and behaves identically in both.
//
// The fragment shader is GL_MODULATE: vertex colour times texture sample. Untextured geometry is
// drawn with a 1x1 opaque white texture bound, so one pipeline state covers both cases and there
// is no branch — white times anything is anything.
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

static id<MTLDevice>              gDevice      = nil;
static id<MTLCommandQueue>        gQueue       = nil;
static id<MTLRenderPipelineState> gPipeline    = nil;
static id<MTLSamplerState>        gSampler     = nil;
static id<MTLTexture>             gWhite       = nil;   // 1x1 opaque white, for untextured draws
static id<MTLTexture>             gTarget      = nil;   // the offscreen frame
static CAMetalLayer *             gLayer       = nil;   // nil until gfx_attach_window()
static id<MTLCommandBuffer>       gCommands    = nil;
static id<MTLRenderCommandEncoder> gEncoder    = nil;

static int    gSurfaceWidth  = 0;
static int    gSurfaceHeight = 0;

// The scissor, remembered because it can be set while no encoder is open (module_pane_clip_begin()
// flushes first, which ends the pass) and has to be reapplied when the next one opens.
static bool gScissorOn = false;
static int  gScissorX  = 0;
static int  gScissorY  = 0;
static int  gScissorW  = 0;
static int  gScissorH  = 0;

// The texture table. A handle is an INDEX, not a pointer: renderBackend.h promises callers a
// uint32_t, and an id<MTLTexture> does not fit in one. Slot 0 is never handed out so that 0 can
// keep meaning "no texture". Freed slots go back to nil and are reused.
#define MAX_TEXTURES    (64)
static id<MTLTexture> gTextures[MAX_TEXTURES] = {nil};

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
    // NO Y FLIP: Metal's scissor origin is the top left, which is already the caller's origin, and
    // the rectangle arrives in whole pixels. So this is the caller's rectangle, clamped and nothing
    // else — where the GL backend has to flip it.
    //
    // CLAMPED, and that part is not optional: a scissor rect that leaves the render target is a
    // VALIDATION FAILURE in Metal, where GL clamps it silently.
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
// contents are kept — which is what makes gfx_submit() able to add to a frame gfx_clear() started.
static void metal_begin_pass(const tRgb * clearColour) {
    if (gEncoder != nil) {
        return;
    }

    if (gTarget == nil) {
        return;
    }

    if (gCommands == nil) {
        gCommands = [gQueue commandBuffer];
    }
    MTLRenderPassDescriptor * pass = [MTLRenderPassDescriptor renderPassDescriptor];

    pass.colorAttachments[0].texture     = gTarget;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;

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

void gfx_init(void) {
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

    // The session-wide blend that gfx_init() promises: straight (non-premultiplied) source alpha,
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

void gfx_set_surface(int width, int height) {
    gfx_init();

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

    // The drawable has to match the target exactly, because gfx_present() blits one to the other
    // and a size mismatch is a validation failure rather than a scaled copy.
    if (gLayer != nil) {
        gLayer.drawableSize = CGSizeMake((CGFloat)width, (CGFloat)height);

        // THE LAYER'S SIZE IS DERIVED FROM THE DRAWABLE, not from the view, and that is the whole
        // of a bug worth remembering. A host asked for a 1367x768 editor and the view ended up
        // 768.9 POINTS tall; convertRectToBacking then truncates 1537.8 pixels to 1537, which is
        // what gets rendered. Leave the layer at the view's 768.9pt and Core Animation scales a
        // 1537-pixel frame into a 1537.8-pixel box — a 0.9995 resample that never looks broken,
        // just faintly soft, and which showed up as 6.5% of pixels differing from the OpenGL build
        // with the text quietly blurred.
        //
        // Sizing the layer at drawable/contentsScale makes the mapping exactly 1:1 again. It can
        // leave the layer a fraction of a point short of the view, which is invisible; a resampled
        // frame is not.
        if (gLayer.superlayer != nil) {
            gLayer.frame = CGRectMake(0.0, 0.0,
                                      (CGFloat)width / gLayer.contentsScale,
                                      (CGFloat)height / gLayer.contentsScale);
        }
    }

    // There is no projection matrix to set: the vertex shader is handed the surface size and does
    // the mapping itself, so this is the whole of what glViewport + glOrtho did.
}

void gfx_clear(tRgb colour) {
    if (gDevice == nil) {
        gfx_init();
    }
    // A clear starts a frame, so the PREVIOUS frame is finished and can go to the GPU. Committing
    // here rather than waiting for a read-back is what keeps one command buffer per frame: without
    // it every frame since the last SCREENSHOT accumulates into one buffer, holding on to a vertex
    // buffer per submission for as long as the app runs between captures.
    //
    // Committed, not waited on. Only the read-back needs the GPU to have finished.
    metal_end_pass();

    if (gCommands != nil) {
        [gCommands commit];
        gCommands = nil;
    }
    metal_begin_pass(&colour);
}

void gfx_submit(const tVertex * verts, size_t count, uint32_t texture) {
    if ((verts == NULL) || (count == 0) || (gDevice == nil)) {
        return;
    }
    metal_begin_pass(NULL);

    if (gEncoder == nil) {
        return;
    }
    // A fresh buffer per submission. There are a few dozen submissions in a frame, so this is not
    // yet worth a ring buffer — and a ring buffer would need fences to know when the GPU is done
    // with a region. Revisit if a profile ever says to.
    id<MTLBuffer> buffer = [gDevice newBufferWithBytes:verts
                                                length:(count * sizeof(tVertex))
                                               options:MTLResourceStorageModeShared];

    // Exactly the matrix glOrtho(0, w, h, 0, -1, 1) builds: x scaled by 2/w and biased by -1,
    // y scaled by -2/h and biased by +1. Computed here, in float, so the shader performs the same
    // single multiply-add per axis that the fixed-function pipeline does, with the same roundings.
    float         xform[4] = {
        2.0f / (float)gSurfaceWidth,
        -2.0f / (float)gSurfaceHeight,
        -1.0f,
        1.0f
    };

    [gEncoder setVertexBuffer:buffer offset:0 atIndex:0];
    [gEncoder setVertexBytes:xform length:sizeof(xform) atIndex:1];

    id<MTLTexture> bound = gWhite;

    if ((texture != 0) && (texture < MAX_TEXTURES) && (gTextures[texture] != nil)) {
        bound = gTextures[texture];
    }
    [gEncoder setFragmentTexture:bound atIndex:0];

    [gEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:count];
}

void gfx_scissor(int x, int y, int width, int height) {
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

bool gfx_read_pixels_rgb(int x, int y, int width, int height, uint8_t * out) {
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

    // TWO CONVERSIONS, and both are the contract's doing rather than Metal's. renderBackend.h
    // promises tightly-packed RGB triples with the BOTTOM row first, because that is what
    // glReadPixels gave and what the backdoor's PNG writer expects — so the rows are walked in
    // reverse. And the target is BGRA, so the two ends of each pixel swap.
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

uint32_t gfx_texture_alloc(int width, int height, const uint8_t * rgba) {
    gfx_init();

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

    if (rgba != NULL) {
        gfx_texture_write(slot, 0, 0, width, height, rgba);
    }
    return slot;
}

void gfx_texture_write(uint32_t texture, int x, int y, int width, int height, const uint8_t * rgba) {
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

void gfx_attach_window(void * nativeWindow) {
    gfx_init();

    if ((gDevice == nil) || (nativeWindow == NULL)) {
        return;
    }
    // EITHER AN NSWindow OR AN NSView, and it has to be both because the two callers differ. The
    // application hands over the NSWindow it got from glfwGetCocoaWindow() — GLFW made that window
    // with GLFW_CLIENT_API = GLFW_NO_API, so it has no context and no drawable of its own, which is
    // the whole point. The VST3 plug-in hands over its OWN view, because in a plug-in the window
    // belongs to the host and is emphatically not ours to put a layer on.
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

    // NOT framebuffer-only: gfx_present() blits INTO the drawable rather than rendering into it,
    // and a framebuffer-only drawable cannot be a blit destination.
    gLayer.framebufferOnly   = NO;

    // OPAQUE, and this is not a hint — it is a correctness fix. Blending writes the alpha channel
    // as well as the colour, so wherever a glyph's antialiased edge is drawn with alpha below one
    // the framebuffer's own alpha ends up below one too. Core Animation then composites the frame
    // over whatever is behind the layer, which for a plug-in view is the host's dark background,
    // and every piece of text acquires a dark fringe. Saying the layer is opaque tells Core
    // Animation to ignore the alpha channel it has been handed, which is what OpenGL's drawable
    // did implicitly all along.
    gLayer.opaque            = YES;

    // NO COLOUR MATCHING. A CAMetalLayer is colour-managed by default: Core Animation treats the
    // pixels as being in some source space and converts them for the display. The OpenGL path is
    // not — an NSOpenGLView's values go to the screen as written — so the two disagreed on solid
    // colours by a few counts per channel, RGB_GREEN_ON arriving as (75,179,87) where OpenGL wrote
    // (77,178,77). Not visible, but it is a real difference in what the user sees and it would
    // have made every future screen-level comparison useless.
    //
    // A nil colorspace means "these pixels are already in the display's space, pass them through",
    // which is exactly what OpenGL was doing implicitly.
    gLayer.colorspace        = nil;

    // HOSTING OR SUBLAYER, AND THE CHOICE IS NOT COSMETIC — it decides whether the view is ever
    // asked to draw at all.
    //
    // A LAYER-HOSTING view (assign .layer, then set wantsLayer) tells AppKit the layer's contents
    // ARE the view's contents, so AppKit stops calling -drawRect: entirely. That is right for the
    // application, where the render loop drives every frame and nothing waits to be asked.
    //
    // It is WRONG for the plug-in, whose every redraw is a -setNeedsDisplay: that AppKit turns into
    // a -drawRect:. Made layer-hosting, the plug-in editor drew nothing at all: a blank window, no
    // error, no warning, because the mechanism that would have drawn it had been switched off by
    // the very call meant to enable it. So a view is given the layer as a SUBLAYER and stays
    // layer-BACKED, which keeps -drawRect: coming.
    if (window != nil && [object isKindOfClass:[NSWindow class]]) {
        view.layer      = gLayer;
        view.wantsLayer = YES;
    } else {
        view.wantsLayer = YES;
        [view.layer addSublayer:gLayer];
    }

    // Points to pixels. Without this the drawable is sized in points and every frame is presented
    // at half resolution on a Retina display. A plug-in view may not be in a window yet when the
    // host attaches it, so fall back to the main screen rather than to 1.0 — being wrong by a
    // factor of two is far more visible than being wrong about which display.
    gLayer.contentsScale     = (window != nil) ? [window backingScaleFactor]
                               : [[NSScreen mainScreen] backingScaleFactor];

    // The layer fills the view. A layer-HOSTING view does not lay its layer out for you, and a
    // plug-in view is resized by its host, so this is set again from gfx_set_surface().
    gLayer.frame             = [view bounds];

    if ((gSurfaceWidth > 0) && (gSurfaceHeight > 0)) {
        gLayer.drawableSize = CGSizeMake((CGFloat)gSurfaceWidth, (CGFloat)gSurfaceHeight);
    }
}

void gfx_present(void) {
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
    // A BLIT, not a second render pass. The frame already exists in gTarget — rendering it again
    // into the drawable would mean a full-screen quad, another pipeline and a sampler, to copy
    // pixels that are already correct. The offscreen target is what makes gfx_read_pixels_rgb()
    // work, so it earns its keep twice.
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

    [gCommands presentDrawable:drawable];
    [gCommands commit];
    gCommands = nil;
}

void gfx_texture_free(uint32_t texture) {
    if ((texture == 0) || (texture >= MAX_TEXTURES)) {
        return;
    }
    gTextures[texture] = nil;
}

#endif // RENDER_BACKEND == RENDER_BACKEND_METAL
