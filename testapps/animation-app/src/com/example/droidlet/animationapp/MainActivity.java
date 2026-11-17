package com.example.droidlet.animationapp;

import android.app.Activity;
import android.os.Bundle;
import android.view.View;
import android.view.animation.LinearInterpolator;

public class MainActivity extends Activity {
    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        setContentView(R.layout.main);
        View square = findViewById(R.id.square);
        square.animate()
                .translationX(700.0f)
                .alpha(0.25f)
                .setInterpolator(new LinearInterpolator())
                .setDuration(4000L)
                .start();
    }
}
