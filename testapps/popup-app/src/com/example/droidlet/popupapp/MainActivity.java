package com.example.droidlet.popupapp;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.os.Bundle;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.PopupWindow;
import android.widget.TextView;

public class MainActivity extends Activity {
    private static final String TAG = "DroidletPopup";

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        setContentView(R.layout.main);

        View content = getLayoutInflater().inflate(R.layout.popup, null);
        final TextView status = content.findViewById(R.id.status);
        CheckBox check = content.findViewById(R.id.check);
        check.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                status.setText(isChecked ? "checked" : "unchecked");
                Log.i(TAG, "checked=" + isChecked);
            }
        });
        PopupWindow popup = new PopupWindow(content, 520, 300, true);
        popup.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
        popup.setOutsideTouchable(true);
        popup.showAtLocation(findViewById(android.R.id.content), Gravity.CENTER, 0, 0);
        Log.i(TAG, "popup shown");
    }
}
