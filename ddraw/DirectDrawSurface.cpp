#include "DirectDrawSurface.hpp"
#include "DirectDrawClipper.hpp"
#include "Blitter.hpp"

#include <glrage_util/Logger.hpp>

#include <algorithm>

namespace glrage {
namespace ddraw {

DirectDrawSurface::DirectDrawSurface(DirectDraw& lpDD,
    Renderer& renderer, LPDDSURFACEDESC lpDDSurfaceDesc)
    : m_dd(lpDD)
    , m_renderer(renderer)
    , m_desc(*lpDDSurfaceDesc)
{
    LOG_TRACE("");

    m_dd.AddRef();

    DDSURFACEDESC displayDesc;
    m_dd.GetDisplayMode(&displayDesc);

    // use display size if surface has no defined dimensions
    if (!(m_desc.dwFlags & (DDSD_WIDTH | DDSD_HEIGHT))) {
        m_desc.dwWidth = displayDesc.dwWidth;
        m_desc.dwHeight = displayDesc.dwHeight;
        m_desc.dwFlags |= DDSD_WIDTH | DDSD_HEIGHT;
    }

    // use display pixel format if surface has no defined pixel format
    if (!(m_desc.dwFlags & DDSD_PIXELFORMAT)) {
        m_desc.ddpfPixelFormat = displayDesc.ddpfPixelFormat;
        m_desc.dwFlags |= DDSD_PIXELFORMAT;
    }

    // calculate pitch if surface has no defined pitch
    if (!(m_desc.dwFlags & DDSD_PITCH)) {
        m_desc.lPitch =
            m_desc.dwWidth * (m_desc.ddpfPixelFormat.dwRGBBitCount / 8);
        m_desc.dwFlags |= DDSD_PITCH;
    }

    // allocate surface buffer
    m_buffer.resize(m_desc.lPitch * m_desc.dwHeight, 0);
    m_desc.lpSurface = nullptr;

    // attach back buffer if defined
    if (m_desc.dwFlags & DDSD_BACKBUFFERCOUNT && m_desc.dwBackBufferCount > 0) {
        LOG_INFO("found DDSD_BACKBUFFERCOUNT, creating back buffer");

        DDSURFACEDESC backBufferDesc = m_desc;
        backBufferDesc.ddsCaps.dwCaps |= DDSCAPS_BACKBUFFER | DDSCAPS_FLIP;
        backBufferDesc.ddsCaps.dwCaps &=
            ~(DDSCAPS_FRONTBUFFER | DDSCAPS_VISIBLE);
        backBufferDesc.dwFlags &= ~DDSD_BACKBUFFERCOUNT;
        backBufferDesc.dwBackBufferCount = 0;
        m_backBuffer = new DirectDrawSurface(lpDD, m_renderer, &backBufferDesc);

        m_desc.ddsCaps.dwCaps |=
            DDSCAPS_FRONTBUFFER | DDSCAPS_FLIP | DDSCAPS_VISIBLE;
    }
}

DirectDrawSurface::~DirectDrawSurface()
{
    LOG_TRACE("");

    if (m_backBuffer) {
        m_backBuffer->Release();
        m_backBuffer = nullptr;
    }

    if (m_depthBuffer) {
        m_depthBuffer->Release();
        m_depthBuffer = nullptr;
    }

    m_dd.Release();

    if (m_desc.lpSurface) {
        m_desc.lpSurface = nullptr;
    }
}

/*** IUnknown methods ***/
HRESULT WINAPI DirectDrawSurface::QueryInterface(REFIID riid, LPVOID* ppvObj)
{
    LOG_TRACE("");

    if (IsEqualGUID(riid, IID_IDirectDrawSurface)) {
        *ppvObj = static_cast<IDirectDrawSurface*>(this);
    } else if (IsEqualGUID(riid, IID_IDirectDrawSurface2)) {
        *ppvObj = static_cast<IDirectDrawSurface2*>(this);
    } else {
        return Unknown::QueryInterface(riid, ppvObj);
    }

    Unknown::AddRef();
    return S_OK;
}

ULONG WINAPI DirectDrawSurface::AddRef()
{
    LOG_TRACE("");

    return Unknown::AddRef();
}

ULONG WINAPI DirectDrawSurface::Release()
{
    LOG_TRACE("");

    return Unknown::Release();
}

/*** IDirectDrawSurface methods ***/
HRESULT WINAPI DirectDrawSurface::AddAttachedSurface(
    LPDIRECTDRAWSURFACE lpDDSAttachedSurface)
{
    LOG_TRACE("");

    if (!lpDDSAttachedSurface) {
        return DDERR_INVALIDOBJECT;
    }

    DirectDrawSurface* ps =
        static_cast<DirectDrawSurface*>(lpDDSAttachedSurface);
    DWORD caps = ps->m_desc.ddsCaps.dwCaps;
    if (caps & DDSCAPS_ZBUFFER) {
        m_depthBuffer = ps;
    } else if (caps & DDSCAPS_BACKBUFFER) {
        m_backBuffer = ps;
    } else {
        return DDERR_CANNOTATTACHSURFACE;
    }

    ps->AddRef();
    return DD_OK;
}

HRESULT WINAPI DirectDrawSurface::AddOverlayDirtyRect(LPRECT lpRect)
{
    LOG_TRACE("");

    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DirectDrawSurface::Blt(LPRECT lpDestRect,
    LPDIRECTDRAWSURFACE lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwFlags,
    LPDDBLTFX lpDDBltFx)
{
    LOG_TRACE("");

    // can't blit while locked
    if (m_locked) {
        return DDERR_LOCKEDSURFACES;
    }

    if (lpDDSrcSurface) {
        m_dirty = true;

        DirectDrawSurface* src =
            static_cast<DirectDrawSurface*>(lpDDSrcSurface);

        int32_t srcWidth = src->m_desc.dwWidth;
        int32_t srcHeight = src->m_desc.dwHeight;

        int32_t dstWidth = m_desc.dwWidth;
        int32_t dstHeight = m_desc.dwHeight;

        int32_t depth = m_desc.ddpfPixelFormat.dwRGBBitCount / 8;

        Blitter::Rect srcRect{0, 0, srcWidth, srcHeight};
        if (lpSrcRect) {
            srcRect.left = lpSrcRect->left;
            srcRect.top = lpSrcRect->top;
            srcRect.right = lpSrcRect->right;
            srcRect.bottom = lpSrcRect->bottom;
        }

        Blitter::Rect dstRect{0, 0, dstWidth, dstHeight};
        if (lpDestRect) {
            dstRect.left = lpDestRect->left;
            dstRect.top = lpDestRect->top;
            dstRect.right = lpDestRect->right;
            dstRect.bottom = lpDestRect->bottom;
        }

        Blitter::Image srcImg{srcWidth, srcHeight, depth, src->m_buffer};
        Blitter::Image dstImg{dstWidth, dstHeight, depth, m_buffer};

        Blitter::blit(srcImg, srcRect, dstImg, dstRect);
    }

    // Clear primary surface in 2D mode ony. OpenGL already does the clearing
    // on hardware in 3D, so it would be a waste of CPU time.
    if (m_context.isRendered() &&
        m_desc.ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE) {
        return DD_OK;
    }

    if (dwFlags & DDBLT_COLORFILL) {
        clear(lpDDBltFx->dwFillColor);
    }

    if (dwFlags & DDBLT_DEPTHFILL && m_depthBuffer) {
        m_depthBuffer->clear(0);
    }

    return DD_OK;
}

HRESULT WINAPI DirectDrawSurface::BltBatch(
    LPDDBLTBATCH lpDDBltBatch, DWORD dwCount, DWORD dwFlags)
{
    LOG_TRACE("");

    // can't blit while locked
    if (m_locked) {
        return DDERR_LOCKEDSURFACES;
    }

    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DirectDrawSurface::BltFast(DWORD dwX, DWORD dwY,
    LPDIRECTDRAWSURFACE lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans)
{
    LOG_TRACE("");

    // can't blit while locked
    if (m_locked) {
        return DDERR_LOCKEDSURFACES;
    }

    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DirectDrawSurface::DeleteAttachedSurface(
    DWORD dwFlags, LPDIRECTDRAWSURFACE lpDDSurface)
{
    LOG_TRACE("");

    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DirectDrawSurface::EnumAttachedSurfaces(
    LPVOID lpContext, LPDDENUMSURFACESCALLBACK lpEnumSurfacesCallback)
{
    LOG_TRACE("");

    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DirectDrawSurface::EnumOverlayZOrders(
    DWORD dwFlags, LPVOID lpContext, LPDDENUMSURFACESCALLBACK lpfnCallback)
{
    LOG_TRACE("");

    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DirectDrawSurface::Flip(
    LPDIRECTDRAWSURFACE lpDDSurfaceTargetOverride, DWORD dwFlags)
{
    LOG_TRACE("");

    // check if the surface can be flipped
    if (!(m_desc.ddsCaps.dwCaps & DDSCAPS_FLIP) ||
        !(m_desc.ddsCaps.dwCaps & DDSCAPS_FRONTBUFFER) || !m_backBuffer) {
        return DDERR_NOTFLIPPABLE;
    }

    bool rendered = m_context.isRendered();

    // don't re-upload surfaces if external rendering was active after lock()
    // has
    // been called, since it wouldn't be visible anyway
    if (rendered) {
        m_dirty = false;
    }

    // swap front and back buffers
    // TODO: use buffer chain correctly
    // TODO: use lpDDSurfaceTargetOverride when defined
    m_buffer.swap(m_backBuffer->m_buffer);

    bool dirtyTmp = m_dirty;
    m_dirty = m_backBuffer->m_dirty;
    m_backBuffer->m_dirty = dirtyTmp;

    // upload surface if dirty
    if (m_dirty) {
        m_renderer.upload(m_desc, m_buffer);
        m_dirty = false;
    }

    // swap buffer now if there was external rendering, otherwise the surface
    // would overwrite it
    if (rendered) {
        m_context.swapBuffers();
    }

    // update viewport in case the window size has changed
    m_context.setupViewport();

    // render surface
    m_renderer.render();

    // swap buffer after the surface has been rendered if there was no external
    // rendering for this frame, fixes title screens and other pure 2D
    // operations
    // that aren't continuously updated
    if (!rendered) {
        m_context.swapBuffers();
    }

    return DD_OK;
}

HRESULT WINAPI DirectDrawSurface::GetAttachedSurface(
    LPDDSCAPS lpDDSCaps, LPDIRECTDRAWSURFACE* lplpDDAttachedSurface)
{
    LOG_TRACE("");

    if (lpDDSCaps->dwCaps & DDSCAPS_BACKBUFFER) {
        *lplpDDAttachedSurface = m_backBuffer;
        return DD_OK;
    }

    if (lpDDSCaps->dwCaps & DDSCAPS_ZBUFFER) {
        *lplpDDAttachedSurface = m_depthBuffer;
        return DD_OK;
    }

    return DDERR_SURFACENOTATTACHED;
}

HRESULT WINAPI DirectDrawSurface::GetBltStatus(DWORD dwFlags)
{
    LOG_TRACE("");

    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DirectDrawSurface::GetCaps(LPDDSCAPS lpDDSCaps)
{
    LOG_TRACE("");

    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DirectDrawSurface::GetClipper(LPDIRECTDRAWCLIPPER* lplpDDClipper)
{
    LOG_TRACE("");

    *lplpDDClipper = reinterpret_cast<LPDIRECTDRAWCLIPPER>(m_clipper);

    return DD_OK;
}

HRESULT WINAPI DirectDrawSurface::GetColorKey(
    DWORD dwFlags, LPDDCOLORKEY lpDDColorKey)
{
    LOG_TRACE("");

    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DirectDrawSurface::GetDC(HDC* phDC)
{
    LOG_TRACE("");

    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DirectDrawSurface::GetFlipStatus(DWORD dwFlags)
{
    LOG_TRACE("");

    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DirectDrawSurface::GetOverlayPosition(LPLONG lplX, LPLONG lplY)
{
    LOG_TRACE("");

    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DirectDrawSurface::GetPalette(LPDIRECTDRAWPALETTE* lplpDDPalette)
{
    LOG_TRACE("");

    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DirectDrawSurface::GetPixelFormat(
    LPDDPIXELFORMAT lpDDPixelFormat)
{
    LOG_TRACE("");

    *lpDDPixelFormat = m_desc.ddpfPixelFormat;

    return DD_OK;
}

HRESULT WINAPI DirectDrawSurface::GetSurfaceDesc(
    LPDDSURFACEDESC lpDDSurfaceDesc)
{
    LOG_TRACE("");

    *lpDDSurfaceDesc = m_desc;

    return DD_OK;
}

HRESULT WINAPI DirectDrawSurface::Initialize(
    LPDIRECTDRAW lpDD, LPDDSURFACEDESC lpDDSurfaceDesc)
{
    LOG_TRACE("");

    // "This method is provided for compliance with the Component Object Model
    // (COM).
    // Because the DirectDrawSurface object is initialized when it is created,
    // this method always
    // returns DDERR_ALREADYINITIALIZED."
    return DDERR_ALREADYINITIALIZED;
}

HRESULT WINAPI DirectDrawSurface::IsLost()
{
    LOG_TRACE("");

    // we're never lost..
    return DD_OK;
}

HRESULT WINAPI DirectDrawSurface::Lock(LPRECT lpDestRect,
    LPDDSURFACEDESC lpDDSurfaceDesc, DWORD dwFlags, HANDLE hEvent)
{
    LOG_TRACE("%p, %p, %d, %p", lpDestRect, lpDDSurfaceDesc, dwFlags, hEvent);

    // ensure that the surface is not already locked
    if (m_locked) {
        return DDERR_SURFACEBUSY;
    }

    // assign lpSurface
    m_desc.lpSurface = &m_buffer[0];
    m_desc.dwFlags |= DDSD_LPSURFACE;

    m_locked = true;
    m_dirty = true;

    *lpDDSurfaceDesc = m_desc;

    return DD_OK;
}

HRESULT WINAPI DirectDrawSurface::ReleaseDC(HDC hDC)
{
    LOG_TRACE("");

    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DirectDrawSurface::Restore()
{
    LOG_TRACE("");

    // we can't lose surfaces..
    return DD_OK;
}

HRESULT WINAPI DirectDrawSurface::SetClipper(LPDIRECTDRAWCLIPPER lpDDClipper)
{
    LOG_TRACE("");

    m_clipper = reinterpret_cast<DirectDrawClipper*>(lpDDClipper);

    return DD_OK;
}

HRESULT WINAPI DirectDrawSurface::SetColorKey(
    DWORD dwFlags, LPDDCOLORKEY lpDDColorKey)
{
    LOG_TRACE("");

    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DirectDrawSurface::SetOverlayPosition(LONG lX, LONG lY)
{
    LOG_TRACE("");

    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DirectDrawSurface::SetPalette(LPDIRECTDRAWPALETTE lpDDPalette)
{
    LOG_TRACE("");

    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DirectDrawSurface::Unlock(LPVOID lp)
{
    LOG_TRACE("");

    // ensure that the surface is actually locked
    if (!m_locked) {
        return DDERR_NOTLOCKED;
    }

    // unassign lpSurface
    m_desc.lpSurface = nullptr;
    m_desc.dwFlags &= ~DDSD_LPSURFACE;

    m_locked = false;

    // re-draw stand-alone back buffers immediately after unlocking
    // (used for video sequences)
    if (m_desc.ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE &&
        !(m_desc.ddsCaps.dwCaps & DDSCAPS_FLIP)) {
        // FMV hack for Tomb Raider
        if (m_context.getGameID().find("tomb") != std::string::npos) {
            // fix black lines by copying even to odd lines
            for (DWORD i = 0; i < m_desc.dwHeight; i += 2) {
                auto itrEven = std::next(m_buffer.begin(), i * m_desc.lPitch);
                auto itrOdd =
                    std::next(m_buffer.begin(), (i + 1) * m_desc.lPitch);
                std::copy(itrEven, std::next(itrEven, m_desc.lPitch), itrOdd);
            }
        }

        m_context.swapBuffers();
        m_context.setupViewport();
        m_renderer.upload(m_desc, m_buffer);
        m_renderer.render();
    }

    return DD_OK;
}

HRESULT WINAPI DirectDrawSurface::UpdateOverlay(LPRECT lpSrcRect,
    LPDIRECTDRAWSURFACE lpDDDestSurface, LPRECT lpDestRect, DWORD dwFlags,
    LPDDOVERLAYFX lpDDOverlayFx)
{
    LOG_TRACE("");

    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DirectDrawSurface::UpdateOverlayDisplay(DWORD dwFlags)
{
    LOG_TRACE("");

    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DirectDrawSurface::UpdateOverlayZOrder(
    DWORD dwFlags, LPDIRECTDRAWSURFACE lpDDSReference)
{
    LOG_TRACE("");

    return DDERR_UNSUPPORTED;
}

/*** IDirectDrawSurface2 methods ***/
HRESULT WINAPI DirectDrawSurface::AddAttachedSurface(
    LPDIRECTDRAWSURFACE2 lpDDSAttachedSurface)
{
    LOG_TRACE("");

    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DirectDrawSurface::Blt(LPRECT lpDestRect,
    LPDIRECTDRAWSURFACE2 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwFlags,
    LPDDBLTFX lpDDBltFx)
{
    LOG_TRACE("DirectDrawSurface2::Blt");
    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DirectDrawSurface::BltFast(DWORD dwX, DWORD dwY,
    LPDIRECTDRAWSURFACE2 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans)
{
    LOG_TRACE("");

    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DirectDrawSurface::DeleteAttachedSurface(
    DWORD dwFlags, LPDIRECTDRAWSURFACE2 lpDDSurface)
{
    LOG_TRACE("");

    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DirectDrawSurface::Flip(
    LPDIRECTDRAWSURFACE2 lpDDSurfaceTargetOverride, DWORD dwFlags)
{
    LOG_TRACE("");

    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DirectDrawSurface::GetAttachedSurface(
    LPDDSCAPS lpDDSCaps, LPDIRECTDRAWSURFACE2* lplpDDAttachedSurface)
{
    LOG_TRACE("");

    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DirectDrawSurface::UpdateOverlay(LPRECT lpSrcRect,
    LPDIRECTDRAWSURFACE2 lpDDDestSurface, LPRECT lpDestRect, DWORD dwFlags,
    LPDDOVERLAYFX lpDDOverlayFx)
{
    LOG_TRACE("");

    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DirectDrawSurface::UpdateOverlayZOrder(
    DWORD dwFlags, LPDIRECTDRAWSURFACE2 lpDDSReference)
{
    LOG_TRACE("");

    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DirectDrawSurface::GetDDInterface(LPVOID* lplpDD)
{
    LOG_TRACE("");

    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DirectDrawSurface::PageLock(DWORD dwFlags)
{
    LOG_TRACE("");

    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DirectDrawSurface::PageUnlock(DWORD dwFlags)
{
    LOG_TRACE("");

    return DDERR_UNSUPPORTED;
}

/*** Custom methods ***/
void DirectDrawSurface::clear(int32_t color)
{
    if (m_desc.ddpfPixelFormat.dwRGBBitCount == 8 || color == 0) {
        // clear() may be called frequently on potentially large buffers, so use
        // memset instead of std::fill
        memset(&m_buffer[0], color & 0xff, m_buffer.size());
    } else if (m_desc.ddpfPixelFormat.dwRGBBitCount % 8 == 0) {
        int32_t i = 0;
        std::generate(m_buffer.begin(), m_buffer.end(), [this, &i, &color]() {
            int32_t colorOffset =
                i++ * 8 % this->m_desc.ddpfPixelFormat.dwRGBBitCount;
            return (color >> colorOffset) & 0xff;
        });
    } else {
        // TODO: support odd bit counts?
    }

    m_dirty = true;
}

} // namespace ddraw
} // namespace glrage