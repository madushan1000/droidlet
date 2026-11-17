package com.example.droidlet.surfaceclip;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.util.Log;
import android.util.AttributeSet;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

public class ClippedSurfaceView extends SurfaceView implements SurfaceHolder.Callback, Runnable {
    private final Paint mPaint = new Paint();

    public ClippedSurfaceView(Context context, AttributeSet attrs) {
        super(context, attrs);
        getHolder().addCallback(this);
        try {
            getClass().getMethod("setEnableSurfaceClipping", boolean.class)
                    .invoke(this, true);
            Log.i("DroidletSurfaceClip", "surface clipping enabled");
        } catch (Throwable t) {
            Log.i("DroidletSurfaceClip", "surface clipping unavailable: " + t);
        }
    }

    @Override
    protected void onLayout(boolean changed, int left, int top, int right, int bottom) {
        super.onLayout(changed, left, top, right, bottom);
        if (getParent() instanceof android.view.View) {
            android.view.View parent = (android.view.View) getParent();
            setClipBounds(new Rect(0, 0, parent.getWidth(), parent.getHeight()));
            Log.i("DroidletSurfaceClip", "clipBounds=" + parent.getWidth() + "x"
                    + parent.getHeight());
        }
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        new Thread(this, "surface-clip-draw").start();
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
    }

    @Override
    public void run() {
        SurfaceHolder holder = getHolder();
        for (int frame = 0; frame < 6; ++frame) {
            Canvas canvas = holder.lockCanvas();
            if (canvas == null) {
                return;
            }
            try {
                canvas.drawColor(Color.rgb(200, 30, 30));
                mPaint.setColor(Color.rgb(30, 200, 60));
                mPaint.setStrokeWidth(8.0f);
                for (int i = -canvas.getHeight(); i < canvas.getWidth(); i += 80) {
                    canvas.drawLine(i, 0, i + canvas.getHeight(), canvas.getHeight(), mPaint);
                }
                mPaint.setColor(Color.WHITE);
                mPaint.setTextSize(40.0f);
                canvas.drawText("surface " + canvas.getWidth() + "x" + canvas.getHeight(),
                        32, 64, mPaint);
            } finally {
                holder.unlockCanvasAndPost(canvas);
            }
            try {
                Thread.sleep(50);
            } catch (InterruptedException e) {
                return;
            }
        }
    }
}
