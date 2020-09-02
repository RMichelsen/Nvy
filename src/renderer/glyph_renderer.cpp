#include "glyph_renderer.h"
#include "renderer/renderer.h"

HRESULT GlyphDrawingEffect::QueryInterface(REFIID riid, void **ppv_object) noexcept {
	if (__uuidof(GlyphDrawingEffect) == riid) {
		*ppv_object = this;
	}
	else if (__uuidof(IUnknown) == riid) {
		*ppv_object = this;
	}
	else {
		*ppv_object = nullptr;
		return E_FAIL;
	}

	this->AddRef();
	return S_OK;
}

GlyphRenderer::GlyphRenderer(Renderer *renderer) : ref_count(0) {
	WIN_CHECK(renderer->d2d_context->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &drawing_effect_brush));
	WIN_CHECK(renderer->d2d_context->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &temp_brush));
}

GlyphRenderer::~GlyphRenderer() {
	SafeRelease(&drawing_effect_brush);
	SafeRelease(&temp_brush);
}

HRESULT GlyphRenderer::DrawGlyphRun(void *client_drawing_context, float baseline_origin_x, 
	float baseline_origin_y, DWRITE_MEASURING_MODE measuring_mode, DWRITE_GLYPH_RUN const *glyph_run, 
	DWRITE_GLYPH_RUN_DESCRIPTION const *glyph_run_description, IUnknown *client_drawing_effect) noexcept {
	
	HRESULT hr = S_OK;
	Renderer *renderer = reinterpret_cast<Renderer *>(client_drawing_context);
	
	if (client_drawing_effect)
	{
		GlyphDrawingEffect *drawing_effect;
		client_drawing_effect->QueryInterface(__uuidof(GlyphDrawingEffect), reinterpret_cast<void **>(&drawing_effect));
		drawing_effect_brush->SetColor(D2D1::ColorF(drawing_effect->text_color));
		SafeRelease(&drawing_effect);
	}
	else {
		drawing_effect_brush->SetColor(D2D1::ColorF(renderer->hl_attribs[0].foreground));
	}

	DWRITE_GLYPH_IMAGE_FORMATS supported_formats =
		DWRITE_GLYPH_IMAGE_FORMATS_TRUETYPE |
		DWRITE_GLYPH_IMAGE_FORMATS_CFF |
		DWRITE_GLYPH_IMAGE_FORMATS_COLR |
		DWRITE_GLYPH_IMAGE_FORMATS_SVG |
		DWRITE_GLYPH_IMAGE_FORMATS_PNG |
		DWRITE_GLYPH_IMAGE_FORMATS_JPEG |
		DWRITE_GLYPH_IMAGE_FORMATS_TIFF |
		DWRITE_GLYPH_IMAGE_FORMATS_PREMULTIPLIED_B8G8R8A8;

	IDWriteColorGlyphRunEnumerator1 *glyph_run_enumerator;
	hr = renderer->dwrite_factory->TranslateColorGlyphRun(
		D2D1_POINT_2F { .x = baseline_origin_x, .y = baseline_origin_y },
		glyph_run,
		glyph_run_description,
		supported_formats,
		measuring_mode,
		nullptr,
		0,
		&glyph_run_enumerator
	);

	if (hr == DWRITE_E_NOCOLOR) {
		renderer->d2d_context->DrawGlyphRun(
			D2D1_POINT_2F { .x = baseline_origin_x, .y = baseline_origin_y },
			glyph_run,
			drawing_effect_brush,
			measuring_mode
		);
	}
	else {
		assert(!FAILED(hr));

		while (true) {
			BOOL has_run;
			WIN_CHECK(glyph_run_enumerator->MoveNext(&has_run));
			if (!has_run) {
				break;
			}

			DWRITE_COLOR_GLYPH_RUN1 const *color_run;
			WIN_CHECK(glyph_run_enumerator->GetCurrentRun(&color_run));

			D2D1_POINT_2F current_baseline_origin {
				.x = color_run->baselineOriginX,
				.y = color_run->baselineOriginY
			};

			switch (color_run->glyphImageFormat) {
			case DWRITE_GLYPH_IMAGE_FORMATS_PNG:
			case DWRITE_GLYPH_IMAGE_FORMATS_JPEG:
			case DWRITE_GLYPH_IMAGE_FORMATS_TIFF:
			case DWRITE_GLYPH_IMAGE_FORMATS_PREMULTIPLIED_B8G8R8A8: {
				renderer->d2d_context->DrawColorBitmapGlyphRun(
					color_run->glyphImageFormat,
					current_baseline_origin,
					&color_run->glyphRun,
					measuring_mode
				);
			} break;
			case DWRITE_GLYPH_IMAGE_FORMATS_SVG: {
				renderer->d2d_context->DrawSvgGlyphRun(
					current_baseline_origin,
					&color_run->glyphRun,
					drawing_effect_brush,
					nullptr,
					0,
					measuring_mode
				);
			} break;
			case DWRITE_GLYPH_IMAGE_FORMATS_TRUETYPE:
			case DWRITE_GLYPH_IMAGE_FORMATS_CFF:
			case DWRITE_GLYPH_IMAGE_FORMATS_COLR:
			default: {
				bool use_palette_color = color_run->paletteIndex != 0xFFFF;
				if (use_palette_color) {
					temp_brush->SetColor(color_run->runColor);
				}
				
				renderer->d2d_context->PushAxisAlignedClip(
					D2D1_RECT_F {
						.left = current_baseline_origin.x,
						.top = current_baseline_origin.y - renderer->font_ascent,
						.right = current_baseline_origin.x + (color_run->glyphRun.glyphCount * 2 * renderer->font_width),
						.bottom = current_baseline_origin.y + renderer->font_descent,
					},
					D2D1_ANTIALIAS_MODE_ALIASED
				);
				renderer->d2d_context->DrawGlyphRun(
					current_baseline_origin,
					&color_run->glyphRun,
					color_run->glyphRunDescription,
					use_palette_color ? temp_brush : drawing_effect_brush,
					measuring_mode
				);
				renderer->d2d_context->PopAxisAlignedClip();

			} break;
			}
		}
	}

	return hr;
}

