package com.android.support;

import android.annotation.SuppressLint;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.os.Build;
import android.view.Choreographer;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;

// ---------------------------------------------------------------------------
// The drawing layer: a transparent, full-screen, click-through overlay that
// paints whatever the native ESP manager hands it each frame.
//
// This View is deliberately dumb. It owns no game knowledge and no projection
// maths — it asks native for a flat draw list (int[] of shape records) plus a
// parallel list of text labels, and renders them. Everything that varies
// between games lives on the native side behind IEntitySource, so this file
// never changes when you target a new title.
//
// The window is NOT_TOUCHABLE and NOT_FOCUSABLE, so every touch falls through
// to the game underneath — the overlay is purely visual and sits below the
// mod menu, which is added afterwards.
// ---------------------------------------------------------------------------
public class ESPView extends View implements Choreographer.FrameCallback {

    // Shape record layout produced by nativeBuildFrame(): SHAPE_STRIDE ints of
    // [type, x1, y1, x2, y2, colorARGB]. Must stay in sync with Esp.cpp.
    private static final int SHAPE_STRIDE = 6;
    private static final int TYPE_RECT = 0;  // stroked rectangle (full box)
    private static final int TYPE_LINE = 1;  // stroked line segment
    private static final int TYPE_FILL = 2;  // filled rectangle (health bar)

    // Label meta layout from nativeLabelMeta(): 3 ints of [x, y, colorARGB].
    private static final int LABEL_STRIDE = 3;

    private final Paint strokePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint fillPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);

    private WindowManager windowManager;
    private boolean attached;

    // Native bridge. Registered from JNI_OnLoad via RegisterESP (Esp.cpp).
    private native boolean nativeEnabled();

    private native int nativeLineThickness();

    private native int[] nativeBuildFrame(int width, int height);

    private native String[] nativeLabelText();

    private native int[] nativeLabelMeta();

    public ESPView(Context context) {
        super(context);

        strokePaint.setStyle(Paint.Style.STROKE);
        strokePaint.setStrokeWidth(2.0f);

        fillPaint.setStyle(Paint.Style.FILL);

        textPaint.setColor(Color.WHITE);
        textPaint.setTextSize(22.0f);
        textPaint.setTextAlign(Paint.Align.CENTER);
        textPaint.setShadowLayer(2.0f, 0.0f, 0.0f, Color.BLACK);

        // We draw with software paints onto a transparent surface; disable the
        // hardware layer so setShadowLayer and per-frame Canvas work cheaply.
        setLayerType(View.LAYER_TYPE_SOFTWARE, null);
    }

    // Adds the overlay to the WindowManager and starts the per-frame loop.
    // Call once, before the mod menu is added, so the menu stays on top.
    @SuppressLint("WrongConstant")
    public void attach() {
        if (attached) {
            return;
        }
        int type = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O ? 2038 : 2002;
        WindowManager.LayoutParams params = new WindowManager.LayoutParams(
                WindowManager.LayoutParams.MATCH_PARENT,
                WindowManager.LayoutParams.MATCH_PARENT,
                type,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                        | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE
                        | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN
                        | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
                PixelFormat.TRANSLUCENT);
        params.gravity = Gravity.TOP | Gravity.LEFT;

        windowManager = (WindowManager) getContext().getSystemService(Context.WINDOW_SERVICE);
        windowManager.addView(this, params);
        attached = true;

        Choreographer.getInstance().postFrameCallback(this);
    }

    public void detach() {
        if (!attached) {
            return;
        }
        Choreographer.getInstance().removeFrameCallback(this);
        try {
            windowManager.removeView(this);
        } catch (IllegalArgumentException ignored) {
            // Already removed (e.g. window leaked during teardown) — nothing to do.
        }
        attached = false;
    }

    // Choreographer ticks once per display refresh; we redraw only while the
    // ESP is enabled so an idle overlay costs nothing.
    @Override
    public void doFrame(long frameTimeNanos) {
        if (!attached) {
            return;
        }
        if (nativeEnabled()) {
            invalidate();
        }
        Choreographer.getInstance().postFrameCallback(this);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);

        if (!nativeEnabled()) {
            return;  // canvas already cleared; leave the screen untouched
        }

        int width = getWidth();
        int height = getHeight();
        if (width <= 0 || height <= 0) {
            return;
        }

        strokePaint.setStrokeWidth(Math.max(1, nativeLineThickness()));

        int[] shapes = nativeBuildFrame(width, height);
        if (shapes != null) {
            drawShapes(canvas, shapes);
        }

        drawLabels(canvas);
    }

    private void drawShapes(Canvas canvas, int[] shapes) {
        int count = shapes.length - (shapes.length % SHAPE_STRIDE);
        for (int i = 0; i < count; i += SHAPE_STRIDE) {
            int type = shapes[i];
            float x1 = shapes[i + 1];
            float y1 = shapes[i + 2];
            float x2 = shapes[i + 3];
            float y2 = shapes[i + 4];
            int color = shapes[i + 5];

            switch (type) {
                case TYPE_RECT:
                    strokePaint.setColor(color);
                    canvas.drawRect(x1, y1, x2, y2, strokePaint);
                    break;
                case TYPE_LINE:
                    strokePaint.setColor(color);
                    canvas.drawLine(x1, y1, x2, y2, strokePaint);
                    break;
                case TYPE_FILL:
                    fillPaint.setColor(color);
                    canvas.drawRect(x1, y1, x2, y2, fillPaint);
                    break;
                default:
                    break;
            }
        }
    }

    private void drawLabels(Canvas canvas) {
        String[] text = nativeLabelText();
        int[] meta = nativeLabelMeta();
        if (text == null || meta == null) {
            return;
        }
        int labels = Math.min(text.length, meta.length / LABEL_STRIDE);
        for (int i = 0; i < labels; i++) {
            int x = meta[i * LABEL_STRIDE];
            int y = meta[i * LABEL_STRIDE + 1];
            int color = meta[i * LABEL_STRIDE + 2];
            textPaint.setColor(color);
            canvas.drawText(text[i], x, y, textPaint);
        }
    }
}
