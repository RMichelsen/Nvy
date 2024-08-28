#pragma once

struct DECLSPEC_UUID("8d4d2884-e4d9-11ea-87d0-0242ac130003") GlyphDrawingEffect : public IUnknown
{
    GlyphDrawingEffect(uint32_t text_color, uint32_t special_color)
        : ref_count(0)
        , text_color(text_color)
        , special_color(special_color)
    {
    }

    inline ULONG AddRef() noexcept override
    {
        return InterlockedIncrement(&ref_count);
    }
    inline ULONG Release() noexcept override
    {
        ULONG new_count = InterlockedDecrement(&ref_count);
        if (new_count == 0)
        {
            delete this;
            return 0;
        }
        return new_count;
    }

    HRESULT QueryInterface(REFIID riid, void **ppv_object) noexcept override;

    ULONG ref_count;
    uint32_t text_color;
    uint32_t special_color;
};

struct Renderer;
struct GlyphRenderer : public IDWriteTextRenderer
{
    GlyphRenderer(Renderer *renderer);
    ~GlyphRenderer();

    HRESULT DrawGlyphRun(void *client_drawing_context, float baseline_origin_x, float baseline_origin_y, DWRITE_MEASURING_MODE measuring_mode,
        DWRITE_GLYPH_RUN const *glyph_run, DWRITE_GLYPH_RUN_DESCRIPTION const *glyph_run_description, IUnknown *client_drawing_effect) noexcept override;

    HRESULT DrawInlineObject(void *client_drawing_context, float origin_x, float origin_y, IDWriteInlineObject *inline_obj, BOOL is_sideways,
        BOOL is_right_to_left, IUnknown *client_drawing_effect) noexcept override;

    HRESULT DrawLine(void *client_drawing_context, float baseline_origin_x, float baseline_origin_y, FLOAT offset, FLOAT width, FLOAT thickness,
        IUnknown *client_drawing_effect, bool use_special_color) noexcept;

    HRESULT DrawStrikethrough(void *client_drawing_context, float baseline_origin_x, float baseline_origin_y, DWRITE_STRIKETHROUGH const *strikethrough,
        IUnknown *client_drawing_effect) noexcept override;

    HRESULT DrawUnderline(void *client_drawing_context, float baseline_origin_x, float baseline_origin_y, DWRITE_UNDERLINE const *underline,
        IUnknown *client_drawing_effect) noexcept override;

    HRESULT IsPixelSnappingDisabled(void *client_drawing_context, BOOL *is_disabled) noexcept override;
    HRESULT GetCurrentTransform(void *client_drawing_context, DWRITE_MATRIX *transform) noexcept override;
    HRESULT GetPixelsPerDip(void *client_drawing_context, float *pixels_per_dip) noexcept override;

    ULONG AddRef() noexcept override;
    ULONG Release() noexcept override;
    HRESULT QueryInterface(REFIID riid, void **ppv_object) noexcept override;

    ULONG ref_count;
    ID2D1SolidColorBrush *drawing_effect_brush;
    ID2D1SolidColorBrush *temp_brush;
};
