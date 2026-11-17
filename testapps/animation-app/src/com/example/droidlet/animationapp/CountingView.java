package com.example.droidlet.animationapp;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.util.AttributeSet;
import android.util.Log;
import android.view.View;

public class CountingView extends View {
    private static final String TAG = "DroidletAnimation";
    private final Paint mPaint = new Paint();
    private int mDraws;

    public CountingView(Context context, AttributeSet attrs) {
        super(context, attrs);
        mPaint.setColor(Color.rgb(220, 40, 40));
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        canvas.drawRect(0, 0, getWidth(), getHeight(), mPaint);
        if (++mDraws % 10 == 0) {
            Log.i(TAG, "draws=" + mDraws);
        }
    }
}
