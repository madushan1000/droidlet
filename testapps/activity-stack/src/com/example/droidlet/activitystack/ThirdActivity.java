package com.example.droidlet.activitystack;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;
import android.view.KeyEvent;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;

public class ThirdActivity extends Activity {
    private static final String TAG = "DroidletStack";

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        setContentView(R.layout.main);
        ((TextView) findViewById(R.id.text)).setText("Third");
        ((Button) findViewById(R.id.next)).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                Log.i(TAG, "Third finishes with result");
                Intent data = new Intent();
                data.putExtra("from", "Third");
                setResult(RESULT_OK, data);
                finish();
            }
        });
        ((Button) findViewById(R.id.finish)).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                Log.i(TAG, "Third finish button");
                finish();
            }
        });
        Log.i(TAG, "Third onCreate");
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        Log.i(TAG, "Third onKeyDown " + keyCode + " tracking=" + event.isTracking());
        return super.onKeyDown(keyCode, event);
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        Log.i(TAG, "Third onKeyUp " + keyCode + " tracking=" + event.isTracking());
        return super.onKeyUp(keyCode, event);
    }

    @Override
    public void onBackPressed() {
        Log.i(TAG, "Third onBackPressed");
        super.onBackPressed();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        Log.i(TAG, "Third windowFocus=" + hasFocus);
    }

    @Override protected void onStart() { super.onStart(); Log.i(TAG, "Third onStart"); }
    @Override protected void onResume() { super.onResume(); Log.i(TAG, "Third onResume"); }
    @Override protected void onPause() { super.onPause(); Log.i(TAG, "Third onPause"); }
    @Override protected void onStop() { super.onStop(); Log.i(TAG, "Third onStop"); }
    @Override protected void onDestroy() { super.onDestroy(); Log.i(TAG, "Third onDestroy"); }
}
