package com.example.droidlet.menuapp;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.view.Menu;
import android.view.MenuItem;
import android.widget.TextView;

public class MainActivity extends Activity {
    private static final String TAG = "DroidletMenu";

    private boolean mLines;
    private boolean mCoordinates;
    private boolean mRings;
    private TextView mStatus;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        setContentView(R.layout.main);
        mStatus = findViewById(R.id.status);
        updateStatus();
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        getMenuInflater().inflate(R.menu.main, menu);
        return true;
    }

    @Override
    public boolean onPrepareOptionsMenu(Menu menu) {
        menu.findItem(R.id.action_lines).setChecked(mLines);
        menu.findItem(R.id.action_coordinates).setChecked(mCoordinates);
        menu.findItem(R.id.action_rings).setChecked(mRings);
        return super.onPrepareOptionsMenu(menu);
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        int id = item.getItemId();
        if (id == R.id.action_lines) {
            mLines = !mLines;
            item.setChecked(mLines);
        } else if (id == R.id.action_coordinates) {
            mCoordinates = !mCoordinates;
            item.setChecked(mCoordinates);
        } else if (id == R.id.action_rings) {
            mRings = !mRings;
            item.setChecked(mRings);
        } else {
            return super.onOptionsItemSelected(item);
        }
        updateStatus();
        return true;
    }

    private void updateStatus() {
        String text = "Lines: " + (mLines ? "on" : "off")
                + "\nCoordinates: " + (mCoordinates ? "on" : "off")
                + "\nRings: " + (mRings ? "on" : "off");
        if (mStatus != null) {
            mStatus.setText(text);
        }
        Log.i(TAG, "menu lines=" + mLines + " coordinates=" + mCoordinates
                + " rings=" + mRings);
    }
}
