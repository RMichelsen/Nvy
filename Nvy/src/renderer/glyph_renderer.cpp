#include "pch.h"
#include "glyph_renderer.h"

#include "renderer/renderer.h"

HRESULT GlyphDrawingEffect::QueryInterface(REFIID riid, void **ppv_object) {
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

GlyphRenderer::GlyphRenderer() : ref_count(0) {}

HRESULT GlyphRenderer::DrawGlyphRun(void *client_drawing_context, float baseline_origin_x, 
	float baseline_origin_y, DWRITE_MEASURING_MODE measuring_mode, DWRITE_GLYPH_RUN const *glyph_run, 
	DWRITE_GLYPH_RUN_DESCRIPTION const *glyph_run_description, IUnknown *client_drawing_effect) {
	
	HRESULT hr = S_OK;
	Renderer *renderer = reinterpret_cast<Renderer *>(client_drawing_context);

	uint32_t color;
	if (client_drawing_effect)
	{
		GlyphDrawingEffect *drawing_effect;
		client_drawing_effect->QueryInterface(__uuidof(GlyphDrawingEffect), reinterpret_cast<void **>(&drawing_effect));
		color = drawing_effect->color;
		drawing_effect->Release();
	}
	else {
		color = renderer->hl_attribs[0].foreground;
	}
	ID2D1SolidColorBrush *brush;
	hr = renderer->render_target->CreateSolidColorBrush(D2D1::ColorF(color), &brush);

	if (SUCCEEDED(hr)) {
		renderer->render_target->DrawGlyphRun(
			D2D1_POINT_2F { .x = baseline_origin_x, .y = baseline_origin_y },
			glyph_run,
			brush,
			measuring_mode
		);
	}

	brush->Release();
	return hr;
}

HRESULT GlyphRenderer::DrawInlineObject(void *client_drawing_context, float origin_x, float origin_y, 
	IDWriteInlineObject *inline_obj, BOOL is_sideways, BOOL is_right_to_left, IUnknown *client_drawing_effect) {
	return E_NOTIMPL;
}

HRESULT GlyphRenderer::DrawStrikethrough(void *client_drawing_context, float baseline_origin_x, 
	float baseline_origin_y, DWRITE_STRIKETHROUGH const *strikethrough, IUnknown *client_drawing_effect) {
	return E_NOTIMPL;
}

HRESULT GlyphRenderer::DrawUnderline(void *client_drawing_context, float baseline_origin_x, 
	float baseline_origin_y, DWRITE_UNDERLINE const *underline, IUnknown *client_drawing_effect) {

	HRESULT hr = S_OK;
	Renderer *renderer = reinterpret_cast<Renderer *>(client_drawing_context);

	int row = static_cast<int>(baseline_origin_y / renderer->font_height);
	int col = static_cast<int>(baseline_origin_x / renderer->font_width);
	int index = row * renderer->grid_cols + col;
	HighlightAttributes *hl_attribs = &renderer->hl_attribs[renderer->grid_hl_attrib_ids[index]];
	uint32_t color = hl_attribs->special == DEFAULT_COLOR ? renderer->hl_attribs[0].special : hl_attribs->special;
	ID2D1SolidColorBrush *brush;
	hr = renderer->render_target->CreateSolidColorBrush(D2D1::ColorF(color), &brush);

	D2D1::Matrix3x2F transform = D2D1::Matrix3x2F(
		1.0f, 0.0f,
		0.0f, 1.0f,
		baseline_origin_x, baseline_origin_y
	);

	if (FAILED(hr)) {
		return hr;
	}

	if (client_drawing_effect) {
		GlyphDrawingEffect *drawing_effect;
		client_drawing_effect->QueryInterface(__uuidof(GlyphDrawingEffect), reinterpret_cast<void **>(&drawing_effect));

		if (drawing_effect->undercurl) {
			ID2D1PathGeometry *path_geometry = nullptr;
			hr = renderer->d2d_factory->CreatePathGeometry(&path_geometry);

			ID2D1GeometrySink *geometry_sink = nullptr;
			if (SUCCEEDED(hr)) {
				hr = path_geometry->Open(&geometry_sink);
			}

			if (SUCCEEDED(hr)) {
				float small_offset = -(renderer->font_width / 20.0f);
				float wiggle_height = (renderer->font_width / 10.0f);
				int wiggle_count = static_cast<int>(underline->width / renderer->font_width) * 2;
				float wiggle_step = underline->width / wiggle_count;

				geometry_sink->SetFillMode(D2D1_FILL_MODE_WINDING);
				geometry_sink->BeginFigure(
					D2D1_POINT_2F { .x = 0, .y = small_offset + underline->offset },
					D2D1_FIGURE_BEGIN_FILLED
				);

				for (int i = 0; i < wiggle_count; ++i) {
					float wiggle_factor = i % 2 == 0 ? wiggle_height : -wiggle_height;
					geometry_sink->AddBezier(
						D2D1::BezierSegment(
							D2D1_POINT_2F { .x = wiggle_step * i, .y = small_offset + underline->offset },
							D2D1_POINT_2F { .x = wiggle_step * i + (wiggle_step / 2.0f), .y = small_offset + underline->offset + wiggle_factor },
							D2D1_POINT_2F { .x = wiggle_step * i + wiggle_step, .y = small_offset + underline->offset }
						)
					);
				}
				geometry_sink->AddLine(D2D1_POINT_2F { .x = underline->width, .y = small_offset + underline->offset + underline->thickness });

				for (int i = wiggle_count; i > 0; --i) {
					float wiggle_factor = i % 2 != 0 ? wiggle_height : -wiggle_height;
					geometry_sink->AddBezier(
						D2D1::BezierSegment(
							D2D1_POINT_2F { .x = wiggle_step * i, .y = small_offset + underline->offset + underline->thickness },
							D2D1_POINT_2F { .x = wiggle_step * i - (wiggle_step / 2.0f), .y = small_offset + underline->offset + underline->thickness + wiggle_factor },
							D2D1_POINT_2F { .x = wiggle_step * i - wiggle_step, .y = small_offset + underline->offset + underline->thickness }
						)
					);
				}
				geometry_sink->AddLine(D2D1_POINT_2F { .x = 0, .y = small_offset + underline->offset });

				geometry_sink->EndFigure(D2D1_FIGURE_END_CLOSED);
			}

			if (SUCCEEDED(hr)) {
				hr = geometry_sink->Close();
			}

			ID2D1TransformedGeometry *transformed_geometry = nullptr;
			if (SUCCEEDED(hr)) {
				hr = renderer->d2d_factory->CreateTransformedGeometry(
					path_geometry,
					&transform,
					&transformed_geometry
				);
			}

			if (SUCCEEDED(hr)) {
				renderer->render_target->DrawGeometry(
					transformed_geometry,
					brush
				);
				renderer->render_target->FillGeometry(
					transformed_geometry,
					brush
				);
			}
			brush->Release();

			path_geometry->Release();
			geometry_sink->Release();
			transformed_geometry->Release();
			return hr;
		}
	}

	D2D1_RECT_F rect = D2D1_RECT_F {
		.left = 0,
		.top = underline->offset,
		.right = underline->width,
		.bottom = underline->offset + underline->thickness
	};

	ID2D1RectangleGeometry *rectangle_geometry = nullptr;
	hr = renderer->d2d_factory->CreateRectangleGeometry(
		&rect,
		&rectangle_geometry
	);

	ID2D1TransformedGeometry *transformed_geometry = nullptr;
	if (SUCCEEDED(hr)) {
		hr = renderer->d2d_factory->CreateTransformedGeometry(
			rectangle_geometry,
			&transform,
			&transformed_geometry
		);
	}

	renderer->render_target->DrawGeometry(
		transformed_geometry,
		brush
	);
	renderer->render_target->FillGeometry(
		transformed_geometry,
		brush
	);
	brush->Release();

	rectangle_geometry->Release();
	transformed_geometry->Release();
	return hr;
}

HRESULT GlyphRenderer::IsPixelSnappingDisabled(void *client_drawing_context, BOOL *is_disabled) {
	*is_disabled = false;
	return S_OK;
}

HRESULT GlyphRenderer::GetCurrentTransform(void *client_drawing_context, DWRITE_MATRIX *transform) {
	Renderer *renderer = reinterpret_cast<Renderer *>(client_drawing_context);
	renderer->render_target->GetTransform(reinterpret_cast<D2D1_MATRIX_3X2_F *>(transform));
	return S_OK;
}

HRESULT GlyphRenderer::GetPixelsPerDip(void *client_drawing_context, float *pixels_per_dip) {
	*pixels_per_dip = 1.0f;
	return S_OK;
}

ULONG GlyphRenderer::AddRef() {
	return InterlockedIncrement(&ref_count);
}

ULONG GlyphRenderer::Release()
{
	ULONG new_count = InterlockedDecrement(&ref_count);
	if (new_count == 0) {
		delete this;
		return 0;
	}
	return new_count;
}

HRESULT GlyphRenderer::QueryInterface(REFIID riid, void **ppv_object) {
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