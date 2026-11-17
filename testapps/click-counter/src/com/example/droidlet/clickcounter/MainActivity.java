package com.example.droidlet.clickcounter;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.text.Editable;
import android.text.TextWatcher;
import android.util.Log;
import android.view.MotionEvent;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;

public class MainActivity extends Activity {
    private static final String TAG = "DroidletFixture";

    private int mClicks;
    private TextView mText;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.main);

        mText = (TextView) findViewById(R.id.text);
        Button button = (Button) findViewById(R.id.button);
        button.setOnTouchListener(new View.OnTouchListener() {
            @Override
            public boolean onTouch(View view, MotionEvent event) {
                Log.i(TAG, "button touch action=" + event.getActionMasked()
                        + " x=" + event.getX() + " y=" + event.getY()
                        + " pressed=" + view.isPressed());
                return false;
            }
        });
        button.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                mClicks++;
                mText.setText("Clicks: " + mClicks);
                Log.i(TAG, "click " + mClicks);
            }
        });

        final TextView progress = (TextView) findViewById(R.id.progress);
        SeekBar seek = (SeekBar) findViewById(R.id.seek);
        seek.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar bar, int value, boolean fromUser) {
                progress.setText("Progress: " + value);
                Log.i(TAG, "seek " + value);
            }

            @Override public void onStartTrackingTouch(SeekBar bar) {}
            @Override public void onStopTrackingTouch(SeekBar bar) {}
        });

        final EditText edit = (EditText) findViewById(R.id.edit);
        edit.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
            @Override public void onTextChanged(CharSequence s, int start, int before, int count) {}

            @Override
            public void afterTextChanged(Editable text) {
                Log.i(TAG, "text=" + text);
            }
        });

        LinearLayout content = (LinearLayout) findViewById(R.id.scroll_content);
        for (int i = 0; i < 40; i++) {
            TextView row = new TextView(this);
            row.setText("Row " + i);
            row.setTextSize(24);
            row.setTextColor(Color.BLACK);
            row.setPadding(32, 24, 32, 24);
            content.addView(row);
        }
        ScrollView scroll = (ScrollView) findViewById(R.id.scroll);
        scroll.setOnScrollChangeListener(new View.OnScrollChangeListener() {
            @Override
            public void onScrollChange(View view, int x, int y, int oldX, int oldY) {
                Log.i(TAG, "scroll y=" + y);
            }
        });

        Log.i(TAG, "created");
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent event) {
        Log.i(TAG, "touch action=" + event.getActionMasked()
                + " x=" + event.getX() + " y=" + event.getY());
        return super.dispatchTouchEvent(event);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        Log.i(TAG, "window focus=" + hasFocus);
    }

    @Override
    public void onBackPressed() {
        Log.i(TAG, "back pressed");
        super.onBackPressed();
    }
}