HRESULT GlyphRenderer::DrawInlineObject(void *client_drawing_context, float origin_x, float origin_y, 
	IDWriteInlineObject *inline_obj, BOOL is_sideways, BOOL is_right_to_left, IUnknown *client_drawing_effect) noexcept {
	return E_NOTIMPL;
}

HRESULT GlyphRenderer::DrawStrikethrough(void *client_drawing_context, float baseline_origin_x, 
	float baseline_origin_y, DWRITE_STRIKETHROUGH const *strikethrough, IUnknown *client_drawing_effect) noexcept {
	return E_NOTIMPL;
}

HRESULT GlyphRenderer::DrawUnderline(void *client_drawing_context, float baseline_origin_x, 
	float baseline_origin_y, DWRITE_UNDERLINE const *underline, IUnknown *client_drawing_effect) noexcept {

	HRESULT hr = S_OK;
	Renderer *renderer = reinterpret_cast<Renderer *>(client_drawing_context);

	if (client_drawing_effect)
	{
		GlyphDrawingEffect *drawing_effect;
		client_drawing_effect->QueryInterface(__uuidof(GlyphDrawingEffect), reinterpret_cast<void **>(&drawing_effect));
		temp_brush->SetColor(D2D1::ColorF(drawing_effect->special_color));
		SafeRelease(&drawing_effect);
	}
	else {
		temp_brush->SetColor(D2D1::ColorF(renderer->hl_attribs[0].special));
	}

	D2D1_RECT_F rect = D2D1_RECT_F {
		.left = baseline_origin_x,
		.top = baseline_origin_y + underline->offset,
		.right = baseline_origin_x + underline->width,
		.bottom = baseline_origin_y + underline->offset + underline->thickness
	};

    renderer->d2d_context->FillRectangle(rect, temp_brush);
	return hr;
}

HRESULT GlyphRenderer::IsPixelSnappingDisabled(void *client_drawing_context, BOOL *is_disabled) noexcept {
	*is_disabled = false;
	return S_OK;
}

HRESULT GlyphRenderer::GetCurrentTransform(void *client_drawing_context, DWRITE_MATRIX *transform) noexcept {
	Renderer *renderer = reinterpret_cast<Renderer *>(client_drawing_context);
	renderer->d2d_context->GetTransform(reinterpret_cast<D2D1_MATRIX_3X2_F *>(transform));
	return S_OK;
}

HRESULT GlyphRenderer::GetPixelsPerDip(void *client_drawing_context, float *pixels_per_dip) noexcept {
	*pixels_per_dip = 1.0f;
	return S_OK;
}

ULONG GlyphRenderer::AddRef() noexcept {
	return InterlockedIncrement(&ref_count);
}

ULONG GlyphRenderer::Release() noexcept {
	ULONG new_count = InterlockedDecrement(&ref_count);
	if (new_count == 0) {
		delete this;
		return 0;
	}
	return new_count;
}

HRESULT GlyphRenderer::QueryInterface(REFIID riid, void **ppv_object) noexcept {
	if (__uuidof(IDWriteTextRenderer) == riid) {
		*ppv_object = this;
	}
	else if (__uuidof(IDWritePixelSnapping) == riid) {
		*ppv_object = this;
	}
	else if (__uuidof(IUnknown) == riid) {
		*ppv_object = this;
	}
	else {
		*ppv_object = nullptr;
		return E_FAIL;
	}

	this->AddRef();
	return S_OK;
}
