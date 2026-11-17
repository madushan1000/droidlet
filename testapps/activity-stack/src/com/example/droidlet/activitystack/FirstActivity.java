package com.example.droidlet.activitystack;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;

public class FirstActivity extends Activity {
    private static final String TAG = "DroidletStack";
    private TextView mText;
    private int mResumes;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        setContentView(R.layout.main);
        mText = (TextView) findViewById(R.id.text);
        mText.setText("First: resumes=" + mResumes);
        ((Button) findViewById(R.id.next)).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                Log.i(TAG, "First starts Second for result");
                startActivityForResult(new Intent(FirstActivity.this, SecondActivity.class), 1);
            }
        });
        ((Button) findViewById(R.id.finish)).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                Log.i(TAG, "First finish button");
                finish();
            }
        });
        Log.i(TAG, "First onCreate");
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        Log.i(TAG, "First onActivityResult request=" + requestCode
                + " result=" + resultCode + " data=" + data);
        mText.setText("First: result=" + resultCode);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        Log.i(TAG, "First windowFocus=" + hasFocus);
    }

    @Override protected void onStart() { super.onStart(); Log.i(TAG, "First onStart"); }
    @Override protected void onResume() {
        super.onResume();
        mResumes++;
        Log.i(TAG, "First onResume " + mResumes);
        if (mText != null) mText.setText("First: resumes=" + mResumes);
    }
    @Override protected void onPause() { super.onPause(); Log.i(TAG, "First onPause"); }
    @Override protected void onStop() { super.onStop(); Log.i(TAG, "First onStop"); }
    @Override protected void onDestroy() { super.onDestroy(); Log.i(TAG, "First onDestroy"); }

    @Override
    protected void onSaveInstanceState(Bundle out) {
        super.onSaveInstanceState(out);
        out.putInt("resumes", mResumes);
        Log.i(TAG, "First onSaveInstanceState resumes=" + mResumes);
    }

    @Override
    protected void onRestoreInstanceState(Bundle state) {
        super.onRestoreInstanceState(state);
        mResumes = state.getInt("resumes", mResumes);
        Log.i(TAG, "First onRestoreInstanceState resumes=" + mResumes);
    }
}
