package com.example.droidlet.activitystack;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;

public class SecondActivity extends Activity {
    private static final String TAG = "DroidletStack";

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        setContentView(R.layout.main);
        ((TextView) findViewById(R.id.text)).setText("Second");
        ((Button) findViewById(R.id.next)).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                Log.i(TAG, "Second starts Third for result");
                startActivityForResult(new Intent(SecondActivity.this, ThirdActivity.class), 2);
            }
        });
        ((Button) findViewById(R.id.finish)).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                Log.i(TAG, "Second finishes with result");
                Intent data = new Intent();
                data.putExtra("from", "Second");
                setResult(RESULT_OK, data);
                finish();
            }
        });
        Log.i(TAG, "Second onCreate");
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        Log.i(TAG, "Second onActivityResult request=" + requestCode
                + " result=" + resultCode + " data=" + data);
        ((TextView) findViewById(R.id.text)).setText("Second: result=" + resultCode);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        Log.i(TAG, "Second windowFocus=" + hasFocus);
    }

    @Override protected void onStart() { super.onStart(); Log.i(TAG, "Second onStart"); }
    @Override protected void onResume() { super.onResume(); Log.i(TAG, "Second onResume"); }
    @Override protected void onPause() { super.onPause(); Log.i(TAG, "Second onPause"); }
    @Override protected void onStop() { super.onStop(); Log.i(TAG, "Second onStop"); }
    @Override protected void onDestroy() { super.onDestroy(); Log.i(TAG, "Second onDestroy"); }
}
